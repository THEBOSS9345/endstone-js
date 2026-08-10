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
               "produced them";
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

// invoke(handle, name, text, ...numbers) - one optional string plus any number of numeric arguments.
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
    // Arguments keep their JavaScript order within each kind: strings in one array, numbers in another.
    std::vector<std::string> strings;
    std::vector<double> numbers;
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
    }
    std::vector<const char *> string_ptrs;
    string_ptrs.reserve(strings.size());
    for (const auto &value : strings) {
        string_ptrs.push_back(value.c_str());
    }

    esn_handle result_handle = 0;
    const auto status = acc->invoke(g_host->api->context, handle, name.c_str(),
                                    string_ptrs.empty() ? nullptr : string_ptrs.data(), string_ptrs.size(),
                                    numbers.empty() ? nullptr : numbers.data(), numbers.size(), &result_handle);
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
    defineFunction(env, exports, "broadcastMessage", jsBroadcastMessage);
    defineFunction(env, exports, "get", jsGet);
    defineFunction(env, exports, "set", jsSet);
    defineFunction(env, exports, "invoke", jsInvoke);
    defineFunction(env, exports, "typeName", jsTypeName);
    defineFunction(env, exports, "subscribe", jsSubscribe);
    defineFunction(env, exports, "unsubscribe", jsUnsubscribe);
    defineFunction(env, exports, "updateCommands", jsUpdateCommands);
    if (g_host) {
        defineString(env, exports, "scriptPath", g_host->script_path.c_str());
    }
    defineString(env, exports, "compiledNodeVersion", embed::compiledNodeVersion());
    return exports;
}

// The bootstrap. Kept as source rather than a file so the host has no runtime asset to locate.
constexpr const char *kBootstrap = R"JS(
const binding = process._linkedBinding('endstone_node');
const util = require('node:util');
const forward = (level) => (...args) => binding.log(level, util.format(...args));
console.log = forward(2);
console.info = forward(2);
console.debug = forward(1);
console.trace = forward(1);
console.warn = forward(3);
console.error = forward(4);
process.on('uncaughtException', (err) => {
  binding.log(5, 'uncaughtException: ' + ((err && err.stack) || err));
});
process.on('unhandledRejection', (err) => {
  binding.log(4, 'unhandledRejection: ' + ((err && err.stack) || err));
});

// The host was compiled against one Node version; libnode at runtime could be another. N-API is
// ABI-stable so this usually still works, but say so rather than let it be mysterious.
if (binding.compiledNodeVersion && process.versions.node !== binding.compiledNodeVersion) {
  binding.log(3,
    `Node version mismatch: the host was built against v${binding.compiledNodeVersion} but libnode ` +
    `reports v${process.versions.node}. Rebuild the host against this libnode if anything misbehaves.`);
}

// A plugin calling process.exit() would take the Minecraft server down with it.
for (const name of ['exit', 'abort', 'reallyExit']) {
  const original = process[name];
  if (typeof original === 'function') {
    process[name] = (...args) => {
      binding.log(4,
        `a plugin called process.${name}(${args.join(', ')}); ignoring - that would stop the server. ` +
        `Use the Endstone API to shut down instead.`);
    };
  }
}

// --- plugin runtime -------------------------------------------------------------------------
// All plugins share this environment and event loop; isolation is by module scope, and each plugin
// gets its own require rooted at its own directory so it resolves its own node_modules.
const nodePath = require('node:path');
const nodeFs = require('node:fs');
const nodeUrl = require('node:url');
const NodeModule = require('node:module');

// --- @endstone-js/server -----------------------------------------------------------------------
// The API surface. Shaped after Endstone's own API (Server, Logger, and later Player and the
// PlayerXxxEvent family) rather than Bedrock's ScriptAPI, so it reads 1:1 with the Endstone docs.
const LEVELS = { trace: 0, debug: 1, info: 2, warning: 3, error: 4, critical: 5 };

function makeLogger(prefix) {
  const emit = (level) => (...args) => {
    const text = util.format(...args);
    binding.pluginLog(level, prefix ? `[${prefix}] ${text}` : text);
  };
  return {
    trace: emit(LEVELS.trace),
    debug: emit(LEVELS.debug),
    info: emit(LEVELS.info),
    warning: emit(LEVELS.warning),
    warn: emit(LEVELS.warning),
    error: emit(LEVELS.error),
    critical: emit(LEVELS.critical),
  };
}

const server = {
  get name() { return binding.serverName(); },
  get version() { return binding.serverVersion(); },
  get minecraftVersion() { return binding.serverMinecraftVersion(); },
  get protocolVersion() { return binding.serverProtocolVersion(); },
  get onlinePlayerCount() { return binding.serverOnlinePlayerCount(); },
  get isAvailable() { return binding.apiAvailable(); },
  /** The loaded level, or null before one exists. Safe to keep: the level outlives any callback. */
  get level() { return wrap(binding.serverLevel()); },
  logger: makeLogger(null),
  broadcastMessage(message) { binding.broadcastMessage(String(message)); },
};

// Handle-backed objects. A Proxy forwards property reads and writes straight to the generic
// accessors, so `event.player.name` and `event.cancelled = true` work without a binding per property.
// Handles are dispatch-scoped: valid only inside the callback that produced them.
const HANDLE = Symbol('endstone.handle');

// Methods rather than properties. Names are shared across object kinds; the Endstone side dispatches
// on the handle's actual type, so a name only has to appear once here.
const METHODS = new Set([
  'sendMessage', 'sendErrorMessage', 'sendPopup', 'sendTip', 'sendTitle', 'resetTitle', 'sendToast',
  'kick', 'performCommand', 'updateCommands', 'transfer', 'teleport', 'setRotation',
  'giveExp', 'giveExpLevels', 'playSound', 'stopSound', 'stopAllSounds', 'spawnParticle',
  'remove', 'getRelative', 'cancel',
  // Inventory
  'getItem', 'setItem', 'addItem', 'removeItem', 'clear', 'setHeldItemSlot',
  'setHelmet', 'setChestplate', 'setLeggings', 'setBoots', 'setItemInMainHand', 'setItemInOffHand',
]);

// An item is described by { type, amount, data } rather than by constructing an ItemStack, so the
// flattening is the same trick vectors use: strings and numbers in the order the host reads them.
const ITEM_KEYS = ['amount', 'data'];
const flattenItem = (item) => {
  if (item === null || item === undefined) return [''];  // empty type clears the slot
  if (typeof item === 'string') return [item, 1, 0];
  const type = typeof item.type === 'string' ? item.type : '';
  return [type, ...ITEM_KEYS.map((key) => (Number.isFinite(item[key]) ? item[key] : key === 'amount' ? 1 : 0))];
};

// Derived from size and getItem rather than crossing the ABI: the host has no way to return a boolean
// or an array from a method call, and looping a few dozen slots in JS costs nothing.
// Methods whose arguments include an item description rather than plain scalars.
const ITEM_METHODS = new Set([
  'setItem', 'addItem', 'removeItem',
  'setHelmet', 'setChestplate', 'setLeggings', 'setBoots', 'setItemInMainHand', 'setItemInOffHand',
]);

const INVENTORY_HELPERS = {
  contents() {
    const all = [];
    for (let slot = 0; slot < this.size; ++slot) all.push(this.getItem(slot));
    return all;
  },
  contains(type) {
    return this.first(type) !== -1;
  },
  first(type) {
    for (let slot = 0; slot < this.size; ++slot) {
      const item = this.getItem(slot);
      if (item && item.type === type) return slot;
    }
    return -1;
  },
  countOf(type) {
    let total = 0;
    for (let slot = 0; slot < this.size; ++slot) {
      const item = this.getItem(slot);
      if (item && item.type === type) total += item.amount;
    }
    return total;
  },
  containsAtLeast(type, amount) {
    return this.countOf(type) >= amount;
  },
};

/** The host tags nested objects so they can be told apart from plain numbers. */
const asHandle = (value) =>
  value !== null && typeof value === 'object' && typeof value.__esn_handle === 'number'
    ? value.__esn_handle
    : null;

// Vector and rotation arguments arrive as objects ({x,y,z} / {yaw,pitch}). Flatten them back into
// the positional numbers the host reads: x, y, z, then yaw, pitch. Reading through the Proxy works,
// so teleport(p.location, {...}) and teleport({x,y,z}) both behave; a missing member throws, which
// is fine - it just means that key is absent.
// A vector contributes exactly x, y, z; a rotation contributes yaw, pitch. A Location satisfies both,
// and the vector reading wins, so facing is always passed as its own argument - that is what makes
// teleport(location) keep the current facing while teleport(location, rotation) turns the actor.
const POSITION_KEYS = ['x', 'y', 'z'];
const ROTATION_KEYS = ['yaw', 'pitch'];
const readNumber = (obj, key) => {
  try {
    const value = obj[key];
    return typeof value === 'number' ? value : NaN;
  } catch {
    return NaN;
  }
};
// All of the keys must be present, so a partial object is passed through untouched rather than
// silently flattening into the wrong positions.
const numbersOf = (arg, keys) => {
  if (arg === null || typeof arg !== 'object') return null;
  const values = keys.map((key) => readNumber(arg, key));
  return values.every((value) => Number.isFinite(value)) ? values : null;
};
const flatten = (args) => {
  const flat = [];
  for (const arg of args) {
    const spread = numbersOf(arg, POSITION_KEYS) ?? numbersOf(arg, ROTATION_KEYS);
    if (spread) {
      flat.push(...spread);
    } else {
      flat.push(arg);
    }
  }
  return flat;
};

// Rotation writes go through their own flattening: only yaw/pitch are picked, so assigning
// `actor.rotation = actor.location` reads the facing off the location rather than the position.
const flattenRotation = (value) =>
  ['yaw', 'pitch'].filter((key) => Number.isFinite(readNumber(value, key))).map((key) => value[key]);

function wrap(handle) {
  if (!handle) return null;
  const proxy = new Proxy({ [HANDLE]: handle }, {
    get(_t, prop) {
      if (prop === HANDLE || prop === 'handle') return handle;
      if (typeof prop !== 'string') return undefined;
      if (prop === 'then') return undefined;            // do not look like a thenable
      if (prop === 'constructor') return Object;
      if (prop === 'toString') return () => `${binding.typeName(handle)}(${handle})`;
      if (prop === 'endstoneType') return binding.typeName(handle);
      // teleport is the one method whose second argument is an options object rather than a value,
      // so it is assembled here instead of going through flatten(). Rotation and dimension are both
      // left out when absent, and the host then keeps the actor's current ones.
      if (prop === 'teleport') {
        return (location, options = {}) => {
          const position = numbersOf(location, POSITION_KEYS);
          if (!position) {
            throw new TypeError('teleport(location): location needs numeric x, y and z');
          }
          const rotation = numbersOf(options && options.rotation, ROTATION_KEYS);
          const args = rotation ? [...position, ...rotation] : [...position];
          const dimension = options && options.dimension;
          if (dimension !== undefined && dimension !== null) args.push(String(dimension));
          binding.invoke(handle, 'teleport', ...args);
        };
      }
      // Inventory helpers are plain JavaScript over size/getItem, bound to this proxy.
      if (INVENTORY_HELPERS[prop] && binding.typeName(handle).endsWith('Inventory')) {
        return INVENTORY_HELPERS[prop].bind(proxy);
      }
      // Item arguments describe a stack; slot indices stay as plain numbers before them.
      if (ITEM_METHODS.has(prop)) {
        return (...args) => {
          const flat = [];
          for (const arg of args) {
            if (typeof arg === 'number') flat.push(arg);
            else flat.push(...flattenItem(arg));
          }
          const result = binding.invoke(handle, prop, ...flat);
          const nested = asHandle(result);
          return nested === null ? result : wrap(nested);
        };
      }
      if (METHODS.has(prop)) {
        // Arguments pass through as-is except vector/rotation objects, which flatten into the
        // positional numbers the host reads; the host sorts strings and numbers into their arrays.
        return (...args) => {
          const result = binding.invoke(handle, prop, ...flatten(args));
          const nested = asHandle(result);
          return nested === null ? result : wrap(nested);
        };
      }
      const value = binding.get(handle, prop);
      const nested = asHandle(value);
      return nested === null ? value : wrap(nested);
    },
    set(_t, prop, value) {
      if (typeof prop !== 'string') return false;
      if (prop === 'rotation') {
        // The `rotation` field is two numbers behind one object; route it through the same
        // dispatch as a method so the host reads yaw, pitch.
        binding.invoke(handle, 'setRotation', ...flattenRotation(value));
      } else {
        binding.set(handle, prop, value);
      }
      return true;
    },
    has(_t, prop) { return typeof prop === 'string'; },
  });
  return proxy;
}

// --- events ---------------------------------------------------------------------------------
// Named after Endstone's event classes minus the "Event" suffix, camelCased, so the mapping back to
// the Endstone documentation is mechanical.
const PRIORITIES = { lowest: 0, low: 1, normal: 2, high: 3, highest: 4, monitor: 5 };
// Subscriptions carry the plugin that made them so a reload can drop a plugin's handlers wholesale.
// A null plugin tag belongs to the runtime itself and survives reloads.
const handlers = new Map();
let activePluginId = null;

function dispatchEvent(subscription, handle) {
  const entry = handlers.get(subscription);
  if (!entry) return;
  const event = wrap(handle);
  for (const { handler } of entry) {
    try {
      handler(event);
    } catch (err) {
      binding.log(4, `event handler threw: ${(err && err.stack) || err}`);
    }
  }
}

// Every Endstone event. Subscription is generic - the name is passed straight through to Endstone's
// PluginManager - so this list is about discoverability and typo-catching, not capability.
const EVENT_NAMES = [
  // Player
  'playerJoin', 'playerQuit', 'playerChat', 'playerCommand', 'playerDeath', 'playerLogin',
  'playerKick', 'playerInteract', 'playerInteractActor', 'playerMove', 'playerJump',
  'playerTeleport', 'playerPortal', 'playerRespawn', 'playerGameModeChange', 'playerItemConsume',
  'playerItemHeld', 'playerPickupItem', 'playerDropItem', 'playerBedEnter', 'playerBedLeave',
  'playerDimensionChange', 'playerEmote', 'playerSkinChange',
  // Actor
  'actorDamage', 'actorDeath', 'actorExplode', 'actorKnockback', 'actorRemove', 'actorSpawn',
  'actorTeleport',
  // Block
  'blockBreak', 'blockPlace', 'blockCook', 'blockExplode', 'blockForm', 'blockFromTo', 'blockGrow',
  'blockPistonExtend', 'blockPistonRetract', 'leavesDecay',
  // Server
  'serverLoad', 'serverCommand', 'serverListPing', 'broadcastMessage', 'scriptMessage',
  'packetReceive', 'packetSend', 'pluginEnable', 'pluginDisable', 'mapInitialize',
  // World
  'chunkLoad', 'chunkUnload', 'thunderChange', 'weatherChange',
];

const toEndstoneName = (camel) => camel.charAt(0).toUpperCase() + camel.slice(1) + 'Event';

const events = {};
for (const name of EVENT_NAMES) {
  events[name] = (handler, options = {}) => {
    if (typeof handler !== 'function') throw new TypeError(`${name}: handler must be a function`);
    const priority = PRIORITIES[String(options.priority ?? 'normal').toLowerCase()] ?? PRIORITIES.normal;
    const subscription = binding.subscribe(toEndstoneName(name), priority, options.ignoreCancelled === true);
    if (!handlers.has(subscription)) handlers.set(subscription, []);
    const record = { handler, plugin: activePluginId };
    handlers.get(subscription).push(record);
    return {
      unsubscribe() {
        const list = handlers.get(subscription);
        if (!list) return;
        for (let i = list.length - 1; i >= 0; --i) {
          if (list[i] === record) list.splice(i, 1);
        }
        if (list.length === 0) {
          handlers.delete(subscription);
          binding.unsubscribe(subscription);
        }
      },
    };
  };
}

// --- commands -----------------------------------------------------------------------------------
// Commands are registered in code rather than declared in a manifest, so a plugin can add or drop one
// at any time and a reload picks up the change. Every command is a slash command, reached as `/name`
// from a player or the console. Registering a name the server already uses shadows it, since the
// command line is intercepted and cancelled before it reaches the server.
const commandsByName = new Map();
let commandRouters = null;
// Non-null only while a plugin's module is being evaluated. Commands registered into it are declared
// to Endstone, which is what gets them into Bedrock's registry - and therefore into the client's
// command list, with autocomplete and validated arguments. Anything registered later can only be
// intercepted.
let collectingDeclarations = null;
// True until the bootstrap has finished. The runtime's own commands are registered during it and can
// never be declared - there is no plugin module for them to be declared in - so they are exempt from
// the "registered too late" warning, which would otherwise give advice that cannot be followed.
let bootstrapping = true;

// Console commands need a sender too. `isConsole` tells a handler which it has - a wrapped Player
// returns undefined for it, since unknown members read as undefined.
const consoleSender = {
  name: 'Console',
  isConsole: true,
  isOp: true,
  sendMessage(text) { binding.log(2, String(text).replace(/§./g, '')); },
  sendErrorMessage(text) { binding.log(4, String(text).replace(/§./g, '')); },
};

const splitArgs = (rest) => {
  const trimmed = String(rest ?? '').trim();
  return trimmed === '' ? [] : trimmed.split(/\s+/);
};

const lower = (value) => String(value).toLowerCase();

/** Matches a bare command word against registered names and aliases. */
function findCommand(token) {
  const key = lower(token);
  const direct = commandsByName.get(key);
  if (direct) return direct;
  for (const command of commandsByName.values()) {
    if (command.aliases.includes(key)) return command;
  }
  return null;
}

function runCommand(command, sender, args, raw) {
  if (command.op && sender.isOp !== true) {
    // Logged as well as replied to: a refusal that only appears in the player's chat is impossible
    // to tell apart from a command that never fired at all.
    binding.log(2, `refused '/${command.name}' for ${sender.name}: operators only`);
    sender.sendMessage('§cYou do not have permission to use that command.');
    return;
  }
  try {
    command.handler(sender, args, raw);
  } catch (err) {
    binding.log(4, `command '${command.name}' threw: ${(err && err.stack) || err}`);
    sender.sendMessage('§cThat command failed. See the server log.');
  }
}

function ensureCommandRouters() {
  if (commandRouters || !binding.apiAvailable()) return;
  // The routers belong to the runtime, not to whichever plugin happened to register first, so the
  // attribution is cleared - otherwise reloading that plugin would drop command handling entirely.
  const previous = activePluginId;
  activePluginId = null;
  try {
    const route = (event, sender) => {
      const line = String(event.command ?? '').trim().replace(/^\//, '');
      const match = /^(\S+)([\s\S]*)$/.exec(line);
      if (!match) return;
      const command = findCommand(match[1]);
      if (!command || command.declared) return;
      event.cancelled = true;
      runCommand(command, sender, splitArgs(match[2]), line);
    };
    commandRouters = [
      // Lowest priority so a plugin watching playerCommand still sees the line first.
      // Declared commands are dispatched by Endstone through onCommand, so the router must not
      // cancel them here - it would swallow the command before Endstone ever ran it.
      events.playerCommand((event) => route(event, event.player), { priority: 'lowest' }),
      events.serverCommand((event) => route(event, consoleSender), { priority: 'lowest' }),
    ];
  }
  finally {
    activePluginId = previous;
  }
}

const commands = {
  register(name, handler, options = {}) {
    if (typeof name !== 'string' || name.trim() === '') {
      throw new TypeError('commands.register: name must be a non-empty string');
    }
    if (typeof handler !== 'function') {
      throw new TypeError(`commands.register('${name}'): handler must be a function`);
    }
    const key = lower(name.trim());
    if (commandsByName.has(key)) {
      throw new Error(`commands.register: '${key}' is already registered`);
    }
    // `usages` is the real thing - Endstone parses each one to build the client's argument list.
    // `usage` stays accepted as the singular shorthand. An empty list means a bare "/name".
    const usages = options.usages ? options.usages.map(String)
      : options.usage ? [String(options.usage)]
      : [];
    const record = {
      name: key,
      handler,
      description: String(options.description ?? ''),
      usages,
      aliases: (options.aliases ?? []).map(lower),
      op: options.op === true,
      permissions: (options.permissions ?? []).map(String),
      plugin: activePluginId,
      declared: false,
    };

    if (collectingDeclarations) {
      record.declared = true;
      record.plugin = collectingDeclarations.pluginId;
      collectingDeclarations.records.push(record);
    }
    else if (!bootstrapping) {
      // Registered after the module finished loading, so it missed the description Endstone builds at
      // load time. It still runs, via interception - it just cannot appear in the client's list.
      binding.log(3,
        `command '/${key}' was registered after loading, so it will not appear in the client's ` +
        `command list or autocomplete. Move the commands.register call to the top level of your ` +
        `plugin module to have it registered properly.`);
    }

    commandsByName.set(key, record);
    ensureCommandRouters();
    return {
      unregister() {
        if (commandsByName.get(key) === record) commandsByName.delete(key);
      },
    };
  },

  list() {
    return [...commandsByName.values()].map((command) => ({
      name: command.name,
      description: command.description,
      usages: [...command.usages],
      aliases: [...command.aliases],
      op: command.op,
      permissions: [...command.permissions],
      declared: command.declared,
    }));
  },
};

/**
 * Drops the commands a plugin registered, so a reload does not leave duplicates behind.
 *
 * Returns the names that were declared to Endstone. Those stay registered on the Endstone side for the
 * life of the server, so a reload has to re-declare them rather than treat them as new.
 */
function dropCommands(pluginId) {
  const wasDeclared = new Set();
  for (const [key, record] of commandsByName) {
    if (record.plugin !== pluginId) continue;
    if (record.declared) wasDeclared.add(key);
    commandsByName.delete(key);
  }
  return wasDeclared;
}

/**
 * Runs a declared command, called from C++ when Endstone dispatches one.
 *
 * Returns false when nothing handled it, which makes Endstone show the command's usage.
 */
function runDeclaredCommand(pluginId, name, senderHandle, args) {
  const command = commandsByName.get(lower(name)) ?? findCommand(name);
  if (!command) return false;
  const sender = wrap(senderHandle) ?? consoleSender;
  runCommand(command, sender, args, [name, ...args].join(' '));
  return true;
}

const endstoneModule = {
  server, events, commands, logger: server.logger, LogLevel: LEVELS, EventPriority: PRIORITIES,
};

// Handed to the virtual module's source through a well-known symbol rather than a bare global.
const API_SYMBOL = Symbol.for('endstone.api');
Object.defineProperty(globalThis, API_SYMBOL, {
  value: endstoneModule, enumerable: false, configurable: false, writable: false,
});

// A real ES module with named exports, served from memory. registerHooks is synchronous and applies
// to require() as well as import(), so one hook covers CommonJS and ESM plugins alike. CommonJS
// plugins get the namespace through Node's require(esm) support.
const VIRTUAL_URL = 'endstone:server';
// The named exports are generated from the API object rather than listed by hand: a hand-written list
// silently goes out of date, and the failure lands on the plugin author as "does not provide an export
// named 'x'" rather than on whoever added x.
const VIRTUAL_SOURCE = [
  `const api = globalThis[Symbol.for('endstone.api')];`,
  ...Object.keys(endstoneModule).map((name) => `export const ${name} = api.${name};`),
  `export default api;`,
  ``,
].join('\n');

NodeModule.registerHooks({
  resolve(specifier, context, nextResolve) {
    if (specifier === '@endstone-js/server') {
      return { url: VIRTUAL_URL, shortCircuit: true };
    }
    return nextResolve(specifier, context);
  },
  load(url, context, nextLoad) {
    if (url === VIRTUAL_URL) {
      return { format: 'module', source: VIRTUAL_SOURCE, shortCircuit: true };
    }
    return nextLoad(url, context);
  },
});
const plugins = new Map();
const pendingLoads = new Map();
let nextPluginId = 1;
let nextToken = 1;
let nextReloadCopy = 1;

const asArray = (value) =>
  value == null ? [] : Array.isArray(value) ? value.map(String) : [String(value)];

function readManifest(dir) {
  // Strip a UTF-8 BOM: editors and shells on Windows add one, and JSON.parse rejects it.
  const raw = nodeFs.readFileSync(nodePath.join(dir, 'package.json'), 'utf8').replace(/^﻿/, '');
  return JSON.parse(raw);
}

function ensureDependencies(dir, manifest) {
  const declared = Object.keys(manifest.dependencies || {});
  if (declared.length === 0) return;
  if (nodeFs.existsSync(nodePath.join(dir, 'node_modules'))) return;

  const endstone = manifest.endstone || {};
  if (endstone.autoInstall !== true) {
    // Dependencies are the user's to install; say so plainly instead of letting them meet a raw
    // MODULE_NOT_FOUND from deep inside the loader.
    binding.log(3,
      `plugin '${manifest.name}' declares ${declared.length} dependency/ies (${declared.join(', ')}) ` +
      `but has no node_modules. Run "npm install" in ${dir} - loading will fail if the plugin ` +
      `imports them. Set endstone.autoInstall to true in package.json to install automatically.`);
    return;
  }

  // Opt-in only, and synchronous on purpose: the plugin must not start before its dependencies
  // exist. This blocks the server thread, which is why it is not the default.
  binding.log(2, `installing dependencies for '${manifest.name}' (npm install in ${dir})...`);
  try {
    const { execFileSync } = require('node:child_process');
    const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
    execFileSync(npm, ['install', '--omit=dev', '--no-audit', '--no-fund'], {
      cwd: dir,
      stdio: 'pipe',
      shell: process.platform === 'win32',
    });
    binding.log(2, `dependencies installed for '${manifest.name}'`);
  } catch (err) {
    binding.log(4,
      `npm install failed for '${manifest.name}': ${(err && err.message) || err}. ` +
      `Install manually with "npm install" in ${dir}.`);
  }
}

async function loadPluginAsync(target) {
  const isDirectory = nodeFs.statSync(target).isDirectory();
  let dir, entry, manifest;
  if (isDirectory) {
    dir = target;
    manifest = readManifest(target);
    entry = nodePath.resolve(dir, manifest.main || 'index.js');
  } else {
    dir = nodePath.dirname(target);
    entry = target;
    manifest = { name: nodePath.basename(target, nodePath.extname(target)) };
  }

  ensureDependencies(dir, manifest);

  // ESM and CommonJS are both first class, and both go through require().
  //
  // Not import(): the bootstrap has no file identity, so dynamic import() from it is routed to
  // Node's *embedder* import callback, which resolves only node: builtins and fails on a file URL
  // with ERR_UNKNOWN_BUILTIN_MODULE. require() has no such problem, and since Node 22.12 it loads
  // ES modules synchronously and returns their namespace, so one path serves both formats.
  const isEsm = manifest.type === 'module' || /\.mjs$/i.test(entry);
  const pluginRequire = NodeModule.createRequire(nodePath.join(dir, 'package.json'));

  // The plugin gets its id before its module runs, so commands registered at the top level can be
  // attributed to it and travel back with the metadata Endstone builds its description from.
  const id = nextPluginId++;
  const declarations = { pluginId: id, records: [] };
  const previousCollector = collectingDeclarations;
  collectingDeclarations = declarations;
  let loaded;
  try {
    loaded = pluginRequire(entry);
  }
  finally {
    collectingDeclarations = previousCollector;
  }
  // An ES module namespace exposes its default export as .default.
  const instance = (loaded && loaded.default) || loaded;
  const endstone = manifest.endstone || {};

  plugins.set(id, { id, dir, entry, instance, manifest, esm: isEsm, dirPlugin: isDirectory });
  return {
    id,
    // Endstone requires lowercase [a-z0-9_]; strip any npm scope and normalize separators.
    name: String(manifest.name || '').replace(/^@[^/]+\//, '').replace(/-/g, '_').toLowerCase(),
    version: String(manifest.version || '0.0.0'),
    apiVersion: endstone.apiVersion == null ? null : String(endstone.apiVersion),
    description: manifest.description == null ? null : String(manifest.description),
    website: manifest.homepage == null ? null : String(manifest.homepage),
    loadOrder: endstone.load == null ? null : String(endstone.load).toLowerCase(),
    authors: asArray(manifest.author ?? manifest.authors),
    depend: asArray(endstone.depend),
    // Endstone registers these with Bedrock, so a usage like "/gm <survival|creative>" becomes a real
    // enum argument the client completes. A usage must start with "/" + the command name.
    commands: declarations.records.map((command) => ({
      name: command.name,
      description: command.description,
      usages: command.usages.length ? command.usages : [`/${command.name}`],
      aliases: command.aliases,
      permissions: command.permissions,
    })),
  };
}

// One plugin load failure becomes a concise, actionable report. The first line says what happened and
// where; the full stack survives separately as `detail` for the cases where that is not enough.
function formatLoadError(err, target) {
  if (!err) return "unknown load failure";
  const name = err.name || "Error";
  const raw = (err.message || String(err)).trim();
  const code = err.code;

  if (code === "ERR_REQUIRE_ASYNC_MODULE") {
    return `${target}: the plugin uses top-level await, which an embedded Node.js host cannot load. ` +
           "Move the awaited work into onEnable() instead.";
  }
  if (code === "MODULE_NOT_FOUND" || code === "ERR_MODULE_NOT_FOUND") {
    const missing = /['"]([^'"]+)['"]/.exec(raw);
    const from = (err.requireStack && err.requireStack[0]) || "";
    return `${target}: cannot find module${missing ? ` '${missing[1]}'` : ""}` +
           `${from ? ` required from ${from}` : ""}. Run "npm install" in the plugin folder, or fix the import path.`;
  }
  if (code === "ERR_REQUIRE_ESM") {
    return `${target}: this file is an ES module and must be imported, not required. ` +
           'Add "type": "module" to the plugin\'s package.json or rename it to .mjs.';
  }

  // First stack frame that points at a real file rather than into Node itself.
  let frame = "";
  if (typeof err.stack === "string") {
    for (const line of err.stack.split("\n").slice(1)) {
      const trimmed = line.trim();
      if (!trimmed || /node:internal|internal\//.test(trimmed)) continue;
      frame = trimmed;
      break;
    }
  }

  if (name === "SyntaxError") {
    const at = /\((\S+?):(\d+):(\d+)\)$/.exec(frame) || /^(\S+?):(\d+):(\d+)$/.exec(frame);
    return `${target}: syntax error${at ? ` in ${at[1]} at line ${at[2]}, column ${at[3]}` : ""}: ${raw}`;
  }
  if (frame) {
    return `${target}: ${raw} at ${frame.replace(/^at\s+/, "")}`;
  }
  return `${target}: ${raw}`;
}

// Loading is asynchronous because ESM imports are. The host starts a load, then pumps the loop and
// polls until the result appears.
function beginLoad(target) {
  const token = nextToken++;
  pendingLoads.set(token, null);
  void (async () => {
    try {
      pendingLoads.set(token, { ok: true, meta: await loadPluginAsync(target) });
    } catch (err) {
      pendingLoads.set(token, {
        ok: false,
        message: formatLoadError(err, target),
        detail: (err && err.stack) || String(err),
      });
    }
  })();
  return token;
}

function pollLoad(token) {
  const result = pendingLoads.get(token);
  if (result == null) return null;
  pendingLoads.delete(token);
  return result;
}

const HOOKS = ['onLoad', 'onEnable', 'onDisable'];

function invokePlugin(id, hook) {
  const record = plugins.get(id);
  if (!record) throw new Error(`unknown plugin id ${id}`);
  const name = HOOKS[hook];
  const target = record.instance;
  if (!target || typeof target[name] !== 'function') return;
  // A hook may be async. The server does not wait for it, but a rejection is still reported rather
  // than surfacing later as an unhandledRejection with no context.
  activePluginId = id;
  try {
    const result = target[name]();
    if (result && typeof result.then === 'function') {
      result.catch((err) => {
        binding.log(4, `${record.manifest.name}.${name}() rejected: ${(err && err.stack) || err}`);
      });
    }
  } catch (err) {
    binding.log(4, `${record.manifest.name}.${name}() threw: ${(err && err.stack) || err}`);
  }
  finally {
    activePluginId = null;
  }
}

function unloadPlugin(id) {
  // Commands are dropped here but subscriptions are not: unload runs during shutdown, and
  // binding.unsubscribe would call back into a plugin manager that may already be tearing down.
  dropCommands(id);
  plugins.delete(id);
}

// --- plugin reload ------------------------------------------------------------------------------
// The Node host is not re-creatable, so a reload re-runs the plugin's own module instead: disable
// the old instance, drop its event subscriptions, purge its files from the require cache, then
// require the entry again and run onLoad/onEnable. The esn_plugin handle and the plugin's identity
// in Endstone never change, so the C++ side needs no involvement. Plugins must clear their own
// timers in onDisable, since only event subscriptions are tracked here.

function dropSubscriptions(pluginId) {
  for (const [subscription, list] of handlers) {
    let removed = false;
    for (let i = list.length - 1; i >= 0; --i) {
      if (list[i].plugin === pluginId) {
        list.splice(i, 1);
        removed = true;
      }
    }
    if (removed && list.length === 0) {
      handlers.delete(subscription);
      binding.unsubscribe(subscription);
    }
  }
}

function purgeModuleCache(dir) {
  // Local code under the plugin's directory is what a reload must pick up; node_modules deps are
  // left cached so shared state is not disturbed.
  //
  // Both the given path and its realpath count as roots: a plugin directory is often a symlink into
  // a checkout, and Node keys the require cache by realpath, so matching only the link path would
  // purge nothing and the reload would silently re-run the old code.
  const roots = [nodePath.resolve(dir)];
  try {
    const real = nodeFs.realpathSync(dir);
    if (real !== roots[0]) roots.push(real);
  } catch { /* the directory may have gone away; the resolved path is enough */ }
  const under = (file) => roots.some((root) => file.startsWith(root + nodePath.sep));
  const inNodeModules = (file) =>
    roots.some((root) => file.startsWith(nodePath.join(root, 'node_modules') + nodePath.sep));
  // The bootstrap's `require` is the embedder's stripped require with no .cache property; the real
  // registry is Module._cache.
  const cache = NodeModule._cache || {};
  for (const filename of Object.keys(cache)) {
    if (under(filename) && !inNodeModules(filename)) delete cache[filename];
  }
  const pathCache = NodeModule._pathCache || {};
  for (const key of Object.keys(pathCache)) delete pathCache[key];
}

/** Runs a lifecycle hook, tagging the plugin for subscription attribution and reporting rejections. */
function runHook(record, id, name) {
  const target = record.instance;
  if (!target || typeof target[name] !== 'function') return;
  activePluginId = id;
  try {
    const result = target[name]();
    if (result && typeof result.then === 'function') {
      result.catch((err) => {
        binding.log(4, `${record.manifest.name}.${name}() rejected: ${(err && err.stack) || err}`);
      });
    }
  } catch (err) {
    binding.log(4, `${record.manifest.name}.${name}() threw: ${(err && err.stack) || err}`);
  }
  finally {
    activePluginId = null;
  }
}

/** Recursively copies a plugin directory, leaving node_modules (and its contents) out. */
function copyPluginTree(dir, copy) {
  for (const entry of nodeFs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === 'node_modules') continue;
    const src = nodePath.join(dir, entry.name);
    const dst = nodePath.join(copy, entry.name);
    if (entry.isDirectory()) {
      nodeFs.mkdirSync(dst, { recursive: true });
      copyPluginTree(src, dst);
    }
    else if (entry.isFile()) {
      nodeFs.copyFileSync(src, dst);
    }
  }
}

// The ES module loader keys its cache by realpath and exposes no way to clear it, so an in-place
// re-require returns the module from the first load. Loading from a uniquely named sibling copy gives
// the loader a fresh realpath: the plugin's edits are re-read, while node_modules (junctioned through)
// stays shared and cached, matching the CommonJS reload path.
function shadowCopyForReload(record) {
  const root = nodePath.dirname(record.dir);
  const prefix = `.${nodePath.basename(record.dir)}-reload-`;
  for (const stale of nodeFs.readdirSync(root)) {
    if (stale.startsWith(prefix)) {
      nodeFs.rmSync(nodePath.join(root, stale), { recursive: true, force: true });
    }
  }
  const copy = nodePath.join(root, prefix + nextReloadCopy++);
  nodeFs.mkdirSync(copy, { recursive: true });
  copyPluginTree(record.dir, copy);
  const realNodeModules = nodePath.join(record.dir, 'node_modules');
  if (nodeFs.existsSync(realNodeModules)) {
    nodeFs.symlinkSync(realNodeModules, nodePath.join(copy, 'node_modules'),
      process.platform === 'win32' ? 'junction' : 'dir');
  }
  return copy;
}

/** Reloads every loaded JavaScript plugin, or just the one named. Returns a per-plugin report. */
function reloadPlugins(filter) {
  const wanted = [...plugins.values()].filter((record) => {
    if (!filter) return true;
    if (typeof filter === 'number') return record.id === filter;
    return record.manifest.name === filter || nodePath.basename(record.dir) === filter;
  });
  const report = [];
  for (const record of wanted) {
    try {
      runHook(record, record.id, 'onDisable');
      dropSubscriptions(record.id);
      const wasDeclared = dropCommands(record.id);

      // Pick up edits to package.json, including a changed main entry point.
      if (nodeFs.existsSync(nodePath.join(record.dir, 'package.json'))) {
        record.manifest = readManifest(record.dir);
      }
      record.entry = nodePath.resolve(record.dir, record.manifest.main || 'index.js');
      purgeModuleCache(record.dir);

      let loadDir = record.dir;
      let entry = record.entry;
      if (record.esm && record.dirPlugin) {
        try {
          loadDir = shadowCopyForReload(record);
          entry = nodePath.resolve(loadDir, record.manifest.main || 'index.js');
        }
        catch (copyErr) {
          binding.log(3,
            `reload of '${record.manifest.name}': shadow copy failed ` +
            `(${(copyErr && copyErr.message) || copyErr}); reloading in place - ESM edits may not apply`);
        }
      }

      // Collect again, so top-level registrations are re-attached to this plugin.
      const declarations = { pluginId: record.id, records: [] };
      const previousCollector = collectingDeclarations;
      collectingDeclarations = declarations;
      let loaded;
      try {
        const pluginRequire = NodeModule.createRequire(nodePath.join(loadDir, 'package.json'));
        loaded = pluginRequire(entry);
      }
      finally {
        collectingDeclarations = previousCollector;
      }
      record.instance = (loaded && loaded.default) || loaded;

      // A name Endstone never saw cannot reach Bedrock's registry now - the description was built at
      // load. It still works by interception, so say so rather than failing the reload.
      for (const declared of declarations.records) {
        if (!wasDeclared.has(declared.name)) {
          declared.declared = false;
          binding.log(3,
            `'/${declared.name}' is new since the server started, so it will not appear in the ` +
            `client's command list until the server restarts. It works in the meantime.`);
        }
      }

      runHook(record, record.id, 'onLoad');
      runHook(record, record.id, 'onEnable');
      report.push({ name: record.manifest.name, ok: true });
    }
    catch (err) {
      binding.log(4, `reload of '${record.manifest.name}' failed: ${(err && err.stack) || err}`);
      report.push({ name: record.manifest.name, ok: false });
    }
  }
  // Which commands a player is allowed to see can have changed; push the refreshed list so it takes
  // effect without anyone reconnecting.
  if (binding.apiAvailable()) binding.updateCommands();
  return report;
}

binding.setRuntime({
  beginLoad, pollLoad, invoke: invokePlugin, unload: unloadPlugin, dispatchEvent,
  reload: reloadPlugins, command: runDeclaredCommand,
});

// --- the built-in reload command ----------------------------------------------------------------
// Registered by the runtime rather than by a plugin, so it works on every server and survives a
// reload of anything - and it is the first user of the command API. Operator-only: reloading every
// plugin is not something an ordinary player should be able to do.
if (binding.apiAvailable()) {
  // Named jsreload, not reload: registering `reload` would intercept and cancel the server's own
  // /reload before it ever ran.
  commands.register('jsreload', (sender, args) => {
    const report = reloadPlugins(args[0]);
    const ok = report.filter((r) => r.ok).length;
    const summary = args[0]
      ? `Reloaded ${ok}/${report.length} matching plugin(s).`
      : `Reloaded ${ok}/${report.length} plugin(s).`;
    binding.log(2, `[reload] ${summary}`);
    sender.sendMessage(`§a${summary}`);
    for (const entry of report) {
      binding.log(2, `[reload] ${entry.name}: ${entry.ok ? 'ok' : 'failed'}`);
      if (!entry.ok) {
        sender.sendMessage(`§c${entry.name}: failed to reload`);
      }
    }
  }, {
    description: 'Reloads JavaScript plugins, or just the one named.',
    usage: '/jsreload [plugin]',
    op: true,
  });
}

// From here on, a registration that is not part of a plugin's module load really is too late, and the
// warning's advice - move it to the top level - is something the author can act on.
bootstrapping = false;

// Optional standalone entry point for a script run without BDS; plugin mode does not require it.
if (binding.scriptPath && nodeFs.existsSync(binding.scriptPath)) {
  const publicRequire = NodeModule.createRequire(binding.scriptPath);
  globalThis.require = publicRequire;
  publicRequire(binding.scriptPath);
}
)JS";

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
        if (!embed::loadEnvironment(host->node, kBootstrap, kBindingName, registerBinding, errorSink(host))) {
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
