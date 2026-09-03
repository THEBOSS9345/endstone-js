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

#pragma once

// Quarantine for Node.js's *unstable* embedder API.
//
// node.h documents itself as "subject to change or removal between Node.js versions, including
// possible API and ABI breakage", and the V8 API moves too. node_embed.cpp is the ONLY translation
// unit permitted to include <node.h>, <v8.h> or <uv.h>; everything else in the host talks to Node
// through N-API, which is ABI-stable across versions by contract.
//
// That makes a Node upgrade a single-file audit. Keep it that way: if you find yourself needing a
// v8:: or node:: type somewhere else, add a function here instead.
//
// node_api.h is deliberately exposed below - it is the stable surface, not part of the quarantine.

#include <functional>
#include <string>
#include <vector>

#include <node_api.h>

namespace endstone::node::embed {

/** One initialized Node.js process state plus its single environment. Opaque by design. */
struct Instance;

/** Where diagnostics go. Errors are reported, never thrown: libnode is built -fno-exceptions. */
using ErrorSink = std::function<void(const std::string &)>;

/** Version of Node the host was *compiled* against. */
const char *compiledNodeVersion();
const char *compiledV8Version();
int compiledNodeModuleVersion();

/**
 * Whether Node has already been initialized in this process, in which case it can never be again -
 * node::InitializeOncePerProcess aborts rather than returning an error, so this must be checked
 * before create() rather than after it fails.
 */
bool initializedOnce();

/**
 * Initializes Node.js for this process and creates one environment. Not repeatable - Node's
 * per-process setup happens once. Returns nullptr on failure, having reported the reason.
 */
Instance *create(const std::vector<std::string> &args, const ErrorSink &on_error);

/** Stops the environment and tears Node.js down. `out_exit_code` is optional. */
void destroy(Instance *instance, int *out_exit_code);

/**
 * Enters the isolate lock plus the isolate, handle and context scopes, in that order.
 *
 * Every call that touches JavaScript - including any napi_* call - must happen with one of these on
 * the stack. Re-entrant, so nesting is safe.
 */
class Scope {
public:
    explicit Scope(Instance *instance);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
    Scope(Scope &&) = delete;
    Scope &operator=(Scope &&) = delete;

    [[nodiscard]] bool valid() const { return state_ != nullptr; }

private:
    struct State;
    State *state_{nullptr};
};

/**
 * Registers a linked N-API binding under `binding_name` and runs `source` as the main script.
 * Requires an active Scope.
 */
bool loadEnvironment(Instance *instance, const char *source, const char *binding_name,
                     napi_addon_register_func binding, const ErrorSink &on_error);

/**
 * Advances the event loop without blocking and drains microtasks. Requires an active Scope.
 * `out_more_work` (optional) receives non-zero while the loop still has pending work.
 */
bool pump(Instance *instance, int *out_more_work, const ErrorSink &on_error);

}  // namespace endstone::node::embed
