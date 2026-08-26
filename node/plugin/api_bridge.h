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
#include <unordered_set>
#include <vector>

#include <endstone/plugin/plugin.h>

#include "endstone_node_abi.h"

namespace endstone {
class Actor;
class Block;
class BlockData;
class BlockState;
class BossBar;
class CommandSender;
class DamageSource;
class Event;
class Inventory;
class Item;
class ItemStack;
class Dimension;
class Level;
class Location;
class MapCanvas;
class MapRenderer;
class MapView;
class Mob;
class Player;
class Scoreboard;
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

    /** Where map draws go - normally esn_host_render_map. Set before a renderer is added. */
    using RenderSink = std::function<void(std::uint32_t renderer, esn_handle canvas, esn_handle player)>;
    void setRenderSink(RenderSink sink);

    /** Attaches a JavaScript renderer to a map. Endstone owns it once added. */
    esn_status addMapRenderer(esn_handle map, std::uint32_t renderer);

    /** Draws one renderer, bracketing its own scope so the canvas cannot outlive the call. */
    void renderMap(std::uint32_t renderer, MapCanvas &canvas, Player &player);

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
    esn_status serverSelf(esn_handle *out);
    void broadcastMessage(std::string_view message);
    void log(int level, std::string_view message);

    esn_status getBool(esn_handle target, std::string_view name, int *out);
    esn_status getInt(esn_handle target, std::string_view name, std::int64_t *out);
    esn_status getDouble(esn_handle target, std::string_view name, double *out);
    esn_status getString(esn_handle target, std::string_view name, char *buf, std::size_t cap, std::size_t *needed);
    esn_status getHandle(esn_handle target, std::string_view name, esn_handle *out);
    esn_status getBytes(esn_handle target, std::string_view name, char *buf, std::size_t cap, std::size_t *needed);
    esn_status setBytes(esn_handle target, std::string_view name, std::string_view value);
    esn_status setBool(esn_handle target, std::string_view name, bool value);
    esn_status setInt(esn_handle target, std::string_view name, std::int64_t value);
    esn_status setDouble(esn_handle target, std::string_view name, double value);
    esn_status setString(esn_handle target, std::string_view name, std::string_view value);
    esn_status invoke(esn_handle target, std::string_view name, const char *const *strings,
                      std::size_t string_count, const double *numbers, std::size_t number_count,
                      const esn_handle *handles, std::size_t handle_count, esn_handle *out_handle);
    esn_status typeName(esn_handle target, char *buf, std::size_t cap, std::size_t *needed);

    /** Where form outcomes go - normally esn_host_form_result. Set before sending a form. */
    using FormSink = std::function<void(std::uint32_t form_id, bool closed, std::string data)>;
    void setFormSink(FormSink sink);

    /** Shows a form. `spec` is the 0x1e/0x1f record format described in the ABI header. */
    esn_status sendForm(esn_handle player, std::uint32_t form_id, std::string_view spec);
    esn_status closeForm(esn_handle player);

    /** Sends a raw packet body to one player. `payload` may contain NUL bytes. */
    esn_status sendPacket(esn_handle player, int packet_id, std::string_view payload);

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
     * is tracked as a CommandSender. Open a scope around it and close that when the handler returns.
     */
    esn_handle trackSender(CommandSender &sender);

    /**
     * @brief Where to unwind a dispatch back to.
     *
     * Dispatches nest: a handler that makes the server fire another event, or run another command,
     * re-enters the bridge. A scope must therefore restore what it found rather than drop everything,
     * or the inner one frees the objects the outer handler is still holding and leaves its handles
     * pointing at freed memory.
     */
    struct ScopeMark {
        esn_handle handle_start;
        std::size_t blocks;
        std::size_t block_data;
        std::size_t block_states;
        std::size_t locations;
        std::size_t vectors;
        std::size_t items;
    };

    /** Opens a dispatch scope. Pair every call with endScope(). */
    [[nodiscard]] ScopeMark beginScope();
    /** Invalidates everything minted since the mark, leaving anything older untouched. */
    void endScope(const ScopeMark &mark);

    esn_status subscribe(std::string_view event_name, int priority, bool ignore_cancelled, std::uint32_t *out);
    esn_status unsubscribe(std::uint32_t subscription);

private:
    // Mob is distinct from Actor because only living things have health, and Endstone's actor events
    // are templated on one or the other (ActorEvent<Mob> vs ActorEvent<Actor>).
    enum class Kind : std::uint8_t {
        // Item is a dropped item stack in the world, distinct from ItemStack which is the stack itself.
        Player, Mob, Actor, Item, Block, Level, DamageSource, ItemStack, Location, Vector, CommandSender,
        // PlayerInventory is distinct from Inventory only so the equipment slots can be reached; every
        // generic inventory operation accepts either.
        Inventory, PlayerInventory, Plugin, MapView, MapCanvas, Server, Dimension, Scoreboard, Event, BossBar,
        // A block's palette entry (type plus its states), and a detached snapshot of one position.
        BlockData, BlockState
    };

    /**
     * @brief Boss bar methods. Unlike a scoreboard objective, a bar is addressed by handle.
     *
     * createBossBar hands back a unique_ptr, so the bridge owns it and the handle is persistent - a bar
     * is meant to outlive the callback that made it and be updated from a timer. `remove` is therefore
     * the only way it goes away, and it both clears the viewers and drops the handle.
     */
    esn_status bossBarInvoke(esn_handle target, BossBar &bar, std::string_view name,
                             const std::function<std::string(std::size_t)> &str,
                             const std::function<esn_handle(std::size_t)> &handle_at);

    /** Owns every bar handed to JavaScript; entries are erased by BossBar `remove`. */
    std::vector<std::unique_ptr<BossBar>> owned_boss_bars_;

    /**
     * Scoreboards made by server.createScoreboard(), which hands back a shared_ptr the caller must keep.
     * A scoreboard outlives the callback that made it - the point of a per-player one is to update it
     * later - so these are held for the plugin's lifetime rather than released with the dispatch scope.
     */
    std::vector<std::shared_ptr<Scoreboard>> owned_scoreboards_;

    /** Scoreboard methods, keyed by objective name rather than by handle - see the implementation. */
    esn_status scoreboardInvoke(Scoreboard &board, std::string_view name,
                                const std::function<std::string(std::size_t)> &str,
                                const std::function<double(std::size_t, double)> &number,
                                std::size_t number_count, esn_handle *out_handle);

    /**
     * @brief One Endstone listener, shared by every subscription that asked for the same thing.
     *
     * Endstone has no per-handler unregister, so registering one listener per subscription would
     * leave a dead listener behind on every unsubscribe - and a plugin that toggles a subscription
     * accumulates them for the server's lifetime. Registrations are keyed by what actually reaches
     * Endstone, so their number is bounded by the distinct combinations a plugin uses.
     */
    struct Registration {
        std::string event_name;
        int priority;
        bool ignore_cancelled;
        /** Subscription ids to deliver to, in the order they subscribed. */
        std::vector<std::uint32_t> subscribers;
        /** False while the plugin is not enabled yet; see flushPendingSubscriptions(). */
        bool registered;
    };

    /** Registers `registrations_[index]` with Endstone. */
    void registerWithEndstone(std::size_t index);
    /** The registration matching these terms, creating one if there is none. */
    std::size_t registrationFor(std::string_view event_name, int priority, bool ignore_cancelled);

    std::vector<Registration> registrations_;

    /** Event names already reported as undeliverable because they fire off the server thread. */
    std::unordered_set<std::string> warned_async_;

    FormSink form_sink_;
    TaskSink task_sink_;
    RenderSink render_sink_;
    /** Keeps every renderer handed to a map alive; Endstone holds a shared_ptr too. */
    std::vector<std::shared_ptr<MapRenderer>> owned_map_renderers_;
    /** Scheduled tasks by the id JavaScript knows them by, so they can be cancelled. */
    std::unordered_map<std::uint32_t, std::shared_ptr<Task>> tasks_;
    std::uint32_t next_task_{1};

    /** Owns anything created on demand whose lifetime is not the caller's, e.g. Block instances. */
    std::vector<std::unique_ptr<Block>> owned_blocks_;
    // Both are handed out as unique_ptr and released with the dispatch scope, like owned_blocks_.
    std::vector<std::unique_ptr<endstone::BlockData>> owned_block_data_;
    std::vector<std::unique_ptr<endstone::BlockState>> owned_block_states_;
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

    /**
     * @brief Records a pointer under a kind and returns its handle.
     *
     * **The pointer's static type must be the class the kind names**, not a base or a derived class of
     * it. `resolve` hands back a void* and every caller `static_cast`s it, and a cast through void does
     * no pointer adjustment - so storing a `CommandSender *` under `Kind::Player`, or an `Actor *` under
     * `Kind::Mob`, yields a pointer that is only usable while that base happens to sit at offset zero.
     * `Player` uses multiple inheritance (`Mob` plus `OfflinePlayer`), so this is not hypothetical: it
     * is layout, not language, that makes a wrong static type appear to work.
     *
     * Nothing here enforces it - `void *` accepts anything. Narrow to the right type at the call site,
     * as `trackSender` (via `asPlayer()`) and `eventActor` (via the trait's `Mob *` getter) both do.
     */
    esn_handle track(void *ptr, Kind kind, bool persistent = false);
    /** Drops a handle, keeping the persistent index in step. */
    void untrack(esn_handle handle);
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

    /**
     * @brief Delivers one event to every subscriber of a registration, then unwinds the scope.
     *
     * One scope and one event handle serve all of them: they see the same event object, and a
     * handler that unsubscribes mid-delivery is honoured for the handlers that follow it.
     */
    void dispatch(std::size_t registration, Event &event);

    Plugin &plugin_;
    EventSink event_sink_;

    std::unordered_map<esn_handle, Entry> handles_;
    /**
     * Persistent handles by the object they point at, so repeated reads of e.g. server.scoreboard
     * hand back the one handle instead of minting another that is never released.
     */
    std::unordered_map<const void *, esn_handle> persistent_handles_;
    esn_handle next_handle_{1};
    esn_handle level_handle_{0};  // cached: the level is a singleton for the server's lifetime
    /** Subscription id -> index into registrations_. */
    std::unordered_map<std::uint32_t, std::size_t> subscriptions_;
    std::uint32_t next_subscription_{1};
};

}  // namespace endstone::node
