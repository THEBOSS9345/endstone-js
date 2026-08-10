// Copyright (c) 2026, The Endstone Project. (https://endstone.dev) All Rights Reserved.
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

// The Node.js side of the ABI firewall. This translation unit links libnode and must never include
// an Endstone header: on Linux it is compiled against libstdc++ while Endstone uses libc++.
// std:: types are free to be used *inside* here; none may appear in endstone_node_abi.h.

#include "endstone_node_abi.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <node.h>
#include <node_api.h>
#include <uv.h>
#include <v8.h>

namespace {

struct HostImpl {
    esn_log_fn log{nullptr};
    void *log_user_data{nullptr};
    std::string script_path;
    std::string exec_path;
    std::shared_ptr<node::InitializationResult> init_result;
    node::MultiIsolatePlatform *platform{nullptr};
    std::unique_ptr<node::CommonEnvironmentSetup> setup;
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

// endstone_node.log(level, message) - the only native function JavaScript gets in this spike.
napi_value jsLog(napi_env env, napi_callback_info info)
{
    std::size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
        return nullptr;
    }

    std::int32_t level = ESN_LOG_INFO;
    (void)napi_get_value_int32(env, argv[0], &level);

    std::size_t length = 0;
    if (napi_get_value_string_utf8(env, argv[1], nullptr, 0, &length) != napi_ok) {
        return nullptr;
    }
    std::vector<char> buffer(length + 1, '\0');
    std::size_t copied = 0;
    if (napi_get_value_string_utf8(env, argv[1], buffer.data(), buffer.size(), &copied) != napi_ok) {
        return nullptr;
    }
    emit(g_host, clampLevel(level), buffer.data(), copied);
    return nullptr;
}

// endstone_node.setRuntime({ load, meta, invoke, unload }) - the bootstrap hands the plugin runtime
// over once, and the C side keeps a strong reference to call back into it.
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

napi_value registerBinding(napi_env env, napi_value exports)
{
    napi_value fn = nullptr;
    if (napi_create_function(env, "log", NAPI_AUTO_LENGTH, jsLog, nullptr, &fn) == napi_ok) {
        (void)napi_set_named_property(env, exports, "log", fn);
    }
    napi_value set_runtime = nullptr;
    if (napi_create_function(env, "setRuntime", NAPI_AUTO_LENGTH, jsSetRuntime, nullptr, &set_runtime) == napi_ok) {
        (void)napi_set_named_property(env, exports, "setRuntime", set_runtime);
    }
    napi_value script_path = nullptr;
    if (g_host && napi_create_string_utf8(env, g_host->script_path.c_str(), NAPI_AUTO_LENGTH, &script_path) == napi_ok) {
        (void)napi_set_named_property(env, exports, "scriptPath", script_path);
    }
    return exports;
}

// Routes console through the linked binding, then hands over to the user script. Kept as source
// rather than a file so the host has no runtime asset to locate.
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
// --- plugin runtime -------------------------------------------------------------------------
// All plugins share this environment and event loop; isolation is by module scope, and each plugin
// gets its own require rooted at its own directory so it resolves its own node_modules.
const nodePath = require('node:path');
const nodeFs = require('node:fs');
const NodeModule = require('node:module');
const plugins = new Map();
let nextPluginId = 1;

const asArray = (value) =>
  value == null ? [] : Array.isArray(value) ? value.map(String) : [String(value)];

function loadPlugin(target) {
  const isDirectory = nodeFs.statSync(target).isDirectory();
  let dir, entry, manifest;
  if (isDirectory) {
    dir = target;
    // Strip a UTF-8 BOM: editors and shells on Windows add one, and JSON.parse rejects it.
    const raw = nodeFs.readFileSync(nodePath.join(target, 'package.json'), 'utf8').replace(/^﻿/, '');
    manifest = JSON.parse(raw);
    entry = nodePath.resolve(dir, manifest.main || 'index.js');
  } else {
    dir = nodePath.dirname(target);
    entry = target;
    manifest = { name: nodePath.basename(target, nodePath.extname(target)) };
  }

  // Dependencies are the user's to install; say so plainly instead of letting them meet a raw
  // MODULE_NOT_FOUND from deep inside the loader.
  const declared = Object.keys(manifest.dependencies || {});
  if (declared.length > 0 && !nodeFs.existsSync(nodePath.join(dir, 'node_modules'))) {
    binding.log(3,
      `plugin '${manifest.name}' declares ${declared.length} dependency/ies ` +
      `(${declared.join(', ')}) but has no node_modules. Run "npm install" in ${dir} - ` +
      `loading will fail if the plugin imports them.`);
  }

  // Rooted at the plugin's own directory so `require('some-dep')` finds its node_modules.
  const pluginRequire = NodeModule.createRequire(nodePath.join(dir, 'package.json'));
  const loaded = pluginRequire(entry);
  const instance = (loaded && loaded.default) || loaded;
  const endstone = manifest.endstone || {};

  const id = nextPluginId++;
  plugins.set(id, { id, dir, entry, instance, manifest });
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
  };
}

const HOOKS = ['onLoad', 'onEnable', 'onDisable'];

function invokePlugin(id, hook) {
  const record = plugins.get(id);
  if (!record) throw new Error(`unknown plugin id ${id}`);
  const name = HOOKS[hook];
  const target = record.instance;
  if (!target || typeof target[name] !== 'function') return;
  // Errors are reported and swallowed here so one plugin cannot take down its neighbours.
  target[name]();
}

function unloadPlugin(id) {
  plugins.delete(id);
}

binding.setRuntime({ load: loadPlugin, invoke: invokePlugin, unload: unloadPlugin });

// Optional standalone entry point; the spike uses it, plugin mode does not require it.
if (binding.scriptPath && nodeFs.existsSync(binding.scriptPath)) {
  const publicRequire = NodeModule.createRequire(binding.scriptPath);
  globalThis.require = publicRequire;
  publicRequire(binding.scriptPath);
}
)JS";

void reportTryCatch(const HostImpl *host, v8::Isolate *isolate, const v8::TryCatch &try_catch)
{
    if (!try_catch.HasCaught()) {
        return;
    }
    const v8::String::Utf8Value message(isolate, try_catch.Exception());
    emitf(host, ESN_LOG_ERROR, "JavaScript exception: %s", *message ? *message : "<unknown>");
    v8::Local<v8::Value> stack;
    if (try_catch.StackTrace(isolate->GetCurrentContext()).ToLocal(&stack)) {
        const v8::String::Utf8Value stack_text(isolate, stack);
        if (*stack_text) {
            emitf(host, ESN_LOG_ERROR, "%s", *stack_text);
        }
    }
}

bool readString(napi_env env, napi_value object, const char *key, std::string &out)
{
    napi_value value = nullptr;
    napi_valuetype type = napi_undefined;
    if (napi_get_named_property(env, object, key, &value) != napi_ok ||
        napi_typeof(env, value, &type) != napi_ok || type != napi_string) {
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
        if (napi_get_element(env, array, i, &element) != napi_ok) {
            continue;
        }
        std::size_t size = 0;
        if (napi_get_value_string_utf8(env, element, nullptr, 0, &size) != napi_ok) {
            continue;
        }
        std::vector<char> buffer(size + 1, '\0');
        std::size_t copied = 0;
        if (napi_get_value_string_utf8(env, element, buffer.data(), buffer.size(), &copied) == napi_ok) {
            out.emplace_back(buffer.data(), copied);
        }
    }
}

// Calls a method on the JS plugin runtime. Any pending JS exception is drained and reported so it
// never escapes into libnode frames, which are compiled without exceptions.
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
    const auto status = napi_call_function(env, runtime, fn, static_cast<std::size_t>(argc), argv, &result);
    if (status != napi_ok) {
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
        g_host = host.get();

        // Leave stdio, signal handlers, resource limits and the process title alone: Endstone owns
        // stdin (it closes and restores it around startup), installs its own signal and crash
        // handlers, and drives an interactive console on the same terminal.
        const auto flags = static_cast<node::ProcessInitializationFlags::Flags>(
            node::ProcessInitializationFlags::kNoStdioInitialization |
            node::ProcessInitializationFlags::kNoDefaultSignalHandling |
            node::ProcessInitializationFlags::kDisableNodeOptionsEnv |
            node::ProcessInitializationFlags::kDisableCLIOptions |
            node::ProcessInitializationFlags::kNoAdjustResourceLimits |
            node::ProcessInitializationFlags::kNoUseLargePages |
            node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput);

        const std::vector<std::string> args{host->exec_path, host->script_path};
        host->init_result = node::InitializeOncePerProcess(args, flags);
        if (!host->init_result) {
            g_host = nullptr;
            return ESN_ERR_INIT_FAILED;
        }
        for (const auto &error : host->init_result->errors()) {
            emit(host.get(), ESN_LOG_ERROR, error);
        }
        if (host->init_result->early_return()) {
            g_host = nullptr;
            return ESN_ERR_INIT_FAILED;
        }

        host->platform = host->init_result->platform();
        if (!host->platform) {
            emit(host.get(), ESN_LOG_ERROR, "Node.js did not provide a V8 platform");
            g_host = nullptr;
            return ESN_ERR_INIT_FAILED;
        }

        std::vector<std::string> errors;
        host->setup = node::CommonEnvironmentSetup::Create(host->platform, &errors, host->init_result->args(),
                                                           host->init_result->exec_args());
        if (!host->setup) {
            for (const auto &error : errors) {
                emit(host.get(), ESN_LOG_ERROR, error);
            }
            g_host = nullptr;
            return ESN_ERR_INIT_FAILED;
        }

        emitf(host.get(), ESN_LOG_INFO, "Node.js %s (V8 %s) initialized", NODE_VERSION_STRING,
              v8::V8::GetVersion());
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
    if (!host || !host->setup) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (host->started) {
        return ESN_ERR_ALREADY_INITIALIZED;
    }

    try {
        auto *isolate = host->setup->isolate();
        const v8::Locker locker(isolate);
        const v8::Isolate::Scope isolate_scope(isolate);
        const v8::HandleScope handle_scope(isolate);
        const v8::Context::Scope context_scope(host->setup->context());

        node::AddLinkedBinding(host->setup->env(), "endstone_node", registerBinding,
                               NODE_API_DEFAULT_MODULE_API_VERSION);

        const v8::TryCatch try_catch(isolate);
        const auto result = node::LoadEnvironment(host->setup->env(), kBootstrap);
        if (result.IsEmpty()) {
            reportTryCatch(host, isolate, try_catch);
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
    if (!host || !host->setup) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (out_more_work) {
        *out_more_work = 0;
    }

    try {
        auto *isolate = host->setup->isolate();
        const v8::Locker locker(isolate);
        const v8::Isolate::Scope isolate_scope(isolate);
        const v8::HandleScope handle_scope(isolate);
        const v8::Context::Scope context_scope(host->setup->context());

        const v8::TryCatch try_catch(isolate);
        const int more = uv_run(host->setup->event_loop(), UV_RUN_NOWAIT);
        isolate->PerformMicrotaskCheckpoint();
        if (try_catch.HasCaught()) {
            reportTryCatch(host, isolate, try_catch);
        }
        if (out_more_work) {
            *out_more_work = more;
        }
        return ESN_OK;
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}

esn_status ESN_CALL esn_host_load_plugin(esn_host *handle, const char *path, esn_plugin **out_plugin)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host || !host->setup || !host->started) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (!path || !out_plugin) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    *out_plugin = nullptr;

    try {
        auto *isolate = host->setup->isolate();
        const v8::Locker locker(isolate);
        const v8::Isolate::Scope isolate_scope(isolate);
        const v8::HandleScope handle_scope(isolate);
        const v8::Context::Scope context_scope(host->setup->context());

        napi_env env = host->napi;
        napi_value arg = nullptr;
        if (!env || napi_create_string_utf8(env, path, NAPI_AUTO_LENGTH, &arg) != napi_ok) {
            return ESN_ERR_INTERNAL;
        }
        napi_value result = nullptr;
        if (!callRuntime(host, "load", 1, &arg, &result) || !result) {
            return ESN_ERR_SCRIPT_FAILED;
        }

        auto plugin = std::make_unique<PluginImpl>();
        plugin->host = host;
        std::int32_t id = -1;
        napi_value id_value = nullptr;
        if (napi_get_named_property(env, result, "id", &id_value) != napi_ok ||
            napi_get_value_int32(env, id_value, &id) != napi_ok) {
            return ESN_ERR_SCRIPT_FAILED;
        }
        plugin->id = id;

        if (!readString(env, result, "name", plugin->name) || plugin->name.empty()) {
            emit(host, ESN_LOG_ERROR, "plugin package.json has no usable \"name\"");
            return ESN_ERR_SCRIPT_FAILED;
        }
        if (!readString(env, result, "version", plugin->version)) {
            plugin->version = "0.0.0";
        }
        (void)readString(env, result, "apiVersion", plugin->api_version);
        (void)readString(env, result, "description", plugin->description);
        (void)readString(env, result, "website", plugin->website);
        (void)readString(env, result, "loadOrder", plugin->load_order);
        readStringArray(env, result, "authors", plugin->authors);
        readStringArray(env, result, "depend", plugin->depend);

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
    return ESN_OK;
}

esn_status ESN_CALL esn_plugin_invoke(esn_plugin *handle, esn_plugin_hook hook)
{
    auto *plugin = reinterpret_cast<PluginImpl *>(handle);
    if (!plugin || !plugin->host || !plugin->host->setup) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    if (hook < ESN_HOOK_LOAD || hook > ESN_HOOK_DISABLE) {
        return ESN_ERR_BAD_ARGUMENT;
    }

    try {
        auto *host = plugin->host;
        auto *isolate = host->setup->isolate();
        const v8::Locker locker(isolate);
        const v8::Isolate::Scope isolate_scope(isolate);
        const v8::HandleScope handle_scope(isolate);
        const v8::Context::Scope context_scope(host->setup->context());

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
    if (host && host->setup && host->napi) {
        try {
            auto *isolate = host->setup->isolate();
            const v8::Locker locker(isolate);
            const v8::Isolate::Scope isolate_scope(isolate);
            const v8::HandleScope handle_scope(isolate);
            const v8::Context::Scope context_scope(host->setup->context());
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

esn_status ESN_CALL esn_host_destroy(esn_host *handle, int *out_exit_code)
{
    auto *host = reinterpret_cast<HostImpl *>(handle);
    if (!host) {
        return ESN_ERR_NOT_INITIALIZED;
    }

    int exit_code = 0;
    try {
        if (host->setup) {
            auto *isolate = host->setup->isolate();
            {
                const v8::Locker locker(isolate);
                const v8::Isolate::Scope isolate_scope(isolate);
                const v8::HandleScope handle_scope(isolate);
                const v8::Context::Scope context_scope(host->setup->context());
                exit_code = node::Stop(host->setup->env());
            }
            host->setup.reset();
        }
        host->init_result.reset();
        node::TearDownOncePerProcess();
    }
    catch (...) {
        exit_code = -1;
    }

    if (out_exit_code) {
        *out_exit_code = exit_code;
    }
    g_host = nullptr;
    delete host;
    return ESN_OK;
}

}  // extern "C"
