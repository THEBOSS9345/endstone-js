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

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <endstone/plugin/plugin.h>
#include <endstone/plugin/plugin_loader.h>

#include "endstone_node_abi.h"
#include "host_api.h"

namespace endstone::node {

/**
 * @brief An Endstone plugin backed by a JavaScript module running in the shared Node environment.
 *
 * Lifecycle hooks are forwarded across the C ABI; a JavaScript throw is reported by the host and
 * surfaces here as a failed status rather than an exception.
 */
class JsPlugin : public Plugin {
public:
    JsPlugin(const HostApi &api, esn_plugin *handle, PluginDescription description);
    ~JsPlugin() override;

    [[nodiscard]] const PluginDescription &getDescription() const override { return description_; }
    void onLoad() override;
    void onEnable() override;
    void onDisable() override;

    /**
     * @brief Drops the host-side module reference, leaving this plugin inert.
     *
     * Idempotent, and must happen before the Node host is torn down. The object itself stays alive
     * because EndstonePluginManager keeps raw pointers to it.
     */
    void release();

private:
    void invoke(esn_plugin_hook hook, const char *what);

    const HostApi &api_;
    esn_plugin *handle_;
    PluginDescription description_;
};

/**
 * @brief Discovers JavaScript plugins as either a directory containing package.json or a single .js
 * file, and hands them to the shared Node host.
 */
class JsPluginLoader : public PluginLoader {
public:
    JsPluginLoader(Server &server, const HostApi &api, esn_host *host);

    [[nodiscard]] Plugin *loadPlugin(std::string file) override;
    [[nodiscard]] std::vector<std::string> getPluginFileFilters() const override;

    /** Paths under `directory` that look like JavaScript plugins, in a form loadPlugin accepts. */
    [[nodiscard]] std::vector<std::string> discover(const std::filesystem::path &directory) const;

    /**
     * @brief Releases every loaded plugin's host handle.
     *
     * Must be called before the Node host is destroyed. This loader is owned by the plugin manager,
     * which outlives the host, so its plugins cannot be left to clean themselves up in their own
     * destructors - by then the host library is gone.
     */
    void releaseAll();

private:
    const HostApi &api_;
    esn_host *host_;
    std::vector<std::unique_ptr<JsPlugin>> plugins_;
};

}  // namespace endstone::node
