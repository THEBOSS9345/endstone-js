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
#include <deque>
#include <unordered_map>
#include <vector>

#include <endstone/plugin/plugin.h>

#include "endstone_node_abi.h"

namespace endstone {
class Actor;
class Block;
class CommandSender;
class DamageSource;
class Event;
class Inventory;
class ItemStack;
class Level;
class Location;
class MapView;
class Mob;
class Player;
class Task;
class PlayerInventory;
class Vector;
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

    /** Where scheduled tasks go - normally esn_host_run_task. Set before scheduling. */
    using TaskSink = std::function<void(std::uint32_t task)>;
    void setTaskSink(TaskSink sink);

    /** Schedules a JavaScript callback on the server thread. period 0 runs it once. */
    esn_status scheduleTask(std::uint32_t delay_ticks, std::uint32_t period_ticks, std::uint32_t *out_task);
    void cancelTask(std::uint32_t task);

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

    /** Resends the permission-filtered command list to every online player. */
    void updateCommands();

    /**
     * @brief Registers the subscriptions that were made before this plugin was enabled.
     *
     * Must be called from the owning plugin's onEnable. Until then Endstone rejects listeners, so
     * subscriptions made during plugin load are held back rather than lost.
     */
    void flushPendingSubscriptions();

    /**
     * @brief Wraps a command sender for the duration of one command dispatch.
     *
     * A player sender is tracked as a Player so a handler gets the whole Player surface; anything else
     * is tracked as a CommandSender. Release it with releaseDispatch() when the handler returns.
     */
    esn_handle trackSender(CommandSender &sender);

    /** Invalidates every non-persistent handle, as the end of an event dispatch does. */
    void releaseDispatch();
    esn_status subscribe(std::string_view event_name, int priority, bool ignore_cancelled, std::uint32_t *out);
    esn_status unsubscribe(std::uint32_t subscription);

private:
    // Mob is distinct from Actor because only living things have health, and Endstone's actor events
    // are templated on one or the other (ActorEvent<Mob> vs ActorEvent<Actor>).
    enum class Kind : std::uint8_t {
        Player, Mob, Actor, Block, Level, DamageSource, ItemStack, Location, Vector, CommandSender,
        // PlayerInventory is distinct from Inventory only so the equipment slots can be reached; every
        // generic inventory operation accepts either.
        Inventory, PlayerInventory, Plugin, MapView, Event
    };

    /** Invalidates every non-persistent handle minted at or after `scope_start`. */
    void releaseScope(esn_handle scope_start);

    void registerWithEndstone(std::uint32_t subscription, std::string_view event_name, int priority,
                              bool ignore_cancelled);

    /** A subscription made before the plugin was enabled, waiting for flushPendingSubscriptions(). */
    struct PendingSubscription {
        std::uint32_t subscription;
        std::string event_name;
        int priority;
        bool ignore_cancelled;
    };
    std::vector<PendingSubscription> pending_;

    TaskSink task_sink_;
    /** Scheduled tasks by the id JavaScript knows them by, so they can be cancelled. */
    std::unordered_map<std::uint32_t, std::shared_ptr<Task>> tasks_;
    std::uint32_t next_task_{1};

    /** Where the current command dispatch's handles begin, for releaseDispatch(). */
    esn_handle command_scope_start_{0};

    /** Owns anything created on demand whose lifetime is not the caller's, e.g. Block instances. */
    std::vector<std::unique_ptr<Block>> owned_blocks_;
    // deques so push_back never invalidates an already-tracked pointer within one dispatch.
    std::deque<Location> owned_locations_;
    std::deque<Vector> owned_vectors_;
    // Inventory::getItem returns by value, so the stack handed to JavaScript has to live somewhere.
    // unique_ptr because ItemStack is only forward-declared here, and because the pointee never moves.
    std::vector<std::unique_ptr<ItemStack>> owned_items_;

    /** The inventory behind a handle, whether it was tracked as a plain or a player inventory. */
    Inventory *resolveInventory(esn_handle target);

    /**
     * @brief Hands out a copy of an item stack that writes its changes back where it came from.
     *
     * Endstone returns item stacks by value, so a stack read from an inventory or an equipment slot is
     * a copy and mutating it would be silently lost. Every such stack is therefore paired with a
     * writeback that puts it back in its slot, which runs after any property is set on it.
     */
    esn_handle trackOwnedItem(ItemStack item, std::function<void(const ItemStack &)> writeback);

    /** Runs the writeback for an item handle, if it has one. Called after every successful set. */
    void persistItem(esn_handle target);

    std::unordered_map<esn_handle, std::function<void(const ItemStack &)>> item_writebacks_;

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
