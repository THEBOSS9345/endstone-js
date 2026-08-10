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

#include "js_plugin_loader.h"

#include <algorithm>
#include <system_error>

#include <endstone/command/command.h>
#include <endstone/command/command_sender.h>
#include <endstone/permissions/permission.h>

#include "api_bridge.h"

namespace fs = std::filesystem;

namespace endstone::node {

namespace {

/**
 * JavaScript plugins are always PostWorld.
 *
 * A startup order cannot work here: the host plugin is itself enabled at startup, and JS plugins enter
 * the plugin list before it, so a startup-order JS plugin would be enabled first - before the host is
 * enabled and before commands are registered. Its events would be refused and its commands lost.
 */
PluginLoadOrder parseLoadOrder(const char *value, Server &server, const std::string &name)
{
    if (value) {
        std::string text{value};
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
        if (text == "startup") {
            server.getLogger().warning("JavaScript plugin '{}' asks for \"load\": \"startup\", which is not "
                                       "supported; loading it after the world instead.",
                                       name);
        }
    }
    return PluginLoadOrder::PostWorld;
}

std::vector<std::string> toVector(const char *const *items, std::size_t count)
{
    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (items[i]) {
            result.emplace_back(items[i]);
        }
    }
    return result;
}

/**
 * Turns the declarations the plugin made during load into Endstone commands.
 *
 * Endstone parses each usage and registers the result with Bedrock's command registry, which is what
 * gives the client its listing, autocompletion and argument validation. A usage Endstone cannot parse
 * is reported by Endstone itself, so nothing is validated twice here.
 */
std::vector<Command> toCommands(const esn_command_decl *decls, std::size_t count)
{
    std::vector<Command> commands;
    commands.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto &decl = decls[i];
        if (!decl.name || !*decl.name) {
            continue;
        }
        commands.emplace_back(decl.name, decl.description ? decl.description : "",
                              toVector(decl.usages, decl.usage_count), toVector(decl.aliases, decl.alias_count),
                              toVector(decl.permissions, decl.permission_count));
    }
    return commands;
}

}  // namespace

JsPlugin::JsPlugin(const HostApi &api, esn_plugin *handle, PluginDescription description, ApiBridge *bridge)
    : api_(api), handle_(handle), description_(std::move(description)), bridge_(bridge)
{
}

bool JsPlugin::onCommand(CommandSender &sender, const Command &command, const std::vector<std::string> &args)
{
    if (!handle_ || !api_.plugin_command || !bridge_) {
        return false;
    }

    std::vector<const char *> argv;
    argv.reserve(args.size());
    for (const auto &arg : args) {
        argv.push_back(arg.c_str());
    }

    // The sender handle is scoped to this call, exactly like an event's handles.
    const auto sender_handle = bridge_->trackSender(sender);
    int handled = 0;
    const auto status = api_.plugin_command(handle_, command.getName().c_str(), sender_handle,
                                           argv.empty() ? nullptr : argv.data(), argv.size(), &handled);
    bridge_->releaseDispatch();

    if (status != ESN_OK) {
        getLogger().error("Command '{}' failed: {}", command.getName(), api_.message(status));
        return true;  // reported already; showing the usage on top would only be noise
    }
    return handled != 0;
}

JsPlugin::~JsPlugin()
{
    release();
}

void JsPlugin::release()
{
    if (handle_ && api_.plugin_unload) {
        api_.plugin_unload(handle_);
    }
    handle_ = nullptr;
}

void JsPlugin::invoke(const esn_plugin_hook hook, const char *what)
{
    if (!handle_ || !api_.plugin_invoke) {
        return;
    }
    if (const auto status = api_.plugin_invoke(handle_, hook); status != ESN_OK) {
        getLogger().error("{} failed: {}", what, api_.message(status));
    }
}

void JsPlugin::onLoad()
{
    invoke(ESN_HOOK_LOAD, "onLoad");
}

void JsPlugin::onEnable()
{
    invoke(ESN_HOOK_ENABLE, "onEnable");
}

void JsPlugin::onDisable()
{
    invoke(ESN_HOOK_DISABLE, "onDisable");
}

JsPluginLoader::JsPluginLoader(Server &server, const HostApi &api, esn_host *host, ApiBridge *bridge)
    : PluginLoader(server), api_(api), host_(host), bridge_(bridge)
{
}

void JsPluginLoader::releaseAll()
{
    for (const auto &plugin : plugins_) {
        plugin->release();
    }
}

std::vector<std::string> JsPluginLoader::getPluginFileFilters() const
{
    // A folder plugin is addressed by its manifest, which keeps directory plugins compatible with
    // EndstonePluginManager::loadPlugin(file) and its filename-regex loader resolution.
    return {"package\\.json$", "\\.js$"};
}

std::vector<std::string> JsPluginLoader::discover(const fs::path &directory) const
{
    std::vector<std::string> found;
    std::error_code ec;
    if (!exists(directory, ec)) {
        return found;
    }

    for (const auto &entry : fs::directory_iterator(directory, ec)) {
        const auto &path = entry.path();
        if (entry.is_directory(ec)) {
            // Skip the shadow-copy and package staging directories Endstone manages itself.
            const auto name = path.filename().string();
            if (name.starts_with(".") || name == "node_modules") {
                continue;
            }
            if (auto manifest = path / "package.json"; exists(manifest, ec)) {
                found.push_back(manifest.string());
            }
        }
        else if (entry.is_regular_file(ec) && path.extension() == ".js") {
            found.push_back(path.string());
        }
    }

    std::ranges::sort(found);
    return found;
}

Plugin *JsPluginLoader::loadPlugin(std::string file)
{
    auto &logger = server_.getLogger();
    auto path = absolute(fs::path(file));

    // A manifest stands in for its directory; the host reads package.json itself.
    if (path.filename() == "package.json") {
        path = path.parent_path();
    }

    std::error_code ec;
    if (!exists(path, ec)) {
        logger.error("Could not load JavaScript plugin from '{}': path does not exist.", path.string());
        return nullptr;
    }
    if (!host_ || !api_.load_plugin || !api_.plugin_get_meta) {
        logger.error("Could not load JavaScript plugin from '{}': the Node host is unavailable.", path.string());
        return nullptr;
    }

    esn_plugin *handle = nullptr;
    if (const auto status = api_.load_plugin(host_, path.string().c_str(), &handle); status != ESN_OK || !handle) {
        if (status == ESN_ERR_SCRIPT_FAILED) {
            // The host already logged the JavaScript error (and the stack) itself.
            logger.error("Could not load JavaScript plugin from '{}' - see the error above for what went wrong.",
                         path.string());
        }
        else {
            logger.error("Could not load JavaScript plugin from '{}': {}", path.string(), api_.message(status));
        }
        return nullptr;
    }

    esn_plugin_meta meta{};
    if (const auto status = api_.plugin_get_meta(handle, &meta); status != ESN_OK) {
        logger.error("Could not read metadata for '{}': {}", path.string(), api_.message(status));
        api_.plugin_unload(handle);
        return nullptr;
    }

    PluginDescription description{
        meta.name ? meta.name : "",
        meta.version ? meta.version : "0.0.0",
        meta.description ? meta.description : "",
        parseLoadOrder(meta.load_order, server_, meta.name ? meta.name : "<unnamed>"),
        toVector(meta.authors, meta.author_count),
        {},  // contributors
        meta.website ? meta.website : "",
        {},  // prefix, derived from the name by Endstone
        {},  // provides
        toVector(meta.depend, meta.depend_count),
        {},  // soft_depend
        {},  // load_before
        PermissionDefault::Operator,
        toCommands(meta.commands, meta.command_count),
    };

    if (meta.api_version) {
        const std::string supported = ENDSTONE_API_VERSION;
        if (supported != meta.api_version) {
            logger.error("Could not load JavaScript plugin '{}': it targets API version {}, "
                         "but this server provides {}.",
                         description.getName(), meta.api_version, supported);
            api_.plugin_unload(handle);
            return nullptr;
        }
    }
    else {
        logger.warning("JavaScript plugin '{}' does not declare endstone.apiVersion in its package.json.",
                       description.getName());
    }

    return plugins_.emplace_back(std::make_unique<JsPlugin>(api_, handle, std::move(description), bridge_)).get();
}

}  // namespace endstone::node
