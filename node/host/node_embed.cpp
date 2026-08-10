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

#include "node_embed.h"

#include <memory>

#include <node.h>
#include <node_version.h>
#include <uv.h>
#include <v8.h>

// Version gate. Node's embedder API is explicitly unstable, so a major bump must be an audit of this
// file rather than a surprise at runtime. When widening the range, re-check every node:: and v8::
// call below against that release's node.h, then run the C smoke harness on it.
#if NODE_MAJOR_VERSION < 24 || NODE_MAJOR_VERSION > 26
#error "Untested Node.js major version. Audit node/host/node_embed.cpp, then widen this range."
#endif

namespace endstone::node::embed {

struct Instance {
    std::shared_ptr<::node::InitializationResult> init_result;
    ::node::MultiIsolatePlatform *platform{nullptr};
    std::unique_ptr<::node::CommonEnvironmentSetup> setup;
};

namespace {

/** Reports a caught JavaScript exception through the sink. Requires an active Scope. */
void reportException(v8::Isolate *isolate, const v8::TryCatch &try_catch, const ErrorSink &on_error)
{
    if (!try_catch.HasCaught() || !on_error) {
        return;
    }
    const v8::String::Utf8Value message(isolate, try_catch.Exception());
    on_error(std::string("JavaScript exception: ") + (*message ? *message : "<unknown>"));
    v8::Local<v8::Value> stack;
    if (try_catch.StackTrace(isolate->GetCurrentContext()).ToLocal(&stack)) {
        const v8::String::Utf8Value stack_text(isolate, stack);
        if (*stack_text) {
            on_error(*stack_text);
        }
    }
}

}  // namespace

const char *compiledNodeVersion()
{
    return NODE_VERSION_STRING;
}

const char *compiledV8Version()
{
    return v8::V8::GetVersion();
}

int compiledNodeModuleVersion()
{
    return NODE_MODULE_VERSION;
}

Instance *create(const std::vector<std::string> &args, const ErrorSink &on_error)
{
    auto instance = std::make_unique<Instance>();

    // Leave stdio, signal handlers, resource limits and the process title alone: Endstone owns stdin
    // (it closes and restores it around startup), installs its own signal and crash handlers, and
    // drives an interactive console on the same terminal. kOwnsProcessState is deliberately not set,
    // so a plugin cannot change the server's cwd or process title.
    const auto flags = static_cast<::node::ProcessInitializationFlags::Flags>(
        ::node::ProcessInitializationFlags::kNoStdioInitialization |
        ::node::ProcessInitializationFlags::kNoDefaultSignalHandling |
        ::node::ProcessInitializationFlags::kDisableNodeOptionsEnv |
        ::node::ProcessInitializationFlags::kDisableCLIOptions |
        ::node::ProcessInitializationFlags::kNoAdjustResourceLimits |
        ::node::ProcessInitializationFlags::kNoUseLargePages |
        ::node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput);

    instance->init_result = ::node::InitializeOncePerProcess(args, flags);
    if (!instance->init_result) {
        if (on_error) {
            on_error("node::InitializeOncePerProcess returned nothing");
        }
        return nullptr;
    }
    for (const auto &error : instance->init_result->errors()) {
        if (on_error) {
            on_error(error);
        }
    }
    if (instance->init_result->early_return()) {
        return nullptr;
    }

    instance->platform = instance->init_result->platform();
    if (!instance->platform) {
        if (on_error) {
            on_error("Node.js did not provide a V8 platform");
        }
        return nullptr;
    }

    std::vector<std::string> errors;
    instance->setup = ::node::CommonEnvironmentSetup::Create(
        instance->platform, &errors, instance->init_result->args(), instance->init_result->exec_args());
    if (!instance->setup) {
        for (const auto &error : errors) {
            if (on_error) {
                on_error(error);
            }
        }
        return nullptr;
    }

    return instance.release();
}

void destroy(Instance *instance, int *out_exit_code)
{
    int exit_code = 0;
    if (instance) {
        if (instance->setup) {
            {
                const Scope scope(instance);
                exit_code = ::node::Stop(instance->setup->env());
            }
            instance->setup.reset();
        }
        instance->init_result.reset();
        delete instance;
    }
    ::node::TearDownOncePerProcess();
    if (out_exit_code) {
        *out_exit_code = exit_code;
    }
}

// Declaration order is load-bearing: the context can only be obtained once a HandleScope exists, and
// member initializers run in declaration order.
struct Scope::State {
    v8::Locker locker;
    v8::Isolate::Scope isolate_scope;
    v8::HandleScope handle_scope;
    v8::Context::Scope context_scope;

    explicit State(Instance *instance)
        : locker(instance->setup->isolate()), isolate_scope(instance->setup->isolate()),
          handle_scope(instance->setup->isolate()), context_scope(instance->setup->context())
    {
    }
};

Scope::Scope(Instance *instance)
{
    if (instance && instance->setup) {
        state_ = new State(instance);
    }
}

Scope::~Scope()
{
    delete state_;
}

bool loadEnvironment(Instance *instance, const char *source, const char *binding_name,
                     const napi_addon_register_func binding, const ErrorSink &on_error)
{
    if (!instance || !instance->setup || !source) {
        return false;
    }
    auto *isolate = instance->setup->isolate();

    ::node::AddLinkedBinding(instance->setup->env(), binding_name, binding,
                             NODE_API_DEFAULT_MODULE_API_VERSION);

    const v8::TryCatch try_catch(isolate);
    if (::node::LoadEnvironment(instance->setup->env(), source).IsEmpty()) {
        reportException(isolate, try_catch, on_error);
        return false;
    }
    return true;
}

bool pump(Instance *instance, int *out_more_work, const ErrorSink &on_error)
{
    if (out_more_work) {
        *out_more_work = 0;
    }
    if (!instance || !instance->setup) {
        return false;
    }
    auto *isolate = instance->setup->isolate();

    const v8::TryCatch try_catch(isolate);
    const int more = uv_run(instance->setup->event_loop(), UV_RUN_NOWAIT);
    isolate->PerformMicrotaskCheckpoint();
    if (try_catch.HasCaught()) {
        reportException(isolate, try_catch, on_error);
    }
    if (out_more_work) {
        *out_more_work = more;
    }
    return true;
}

}  // namespace endstone::node::embed
