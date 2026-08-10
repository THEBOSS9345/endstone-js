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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <endstone/plugin/plugin.h>

#include "endstone_node_abi.h"

namespace endstone {
class Actor;
class Block;
class DamageSource;
class Event;
class Level;
class Mob;
class Player;
}  // namespace endstone

namespace endstone::node {

/**
 * @brief Exposes the Endstone API to the Node host over the C ABI.
 *
 * Two jobs. First, a handle table: JavaScript never sees an Endstone pointer, only an opaque integer
 * this class resolves and type-checks, so a stale reference is a clean error rather than a crash.
 * Handles are dispatch-scoped - everything minted while delivering one event is invalidated when that
 * event returns.
 *
 * Second, generic property dispatch. Endstone's API is far too large for one C entry point per
 * property, so the ABI carries a fixed handful of typed accessors keyed by name and this class routes
 * them. Adding a property is a case here plus a line of JavaScript, never an ABI change.
 *
 * Everything runs on the BDS server thread, the only thread that runs JavaScript.
 */
class ApiBridge {
public:
    /** Both out of line: the owned-object vectors hold types only forward-declared here. */
    explicit ApiBridge(Plugin &plugin);
    ~ApiBridge();
    ApiBridge(const ApiBridge &) = delete;
    ApiBridge &operator=(const ApiBridge &) = delete;

    /** Fills in the accessor and subscription members of the table handed to the host. */
    void fill(esn_endstone_api &api);

    /** Where dispatched events go - normally esn_host_dispatch_event. Set before subscribing. */
    using EventSink = std::function<void(std::uint32_t subscription, esn_handle event)>;
    void setEventSink(EventSink sink);

    /** Drops all subscriptions. Must run before the Node host is destroyed. */
    void shutdown();

    // --- called by the ABI trampolines ------------------------------------------------------------
    std::size_t serverName(char *buf, std::size_t cap);
    std::size_t serverVersion(char *buf, std::size_t cap);
    std::size_t serverMinecraftVersion(char *buf, std::size_t cap);
    int serverProtocolVersion();
    int serverOnlinePlayerCount();
    esn_status serverLevel(esn_handle *out);
    void broadcastMessage(std::string_view message);
    void log(int level, std::string_view message);

    esn_status getBool(esn_handle target, std::string_view name, int *out);
    esn_status getInt(esn_handle target, std::string_view name, std::int64_t *out);
    esn_status getDouble(esn_handle target, std::string_view name, double *out);
    esn_status getString(esn_handle target, std::string_view name, char *buf, std::size_t cap, std::size_t *needed);
    esn_status getHandle(esn_handle target, std::string_view name, esn_handle *out);
    esn_status setBool(esn_handle target, std::string_view name, bool value);
    esn_status setInt(esn_handle target, std::string_view name, std::int64_t value);
    esn_status setDouble(esn_handle target, std::string_view name, double value);
    esn_status setString(esn_handle target, std::string_view name, std::string_view value);
    esn_status invoke(esn_handle target, std::string_view name, const char *const *strings,
                      std::size_t string_count, const double *numbers, std::size_t number_count,
                      esn_handle *out_handle);
    esn_status typeName(esn_handle target, char *buf, std::size_t cap, std::size_t *needed);
    esn_status subscribe(std::string_view event_name, int priority, bool ignore_cancelled, std::uint32_t *out);
    esn_status unsubscribe(std::uint32_t subscription);

private:
    // Mob is distinct from Actor because only living things have health, and Endstone's actor events
    // are templated on one or the other (ActorEvent<Mob> vs ActorEvent<Actor>).
    enum class Kind : std::uint8_t { Player, Mob, Actor, Block, Level, DamageSource, Event };

    /** Owns anything created on demand whose lifetime is not the caller's, e.g. Block instances. */
    std::vector<std::unique_ptr<Block>> owned_blocks_;

    struct Entry {
        void *ptr{nullptr};
        Kind kind{Kind::Player};
        /** Survives dispatch-scope invalidation. Only for objects that outlive a callback, e.g. Level. */
        bool persistent{false};
    };

    esn_handle track(void *ptr, Kind kind, bool persistent = false);
    /**
     * Tracks an actor whose concrete type is not known statically, picking Kind::Player when the
     * pointer matches an online player. That check avoids RTTI, which is unusable across the plugin
     * boundary, so JavaScript still sees a real Player for things like a damage source's attacker.
     */
    esn_handle trackActor(Actor *actor);
    /** Resolves a handle to the requested kind, or nullptr when stale or mismatched. */
    void *resolve(esn_handle handle, Kind kind) const;
    /** Any living or non-living actor, i.e. Kind::Actor or Kind::Mob. Players resolve separately. */
    Actor *resolveActor(esn_handle handle) const;
    /** Only a living actor, so only Kind::Mob. */
    Mob *resolveMob(esn_handle handle) const;
    const Entry *find(esn_handle handle) const;

    /** Delivers one event to the host, then invalidates every handle that delivery created. */
    void dispatch(std::uint32_t subscription, Event &event);

    Plugin &plugin_;
    EventSink event_sink_;

    std::unordered_map<esn_handle, Entry> handles_;
    esn_handle next_handle_{1};
    esn_handle level_handle_{0};  // cached: the level is a singleton for the server's lifetime
    std::unordered_map<std::uint32_t, std::string> subscriptions_;
    std::uint32_t next_subscription_{1};
};

}  // namespace endstone::node
