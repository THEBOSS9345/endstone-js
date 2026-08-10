/*
 * Copyright (c) 2026, The Endstone Project. (https://endstone.dev) All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * The ABI firewall between Endstone and the embedded Node.js host.
 *
 * Endstone is built against libc++ on Linux for ABI compatibility with BDS; libnode is built
 * against libstdc++. Nothing declared here may therefore be a C++ standard library type, carry a
 * non-trivial destructor, or throw. Only POD, opaque pointers, function pointers, buffer+length
 * pairs and error codes cross.
 *
 * Valid C and C++. Include no other project header.
 */

#ifndef ENDSTONE_NODE_ABI_H_
#define ENDSTONE_NODE_ABI_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define ESN_CALL __cdecl
#ifdef ESN_HOST_BUILD
#define ESN_EXPORT __declspec(dllexport)
#else
#define ESN_EXPORT __declspec(dllimport)
#endif
#else
#define ESN_CALL
#ifdef ESN_HOST_BUILD
#define ESN_EXPORT __attribute__((visibility("default")))
#else
#define ESN_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped on any incompatible change below. Checked by both sides at load time. */
#define ESN_ABI_VERSION 6u

typedef enum esn_status {
    ESN_OK = 0,
    ESN_ERR_BAD_ARGUMENT = 1,
    ESN_ERR_ABI_MISMATCH = 2,
    ESN_ERR_ALREADY_INITIALIZED = 3,
    ESN_ERR_NOT_INITIALIZED = 4,
    ESN_ERR_INIT_FAILED = 5,
    ESN_ERR_SCRIPT_FAILED = 6,
    ESN_ERR_INTERNAL = 7,
    /* The handle has outlived the dispatch it came from. */
    ESN_ERR_STALE_HANDLE = 8,
    /* No such property or method on that object type. */
    ESN_ERR_NO_SUCH_MEMBER = 9,
    /* The property exists but is not of the requested type, or is read-only. */
    ESN_ERR_WRONG_TYPE = 10
} esn_status;

/* Mirrors endstone::Logger::Level numerically, but is declared independently on purpose: the host
   must not include any Endstone header. */
typedef enum esn_log_level {
    ESN_LOG_TRACE = 0,
    ESN_LOG_DEBUG = 1,
    ESN_LOG_INFO = 2,
    ESN_LOG_WARNING = 3,
    ESN_LOG_ERROR = 4,
    ESN_LOG_CRITICAL = 5
} esn_log_level;

/*
 * Host -> Endstone. Called on whichever thread produced the message, which for JavaScript output is
 * always the thread that called esn_host_start/esn_host_pump. `message` is UTF-8 and is NOT
 * guaranteed to be NUL-terminated; use `length`. The pointer is valid only for the call.
 * Implementations must not throw across this boundary.
 */
typedef void(ESN_CALL *esn_log_fn)(void *user_data, int level, const char *message, size_t length);

/*
 * Endstone -> host. The API JavaScript reaches through, supplied by the Endstone side and called on
 * the thread running JavaScript (which is the BDS server thread).
 *
 * String getters follow one convention throughout: copy up to `cap` bytes into `buf`, NUL-terminating
 * when it fits, and return the number of bytes the value needs excluding the NUL. A return value >=
 * `cap` means the caller should retry with a bigger buffer. `buf` may be NULL when `cap` is 0, which
 * is how the host asks for a size.
 *
 * Every function must be safe to call at any time and must not throw; an implementation with nothing
 * to return should write nothing and return 0. Any member may be NULL if unsupported, and the host
 * checks before calling.
 */
/*
 * A reference to an Endstone object (a Player, an Event, ...). Zero is never valid.
 *
 * LIFETIME: handles are dispatch-scoped. One obtained during an event callback is valid only until
 * that callback returns, and the Endstone side invalidates it afterwards. To keep information beyond
 * the callback, copy the primitives out (name, uuid, coordinates); never retain a handle. Using a
 * stale handle is detected and fails with ESN_ERR_STALE_HANDLE rather than crashing.
 */
typedef uint64_t esn_handle;

/*
 * Generic typed accessors, keyed by property name.
 *
 * This is deliberately a small fixed surface: Endstone's API has hundreds of properties, and one C
 * entry point per property would neither scale nor survive upstream additions. Exposing a new
 * property means adding a dispatch entry on the Endstone side and a line of JavaScript - never an ABI
 * change. Unknown names fail with ESN_ERR_NO_SUCH_MEMBER; type mismatches with ESN_ERR_WRONG_TYPE.
 */
typedef struct esn_accessors {
    esn_status(ESN_CALL *get_bool)(void *context, esn_handle target, const char *name, int *out);
    esn_status(ESN_CALL *get_int)(void *context, esn_handle target, const char *name, int64_t *out);
    esn_status(ESN_CALL *get_double)(void *context, esn_handle target, const char *name, double *out);
    /* Size-then-fetch, as elsewhere: `out_needed` receives the length excluding the NUL. */
    esn_status(ESN_CALL *get_string)(void *context, esn_handle target, const char *name, char *buf, size_t cap,
                                     size_t *out_needed);
    esn_status(ESN_CALL *get_handle)(void *context, esn_handle target, const char *name, esn_handle *out);

    esn_status(ESN_CALL *set_bool)(void *context, esn_handle target, const char *name, int value);
    esn_status(ESN_CALL *set_int)(void *context, esn_handle target, const char *name, int64_t value);
    esn_status(ESN_CALL *set_double)(void *context, esn_handle target, const char *name, double value);
    esn_status(ESN_CALL *set_string)(void *context, esn_handle target, const char *name, const char *value,
                                     size_t length);

    /*
     * Calls a method. Arguments are one optional string plus an optional array of doubles, which
     * between them cover Endstone's method surface (sendMessage(text), teleport(x,y,z),
     * playSound(x,y,z,name,volume,pitch)). Keeping the shape fixed means new methods never touch
     * the ABI. `out_handle` is optional and receives a result object for methods that return one.
     */
    esn_status(ESN_CALL *invoke)(void *context, esn_handle target, const char *name, const char *text,
                                size_t text_length, const double *numbers, size_t number_count,
                                esn_handle *out_handle);

    /* Names the concrete Endstone type behind a handle, for diagnostics and JS class selection. */
    esn_status(ESN_CALL *type_name)(void *context, esn_handle target, char *buf, size_t cap, size_t *out_needed);
} esn_accessors;

typedef struct esn_endstone_api {
    uint32_t abi_version; /* must be ESN_ABI_VERSION */
    void *context;        /* opaque, passed back as the first argument */

    size_t(ESN_CALL *server_name)(void *context, char *buf, size_t cap);
    size_t(ESN_CALL *server_version)(void *context, char *buf, size_t cap);
    size_t(ESN_CALL *server_minecraft_version)(void *context, char *buf, size_t cap);
    /* Network protocol version, or -1 if unavailable. */
    int(ESN_CALL *server_protocol_version)(void *context);
    /* Number of players currently online, or -1 if the level is not loaded yet. */
    int(ESN_CALL *server_online_player_count)(void *context);
    /*
     * The loaded level, or ESN_ERR_NOT_INITIALIZED before one exists. Unlike handles obtained during
     * dispatch this one is persistent: the level outlives any single callback, so it is safe to keep.
     */
    esn_status(ESN_CALL *server_level)(void *context, esn_handle *out);

    void(ESN_CALL *broadcast_message)(void *context, const char *message, size_t length);
    /* Logs through the owning plugin's logger. `level` is an esn_log_level. */
    void(ESN_CALL *log)(void *context, int level, const char *message, size_t length);

    /* Property access on handles. */
    esn_accessors accessors;

    /*
     * Registers interest in an Endstone event by its class name, e.g. "PlayerJoinEvent".
     * `priority` is an esn_event_priority; `ignore_cancelled` skips already-cancelled events.
     * The resulting subscription id is passed back to esn_event_fn on every dispatch.
     */
    esn_status(ESN_CALL *subscribe)(void *context, const char *event_name, int priority, int ignore_cancelled,
                                    uint32_t *out_subscription);
    esn_status(ESN_CALL *unsubscribe)(void *context, uint32_t subscription);
} esn_endstone_api;

/* Mirrors endstone::EventPriority. */
typedef enum esn_event_priority {
    ESN_PRIORITY_LOWEST = 0,
    ESN_PRIORITY_LOW = 1,
    ESN_PRIORITY_NORMAL = 2,
    ESN_PRIORITY_HIGH = 3,
    ESN_PRIORITY_HIGHEST = 4,
    ESN_PRIORITY_MONITOR = 5
} esn_event_priority;

typedef struct esn_host_config {
    uint32_t abi_version;   /* must be ESN_ABI_VERSION */
    esn_log_fn log;         /* required */
    void *log_user_data;    /* opaque, passed back to log */
    const char *script_path; /* UTF-8, absolute, NUL-terminated */
    const char *exec_path;   /* UTF-8 argv[0] for Node, NUL-terminated */
    /*
     * Optional. Borrowed, and must outlive the host: the Endstone side owns it. When NULL, the
     * @endstone/server module still loads but reports itself unavailable rather than crashing.
     */
    const esn_endstone_api *api;
} esn_host_config;

typedef struct esn_host esn_host; /* opaque */

/* ABI version the host was compiled against. Call before anything else. */
ESN_EXPORT uint32_t ESN_CALL esn_abi_version(void);

/* Static, NUL-terminated, never null. Valid for the lifetime of the module. */
ESN_EXPORT const char *ESN_CALL esn_status_message(esn_status status);

/*
 * Initializes Node.js and V8 for this process and creates one environment. At most one host may
 * exist per process (Node's per-process initialization is not repeatable); a second call returns
 * ESN_ERR_ALREADY_INITIALIZED. Must be called on the thread that will own the JavaScript.
 */
ESN_EXPORT esn_status ESN_CALL esn_host_create(const esn_host_config *config, esn_host **out_host);

/* Loads the environment and runs the configured main script. Same thread as esn_host_create. */
ESN_EXPORT esn_status ESN_CALL esn_host_start(esn_host *host);

/*
 * Advances the libuv event loop without blocking and drains microtasks. Same thread as
 * esn_host_create. `out_more_work` (optional) receives non-zero while the loop has pending work.
 */
ESN_EXPORT esn_status ESN_CALL esn_host_pump(esn_host *host, int *out_more_work);

/* Stops the environment and tears down Node.js. `out_exit_code` is optional. */
ESN_EXPORT esn_status ESN_CALL esn_host_destroy(esn_host *host, int *out_exit_code);

/* ------------------------------------------------------------------------------------------------
 * Plugins
 *
 * All JS plugins share one Node environment and one event loop; they are isolated by module scope,
 * each with its own `require` rooted at its directory so it resolves its own node_modules. Node's
 * module loader is bound to a single environment, so per-plugin contexts are not an option without
 * giving up npm.
 * ---------------------------------------------------------------------------------------------- */

typedef struct esn_plugin esn_plugin; /* opaque, owned by the host */

/* Lifecycle hooks, invoked by Endstone on the server thread. */
typedef enum esn_plugin_hook {
    ESN_HOOK_LOAD = 0,
    ESN_HOOK_ENABLE = 1,
    ESN_HOOK_DISABLE = 2
} esn_plugin_hook;

/*
 * Metadata read out of the plugin's package.json. Every string is UTF-8, NUL-terminated, owned by
 * the host, and valid until esn_plugin_unload for that plugin. Absent optional fields are NULL.
 * Arrays are a pointer plus a count; no ownership transfers.
 */
typedef struct esn_plugin_meta {
    const char *name;         /* required, matched against Endstone's naming rules */
    const char *version;      /* required */
    const char *api_version;  /* endstone.apiVersion, may be NULL */
    const char *description;  /* may be NULL */
    const char *website;      /* may be NULL */
    const char *load_order;   /* "startup" | "postworld", may be NULL */
    const char *const *authors;
    size_t author_count;
    const char *const *depend;
    size_t depend_count;
} esn_plugin_meta;

/*
 * Loads one plugin from `path`, which is either a directory containing package.json or a single .js
 * file. Reads metadata but runs no lifecycle hook yet, so Endstone can validate the description and
 * reject the plugin before any of its code takes effect.
 */
ESN_EXPORT esn_status ESN_CALL esn_host_load_plugin(esn_host *host, const char *path, esn_plugin **out_plugin);

/* Borrowed pointers into host-owned storage; valid until esn_plugin_unload. */
ESN_EXPORT esn_status ESN_CALL esn_plugin_get_meta(esn_plugin *plugin, esn_plugin_meta *out_meta);

/*
 * Runs a lifecycle hook. A JavaScript exception is reported through the host's log callback and
 * returns ESN_ERR_SCRIPT_FAILED; it never propagates, so one misbehaving plugin cannot take down the
 * server or its neighbours.
 */
ESN_EXPORT esn_status ESN_CALL esn_plugin_invoke(esn_plugin *plugin, esn_plugin_hook hook);

/* Releases the plugin's module reference and its metadata storage. */
ESN_EXPORT esn_status ESN_CALL esn_plugin_unload(esn_plugin *plugin);

/*
 * Delivers a subscribed event to JavaScript. Called by Endstone on the server thread, synchronously,
 * so a handler can mutate the event - including cancelling it - before this returns. `event` is a
 * dispatch-scoped handle and must not be retained by the host past this call.
 */
ESN_EXPORT esn_status ESN_CALL esn_host_dispatch_event(esn_host *host, uint32_t subscription, esn_handle event);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ENDSTONE_NODE_ABI_H_ */
