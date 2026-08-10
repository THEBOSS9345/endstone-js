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

// Milestone 0: the Endstone half of the ABI firewall.
//
// This translation unit is built with Endstone's toolchain (libc++ on Linux) and links only the
// header-only Endstone API. It never links libnode and never includes a Node header - it reaches the
// Node host through the C ABI in endstone_node_abi.h, loaded at runtime.
//
// No Minecraft API is exposed to JavaScript yet. The only thing crossing into JS is console output.

#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <endstone/plugin/plugin.h>
#include <endstone/scheduler/scheduler.h>

#include "api_bridge.h"
#include "endstone_node_abi.h"
#include "host_api.h"
#include "js_plugin_loader.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
using ModuleHandle = HMODULE;
constexpr const char *kHostLibraryName = "endstone_node_host.dll";
#else
using ModuleHandle = void *;
constexpr const char *kHostLibraryName = "libendstone_node_host.so";
#endif

std::string currentThreadId()
{
    std::ostringstream out;
    out << std::this_thread::get_id();
    return out.str();
}

using endstone::node::HostApi;
using endstone::node::JsPluginLoader;

/**
 * Copies `text` out under the ABI's string convention: fill up to `cap` bytes, NUL-terminate when it
 * fits, and return the length needed excluding the NUL.
 */
std::size_t copyOut(std::string_view text, char *buf, std::size_t cap)
{
    if (buf && cap > 0) {
        const auto count = text.size() < cap - 1 ? text.size() : cap - 1;
        std::memcpy(buf, text.data(), count);
        buf[count] = '\0';
    }
    return text.size();
}

ModuleHandle loadModule(const fs::path &path)
{
#ifdef _WIN32
    // ALTERED_SEARCH_PATH makes the host's own directory the first place libnode.dll is looked for.
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void unloadModule(ModuleHandle module)
{
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
}

template <typename Fn>
bool resolve(ModuleHandle module, const char *name, Fn &out)
{
#ifdef _WIN32
    auto *symbol = reinterpret_cast<void *>(GetProcAddress(module, name));
#else
    auto *symbol = dlsym(module, name);
#endif
    out = reinterpret_cast<Fn>(symbol);
    return out != nullptr;
}

std::string lastLoadError()
{
#ifdef _WIN32
    return std::to_string(GetLastError());
#else
    const char *error = dlerror();
    return error ? error : "unknown";
#endif
}

constexpr const char *kDefaultScript = R"JS(// Endstone Node.js spike entry point.
console.log("Hello from Endstone Node");
console.log(`node=${process.versions.node} v8=${process.versions.v8} uv=${process.versions.uv}`);
)JS";

class NodeJsPlugin : public endstone::Plugin {
public:
    void onLoad() override
    {
        getLogger().info("onLoad: thread={} primary={}", currentThreadId(), getServer().isPrimaryThread());
    }

    void onEnable() override
    {
        getLogger().info("onEnable: thread={} primary={}", currentThreadId(), getServer().isPrimaryThread());

        const auto data_folder = getDataFolder();
        std::error_code ec;
        create_directories(data_folder, ec);

        const auto script = data_folder / "main.js";
        if (!exists(script)) {
            std::ofstream out(script, std::ios::binary);
            out << kDefaultScript;
            getLogger().info("wrote default entry point: {}", script.string());
        }

        if (!loadHost(data_folder)) {
            return;
        }
        if (!startNode(script)) {
            return;
        }

        // The pump. Period 1 = once per BDS tick, on the server thread, which is the same thread
        // that just initialized Node.
        task_ = getServer().getScheduler().runTaskTimer(*this, [this]() { pump(); }, 1, 1);
        if (!task_) {
            getLogger().error("failed to schedule the Node event loop pump");
        }

        publishTypes(data_folder, data_folder.parent_path());
        loadJsPlugins(data_folder.parent_path());
    }

    void onDisable() override
    {
        if (task_) {
            task_->cancel();
            task_.reset();
        }

        // Before Node goes away: the loader outlives us (the plugin manager owns it), so its plugins
        // must let go of their host handles now or their destructors will call into an unloaded DLL.
        if (loader_) {
            loader_->releaseAll();
            loader_ = nullptr;
        }
        // Same reasoning for events: a dispatch after the host is gone would call into freed code.
        if (bridge_) {
            bridge_->shutdown();
        }

        if (host_ && api_.destroy) {
            int exit_code = 0;
            const auto status = api_.destroy(host_, &exit_code);
            host_ = nullptr;
            getLogger().info("Node host destroyed: status={} exit_code={} (after {} pumps)",
                             api_.status_message ? api_.status_message(status) : "?", exit_code, pumps_);
        }
        if (module_) {
            unloadModule(module_);
            module_ = nullptr;
        }
        getLogger().info("onDisable: thread={} primary={}", currentThreadId(), getServer().isPrimaryThread());
    }

private:
    // Called by the host on the thread that runs JavaScript. Nothing may propagate back out: the
    // return path runs through libnode and V8 frames, which are compiled without exceptions.
    static void ESN_CALL onHostLog(void *user_data, int level, const char *message, std::size_t length)
    {
        auto *self = static_cast<NodeJsPlugin *>(user_data);
        if (!self || !message) {
            return;
        }
        try {
            auto mapped = static_cast<endstone::Logger::Level>(level);
            if (level < endstone::Logger::Trace || level > endstone::Logger::Critical) {
                mapped = endstone::Logger::Info;
            }
            // First message decides whether JavaScript really ran on the server thread.
            if (!self->logged_thread_) {
                self->logged_thread_ = true;
                self->getLogger().info("first JS output: thread={} primary={}", currentThreadId(),
                                       self->getServer().isPrimaryThread());
            }
            self->getLogger().log(mapped, std::string_view(message, length));
        }
        catch (...) {
        }
    }

    bool loadHost(const fs::path &data_folder)
    {
        const fs::path candidates[] = {data_folder / kHostLibraryName, data_folder.parent_path() / kHostLibraryName};
        for (const auto &candidate : candidates) {
            if (!exists(candidate)) {
                continue;
            }
            module_ = loadModule(candidate);
            if (!module_) {
                getLogger().error("failed to load {}: {}", candidate.string(), lastLoadError());
                return false;
            }
            getLogger().info("loaded Node host: {}", candidate.string());
            break;
        }
        if (!module_) {
            getLogger().warning("{} not found in {} - Node.js support is disabled", kHostLibraryName,
                                data_folder.string());
            return false;
        }

        const bool resolved = resolve(module_, "esn_abi_version", api_.abi_version) &&
                              resolve(module_, "esn_status_message", api_.status_message) &&
                              resolve(module_, "esn_host_create", api_.create) &&
                              resolve(module_, "esn_host_start", api_.start) &&
                              resolve(module_, "esn_host_pump", api_.pump) &&
                              resolve(module_, "esn_host_destroy", api_.destroy) &&
                              resolve(module_, "esn_host_load_plugin", api_.load_plugin) &&
                              resolve(module_, "esn_plugin_get_meta", api_.plugin_get_meta) &&
                              resolve(module_, "esn_plugin_invoke", api_.plugin_invoke) &&
                              resolve(module_, "esn_plugin_unload", api_.plugin_unload) &&
                              resolve(module_, "esn_host_dispatch_event", api_.dispatch_event);
        if (!resolved) {
            getLogger().error("Node host is missing expected entry points");
            return false;
        }
        const auto host_abi = api_.abi_version();
        if (host_abi != ESN_ABI_VERSION) {
            getLogger().error("Node host ABI {} does not match plugin ABI {}", host_abi, ESN_ABI_VERSION);
            return false;
        }
        getLogger().info("Node host ABI version {}", host_abi);
        return true;
    }

    bool startNode(const fs::path &script)
    {
        script_path_ = script.string();
        exec_path_ = "endstone";

        // The whole API surface lives in ApiBridge, which also owns the handle table and event
        // subscriptions. The ABI carries one `context`, so everything routes through it.
        bridge_ = std::make_unique<endstone::node::ApiBridge>(*this);
        api_table_ = {};
        bridge_->fill(api_table_);

        esn_host_config config{};
        config.abi_version = ESN_ABI_VERSION;
        config.log = &NodeJsPlugin::onHostLog;
        config.log_user_data = this;
        config.script_path = script_path_.c_str();
        config.exec_path = exec_path_.c_str();
        config.api = &api_table_;

        auto status = api_.create(&config, &host_);
        if (status != ESN_OK) {
            getLogger().error("esn_host_create failed: {}", api_.status_message(status));
            return false;
        }
        // Wire dispatch only once the host exists, then start: subscriptions made by a plugin's
        // onEnable must already have somewhere to deliver to.
        if (api_.dispatch_event) {
            auto *api = &api_;
            auto *host = host_;
            bridge_->setEventSink([api, host](const std::uint32_t subscription, const esn_handle event) {
                (void)api->dispatch_event(host, subscription, event);
            });
        }

        status = api_.start(host_);
        if (status != ESN_OK) {
            getLogger().error("esn_host_start failed: {}", api_.status_message(status));
            return false;
        }
        return true;
    }

    /**
     * Publishes the TypeScript declarations as a stub package at plugins/node_modules/@endstone/server.
     *
     * TypeScript and editors resolve `@endstone/server` by walking up from the plugin folder, so this
     * gives completions and type checking with no tsconfig or install step. At runtime the module still
     * comes from the host's in-memory copy: the resolver hook short-circuits before the filesystem is
     * consulted, so the stub is only ever read by tooling.
     */
    void publishTypes(const fs::path &data_folder, const fs::path &plugin_dir)
    {
        std::error_code ec;
        const auto source = data_folder / "index.d.ts";
        if (!exists(source, ec)) {
            getLogger().debug("no index.d.ts in {} - skipping type publication", data_folder.string());
            return;
        }

        const auto package_dir = plugin_dir / "node_modules" / "@endstone" / "server";
        create_directories(package_dir, ec);
        if (ec) {
            getLogger().warning("could not create {}: {}", package_dir.string(), ec.message());
            return;
        }

        // Only rewrite when the content actually changed, so we do not churn timestamps every boot.
        const auto destination = package_dir / "index.d.ts";
        const auto read = [](const fs::path &path) {
            std::ifstream in(path, std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        };
        const auto declarations = read(source);
        if (!exists(destination, ec) || read(destination) != declarations) {
            std::ofstream out(destination, std::ios::binary | std::ios::trunc);
            out << declarations;
        }

        const auto manifest = package_dir / "package.json";
        if (!exists(manifest, ec)) {
            std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
            out << "{\n"
                   "  \"name\": \"@endstone/server\",\n"
                   "  \"version\": \"" ENDSTONE_API_VERSION "\",\n"
                   "  \"description\": \"Type declarations for the Endstone JavaScript API. Provided by the "
                   "server; the implementation lives in the host, not on disk.\",\n"
                   "  \"types\": \"index.d.ts\",\n"
                   "  \"main\": \"index.js\"\n"
                   "}\n";
        }

        const auto shim = package_dir / "index.js";
        if (!exists(shim, ec)) {
            std::ofstream out(shim, std::ios::binary | std::ios::trunc);
            out << "// The real module is served from memory by the Endstone Node host, so this file is\n"
                   "// never loaded in a running server. It exists only so package.json \"main\" resolves.\n"
                   "throw new Error('@endstone/server is only available inside an Endstone server');\n";
        }

        getLogger().info("published @endstone/server types to {}", package_dir.string());
    }

    // Registers the JavaScript loader and loads what it finds. Discovery has to be driven from here
    // rather than left to the normal pass: EndstonePluginManager::loadPlugins snapshots the loader
    // list before any onLoad runs, and enablePlugins snapshots the plugin list before any onEnable,
    // so a loader registered by a plugin is never consulted by either.
    void loadJsPlugins(const fs::path &plugin_dir)
    {
        auto &manager = getServer().getPluginManager();
        auto loader = std::make_unique<JsPluginLoader>(getServer(), api_, host_);
        loader_ = loader.get();
        manager.registerLoader(std::move(loader));

        const auto candidates = loader_->discover(plugin_dir);
        if (candidates.empty()) {
            getLogger().info("no JavaScript plugins found in {}", plugin_dir.string());
            return;
        }

        int loaded = 0;
        for (const auto &candidate : candidates) {
            if (auto *plugin = manager.loadPlugin(candidate)) {
                manager.enablePlugin(*plugin);
                ++loaded;
            }
        }
        getLogger().info("loaded {}/{} JavaScript plugin(s)", loaded, candidates.size());
    }

    void pump()
    {
        if (!host_) {
            return;
        }
        int more_work = 0;
        const auto status = api_.pump(host_, &more_work);
        ++pumps_;
        if (status != ESN_OK) {
            getLogger().error("esn_host_pump failed: {} - stopping the pump", api_.status_message(status));
            if (task_) {
                task_->cancel();
                task_.reset();
            }
        }
    }

    std::shared_ptr<endstone::Task> task_;
    JsPluginLoader *loader_{nullptr};  // owned by the plugin manager, which outlives us
    ModuleHandle module_{nullptr};
    HostApi api_{};
    esn_host *host_{nullptr};
    std::unique_ptr<endstone::node::ApiBridge> bridge_;
    esn_endstone_api api_table_{};  // borrowed by the host, so it must outlive it
    std::string script_path_;
    std::string exec_path_;
    std::uint64_t pumps_{0};
    bool logged_thread_{false};
};

}  // namespace

ENDSTONE_PLUGIN("nodejs", "0.1.0", NodeJsPlugin)
{
    description = "Node.js embedding spike for Endstone.";
    website = "https://endstone.dev";
}
