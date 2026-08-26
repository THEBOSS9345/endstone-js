// Copyright (c) 2026 THEBOSS9345 (https://github.com/THEBOSS9345/endstone-js)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The Node.js side of the ABI firewall. This translation unit links libnode and must never include an
// Endstone header: on Linux it is compiled against libstdc++ while Endstone uses libc++. std:: types
// are free to be used *inside* here; none may appear in endstone_node_abi.h.
//
// Node's unstable embedder API is quarantined in node_embed.cpp - everything here goes through
// N-API, which is ABI-stable across Node versions. Do not include <node.h> or <v8.h> in this file.

#include "endstone_node_abi.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <deque>
#include <vector>

#include <node_api.h>

#include "bootstrap_source.h"  // generated from bootstrap.js
#include "node_embed.h"

namespace embed = endstone::node::embed;

namespace {

// How long esn_host_load_plugin will pump waiting for an import to settle. Generous because an
// opt-in `npm install` runs inside that window and can take minutes on a cold cache.
constexpr auto kLoadTimeout = std::chrono::seconds(300);
constexpr const char *kBindingName = "endstone_node";

struct HostImpl {
    esn_log_fn log{nullptr};
    void *log_user_data{nullptr};
    std::string script_path;
    std::string exec_path;
    embed::Instance *node{nullptr};
    const esn_endstone_api *api{nullptr};  // borrowed; the Endstone side owns it
    bool started{false};
    // The JS-side plugin runtime, handed over by the bootstrap via endstone_node.setRuntime().
    napi_env napi{nullptr};
    napi_ref runtime{nullptr};
};

// One loaded JS plugin. Owns the strings behind esn_plugin_meta so the borrowed const char* the
// Endstone side reads stay valid until esn_plugin_unload.
struct PluginImpl {
    HostImpl *host{nullptr};
    std::int32_t id{-1};
    std::string name;
    std::string version;
    std::string api_version;
    std::string description;
    std::string website;
    std::string load_order;
    std::vector<std::string> authors;
    std::vector<std::string> depend;
    std::vector<const char *> author_ptrs;
    std::vector<const char *> depend_ptrs;

    // Command declarations, kept in a deque so the strings the pointer arrays refer to keep their
    // addresses as more commands are appended.
    struct CommandStorage {
        std::string name;
        std::string description;
        std::deque<std::string> usages;
        std::deque<std::string> aliases;
        std::deque<std::string> permissions;
        std::vector<const char *> usage_ptrs;
        std::vector<const char *> alias_ptrs;
        std::vector<const char *> permission_ptrs;
    };
    std::deque<CommandStorage> commands;
    std::vector<esn_command_decl> command_decls;
};

// Node's per-process initialization happens once, so the host is a process singleton. The N-API
// binding registration callback has no user-data parameter, so it reads this.
HostImpl *g_host = nullptr;

void emit(const HostImpl *host, esn_log_level level, const char *message, std::size_t length)
{
    if (host && host->log && message) {
        host->log(host->log_user_data, static_cast<int>(level), message, length);
    }
}

void emit(const HostImpl *host, esn_log_level level, const std::string &message)
{
    emit(host, level, message.data(), message.size());
}

void emitf(const HostImpl *host, esn_log_level level, const char *format, ...)
{
    char buffer[1024];
    std::va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written > 0) {
        const auto length = static_cast<std::size_t>(written) < sizeof(buffer)
                                ? static_cast<std::size_t>(written)
                                : sizeof(buffer) - 1;
        emit(host, level, buffer, length);
    }
}

esn_log_level clampLevel(std::int32_t level)
{
    if (level < ESN_LOG_TRACE) {
        return ESN_LOG_TRACE;
    }
    if (level > ESN_LOG_CRITICAL) {
        return ESN_LOG_CRITICAL;
    }
    return static_cast<esn_log_level>(level);
}

/** Reads a JS string value into `out`. Returns false if it is absent or not a string. */
bool readStringValue(napi_env env, napi_value value, std::string &out)
{
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_string) {
        return false;
    }
    std::size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) {
        return false;
    }
    std::vector<char> buffer(length + 1, '\0');
    std::size_t copied = 0;
    if (napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &copied) != napi_ok) {
        return false;
    }
    out.assign(buffer.data(), copied);
    return true;
}

bool readString(napi_env env, napi_value object, const char *key, std::string &out)
{
    napi_value value = nullptr;
    if (napi_get_named_property(env, object, key, &value) != napi_ok) {
        return false;
    }
    return readStringValue(env, value, out);
}

void readStringArray(napi_env env, napi_value object, const char *key, std::vector<std::string> &out)
{
    napi_value array = nullptr;
    bool is_array = false;
    if (napi_get_named_property(env, object, key, &array) != napi_ok ||
        napi_is_array(env, array, &is_array) != napi_ok || !is_array) {
        return;
    }
    std::uint32_t length = 0;
    (void)napi_get_array_length(env, array, &length);
    for (std::uint32_t i = 0; i < length; ++i) {
        napi_value element = nullptr;
        std::string text;
        if (napi_get_element(env, array, i, &element) == napi_ok && readStringValue(env, element, text)) {
            out.emplace_back(std::move(text));
        }
    }
}

/**
 * Copies meta.commands into storage the plugin owns, so the pointers handed across the ABI stay valid
 * until the plugin is unloaded.
 *
 * These declarations are produced by the runtime's own collector rather than read from user input, so
 * a malformed entry means a bug here; such an entry is skipped rather than reported.
 */
void readCommandDecls(napi_env env, napi_value meta, PluginImpl &plugin)
{
    napi_value array = nullptr;
    bool is_array = false;
    if (napi_get_named_property(env, meta, "commands", &array) != napi_ok ||
        napi_is_array(env, array, &is_array) != napi_ok || !is_array) {
        return;
    }
    std::uint32_t length = 0;
    (void)napi_get_array_length(env, array, &length);

    const auto fill = [env](napi_value object, const char *key, std::deque<std::string> &storage,
                            std::vector<const char *> &ptrs) {
        std::vector<std::string> values;
        readStringArray(env, object, key, values);
        for (auto &value : values) {
            storage.push_back(std::move(value));
            ptrs.push_back(storage.back().c_str());
        }
    };

    for (std::uint32_t i = 0; i < length; ++i) {
        napi_value entry = nullptr;
        if (napi_get_element(env, array, i, &entry) != napi_ok) {
            continue;
        }
        PluginImpl::CommandStorage storage;
        if (!readString(env, entry, "name", storage.name) || storage.name.empty()) {
            continue;
        }
        (void)readString(env, entry, "description", storage.description);
        plugin.commands.push_back(std::move(storage));
        auto &kept = plugin.commands.back();
        fill(entry, "usages", kept.usages, kept.usage_ptrs);
        fill(entry, "aliases", kept.aliases, kept.alias_ptrs);
        fill(entry, "permissions", kept.permissions, kept.permission_ptrs);
    }

    plugin.command_decls.reserve(plugin.commands.size());
    for (const auto &command : plugin.commands) {
        esn_command_decl decl{};
        decl.name = command.name.c_str();
        decl.description = command.description.empty() ? nullptr : command.description.c_str();
        decl.usages = command.usage_ptrs.empty() ? nullptr : command.usage_ptrs.data();
        decl.usage_count = command.usage_ptrs.size();
        decl.aliases = command.alias_ptrs.empty() ? nullptr : command.alias_ptrs.data();
        decl.alias_count = command.alias_ptrs.size();
        decl.permissions = command.permission_ptrs.empty() ? nullptr : command.permission_ptrs.data();
        decl.permission_count = command.permission_ptrs.size();
        plugin.command_decls.push_back(decl);
    }
}

// endstone_node.log(level, message)
napi_value jsLog(napi_env env, napi_callback_info info)
{
    std::size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
        return nullptr;
    }
    std::int32_t level = ESN_LOG_INFO;
    (void)napi_get_value_int32(env, argv[0], &level);
    std::string message;
    if (readStringValue(env, argv[1], message)) {
        emit(g_host, clampLevel(level), message);
    }
    return nullptr;
}

// endstone_node.setRuntime({ beginLoad, pollLoad, invoke, unload }) - the bootstrap hands the plugin
// runtime over once, and the C side keeps a strong reference to call back into it.
napi_value jsSetRuntime(napi_env env, napi_callback_info info)
{
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1 || !g_host) {
        return nullptr;
    }
    if (g_host->runtime) {
        (void)napi_delete_reference(env, g_host->runtime);
        g_host->runtime = nullptr;
    }
    g_host->napi = env;
    (void)napi_create_reference(env, argv[0], 1, &g_host->runtime);
    return nullptr;
}

// --- the Endstone API, as seen from JavaScript ---------------------------------------------------
//
// Each of these forwards to the esn_endstone_api table the Endstone side supplied. They are only ever
// called from JavaScript, which only ever runs on the BDS server thread, so no marshalling is needed.

using StringGetter = std::size_t(ESN_CALL *)(void *, char *, std::size_t);

/** Applies the ABI's two-call string convention: ask for the size, then fetch. */
napi_value fetchString(napi_env env, const HostImpl *host, StringGetter getter)
{
    napi_value undefined = nullptr;
    (void)napi_get_undefined(env, &undefined);
    if (!host || !host->api || !getter) {
        return undefined;
    }
    const std::size_t needed = getter(host->api->context, nullptr, 0);
    if (needed == 0) {
        napi_value empty = nullptr;
        return napi_create_string_utf8(env, "", 0, &empty) == napi_ok ? empty : undefined;
    }
    std::vector<char> buffer(needed + 1, '\0');
    const std::size_t written = getter(host->api->context, buffer.data(), buffer.size());
    const std::size_t length = written < needed ? written : needed;
    napi_value result = nullptr;
    return napi_create_string_utf8(env, buffer.data(), length, &result) == napi_ok ? result : undefined;
}

napi_value intResult(napi_env env, int value)
{
    napi_value result = nullptr;
    return napi_create_int32(env, value, &result) == napi_ok ? result : nullptr;
}

napi_value jsServerName(napi_env env, napi_callback_info)
{
    return fetchString(env, g_host, g_host && g_host->api ? g_host->api->server_name : nullptr);
}

napi_value jsServerVersion(napi_env env, napi_callback_info)
{
    return fetchString(env, g_host, g_host && g_host->api ? g_host->api->server_version : nullptr);
}

napi_value jsServerMinecraftVersion(napi_env env, napi_callback_info)
{
    return fetchString(env, g_host, g_host && g_host->api ? g_host->api->server_minecraft_version : nullptr);
}

napi_value jsServerProtocolVersion(napi_env env, napi_callback_info)
{
    const auto *api = g_host ? g_host->api : nullptr;
    return intResult(env, api && api->server_protocol_version ? api->server_protocol_version(api->context) : -1);
}

napi_value jsServerOnlinePlayerCount(napi_env env, napi_callback_info)
{
    const auto *api = g_host ? g_host->api : nullptr;
    return intResult(env, api && api->server_online_player_count ? api->server_online_player_count(api->context) : -1);
}

napi_value jsServerSelf(napi_env env, napi_callback_info)
{
    const auto *api = g_host ? g_host->api : nullptr;
    esn_handle handle = 0;
    if (api && api->server_self) {
        (void)api->server_self(api->context, &handle);
    }
    napi_value result = nullptr;
    return napi_create_double(env, static_cast<double>(handle), &result) == napi_ok ? result : nullptr;
}

napi_value jsServerLevel(napi_env env, napi_callback_info)
{
    napi_value null_value = nullptr;
    (void)napi_get_null(env, &null_value);
    const auto *api = g_host ? g_host->api : nullptr;
    if (!api || !api->server_level) {
        return null_value;
    }
    esn_handle level = 0;
    if (api->server_level(api->context, &level) != ESN_OK || level == 0) {
        return null_value;  // no level loaded yet
    }
    napi_value result = nullptr;
    return napi_create_int64(env, static_cast<std::int64_t>(level), &result) == napi_ok ? result : null_value;
}

napi_value jsBroadcastMessage(napi_env env, napi_callback_info info)
{
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1 || !api ||
        !api->broadcast_message) {
        return nullptr;
    }
    std::string message;
    if (readStringValue(env, argv[0], message)) {
        api->broadcast_message(api->context, message.data(), message.size());
    }
    return nullptr;
}

// Distinct from jsLog: this goes through the Endstone plugin's logger rather than the host's own
// diagnostic channel, so plugin output is attributed correctly.
napi_value jsPluginLog(napi_env env, napi_callback_info info)
{
    std::size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
        return nullptr;
    }
    std::int32_t level = ESN_LOG_INFO;
    (void)napi_get_value_int32(env, argv[0], &level);
    std::string message;
    if (!readStringValue(env, argv[1], message)) {
        return nullptr;
    }
    if (api && api->log) {
        api->log(api->context, static_cast<int>(clampLevel(level)), message.data(), message.size());
    }
    else {
        emit(g_host, clampLevel(level), message);
    }
    return nullptr;
}

// --- handle-backed property access ---------------------------------------------------------------
//
// JavaScript passes (handle, name); these forward to the generic accessors. Unknown members and stale
// handles come back as a thrown JS error rather than a silent undefined, so plugin bugs are loud.

/** Reads (handle, name) from the callback info. Returns false if the shape is wrong. */
bool readTargetAndName(napi_env env, napi_callback_info info, esn_handle *handle, std::string &name,
                       napi_value *extra = nullptr)
{
    std::size_t argc = 3;
    napi_value argv[3] = {nullptr, nullptr, nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
        return false;
    }
    bool lossless = false;
    if (napi_get_value_bigint_uint64(env, argv[0], handle, &lossless) != napi_ok) {
        // Handles also arrive as plain numbers when small enough to be exact.
        double as_double = 0;
        if (napi_get_value_double(env, argv[0], &as_double) != napi_ok) {
            return false;
        }
        *handle = static_cast<esn_handle>(as_double);
    }
    if (!readStringValue(env, argv[1], name)) {
        return false;
    }
    if (extra) {
        *extra = argc >= 3 ? argv[2] : nullptr;
    }
    return true;
}

const char *statusText(esn_status status)
{
    switch (status) {
    case ESN_ERR_STALE_HANDLE:
        return "the object is no longer valid - handles must not be kept past the callback that "
               "produced them. Copy the primitives you need (name, uniqueId, coordinates) before "
               "the callback returns; never retain a Player, Block, or other handle for later use";
    case ESN_ERR_NO_SUCH_MEMBER:
        return "no such property or method on this object";
    case ESN_ERR_WRONG_TYPE:
        return "wrong type, or the property is read-only";
    default:
        return "Endstone API call failed";
    }
}

/** Turns a non-OK status into a thrown JS Error and returns nullptr. */
napi_value fail(napi_env env, esn_status status, const std::string &name)
{
    const std::string message = std::string(statusText(status)) + ": '" + name + "'";
    (void)napi_throw_error(env, nullptr, message.c_str());
    return nullptr;
}

const esn_accessors *accessors()
{
    return g_host && g_host->api ? &g_host->api->accessors : nullptr;
}

napi_value jsGet(napi_env env, napi_callback_info info)
{
    esn_handle handle = 0;
    std::string name;
    const auto *acc = accessors();
    if (!acc || !readTargetAndName(env, info, &handle, name)) {
        return nullptr;
    }
    void *context = g_host->api->context;
    napi_value result = nullptr;

    // Probe in order of cheapness. ESN_ERR_WRONG_TYPE means "exists, different type", so keep going;
    // ESN_ERR_NO_SUCH_MEMBER from every probe means it genuinely is not there.
    std::size_t needed = 0;
    if (acc->get_string) {
        const auto status = acc->get_string(context, handle, name.c_str(), nullptr, 0, &needed);
        if (status == ESN_OK) {
            std::vector<char> buffer(needed + 1, '\0');
            (void)acc->get_string(context, handle, name.c_str(), buffer.data(), buffer.size(), &needed);
            return napi_create_string_utf8(env, buffer.data(), needed, &result) == napi_ok ? result : nullptr;
        }
        if (status == ESN_ERR_STALE_HANDLE) {
            return fail(env, status, name);
        }
    }
    if (acc->get_bool) {
        int value = 0;
        if (acc->get_bool(context, handle, name.c_str(), &value) == ESN_OK) {
            return napi_get_boolean(env, value != 0, &result) == napi_ok ? result : nullptr;
        }
    }
    if (acc->get_int) {
        std::int64_t value = 0;
        if (acc->get_int(context, handle, name.c_str(), &value) == ESN_OK) {
            return napi_create_int64(env, value, &result) == napi_ok ? result : nullptr;
        }
    }
    if (acc->get_double) {
        double value = 0;
        if (acc->get_double(context, handle, name.c_str(), &value) == ESN_OK) {
            return napi_create_double(env, value, &result) == napi_ok ? result : nullptr;
        }
    }
    if (acc->get_handle) {
        esn_handle value = 0;
        if (acc->get_handle(context, handle, name.c_str(), &value) == ESN_OK) {
            // Tagged so JavaScript can tell a nested object from a plain number and wrap it.
            napi_value tagged = nullptr;
            napi_value inner = nullptr;
            if (napi_create_object(env, &tagged) == napi_ok &&
                napi_create_int64(env, static_cast<std::int64_t>(value), &inner) == napi_ok &&
                napi_set_named_property(env, tagged, "__esn_handle", inner) == napi_ok) {
                return tagged;
            }
            return nullptr;
        }
    }
    // Unknown members read as undefined rather than throwing; stale handles still throw above.
    napi_value undefined_value = nullptr;
    return napi_get_undefined(env, &undefined_value) == napi_ok ? undefined_value : nullptr;
}

napi_value jsSet(napi_env env, napi_callback_info info)
{
    esn_handle handle = 0;
    std::string name;
    napi_value value = nullptr;
    const auto *acc = accessors();
    if (!acc || !readTargetAndName(env, info, &handle, name, &value) || !value) {
        return nullptr;
    }
    void *context = g_host->api->context;

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok) {
        return nullptr;
    }
    esn_status status = ESN_ERR_NO_SUCH_MEMBER;
    switch (type) {
    case napi_boolean: {
        bool flag = false;
        (void)napi_get_value_bool(env, value, &flag);
        status = acc->set_bool ? acc->set_bool(context, handle, name.c_str(), flag ? 1 : 0) : status;
        break;
    }
    case napi_number: {
        double number = 0;
        (void)napi_get_value_double(env, value, &number);
        // Try the integer setter first so whole numbers keep their exact type.
        if (acc->set_int && number == static_cast<double>(static_cast<std::int64_t>(number))) {
            status = acc->set_int(context, handle, name.c_str(), static_cast<std::int64_t>(number));
        }
        if (status != ESN_OK && acc->set_double) {
            status = acc->set_double(context, handle, name.c_str(), number);
        }
        break;
    }
    case napi_string: {
        std::string text;
        if (readStringValue(env, value, text) && acc->set_string) {
            status = acc->set_string(context, handle, name.c_str(), text.data(), text.size());
        }
        break;
    }
    default:
        status = ESN_ERR_WRONG_TYPE;
        break;
    }
    if (status != ESN_OK) {
        return fail(env, status, name);
    }
    return nullptr;
}

// invoke(handle, name, ...args) - strings, numbers and handle-backed objects, each kind gathered into
// its own array in JavaScript order. An object counts as a handle when it carries __esn_handle, which
// is what every wrapped Endstone object exposes.
napi_value jsInvoke(napi_env env, napi_callback_info info)
{
    std::size_t argc = 8;
    napi_value argv[8] = {};
    const auto *acc = accessors();
    if (!acc || !acc->invoke || napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
        return nullptr;
    }
    double as_double = 0;
    if (napi_get_value_double(env, argv[0], &as_double) != napi_ok) {
        return nullptr;
    }
    const auto handle = static_cast<esn_handle>(as_double);
    std::string name;
    if (!readStringValue(env, argv[1], name)) {
        return nullptr;
    }
    // Arguments keep their JavaScript order within each kind: strings in one array, numbers in another,
    // handles in a third.
    std::vector<std::string> strings;
    std::vector<double> numbers;
    std::vector<esn_handle> handles;
    for (std::size_t i = 2; i < argc; ++i) {
        napi_valuetype type = napi_undefined;
        if (napi_typeof(env, argv[i], &type) != napi_ok) {
            continue;
        }
        if (type == napi_string) {
            std::string value;
            if (readStringValue(env, argv[i], value)) {
                strings.push_back(std::move(value));
            }
        }
        else if (type == napi_number) {
            double value = 0;
            if (napi_get_value_double(env, argv[i], &value) == napi_ok) {
                numbers.push_back(value);
            }
        }
        else if (type == napi_object) {
            // Reading through a Proxy runs its get trap, which is how a wrapped object answers.
            napi_value tagged = nullptr;
            double value = 0;
            if (napi_get_named_property(env, argv[i], "__esn_handle", &tagged) == napi_ok &&
                napi_get_value_double(env, tagged, &value) == napi_ok) {
                handles.push_back(static_cast<esn_handle>(value));
            }
        }
    }
    std::vector<const char *> string_ptrs;
    string_ptrs.reserve(strings.size());
    for (const auto &value : strings) {
        string_ptrs.push_back(value.c_str());
    }

    esn_handle result_handle = 0;
    const auto status = acc->invoke(g_host->api->context, handle, name.c_str(),
                                    string_ptrs.empty() ? nullptr : string_ptrs.data(), string_ptrs.size(),
                                    numbers.empty() ? nullptr : numbers.data(), numbers.size(),
                                    handles.empty() ? nullptr : handles.data(), handles.size(), &result_handle);
    if (status != ESN_OK) {
        return fail(env, status, name);
    }
    if (result_handle != 0) {
        napi_value tagged = nullptr;
        napi_value inner = nullptr;
        if (napi_create_object(env, &tagged) == napi_ok &&
            napi_create_int64(env, static_cast<std::int64_t>(result_handle), &inner) == napi_ok &&
            napi_set_named_property(env, tagged, "__esn_handle", inner) == napi_ok) {
            return tagged;
        }
    }
    return nullptr;
}

napi_value jsTypeName(napi_env env, napi_callback_info info)
{
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    const auto *acc = accessors();
    if (!acc || !acc->type_name || napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
        return nullptr;
    }
    double as_double = 0;
    if (napi_get_value_double(env, argv[0], &as_double) != napi_ok) {
        return nullptr;
    }
    const auto handle = static_cast<esn_handle>(as_double);
    std::size_t needed = 0;
    if (acc->type_name(g_host->api->context, handle, nullptr, 0, &needed) != ESN_OK) {
        return nullptr;
    }
    std::vector<char> buffer(needed + 1, '\0');
    (void)acc->type_name(g_host->api->context, handle, buffer.data(), buffer.size(), &needed);
    napi_value result = nullptr;
    return napi_create_string_utf8(env, buffer.data(), needed, &result) == napi_ok ? result : nullptr;
}

// getBytes(handle, name) / setBytes(handle, name, byteString)
//
// Bytes cross as latin1 - one JavaScript code unit per byte, NUL included - which is the only encoding
// that round-trips arbitrary data through a JS string. napi_create_string_utf8 would replace every
// invalid sequence with U+FFFD and quietly destroy a packet payload.
napi_value jsGetBytes(napi_env env, napi_callback_info info)
{
    esn_handle handle = 0;
    std::string name;
    const auto *acc = accessors();
    napi_value undefined_value = nullptr;
    (void)napi_get_undefined(env, &undefined_value);
    if (!acc || !acc->get_bytes || !readTargetAndName(env, info, &handle, name)) {
        return undefined_value;
    }
    void *context = g_host->api->context;
    std::size_t needed = 0;
    const auto status = acc->get_bytes(context, handle, name.c_str(), nullptr, 0, &needed);
    if (status == ESN_ERR_STALE_HANDLE) {
        return fail(env, status, name);
    }
    if (status != ESN_OK) {
        return undefined_value;
    }
    std::vector<char> buffer(needed + 1, '\0');
    (void)acc->get_bytes(context, handle, name.c_str(), buffer.data(), buffer.size(), &needed);
    napi_value result = nullptr;
    return napi_create_string_latin1(env, buffer.data(), needed, &result) == napi_ok ? result : undefined_value;
}

napi_value jsSetBytes(napi_env env, napi_callback_info info)
{
    esn_handle handle = 0;
    std::string name;
    napi_value value = nullptr;
    const auto *acc = accessors();
    if (!acc || !acc->set_bytes || !readTargetAndName(env, info, &handle, name, &value) || !value) {
        return nullptr;
    }
    std::size_t length = 0;
    if (napi_get_value_string_latin1(env, value, nullptr, 0, &length) != napi_ok) {
        return nullptr;
    }
    std::vector<char> bytes(length + 1, '\0');
    if (napi_get_value_string_latin1(env, value, bytes.data(), bytes.size(), &length) != napi_ok) {
        return nullptr;
    }
    const auto status = acc->set_bytes(g_host->api->context, handle, name.c_str(), bytes.data(), length);
    if (status != ESN_OK) {
        return fail(env, status, name);
    }
    return nullptr;
}

napi_value jsSubscribe(napi_env env, napi_callback_info info)
{
    std::size_t argc = 3;
    napi_value argv[3] = {nullptr, nullptr, nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    if (!api || !api->subscribe || napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
        return nullptr;
    }
    std::string event_name;
    if (!readStringValue(env, argv[0], event_name)) {
        return nullptr;
    }
    std::int32_t priority = ESN_PRIORITY_NORMAL;
    if (argc >= 2) {
        (void)napi_get_value_int32(env, argv[1], &priority);
    }
    bool ignore_cancelled = false;
    if (argc >= 3) {
        (void)napi_get_value_bool(env, argv[2], &ignore_cancelled);
    }

    std::uint32_t subscription = 0;
    const auto status = api->subscribe(api->context, event_name.c_str(), priority, ignore_cancelled ? 1 : 0,
                                       &subscription);
    if (status != ESN_OK) {
        return fail(env, status, event_name);
    }
    napi_value result = nullptr;
    return napi_create_uint32(env, subscription, &result) == napi_ok ? result : nullptr;
}

napi_value jsUnsubscribe(napi_env env, napi_callback_info info)
{
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    if (!api || !api->unsubscribe || napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok ||
        argc < 1) {
        return nullptr;
    }
    std::uint32_t subscription = 0;
    (void)napi_get_value_uint32(env, argv[0], &subscription);
    (void)api->unsubscribe(api->context, subscription);
    return nullptr;
}

napi_value jsApiAvailable(napi_env env, napi_callback_info)
{
    napi_value result = nullptr;
    (void)napi_get_boolean(env, g_host != nullptr && g_host->api != nullptr, &result);
    return result;
}

void defineFunction(napi_env env, napi_value exports, const char *name, napi_callback fn)
{
    napi_value value = nullptr;
    if (napi_create_function(env, name, NAPI_AUTO_LENGTH, fn, nullptr, &value) == napi_ok) {
        (void)napi_set_named_property(env, exports, name, value);
    }
}

void defineString(napi_env env, napi_value exports, const char *name, const char *text)
{
    napi_value value = nullptr;
    if (text && napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &value) == napi_ok) {
        (void)napi_set_named_property(env, exports, name, value);
    }
}

napi_value jsSendForm(napi_env env, napi_callback_info info)
{
    std::size_t argc = 3;
    napi_value argv[3] = {nullptr, nullptr, nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    double handle = 0;
    std::uint32_t form_id = 0;
    std::string spec;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 3 || !api ||
        !api->send_form || napi_get_value_double(env, argv[0], &handle) != napi_ok ||
        napi_get_value_uint32(env, argv[1], &form_id) != napi_ok || !readStringValue(env, argv[2], spec)) {
        return nullptr;
    }
    const auto status = api->send_form(api->context, static_cast<esn_handle>(handle), form_id, spec.data(),
                                      spec.size());
    if (status != ESN_OK) {
        return fail(env, status, "sendForm");
    }
    napi_value undefined_value = nullptr;
    return napi_get_undefined(env, &undefined_value) == napi_ok ? undefined_value : nullptr;
}

napi_value jsCloseForm(napi_env env, napi_callback_info info)
{
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    double handle = 0;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) == napi_ok && argc >= 1 && api &&
        api->close_form && napi_get_value_double(env, argv[0], &handle) == napi_ok) {
        api->close_form(api->context, static_cast<esn_handle>(handle));
    }
    napi_value undefined_value = nullptr;
    return napi_get_undefined(env, &undefined_value) == napi_ok ? undefined_value : nullptr;
}

napi_value jsSendPacket(napi_env env, napi_callback_info info)
{
    std::size_t argc = 3;
    napi_value argv[3] = {nullptr, nullptr, nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    double handle = 0;
    std::int32_t packet_id = 0;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 3 || !api ||
        !api->send_packet || napi_get_value_double(env, argv[0], &handle) != napi_ok ||
        napi_get_value_int32(env, argv[1], &packet_id) != napi_ok) {
        return nullptr;
    }
    // The payload is bytes, so it arrives as a latin1 string: one code unit per byte, NULs included.
    std::size_t length = 0;
    if (napi_get_value_string_latin1(env, argv[2], nullptr, 0, &length) != napi_ok) {
        return nullptr;
    }
    std::vector<char> payload(length + 1, '\0');
    if (napi_get_value_string_latin1(env, argv[2], payload.data(), payload.size(), &length) != napi_ok) {
        return nullptr;
    }
    const auto status = api->send_packet(api->context, static_cast<esn_handle>(handle), packet_id, payload.data(),
                                        length);
    if (status != ESN_OK) {
        return fail(env, status, "sendPacket");
    }
    napi_value undefined_value = nullptr;
    return napi_get_undefined(env, &undefined_value) == napi_ok ? undefined_value : nullptr;
}

napi_value jsUpdateCommands(napi_env env, napi_callback_info info)
{
    (void)info;
    const auto *api = g_host ? g_host->api : nullptr;
    if (api && api->update_commands) {
        api->update_commands(api->context);
    }
    napi_value undefined_value = nullptr;
    return napi_get_undefined(env, &undefined_value) == napi_ok ? undefined_value : nullptr;
}

napi_value jsScheduleTask(napi_env env, napi_callback_info info)
{
    std::size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    std::uint32_t delay = 0;
    std::uint32_t period = 0;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2 || !api ||
        !api->schedule_task || napi_get_value_uint32(env, argv[0], &delay) != napi_ok ||
        napi_get_value_uint32(env, argv[1], &period) != napi_ok) {
        return nullptr;
    }
    std::uint32_t task = 0;
    if (api->schedule_task(api->context, delay, period, &task) != ESN_OK) {
        return nullptr;
    }
    napi_value result = nullptr;
    return napi_create_uint32(env, task, &result) == napi_ok ? result : nullptr;
}

napi_value jsCancelTask(napi_env env, napi_callback_info info)
{
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    const auto *api = g_host ? g_host->api : nullptr;
    std::uint32_t task = 0;
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) == napi_ok && argc >= 1 && api &&
        api->cancel_task && napi_get_value_uint32(env, argv[0], &task) == napi_ok) {
        api->cancel_task(api->context, task);
    }
    napi_value undefined_value = nullptr;
    return napi_get_undefined(env, &undefined_value) == napi_ok ? undefined_value : nullptr;
}

napi_value registerBinding(napi_env env, napi_value exports)
{
    defineFunction(env, exports, "log", jsLog);
    defineFunction(env, exports, "setRuntime", jsSetRuntime);
    defineFunction(env, exports, "apiAvailable", jsApiAvailable);
    defineFunction(env, exports, "pluginLog", jsPluginLog);
    defineFunction(env, exports, "serverName", jsServerName);
    defineFunction(env, exports, "serverVersion", jsServerVersion);
    defineFunction(env, exports, "serverMinecraftVersion", jsServerMinecraftVersion);
    defineFunction(env, exports, "serverProtocolVersion", jsServerProtocolVersion);
    defineFunction(env, exports, "serverOnlinePlayerCount", jsServerOnlinePlayerCount);
    defineFunction(env, exports, "serverLevel", jsServerLevel);
    defineFunction(env, exports, "serverSelf", jsServerSelf);
    defineFunction(env, exports, "broadcastMessage", jsBroadcastMessage);
    defineFunction(env, exports, "get", jsGet);
    defineFunction(env, exports, "set", jsSet);
    defineFunction(env, exports, "getBytes", jsGetBytes);
    defineFunction(env, exports, "setBytes", jsSetBytes);
    defineFunction(env, exports, "invoke", jsInvoke);
    defineFunction(env, exports, "typeName", jsTypeName);
    defineFunction(env, exports, "subscribe", jsSubscribe);
    defineFunction(env, exports, "unsubscribe", jsUnsubscribe);
    defineFunction(env, exports, "updateCommands", jsUpdateCommands);
    defineFunction(env, exports, "sendPacket", jsSendPacket);
    defineFunction(env, exports, "sendForm", jsSendForm);
    defineFunction(env, exports, "closeForm", jsCloseForm);
    defineFunction(env, exports, "scheduleTask", jsScheduleTask);
    defineFunction(env, exports, "cancelTask", jsCancelTask);
    if (g_host) {
        defineString(env, exports, "scriptPath", g_host->script_path.c_str());
    }
    defineString(env, exports, "compiledNodeVersion", embed::compiledNodeVersion());
    return exports;
}

// The bootstrap lives in bootstrap.js and is embedded by scripts/embed_js.py at build time, so it
// stays real JavaScript - linted, formatted and syntax-checked - while the host still has no runtime
// asset to locate. kBootstrapSource comes from the generated header.

/**
 * Calls a method on the JS plugin runtime. Requires an active embed::Scope. Any pending JS exception
 * is drained and reported so it never escapes into libnode frames, which are compiled without
 * exceptions.
 */
bool callRuntime(HostImpl *host, const char *method, int argc, napi_value *argv, napi_value *out)
{
    if (!host || !host->runtime || !host->napi) {
        return false;
    }
    napi_env env = host->napi;
    napi_value runtime = nullptr;
    napi_value fn = nullptr;
    if (napi_get_reference_value(env, host->runtime, &runtime) != napi_ok || !runtime) {
        return false;
    }
    if (napi_get_named_property(env, runtime, method, &fn) != napi_ok) {
        return false;
    }
    napi_value result = nullptr;
    if (napi_call_function(env, runtime, fn, static_cast<std::size_t>(argc), argv, &result) != napi_ok) {
        bool pending = false;
        if (napi_is_exception_pending(env, &pending) == napi_ok && pending) {
            napi_value error = nullptr;
            if (napi_get_and_clear_last_exception(env, &error) == napi_ok) {
                std::string message;
                if (!readString(env, error, "stack", message)) {
                    (void)readString(env, error, "message", message);
                }
                emitf(host, ESN_LOG_ERROR, "plugin runtime '%s' threw: %s", method,
                      message.empty() ? "<unknown>" : message.c_str());
            }
        }
        return false;
    }
    if (out) {
        *out = result;
    }
    return true;
}

embed::ErrorSink errorSink(HostImpl *host)
{
    return [host](const std::string &message) { emit(host, ESN_LOG_ERROR, message); };
}

}  // namespace

extern "C" {

uint32_t ESN_CALL esn_abi_version(void)
{
    return ESN_ABI_VERSION;
}

const char *ESN_CALL esn_status_message(esn_status status)
{
    switch (status) {
    case ESN_OK:
        return "ok";
    case ESN_ERR_BAD_ARGUMENT:
        return "bad argument";
    case ESN_ERR_ABI_MISMATCH:
        return "ABI version mismatch between the Endstone plugin and the Node host";
    case ESN_ERR_ALREADY_INITIALIZED:
        return "a Node host already exists in this process";
    case ESN_ERR_NOT_INITIALIZED:
        return "no Node host";
    case ESN_ERR_INIT_FAILED:
        return "Node.js initialization failed";
    case ESN_ERR_SCRIPT_FAILED:
        return "the JavaScript entry point failed";
    case ESN_ERR_INTERNAL:
        return "internal host error";
    default:
        return "unknown status";
    }
}

esn_status ESN_CALL esn_host_create(const esn_host_config *config, esn_host **out_host)
{
    if (!config || !out_host || !config->log || !config->script_path) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    if (config->abi_version != ESN_ABI_VERSION) {
        return ESN_ERR_ABI_MISMATCH;
    }
    if (g_host) {
        return ESN_ERR_ALREADY_INITIALIZED;
    }
    *out_host = nullptr;

    // Nothing below may let an exception escape: libnode is built -fno-exceptions and the caller is
    // on the far side of a C boundary.
    try {
        auto host = std::make_unique<HostImpl>();
        host->log = config->log;
        host->log_user_data = config->log_user_data;
        host->script_path = config->script_path;
        host->exec_path = config->exec_path ? config->exec_path : "node";
        if (config->api) {
            if (config->api->abi_version != ESN_ABI_VERSION) {
                return ESN_ERR_ABI_MISMATCH;
            }
            host->api = config->api;
        }
        g_host = host.get();

        const std::vector<std::string> args{host->exec_path, host->script_path};
        host->node = embed::create(args, errorSink(host.get()));
        if (!host->node) {
            g_host = nullptr;
            return ESN_ERR_INIT_FAILED;
        }

        emitf(host.get(), ESN_LOG_INFO, "Node.js %s (V8 %s, module ABI %d) initialized",
              embed::compiledNodeVersion(), embed::compiledV8Version(), embed::compiledNodeModuleVersion());
        *out_host = reinterpret_cast<esn_host *>(host.release());
        return ESN_OK;
    }
    catch (...) {
        g_host = nullptr;
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_start(esn_host *handle)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (host->started) {
        return ESN_ERR_ALREADY_INITIALIZED;
    }

    try {
        const embed::Scope scope(host->node);
        if (!embed::loadEnvironment(host->node, kBootstrapSource, kBindingName, registerBinding, errorSink(host))) {
            return ESN_ERR_SCRIPT_FAILED;
        }
        host->started = true;
        return ESN_OK;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_pump(esn_host *handle, int *out_more_work)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    try {
        const embed::Scope scope(host->node);
        return embed::pump(host->node, out_more_work, errorSink(host)) ? ESN_OK : ESN_ERR_INTERNAL;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_load_plugin(esn_host *handle, const char *path, esn_plugin **out_plugin)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->node || !host->started) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (!path || !out_plugin) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    *out_plugin = nullptr;

    try {
        napi_env env = host->napi;
        if (!env) {
            return ESN_ERR_NOT_INITIALIZED;
        }

        std::int32_t token = 0;
        {
            const embed::Scope scope(host->node);
            napi_value arg = nullptr;
            if (napi_create_string_utf8(env, path, NAPI_AUTO_LENGTH, &arg) != napi_ok) {
                return ESN_ERR_INTERNAL;
            }
            napi_value token_value = nullptr;
            if (!callRuntime(host, "beginLoad", 1, &arg, &token_value) ||
                napi_get_value_int32(env, token_value, &token) != napi_ok) {
                return ESN_ERR_SCRIPT_FAILED;
            }
        }

        // ESM imports resolve asynchronously, so drive the loop until the result appears. This is a
        // startup path; blocking here is preferable to handing Endstone a half-loaded plugin.
        const auto deadline = std::chrono::steady_clock::now() + kLoadTimeout;
        auto plugin = std::make_unique<PluginImpl>();
        plugin->host = host;
        bool settled = false;

        while (!settled) {
            const embed::Scope scope(host->node);
            (void)embed::pump(host->node, nullptr, errorSink(host));

            napi_value token_value = nullptr;
            napi_value result = nullptr;
            if (napi_create_int32(env, token, &token_value) != napi_ok ||
                !callRuntime(host, "pollLoad", 1, &token_value, &result)) {
                return ESN_ERR_SCRIPT_FAILED;
            }
            napi_valuetype type = napi_undefined;
            if (napi_typeof(env, result, &type) != napi_ok) {
                return ESN_ERR_INTERNAL;
            }
            if (type == napi_null || type == napi_undefined) {
                if (std::chrono::steady_clock::now() > deadline) {
                    emitf(host, ESN_LOG_ERROR, "timed out loading '%s' after %lld seconds", path,
                          static_cast<long long>(kLoadTimeout.count()));
                    return ESN_ERR_SCRIPT_FAILED;
                }
                continue;
            }

            bool ok = false;
            napi_value ok_value = nullptr;
            if (napi_get_named_property(env, result, "ok", &ok_value) == napi_ok) {
                (void)napi_get_value_bool(env, ok_value, &ok);
            }
            if (!ok) {
                std::string message;
                std::string detail;
                (void)readString(env, result, "message", message);
                (void)readString(env, result, "detail", detail);
                emitf(host, ESN_LOG_ERROR, "failed to load '%s': %s", path,
                      message.empty() ? "<unknown>" : message.c_str());
                if (!detail.empty()) {
                    emitf(host, ESN_LOG_ERROR, "  %s", detail.c_str());
                }
                return ESN_ERR_SCRIPT_FAILED;
            }

            napi_value meta = nullptr;
            if (napi_get_named_property(env, result, "meta", &meta) != napi_ok) {
                return ESN_ERR_SCRIPT_FAILED;
            }
            napi_value id_value = nullptr;
            if (napi_get_named_property(env, meta, "id", &id_value) != napi_ok ||
                napi_get_value_int32(env, id_value, &plugin->id) != napi_ok) {
                return ESN_ERR_SCRIPT_FAILED;
            }
            if (!readString(env, meta, "name", plugin->name) || plugin->name.empty()) {
                emit(host, ESN_LOG_ERROR, "plugin package.json has no usable \"name\"");
                return ESN_ERR_SCRIPT_FAILED;
            }
            if (!readString(env, meta, "version", plugin->version)) {
                plugin->version = "0.0.0";
            }
            (void)readString(env, meta, "apiVersion", plugin->api_version);
            (void)readString(env, meta, "description", plugin->description);
            (void)readString(env, meta, "website", plugin->website);
            (void)readString(env, meta, "loadOrder", plugin->load_order);
            readStringArray(env, meta, "authors", plugin->authors);
            readStringArray(env, meta, "depend", plugin->depend);
            readCommandDecls(env, meta, *plugin);
            settled = true;
        }

        plugin->author_ptrs.reserve(plugin->authors.size());
        for (const auto &author : plugin->authors) {
            plugin->author_ptrs.push_back(author.c_str());
        }
        plugin->depend_ptrs.reserve(plugin->depend.size());
        for (const auto &dependency : plugin->depend) {
            plugin->depend_ptrs.push_back(dependency.c_str());
        }

        *out_plugin = reinterpret_cast<esn_plugin *>(plugin.release());
        return ESN_OK;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_plugin_get_meta(esn_plugin *handle, esn_plugin_meta *out_meta)
{
    auto *plugin = reinterpret_cast<PluginImpl *>(handle);
    if (!plugin) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (!out_meta) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    const auto or_null = [](const std::string &value) { return value.empty() ? nullptr : value.c_str(); };
    out_meta->name = plugin->name.c_str();
    out_meta->version = plugin->version.c_str();
    out_meta->api_version = or_null(plugin->api_version);
    out_meta->description = or_null(plugin->description);
    out_meta->website = or_null(plugin->website);
    out_meta->load_order = or_null(plugin->load_order);
    out_meta->authors = plugin->author_ptrs.empty() ? nullptr : plugin->author_ptrs.data();
    out_meta->author_count = plugin->author_ptrs.size();
    out_meta->depend = plugin->depend_ptrs.empty() ? nullptr : plugin->depend_ptrs.data();
    out_meta->depend_count = plugin->depend_ptrs.size();
    out_meta->commands = plugin->command_decls.empty() ? nullptr : plugin->command_decls.data();
    out_meta->command_count = plugin->command_decls.size();
    return ESN_OK;
}

esn_status ESN_CALL esn_plugin_invoke(esn_plugin *handle, esn_plugin_hook hook)
{
    auto *plugin = reinterpret_cast<PluginImpl *>(handle);
    if (!plugin || !plugin->host || !plugin->host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (hook < ESN_HOOK_LOAD || hook > ESN_HOOK_DISABLE) {
        return ESN_ERR_BAD_ARGUMENT;
    }

    try {
        auto *host = plugin->host;
        const embed::Scope scope(host->node);
        napi_env env = host->napi;
        napi_value args[2] = {nullptr, nullptr};
        if (!env || napi_create_int32(env, plugin->id, &args[0]) != napi_ok ||
            napi_create_int32(env, static_cast<std::int32_t>(hook), &args[1]) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        return callRuntime(host, "invoke", 2, args, nullptr) ? ESN_OK : ESN_ERR_SCRIPT_FAILED;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_plugin_unload(esn_plugin *handle)
{
    auto *plugin = reinterpret_cast<PluginImpl *>(handle);
    if (!plugin) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    auto *host = plugin->host;
    if (host && host->node && host->napi) {
        try {
            const embed::Scope scope(host->node);
            napi_value arg = nullptr;
            if (napi_create_int32(host->napi, plugin->id, &arg) == napi_ok) {
                (void)callRuntime(host, "unload", 1, &arg, nullptr);
            }
        }
        catch (...) {
        }
    }
    delete plugin;
    return ESN_OK;
}

esn_status ESN_CALL esn_plugin_command(esn_plugin *handle, const char *name, esn_handle sender,
                                      const char *const *args, size_t arg_count, int *out_handled)
{
    auto *plugin = reinterpret_cast<PluginImpl *>(handle);
    if (!plugin || !plugin->host || !plugin->host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (!name || !out_handled) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    *out_handled = 0;

    try {
        auto *host = plugin->host;
        const embed::Scope scope(host->node);
        napi_env env = host->napi;
        if (!env) {
            return ESN_ERR_INTERNAL;
        }

        napi_value argv[4] = {nullptr, nullptr, nullptr, nullptr};
        napi_value arg_array = nullptr;
        if (napi_create_int32(env, plugin->id, &argv[0]) != napi_ok ||
            napi_create_string_utf8(env, name, NAPI_AUTO_LENGTH, &argv[1]) != napi_ok ||
            napi_create_double(env, static_cast<double>(sender), &argv[2]) != napi_ok ||
            napi_create_array_with_length(env, arg_count, &arg_array) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        for (size_t i = 0; i < arg_count; ++i) {
            napi_value element = nullptr;
            const char *text = args && args[i] ? args[i] : "";
            if (napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &element) != napi_ok ||
                napi_set_element(env, arg_array, static_cast<std::uint32_t>(i), element) != napi_ok) {
                return ESN_ERR_INTERNAL;
            }
        }
        argv[3] = arg_array;

        napi_value result = nullptr;
        if (!callRuntime(host, "command", 4, argv, &result)) {
            return ESN_ERR_SCRIPT_FAILED;
        }
        bool handled = false;
        if (napi_get_value_bool(env, result, &handled) == napi_ok) {
            *out_handled = handled ? 1 : 0;
        }
        return ESN_OK;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_plugin_reload(esn_plugin *handle)
{
    auto *plugin = reinterpret_cast<PluginImpl *>(handle);
    if (!plugin || !plugin->host || !plugin->host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }

    try {
        auto *host = plugin->host;
        const embed::Scope scope(host->node);
        napi_env env = host->napi;
        napi_value arg = nullptr;
        if (!env || napi_create_int32(env, plugin->id, &arg) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        napi_value report = nullptr;
        if (!callRuntime(host, "reload", 1, &arg, &report)) {
            return ESN_ERR_SCRIPT_FAILED;
        }
        napi_valuetype type = napi_undefined;
        bool is_array = false;
        if (napi_typeof(env, report, &type) != napi_ok || napi_is_array(env, report, &is_array) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        if (!is_array) {
            return ESN_ERR_BAD_ARGUMENT;  // unknown plugin id
        }
        napi_value first = nullptr;
        if (napi_get_element(env, report, 0, &first) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        napi_value ok_value = nullptr;
        bool ok = false;
        if (napi_get_named_property(env, first, "ok", &ok_value) != napi_ok ||
            napi_get_value_bool(env, ok_value, &ok) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        return ok ? ESN_OK : ESN_ERR_SCRIPT_FAILED;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_form_result(esn_host *handle, uint32_t form_id, int closed, const char *data,
                                        size_t length)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    try {
        const embed::Scope scope(host->node);
        napi_env env = host->napi;
        napi_value argv[3] = {nullptr, nullptr, nullptr};
        if (!env || napi_create_uint32(env, form_id, &argv[0]) != napi_ok ||
            napi_get_boolean(env, closed != 0, &argv[1]) != napi_ok ||
            napi_create_string_utf8(env, data ? data : "", length, &argv[2]) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        return callRuntime(host, "formResult", 3, argv, nullptr) ? ESN_OK : ESN_ERR_SCRIPT_FAILED;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_run_task(esn_host *handle, uint32_t task)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->node) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    try {
        const embed::Scope scope(host->node);
        napi_env env = host->napi;
        napi_value arg = nullptr;
        if (!env || napi_create_uint32(env, task, &arg) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        return callRuntime(host, "task", 1, &arg, nullptr) ? ESN_OK : ESN_ERR_SCRIPT_FAILED;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_dispatch_event(esn_host *handle, uint32_t subscription, esn_handle event)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->node || !host->started || !host->napi) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    try {
        const embed::Scope scope(host->node);
        napi_env env = host->napi;
        napi_value args[2] = {nullptr, nullptr};
        if (napi_create_uint32(env, subscription, &args[0]) != napi_ok ||
            napi_create_int64(env, static_cast<std::int64_t>(event), &args[1]) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        // Synchronous on purpose: the handler must be able to cancel the event before we return.
        return callRuntime(host, "dispatchEvent", 2, args, nullptr) ? ESN_OK : ESN_ERR_SCRIPT_FAILED;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_destroy(esn_host *handle, int *out_exit_code)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    try {
        if (host->node) {
            embed::destroy(host->node, out_exit_code);
            host->node = nullptr;
        }
    }
    catch (...) {
        if (out_exit_code) {
            *out_exit_code = -1;
        }
    }
    g_host = nullptr;
    delete host;
    return ESN_OK;
}

}  // extern "C"
