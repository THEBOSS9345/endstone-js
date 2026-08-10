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
    defineFunction(env, exports, "broadcastMessage", jsBroadcastMessage);
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

// --- @endstone/server -----------------------------------------------------------------------
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
  logger: makeLogger(null),
  broadcastMessage(message) { binding.broadcastMessage(String(message)); },
};

const endstoneModule = { server, logger: server.logger, LogLevel: LEVELS };

// Handed to the virtual module's source through a well-known symbol rather than a bare global.
const API_SYMBOL = Symbol.for('endstone.api');
Object.defineProperty(globalThis, API_SYMBOL, {
  value: endstoneModule, enumerable: false, configurable: false, writable: false,
});

// A real ES module with named exports, served from memory. registerHooks is synchronous and applies
// to require() as well as import(), so one hook covers CommonJS and ESM plugins alike. CommonJS
// plugins get the namespace through Node's require(esm) support.
const VIRTUAL_URL = 'endstone:server';
const VIRTUAL_SOURCE = `
const api = globalThis[Symbol.for('endstone.api')];
export const server = api.server;
export const logger = api.logger;
export const LogLevel = api.LogLevel;
export default api;
`;

NodeModule.registerHooks({
  resolve(specifier, context, nextResolve) {
    if (specifier === '@endstone/server') {
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
  let loaded;
  try {
    loaded = pluginRequire(entry);
  } catch (err) {
    if (err && err.code === 'ERR_REQUIRE_ASYNC_MODULE') {
      throw new Error(
        `plugin '${manifest.name}' uses top-level await, which cannot be loaded by an embedded ` +
        `Node.js host. Move the awaited work into onEnable() instead.`);
    }
    throw err;
  }
  // An ES module namespace exposes its default export as .default.
  const instance = (loaded && loaded.default) || loaded;
  const endstone = manifest.endstone || {};

  const id = nextPluginId++;
  plugins.set(id, { id, dir, entry, instance, manifest, esm: isEsm });
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

// Loading is asynchronous because ESM imports are. The host starts a load, then pumps the loop and
// polls until the result appears.
function beginLoad(target) {
  const token = nextToken++;
  pendingLoads.set(token, null);
  void (async () => {
    try {
      pendingLoads.set(token, { ok: true, meta: await loadPluginAsync(target) });
    } catch (err) {
      pendingLoads.set(token, { ok: false, message: (err && err.stack) || String(err) });
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
  const result = target[name]();
  if (result && typeof result.then === 'function') {
    result.catch((err) => {
      binding.log(4, `${record.manifest.name}.${name}() rejected: ${(err && err.stack) || err}`);
    });
  }
}

function unloadPlugin(id) {
  plugins.delete(id);
}

binding.setRuntime({ beginLoad, pollLoad, invoke: invokePlugin, unload: unloadPlugin });

// Optional standalone entry point; the smoke test uses it, plugin mode does not require it.
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
                (void)readString(env, result, "message", message);
                emitf(host, ESN_LOG_ERROR, "failed to load '%s': %s", path,
                      message.empty() ? "<unknown>" : message.c_str());
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
