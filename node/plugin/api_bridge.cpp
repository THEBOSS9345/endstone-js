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

#include "api_bridge.h"

#include "types/bind.h"
#include "types/block_face.h"
#include "types/block_states.h"
#include "types/inventory/item_meta.h"
#include "types/records.h"
#include "types/descriptor.h"

#include <cstring>
#include <optional>
#include <variant>
#include <utility>

#include <endstone/actor/actor.h>
#include <endstone/actor/item.h>
#include <endstone/actor/mob.h>
#include <endstone/ban/ip_ban_list.h>
#include <endstone/ban/player_ban_list.h>
#include <endstone/block/block.h>
#include <endstone/block/block_data.h>
#include <endstone/block/block_state.h>
#include <endstone/boss/boss_bar.h>
#include <endstone/command/command_sender.h>
#include <endstone/command/console_command_sender.h>
#include <endstone/damage/damage_source.h>
#include <endstone/command/block_command_sender.h>
#include <endstone/enchantments/enchantment.h>
#include <endstone/event/actor/actor_damage_event.h>
#include <endstone/event/actor/actor_death_event.h>
#include <endstone/event/actor/actor_explode_event.h>
#include <endstone/event/actor/actor_knockback_event.h>
#include <endstone/event/actor/actor_remove_event.h>
#include <endstone/event/actor/actor_spawn_event.h>
#include <endstone/event/actor/actor_teleport_event.h>
#include <endstone/event/actor/player_death_event.h>
#include <endstone/event/block/block_break_event.h>
#include <endstone/event/block/block_cook_event.h>
#include <endstone/event/block/block_event.h>
#include <endstone/event/block/block_explode_event.h>
#include <endstone/event/block/block_form_event.h>
#include <endstone/event/block/block_from_to_event.h>
#include <endstone/event/block/block_grow_event.h>
#include <endstone/event/block/block_piston_extend_event.h>
#include <endstone/event/block/block_piston_retract_event.h>
#include <endstone/event/block/block_place_event.h>
#include <endstone/event/block/leaves_decay_event.h>
#include <endstone/event/cancellable.h>
#include <endstone/event/chunk/chunk_event.h>
#include <endstone/event/player/player_bed_enter_event.h>
#include <endstone/event/player/player_bed_leave_event.h>
#include <endstone/event/player/player_chat_event.h>
#include <endstone/event/player/player_command_event.h>
#include <endstone/event/player/player_dimension_change_event.h>
#include <endstone/event/player/player_drop_item_event.h>
#include <endstone/event/player/player_emote_event.h>
#include <endstone/event/player/player_event.h>
#include <endstone/event/player/player_game_mode_change_event.h>
#include <endstone/event/player/player_interact_actor_event.h>
#include <endstone/event/player/player_interact_event.h>
#include <endstone/event/player/player_item_consume_event.h>
#include <endstone/event/player/player_item_held_event.h>
#include <endstone/event/player/player_join_event.h>
#include <endstone/event/player/player_jump_event.h>
#include <endstone/event/player/player_kick_event.h>
#include <endstone/event/player/player_login_event.h>
#include <endstone/event/player/player_move_event.h>
#include <endstone/event/player/player_pickup_item_event.h>
#include <endstone/event/player/player_portal_event.h>
#include <endstone/event/player/player_quit_event.h>
#include <endstone/event/player/player_respawn_event.h>
#include <endstone/event/player/player_skin_change_event.h>
#include <endstone/event/player/player_teleport_event.h>
#include <endstone/event/server/broadcast_message_event.h>
#include <endstone/event/server/map_initialize_event.h>
#include <endstone/event/server/packet_receive_event.h>
#include <endstone/event/server/packet_send_event.h>
#include <endstone/event/server/plugin_disable_event.h>
#include <endstone/event/server/plugin_enable_event.h>
#include <endstone/event/server/script_message_event.h>
#include <endstone/event/server/server_command_event.h>
#include <endstone/event/server/server_list_ping_event.h>
#include <endstone/event/server/server_load_event.h>
#include <endstone/event/weather/thunder_change_event.h>
#include <endstone/event/weather/weather_change_event.h>
#include <endstone/form/action_form.h>
#include <endstone/form/message_form.h>
#include <endstone/form/modal_form.h>
#include <endstone/game_mode.h>
#include <endstone/inventory/equipment_slot.h>
#include <endstone/inventory/inventory.h>
#include <endstone/inventory/item_stack.h>
#include <endstone/inventory/item_type.h>
#include <endstone/inventory/meta/book_meta.h>
#include <endstone/inventory/meta/crossbow_meta.h>
#include <endstone/inventory/meta/item_meta.h>
#include <endstone/inventory/meta/map_meta.h>
#include <endstone/inventory/meta/writable_book_meta.h>
#include <endstone/inventory/player_inventory.h>
#include <endstone/lang/language.h>
#include <endstone/level/chunk.h>
#include <endstone/level/dimension.h>
#include <endstone/level/level.h>
#include <endstone/level/location.h>
#include <endstone/map/map_canvas.h>
#include <endstone/map/map_renderer.h>
#include <endstone/map/map_view.h>
#include <endstone/nbt/tag.h>
#include <endstone/permissions/permissible.h>
#include <endstone/permissions/permission_level.h>
#include <endstone/player.h>
#include <endstone/plugin/plugin_manager.h>
#include <endstone/scheduler/scheduler.h>
#include <endstone/scheduler/task.h>
#include <endstone/scoreboard/criteria.h>
#include <endstone/scoreboard/objective.h>
#include <endstone/scoreboard/score.h>
#include <endstone/scoreboard/scoreboard.h>
#include <endstone/server.h>
#include <endstone/skin.h>
#include <endstone/util/color.h>
#include <endstone/util/socket_address.h>
#include <endstone/util/vector.h>

namespace endstone::node {

namespace {

/** The ABI's string convention: fill up to `cap`, NUL-terminate when it fits, report the true size. */
esn_status emitString(std::string_view text, char *buf, std::size_t cap, std::size_t *needed)
{
    if (needed) {
        *needed = text.size();
    }
    if (buf && cap > 0) {
        const auto count = text.size() < cap - 1 ? text.size() : cap - 1;
        std::memcpy(buf, text.data(), count);
        buf[count] = '\0';
    }
    return ESN_OK;
}

/*
 * Downcasting an Endstone event.
 *
 * NOT dynamic_cast. The plugin is built with hidden visibility and its own statically linked
 * libc++abi, so its type_info for PlayerEvent is a different object from the one endstone_runtime used
 * when it created the event. libc++abi compares type_info by pointer identity, so the cast silently
 * fails across the library boundary - which is exactly what it did the first time a player joined.
 *
 * getEventName() is a virtual call, so it always works. Once the concrete type is known by name, a
 * static_cast is safe and needs no RTTI at all.
 */
template <typename E>
Player *playerOf(Event *event)
{
    return &static_cast<E *>(event)->getPlayer();
}

template <typename E>
Actor *actorOf(Event *event)
{
    return &static_cast<E *>(event)->getActor();
}

/** For events templated on Mob, so the handle carries that and health becomes reachable. */
template <typename E>
Mob *mobOf(Event *event)
{
    return &static_cast<E *>(event)->getActor();
}

template <typename E>
Block *blockOf(Event *event)
{
    return &static_cast<E *>(event)->getBlock();
}

template <typename E>
ICancellable *cancellableOf(Event *event)
{
    return static_cast<E *>(event);
}
// GameMode is an enum; magic_enum is a core dependency and not available to plugins, so map by hand.
constexpr std::pair<GameMode, std::string_view> kGameModes[] = {
    {GameMode::Survival, "survival"},
    {GameMode::Creative, "creative"},
    {GameMode::Adventure, "adventure"},
    {GameMode::Spectator, "spectator"},
};

std::string_view gameModeName(const GameMode mode)
{
    for (const auto &[value, name] : kGameModes) {
        if (value == mode) {
            return name;
        }
    }
    return "survival";
}

EventPriority toPriority(int value)
{
    switch (value) {
    case ESN_PRIORITY_LOWEST:
        return EventPriority::Lowest;
    case ESN_PRIORITY_LOW:
        return EventPriority::Low;
    case ESN_PRIORITY_HIGH:
        return EventPriority::High;
    case ESN_PRIORITY_HIGHEST:
        return EventPriority::Highest;
    case ESN_PRIORITY_MONITOR:
        return EventPriority::Monitor;
    default:
        return EventPriority::Normal;
    }
}

}  // namespace

ApiBridge::ApiBridge(Plugin &plugin) : plugin_(plugin) {}

ApiBridge::~ApiBridge()
{
    shutdown();
}

// --- server -------------------------------------------------------------------------------------

std::size_t ApiBridge::serverName(char *buf, const std::size_t cap)
{
    std::size_t needed = 0;
    (void)emitString(plugin_.getServer().getName(), buf, cap, &needed);
    return needed;
}

std::size_t ApiBridge::serverVersion(char *buf, const std::size_t cap)
{
    std::size_t needed = 0;
    (void)emitString(plugin_.getServer().getVersion(), buf, cap, &needed);
    return needed;
}

std::size_t ApiBridge::serverMinecraftVersion(char *buf, const std::size_t cap)
{
    std::size_t needed = 0;
    (void)emitString(plugin_.getServer().getMinecraftVersion(), buf, cap, &needed);
    return needed;
}

int ApiBridge::serverProtocolVersion()
{
    return plugin_.getServer().getProtocolVersion();
}

int ApiBridge::serverOnlinePlayerCount()
{
    // Before the level loads there is no player list to ask.
    auto &server = plugin_.getServer();
    return server.getLevel() ? static_cast<int>(server.getOnlinePlayers().size()) : -1;
}

esn_status ApiBridge::serverSelf(esn_handle *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    // Persistent: the server outlives every callback, so this handle never goes stale.
    *out = track(&plugin_.getServer(), Kind::Server, true);
    return ESN_OK;
}

esn_status ApiBridge::serverLevel(esn_handle *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    auto *level = plugin_.getServer().getLevel();
    if (!level) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    // The level is a singleton for the server's lifetime, so its handle is minted once and kept.
    if (level_handle_ == 0 || resolve(level_handle_, Kind::Level) != level) {
        level_handle_ = track(level, Kind::Level, true);
    }
    *out = level_handle_;
    return ESN_OK;
}

void ApiBridge::broadcastMessage(const std::string_view message)
{
    plugin_.getServer().broadcastMessage(Message{std::string{message}});
}

void ApiBridge::log(const int level, const std::string_view message)
{
    auto mapped = static_cast<Logger::Level>(level);
    if (level < Logger::Trace || level > Logger::Critical) {
        mapped = Logger::Info;
    }
    plugin_.getLogger().log(mapped, message);
}

// --- handle table --------------------------------------------------------------------------------

esn_handle ApiBridge::track(void *ptr, const Kind kind, const bool persistent)
{
    if (!ptr) {
        return 0;
    }
    // A persistent handle is never released, so minting a fresh one per read would grow the table for
    // the server's lifetime - and every dispatch walks that table. Reuse the one this object already
    // has, unless it was tracked under a different kind.
    if (persistent) {
        if (const auto it = persistent_handles_.find(ptr); it != persistent_handles_.end()) {
            const auto *entry = find(it->second);
            if (entry && entry->kind == kind) {
                return it->second;
            }
            persistent_handles_.erase(it);
        }
    }
    const auto handle = next_handle_++;
    handles_[handle] = Entry{ptr, kind, persistent};
    if (persistent) {
        persistent_handles_.emplace(ptr, handle);
    }
    return handle;
}

void ApiBridge::untrack(const esn_handle handle)
{
    if (const auto it = handles_.find(handle); it != handles_.end()) {
        if (it->second.persistent) {
            persistent_handles_.erase(it->second.ptr);
        }
        handles_.erase(it);
    }
}

esn_handle ApiBridge::trackActor(Actor *actor)
{
    if (!actor) {
        return 0;
    }
    // A pointer match against the online players is a reliable Player test that needs no RTTI.
    for (auto *player : plugin_.getServer().getOnlinePlayers()) {
        if (static_cast<Actor *>(player) == actor) {
            return track(player, Kind::Player);
        }
    }
    // asItem() is a virtual accessor, so it narrows without RTTI the same way asPlayer() does.
    if (auto *item = actor->asItem()) {
        return track(item, Kind::Item);
    }
    return track(actor, Kind::Actor);
}

const ApiBridge::Entry *ApiBridge::find(const esn_handle handle) const
{
    const auto it = handles_.find(handle);
    return it == handles_.end() ? nullptr : &it->second;
}

void *ApiBridge::resolve(const esn_handle handle, const Kind kind) const
{
    const auto *entry = find(handle);
    return (entry && entry->kind == kind) ? entry->ptr : nullptr;
}

Actor *ApiBridge::resolveActor(const esn_handle handle) const
{
    // Mob and Item both derive from Actor, so any of the three answers an Actor question.
    if (auto *mob = resolve(handle, Kind::Mob)) {
        return static_cast<Mob *>(mob);
    }
    if (auto *item = resolve(handle, Kind::Item)) {
        return static_cast<Item *>(item);
    }
    return static_cast<Actor *>(resolve(handle, Kind::Actor));
}

Mob *ApiBridge::resolveMob(const esn_handle handle) const
{
    return static_cast<Mob *>(resolve(handle, Kind::Mob));
}

// --- events --------------------------------------------------------------------------------------

void ApiBridge::setEventSink(EventSink sink)
{
    event_sink_ = std::move(sink);
}

std::size_t ApiBridge::registrationFor(const std::string_view event_name, const int priority,
                                      const bool ignore_cancelled)
{
    for (std::size_t i = 0; i < registrations_.size(); ++i) {
        const auto &registration = registrations_[i];
        if (registration.event_name == event_name && registration.priority == priority &&
            registration.ignore_cancelled == ignore_cancelled) {
            return i;
        }
    }
    registrations_.push_back(Registration{std::string{event_name}, priority, ignore_cancelled, {}, false});
    return registrations_.size() - 1;
}

esn_status ApiBridge::subscribe(const std::string_view event_name, const int priority, const bool ignore_cancelled,
                                std::uint32_t *out)
{
    if (event_name.empty() || !out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    const auto index = registrationFor(event_name, priority, ignore_cancelled);
    const auto subscription = next_subscription_++;
    subscriptions_.emplace(subscription, index);
    registrations_[index].subscribers.push_back(subscription);

    // Endstone refuses to register a listener while the owning plugin is not enabled, and every
    // JavaScript subscription is registered in this plugin's name. Plugins subscribe during load -
    // before this plugin is enabled - so registration waits for the enable. The subscription id is
    // minted here either way, so JavaScript never has to know which path was taken.
    if (plugin_.isEnabled() && !registrations_[index].registered) {
        registerWithEndstone(index);
    }

    *out = subscription;
    return ESN_OK;
}

void ApiBridge::registerWithEndstone(const std::size_t index)
{
    registrations_[index].registered = true;
    plugin_.getServer().getPluginManager().registerEvent(
        registrations_[index].event_name, [this, index](Event &event) { dispatch(index, event); },
        toPriority(registrations_[index].priority), plugin_, registrations_[index].ignore_cancelled);
}

void ApiBridge::flushPendingSubscriptions()
{
    for (std::size_t i = 0; i < registrations_.size(); ++i) {
        // Anything unsubscribed before the flush leaves an empty registration, which is not worth
        // wiring up until something subscribes to it again.
        if (!registrations_[i].registered && !registrations_[i].subscribers.empty()) {
            registerWithEndstone(i);
        }
    }
}

esn_status ApiBridge::unsubscribe(const std::uint32_t subscription)
{
    const auto it = subscriptions_.find(subscription);
    if (it == subscriptions_.end()) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    // The Endstone listener stays wired - there is no per-handler unregister - but it is shared by
    // every subscription with the same terms, so it is reused rather than accumulated when something
    // subscribes again.
    auto &subscribers = registrations_[it->second].subscribers;
    std::erase(subscribers, subscription);
    subscriptions_.erase(it);
    return ESN_OK;
}

void ApiBridge::dispatch(const std::size_t registration, Event &event)
{
    if (!event_sink_) {
        return;
    }
    // An asynchronous event is fired from whatever thread raised it - ServerListPingEvent comes off
    // RakNet's, not the server's. Delivering it would enter V8 from a thread that does not own the
    // isolate and would race this handle table, which is unsynchronised: two concurrent dispatches can
    // capture the same scope_start and then release each other's handles, which surfaces as a stale
    // handle inside a live callback. Dropped loudly rather than delivered unsafely.
    if (event.isAsynchronous() && !plugin_.getServer().isPrimaryThread()) {
        if (warned_async_.insert(std::string{event.getEventName()}).second) {
            plugin_.getLogger().warning(
                "{} is fired off the server thread, so JavaScript handlers for it are not run. "
                "Asynchronous events cannot reach JavaScript - see node/README.md.",
                event.getEventName());
        }
        return;
    }

    // Everything minted from here on belongs to this dispatch only.
    const auto mark = beginScope();
    const auto handle = track(&event, Kind::Event);

    // Copied, not referenced: a handler may subscribe or unsubscribe while it runs, which would move
    // the registration vector under us. Each id is re-checked so a handler that unsubscribes a later
    // one in this same list is honoured.
    const auto subscribers = registrations_[registration].subscribers;

    // The unwind must happen even if the sink throws. It cannot today - every host entry point
    // catches - but a handle surviving this scope would point at an Event that is about to be
    // destroyed, so the invariant is enforced here rather than relied upon from the other side.
    try {
        for (const auto subscription : subscribers) {
            if (subscriptions_.contains(subscription)) {
                event_sink_(subscription, handle);
            }
        }
    }
    catch (...) {
        endScope(mark);
        throw;
    }
    endScope(mark);
}

ApiBridge::ScopeMark ApiBridge::beginScope()
{
    return ScopeMark{next_handle_,          owned_blocks_.size(),  owned_block_data_.size(),
                     owned_block_states_.size(), owned_locations_.size(), owned_vectors_.size(),
                     owned_items_.size()};
}

void ApiBridge::endScope(const ScopeMark &mark)
{
    for (auto it = handles_.begin(); it != handles_.end();) {
        const bool expired = it->first >= mark.handle_start && !it->second.persistent;
        it = expired ? handles_.erase(it) : std::next(it);
    }
    // Truncated back to the mark rather than cleared: a nested dispatch must not free what the
    // dispatch around it is still holding. Erasing the tail keeps every earlier element's address,
    // which is what the outer scope's handles point at.
    const auto trim = [](auto &owned, const std::size_t keep) {
        if (owned.size() > keep) {
            owned.erase(owned.begin() + static_cast<std::ptrdiff_t>(keep), owned.end());
        }
    };
    trim(owned_blocks_, mark.blocks);
    trim(owned_block_data_, mark.block_data);
    trim(owned_block_states_, mark.block_states);
    trim(owned_locations_, mark.locations);
    trim(owned_vectors_, mark.vectors);
    trim(owned_items_, mark.items);
    std::erase_if(item_writebacks_,
                  [&](const auto &entry) { return entry.first >= mark.handle_start; });
}

esn_handle ApiBridge::trackOwnedItem(ItemStack item, std::function<void(const ItemStack &)> writeback)
{
    owned_items_.push_back(std::make_unique<ItemStack>(std::move(item)));
    const auto handle = track(owned_items_.back().get(), Kind::ItemStack);
    if (writeback) {
        item_writebacks_.emplace(handle, std::move(writeback));
    }
    return handle;
}

void ApiBridge::persistItem(const esn_handle target)
{
    const auto it = item_writebacks_.find(target);
    if (it == item_writebacks_.end() || !it->second) {
        return;
    }
}

Inventory *ApiBridge::resolveInventory(const esn_handle target)
{
    if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
        return player_inventory;
    }
    return static_cast<Inventory *>(resolve(target, Kind::Inventory));
}

void ApiBridge::setTaskSink(TaskSink sink)
{
    task_sink_ = std::move(sink);
}

esn_status ApiBridge::scheduleTask(const std::uint32_t delay_ticks, const std::uint32_t period_ticks,
                                   std::uint32_t *out_task)
{
    if (!out_task || !task_sink_) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    const auto id = next_task_++;
    auto &scheduler = plugin_.getServer().getScheduler();
    // A one-shot task drops itself from the table when it fires; a repeating one lives until cancelled.
    auto task = period_ticks == 0
                    ? scheduler.runTaskLater(plugin_,
                                             [this, id]() {
                                                 // Both copied to the stack first: erasing drops this
                                                 // task's shared_ptr, and if that were the last one the
                                                 // closure being executed - and `id` inside it - would
                                                 // go with it.
                                                 const auto sink = task_sink_;
                                                 const auto task_id = id;
                                                 tasks_.erase(task_id);
                                                 if (sink) {
                                                     sink(task_id);
                                                 }
                                             },
                                             delay_ticks)
                    : scheduler.runTaskTimer(plugin_, [this, id]() { if (task_sink_) { task_sink_(id); } },
                                             delay_ticks, period_ticks);
    if (!task) {
        return ESN_ERR_INTERNAL;
    }
    tasks_.emplace(id, std::move(task));
    *out_task = id;
    return ESN_OK;
}

void ApiBridge::cancelTask(const std::uint32_t task)
{
    const auto it = tasks_.find(task);
    if (it == tasks_.end()) {
        return;
    }
    if (it->second) {
        it->second->cancel();
    }
    tasks_.erase(it);
}

// --- forms ---------------------------------------------------------------------------------------
// The spec arrives as records separated by 0x1e, fields within a record by 0x1f. That format exists
// because the plugin side links only Endstone's header-only API - nlohmann is a core dependency and is
// not available here - and those two control bytes never occur in form text.
//
// Record 0 is the header: kind, title, content, primary button, secondary button.
// Records 1..n are controls, each starting with its kind.

namespace {

float floatAt(const std::vector<std::string> &fields, const std::size_t index, const float fallback)
{
    const auto text = fieldAt(fields, index);
    if (text.empty()) {
        return fallback;
    }
    try {
        return std::stof(text);
    }
    catch (...) {
        return fallback;
    }
}

std::optional<int> optionalIntAt(const std::vector<std::string> &fields, const std::size_t index)
{
    const auto text = fieldAt(fields, index);
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoi(text);
    }
    catch (...) {
        return std::nullopt;
    }
}

/** Options are the tail of a control record, after its fixed fields. */
std::vector<std::string> tailFrom(const std::vector<std::string> &fields, const std::size_t start)
{
    std::vector<std::string> out;
    for (std::size_t i = start; i < fields.size(); ++i) {
        out.push_back(fields[i]);
    }
    return out;
}

}  // namespace

// --- scoreboard ----------------------------------------------------------------------------------
// Deliberately keyed by objective name rather than handing out Objective handles. Endstone returns
// objectives as unique_ptr wrappers, so a handle would either have to be dispatch-scoped - useless for
// a sidebar you update from a timer - or leak. Looking each one up by name on every call costs nothing
// and cannot go stale.

namespace {

std::optional<DisplaySlot> displaySlotFromName(const std::string_view name)
{
    if (name == "sidebar") return DisplaySlot::SideBar;
    if (name == "belowName" || name == "belowname") return DisplaySlot::BelowName;
    if (name == "playerList" || name == "playerlist") return DisplaySlot::PlayerList;
    return std::nullopt;
}

std::string_view displaySlotName(const DisplaySlot slot)
{
    switch (slot) {
    case DisplaySlot::SideBar: return "sidebar";
    case DisplaySlot::BelowName: return "belowName";
    case DisplaySlot::PlayerList: return "playerList";
    }
    return "sidebar";
}

}  // namespace

esn_status ApiBridge::scoreboardInvoke(Scoreboard &board, const std::string_view name,
                                      const std::function<std::string(std::size_t)> &str,
                                      const std::function<double(std::size_t, double)> &number,
                                      const std::size_t number_count, esn_handle *out_handle)
{
    (void)out_handle;
    // addObjective(name, displayName): the only criteria Endstone offers is Dummy.
    if (name == "addObjective") {
        const auto objective_name = str(0);
        if (objective_name.empty()) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        if (board.getObjective(objective_name)) {
            return ESN_OK;  // idempotent, so a reload does not have to guard
        }
        const auto display = str(1);
        (void)board.addObjective(objective_name, Criteria::Type::Dummy,
                                 display.empty() ? objective_name : display);
        return ESN_OK;
    }
    if (name == "removeObjective") {
        if (const auto objective = board.getObjective(str(0))) {
            objective->unregister();
        }
        return ESN_OK;
    }
    if (name == "setDisplayName") {
        if (const auto objective = board.getObjective(str(0))) {
            objective->setDisplayName(str(1));
        }
        return ESN_OK;
    }
    // setDisplay(objective, slot, sortOrder)
    if (name == "setDisplay") {
        const auto objective = board.getObjective(str(0));
        if (!objective) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        const auto slot = displaySlotFromName(str(1));
        if (!slot) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        const auto order = str(2) == "ascending" ? ObjectiveSortOrder::Ascending : ObjectiveSortOrder::Descending;
        objective->setDisplay(*slot, order);
        return ESN_OK;
    }
    // setDisplaySlot(objective, slot) - an empty slot takes it off the board without unregistering it,
    // which is what Objective::setDisplaySlot(nullopt) does.
    if (name == "setDisplaySlot") {
        const auto objective = board.getObjective(str(0));
        if (!objective) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        const auto slot_name = str(1);
        if (slot_name.empty()) {
            objective->setDisplaySlot(std::nullopt);
            return ESN_OK;
        }
        const auto slot = displaySlotFromName(slot_name);
        if (!slot) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        objective->setDisplaySlot(*slot);
        return ESN_OK;
    }
    // setSortOrder(objective, order) - changes the ordering without disturbing where it is shown.
    if (name == "setSortOrder") {
        const auto objective = board.getObjective(str(0));
        if (!objective) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        const auto requested = str(1);
        if (requested != "ascending" && requested != "descending") {
            return ESN_ERR_BAD_ARGUMENT;
        }
        objective->setSortOrder(requested == "ascending" ? ObjectiveSortOrder::Ascending
                                                         : ObjectiveSortOrder::Descending);
        return ESN_OK;
    }
    if (name == "clearSlot") {
        const auto slot = displaySlotFromName(str(0));
        if (!slot) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        board.clearSlot(*slot);
        return ESN_OK;
    }
    // setScore(objective, entry, value). The entry is a name, which works for offline players too.
    if (name == "setScore") {
        const auto objective = board.getObjective(str(0));
        if (!objective) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        if (const auto score = objective->getScore(ScoreEntry{str(1)})) {
            score->setValue(static_cast<int>(number(0, 0)));
        }
        return ESN_OK;
    }
    if (name == "addScore") {
        const auto objective = board.getObjective(str(0));
        if (!objective) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        if (const auto score = objective->getScore(ScoreEntry{str(1)})) {
            const auto current = score->isScoreSet() ? score->getValue() : 0;
            score->setValue(current + static_cast<int>(number(0, 0)));
        }
        return ESN_OK;
    }
    if (name == "resetScores") {
        board.resetScores(ScoreEntry{str(0)});
        return ESN_OK;
    }
    (void)number_count;
    return ESN_ERR_NO_SUCH_MEMBER;
}

// --- ban lists -----------------------------------------------------------------------------------
// Keyed by target name, like the scoreboard and for the same reason: entries are owning objects whose
// lifetime is not the caller's. A whole entry crosses as one 0x1f-delimited record - the format the
// forms already use - because the accessors carry one scalar each and an entry has seven fields.

namespace {


/** Dates are system_clock; JavaScript wants epoch milliseconds. */
std::string epochMillis(const BanEntry::Date &date)
{
    return std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(date.time_since_epoch()).count());
}

/** An absent expiration means a permanent ban, and crosses as an empty field. */
std::string epochMillis(const std::optional<BanEntry::Date> &date)
{
    return date ? epochMillis(*date) : std::string{};
}

/**
 * @brief A block's states as one "key\x1ftype\x1fvalue" record per line.
 *
 * `BlockStates` is a `variant<bool, string, int>`, so the type travels with each value - otherwise the
 * runtime could not tell the string "true" from the boolean true, and a state like `upside_down_bit`
 * would come back as the wrong JavaScript type.
 */
/** name, uuid, xuid, reason, source, created, expiration - empty field for anything absent. */
template <typename Entry>
std::string banRecord(const Entry &entry)
{
    std::string out;
    const auto field = [&](const std::string &value) {
        if (!out.empty()) {
            out += kUnitSeparator;
        }
        out += value;
    };
    if constexpr (requires { entry.getName(); }) {
        field(entry.getName());
        field(entry.getUniqueId() ? entry.getUniqueId()->str() : "");
        field(entry.getXuid().value_or(""));
    }
    else {
        field(entry.getAddress());
        field("");
        field("");
    }
    field(entry.getReason());
    field(entry.getSource());
    field(epochMillis(entry.getCreated()));
    field(epochMillis(entry.getExpiration()));
    return out;
}

}  // namespace

// --- boss bars -----------------------------------------------------------------------------------

namespace {

std::optional<BarColor> barColorFromName(const std::string_view name)
{
    if (name == "pink") return BarColor::Pink;
    if (name == "blue") return BarColor::Blue;
    if (name == "red") return BarColor::Red;
    if (name == "green") return BarColor::Green;
    if (name == "yellow") return BarColor::Yellow;
    if (name == "purple") return BarColor::Purple;
    if (name == "rebeccaPurple" || name == "rebeccapurple") return BarColor::RebeccaPurple;
    if (name == "white") return BarColor::White;
    return std::nullopt;
}

std::string_view barColorName(const BarColor color)
{
    switch (color) {
    case BarColor::Pink: return "pink";
    case BarColor::Blue: return "blue";
    case BarColor::Red: return "red";
    case BarColor::Green: return "green";
    case BarColor::Yellow: return "yellow";
    case BarColor::Purple: return "purple";
    case BarColor::RebeccaPurple: return "rebeccaPurple";
    case BarColor::White: return "white";
    }
    return "white";
}

std::optional<BarStyle> barStyleFromName(const std::string_view name)
{
    if (name == "solid") return BarStyle::Solid;
    if (name == "segmented6") return BarStyle::Segmented6;
    if (name == "segmented10") return BarStyle::Segmented10;
    if (name == "segmented12") return BarStyle::Segmented12;
    if (name == "segmented20") return BarStyle::Segmented20;
    return std::nullopt;
}

std::string_view barStyleName(const BarStyle style)
{
    switch (style) {
    case BarStyle::Solid: return "solid";
    case BarStyle::Segmented6: return "segmented6";
    case BarStyle::Segmented10: return "segmented10";
    case BarStyle::Segmented12: return "segmented12";
    case BarStyle::Segmented20: return "segmented20";
    }
    return "solid";
}

/** The two optional flags, exposed as ordinary boolean properties rather than as a flag list. */
std::optional<BarFlag> barFlagFromName(const std::string_view name)
{
    if (name == "darkenSky" || name == "darkensky") return BarFlag::DarkenSky;
    if (name == "createFog" || name == "createfog") return BarFlag::CreateFog;
    return std::nullopt;
}

}  // namespace

esn_status ApiBridge::bossBarInvoke(const esn_handle target, BossBar &bar, const std::string_view name,
                                   const std::function<std::string(std::size_t)> &str,
                                   const std::function<esn_handle(std::size_t)> &handle_at)
{
    if (name == "addPlayer" || name == "removePlayer") {
        auto *player = static_cast<Player *>(resolve(handle_at(0), Kind::Player));
        if (!player) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        if (name == "addPlayer") {
            bar.addPlayer(*player);
        }
        else {
            bar.removePlayer(*player);
        }
        return ESN_OK;
    }
    if (name == "removeAll") {
        bar.removeAll();
        return ESN_OK;
    }
    if (name == "addFlag" || name == "removeFlag") {
        const auto flag = barFlagFromName(str(0));
        if (!flag) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        if (name == "addFlag") {
            bar.addFlag(*flag);
        }
        else {
            bar.removeFlag(*flag);
        }
        return ESN_OK;
    }
    // Destroys the bar: clears it from every viewer's screen, then drops the handle so a later call on
    // the same object is a clean stale-handle error rather than a use-after-free.
    if (name == "remove") {
        bar.removeAll();
        untrack(target);
        std::erase_if(owned_boss_bars_, [&](const std::unique_ptr<BossBar> &owned) { return owned.get() == &bar; });
        return ESN_OK;
    }
    return ESN_ERR_NO_SUCH_MEMBER;
}

/**
 * @brief A MapRenderer whose draw call is a JavaScript function.
 *
 * Endstone lets a plugin supply one of these - its Python bindings do exactly this through a
 * trampoline - so a JavaScript plugin can too. The renderer holds only an id; the function lives in
 * the runtime, the same way a form or a scheduled task does.
 */
class JsMapRenderer : public MapRenderer {
public:
    JsMapRenderer(ApiBridge &bridge, const std::uint32_t id) : bridge_(bridge), id_(id) {}

    void render(MapView &, MapCanvas &canvas, Player &player) override
    {
        bridge_.renderMap(id_, canvas, player);
    }

private:
    ApiBridge &bridge_;
    std::uint32_t id_;
};

esn_status ApiBridge::sendForm(const esn_handle player_handle, const std::uint32_t form_id,
                              const std::string_view spec)
{
    auto *player = static_cast<Player *>(resolve(player_handle, Kind::Player));
    if (!player) {
        return find(player_handle) ? ESN_ERR_WRONG_TYPE : ESN_ERR_STALE_HANDLE;
    }
    if (!form_sink_) {
        return ESN_ERR_NOT_INITIALIZED;
    }

    const auto records = splitOn(spec, '\x1e');
    if (records.empty()) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    const auto header = splitOn(records[0], '\x1f');
    const auto kind = fieldAt(header, 0);
    const auto title = fieldAt(header, 1);
    const auto content = fieldAt(header, 2);

    // Both callbacks forward to JavaScript by form id; the sink is what reaches the host.
    const auto sink = form_sink_;
    auto on_close = [sink, form_id](Player *) { sink(form_id, true, std::string{}); };

    if (kind == "message") {
        MessageForm form;
        form.setTitle(Message{title}).setContent(Message{content});
        form.setButton1(Message{fieldAt(header, 3)});
        form.setButton2(Message{fieldAt(header, 4)});
        form.setOnSubmit([sink, form_id](Player *, const int selection) {
            sink(form_id, false, std::to_string(selection));
        });
        form.setOnClose(on_close);
        player->sendForm(form);
        return ESN_OK;
    }

    if (kind == "action") {
        ActionForm form;
        form.setTitle(Message{title}).setContent(Message{content});
        for (std::size_t i = 1; i < records.size(); ++i) {
            const auto control = splitOn(records[i], '\x1f');
            const auto type = fieldAt(control, 0);
            if (type == "button") {
                const auto icon = fieldAt(control, 2);
                form.addButton(Message{fieldAt(control, 1)},
                               icon.empty() ? std::nullopt : std::optional<std::string>{icon});
            }
            else if (type == "header") {
                form.addHeader(Message{fieldAt(control, 1)});
            }
            else if (type == "label") {
                form.addLabel(Message{fieldAt(control, 1)});
            }
            else if (type == "divider") {
                form.addDivider();
            }
        }
        form.setOnSubmit([sink, form_id](Player *, const int selection) {
            sink(form_id, false, std::to_string(selection));
        });
        form.setOnClose(on_close);
        player->sendForm(form);
        return ESN_OK;
    }

    if (kind == "modal") {
        ModalForm form;
        form.setTitle(Message{title});
        if (const auto submit = fieldAt(header, 3); !submit.empty()) {
            form.setSubmitButton(Message{submit});
        }
        for (std::size_t i = 1; i < records.size(); ++i) {
            const auto control = splitOn(records[i], '\x1f');
            const auto type = fieldAt(control, 0);
            const auto label = Message{fieldAt(control, 1)};
            if (type == "toggle") {
                form.addControl(Toggle{label, fieldAt(control, 2) == "1"});
            }
            else if (type == "slider") {
                const auto min = floatAt(control, 2, 0.0F);
                const auto max = floatAt(control, 3, 1.0F);
                const auto step = floatAt(control, 4, 1.0F);
                const auto value = fieldAt(control, 5);
                form.addControl(Slider{label, min, max, step,
                                       value.empty() ? std::nullopt
                                                     : std::optional<float>{floatAt(control, 5, min)}});
            }
            else if (type == "dropdown") {
                form.addControl(Dropdown{label, tailFrom(control, 3), optionalIntAt(control, 2)});
            }
            else if (type == "stepslider") {
                form.addControl(StepSlider{label, tailFrom(control, 3), optionalIntAt(control, 2)});
            }
            else if (type == "textinput") {
                const auto initial = fieldAt(control, 3);
                form.addControl(TextInput{label, Message{fieldAt(control, 2)},
                                          initial.empty() ? std::nullopt
                                                          : std::optional<std::string>{initial}});
            }
            else if (type == "header") {
                form.addControl(Header{label});
            }
            else if (type == "label") {
                form.addControl(Label{label});
            }
            else if (type == "divider") {
                form.addControl(Divider{});
            }
        }
        form.setOnSubmit([sink, form_id](Player *, std::string response) {
            sink(form_id, false, std::move(response));
        });
        form.setOnClose(on_close);
        player->sendForm(form);
        return ESN_OK;
    }

    return ESN_ERR_BAD_ARGUMENT;
}

void ApiBridge::setRenderSink(RenderSink sink)
{
    render_sink_ = std::move(sink);
}

esn_status ApiBridge::addMapRenderer(const esn_handle map_handle, const std::uint32_t renderer)
{
    auto *map = static_cast<MapView *>(resolve(map_handle, Kind::MapView));
    if (!map) {
        return find(map_handle) ? ESN_ERR_WRONG_TYPE : ESN_ERR_STALE_HANDLE;
    }
    if (!render_sink_) {
        return ESN_ERR_NOT_INITIALIZED;
    }
    auto owned = std::make_shared<JsMapRenderer>(*this, renderer);
    owned_map_renderers_.push_back(owned);
    map->addRenderer(std::move(owned));
    return ESN_OK;
}

void ApiBridge::renderMap(const std::uint32_t renderer, MapCanvas &canvas, Player &player)
{
    if (!render_sink_) {
        return;
    }
    // A draw is its own scope: the canvas is only valid for this call, exactly like an event's handles.
    const auto mark = beginScope();
    const auto canvas_handle = track(&canvas, Kind::MapCanvas);
    const auto player_handle = track(&player, Kind::Player);
    try {
        render_sink_(renderer, canvas_handle, player_handle);
    }
    catch (...) {
        endScope(mark);
        throw;
    }
    endScope(mark);
}

esn_status ApiBridge::closeForm(const esn_handle player_handle)
{
    auto *player = static_cast<Player *>(resolve(player_handle, Kind::Player));
    if (!player) {
        return find(player_handle) ? ESN_ERR_WRONG_TYPE : ESN_ERR_STALE_HANDLE;
    }
    player->closeForm();
    return ESN_OK;
}

void ApiBridge::setFormSink(FormSink sink)
{
    form_sink_ = std::move(sink);
}

esn_status ApiBridge::sendPacket(const esn_handle player_handle, const int packet_id,
                                const std::string_view payload)
{
    auto *player = static_cast<Player *>(resolve(player_handle, Kind::Player));
    if (!player) {
        return find(player_handle) ? ESN_ERR_WRONG_TYPE : ESN_ERR_STALE_HANDLE;
    }
    player->sendPacket(packet_id, payload);
    return ESN_OK;
}

void ApiBridge::updateCommands()
{
    for (auto *player : plugin_.getServer().getOnlinePlayers()) {
        if (player) {
            player->updateCommands();
        }
    }
}

esn_handle ApiBridge::trackSender(CommandSender &sender)
{
    // A player gets the full Player surface; the console and anything else gets the sender surface.
    if (auto *player = sender.asPlayer()) {
        return track(player, Kind::Player);
    }
    return track(&sender, Kind::CommandSender);
}

void ApiBridge::shutdown()
{
    subscriptions_.clear();
    registrations_.clear();
    handles_.clear();
    persistent_handles_.clear();
    event_sink_ = nullptr;
    // Cancel before dropping the sink: a task that fired afterwards would call into a host that is
    // being torn down.
    for (auto &[id, task] : tasks_) {
        if (task) {
            task->cancel();
        }
    }
    tasks_.clear();
    task_sink_ = nullptr;
    render_sink_ = nullptr;
    // Endstone keeps its own shared_ptr to each of these, so dropping ours only releases our claim.
    owned_map_renderers_.clear();
    // Clear the bars off every screen rather than leaving them there until the client reconnects.
    for (auto &bar : owned_boss_bars_) {
        if (bar) {
            bar->removeAll();
        }
    }
    owned_boss_bars_.clear();
    // Players holding one of these keep working: Endstone falls back to the main scoreboard once the
    // last reference goes, so releasing them here cannot leave a dangling sidebar.
    owned_scoreboards_.clear();
}

// --- descriptor tables ---------------------------------------------------------------------------

esn_handle ApiBridge::own(std::unique_ptr<Block> block)
{
    auto *raw = block.get();
    owned_blocks_.push_back(std::move(block));
    return track(raw, Kind::Block);
}

esn_handle ApiBridge::own(std::unique_ptr<BlockData> data)
{
    auto *raw = data.get();
    owned_block_data_.push_back(std::move(data));
    return track(raw, Kind::BlockData);
}

esn_handle ApiBridge::own(std::unique_ptr<BlockState> state)
{
    auto *raw = state.get();
    owned_block_states_.push_back(std::move(state));
    return track(raw, Kind::BlockState);
}

esn_handle ApiBridge::own(const Location &location)
{
    // A deque, so taking a handle to one entry survives the next push within the same dispatch.
    owned_locations_.push_back(location);
    return track(&owned_locations_.back(), Kind::Location);
}

esn_handle ApiBridge::own(const Vector &vector)
{
    owned_vectors_.push_back(vector);
    return track(&owned_vectors_.back(), Kind::Vector);
}

esn_handle ApiBridge::ownItem(ItemStack item, std::function<void(const ItemStack &)> writeback)
{
    return trackOwnedItem(std::move(item), std::move(writeback));
}

Server &ApiBridge::server()
{
    return plugin_.getServer();
}

Plugin &ApiBridge::owner()
{
    return plugin_;
}

std::optional<esn_status> ApiBridge::registryGet(const esn_handle target, const std::string_view name,
                                                 const ValueKind want, Value &out)
{
    const auto *entry = find(target);
    if (entry == nullptr) {
        return std::nullopt;
    }
    // Copied out before the thunk runs: minting a handle can rehash the table and leave `entry`
    // dangling, and a binding that returns a handle does exactly that.
    auto *const self = entry->ptr;
    const auto kind = entry->kind;

    // An event says what it is rather than being resolvable to a kind, so its lookup starts from the
    // name it reports. Everything else is keyed by kind.
    if (kind == Kind::Event) {
        const auto event_name = static_cast<Event *>(self)->getEventName();
        if (const auto found = findEventMember(event_name, name, self)) {
            const auto *member = found.member;
            return member->get && member->kind == want ? std::optional{member->get(found.self, *this, out)}
                                                       : std::nullopt;
        }
        if (const auto found = findEventDynamic(event_name, name, self, want)) {
            return found.dynamic->get(found.self, found.suffix, *this, out);
        }
        return std::nullopt;
    }
    if (const auto found = findMember(kind, name, self)) {
        // A member declared on the string accessor does not answer a request for an int. Falling
        // through rather than erroring lets the runtime probe the accessors to find the right one.
        const auto *member = found.member;
        return member->get && member->kind == want ? std::optional{member->get(found.self, *this, out)}
                                                   : std::nullopt;
    }
    if (const auto found = findDynamic(kind, name, self, want)) {
        return found.dynamic->get(found.self, found.suffix, *this, out);
    }
    return std::nullopt;
}

std::optional<esn_status> ApiBridge::registrySet(const esn_handle target, const std::string_view name,
                                                 const ValueKind want, const Value &in)
{
    const auto *entry = find(target);
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto found = entry->kind == Kind::Event
                           ? findEventMember(static_cast<Event *>(entry->ptr)->getEventName(), name, entry->ptr)
                           : findMember(entry->kind, name, entry->ptr);
    if (!found || !found.member->set || found.member->kind != want) {
        return std::nullopt;
    }
    const auto status = found.member->set(found.self, *this, in);
    if (status == ESN_OK) {
        // Item stacks are values, so a write to one is lost unless it is put back where it came from.
        persistItem(target);
    }
    return status;
}

std::optional<esn_status> ApiBridge::registryCall(const esn_handle target, const std::string_view name,
                                                  const Args &args, esn_handle *out_handle)
{
    const auto *entry = find(target);
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto found = entry->kind == Kind::Event
                           ? findEventMember(static_cast<Event *>(entry->ptr)->getEventName(), name, entry->ptr)
                           : findMember(entry->kind, name, entry->ptr);
    if (!found || !found.member->call) {
        return std::nullopt;
    }
    const auto status = found.member->call(found.self, args, out_handle);
    if (status == ESN_OK) {
        persistItem(target);
    }
    return status;
}

// --- property dispatch ---------------------------------------------------------------------------
//
// String comparison per access is deliberate: it keeps the ABI fixed and makes adding a property a
// one-line change. If a hot path ever shows up in a profile, give that specific property its own
// entry point rather than redesigning this.

namespace {

// Player derives from Mob derives from Actor, so the shared members live in helpers that each kind's
// dispatch falls through to. Adding a property to Actor therefore reaches Player for free.

/** The remainder of `name` after `prefix`, or nullopt when it does not start with it. */
std::optional<std::string> suffixOf(const std::string_view name, const std::string_view prefix)
{
    if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix) {
        return std::string{name.substr(prefix.size())};
    }
    return std::nullopt;
}

/**
 * @brief Parses the 36-character form UUID::str() produces. Endstone offers no parser of its own.
 *
 * nullopt on anything that is not exactly 32 hex digits with the dashes ignored, so a player name that
 * happens to look hex-ish cannot be mistaken for an id.
 */
std::optional<UUID> uuidFromString(const std::string_view text)
{
    const auto nibble = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    UUID out;
    std::size_t digits = 0;
    for (const auto c : text) {
        if (c == '-') {
            continue;
        }
        const auto value = nibble(c);
        if (value < 0 || digits >= 32) {
            return std::nullopt;
        }
        if (digits % 2 == 0) {
            out.data[digits / 2] = static_cast<std::uint8_t>(value << 4);
        }
        else {
            out.data[digits / 2] |= static_cast<std::uint8_t>(value);
        }
        ++digits;
    }
    return digits == 32 ? std::optional{out} : std::nullopt;
}

/** The handle a "name:<handle>" accessor carries, or 0 when it is not a number. */
esn_handle parseHandleSuffix(const std::string_view digits)
{
    esn_handle value = 0;
    for (const auto c : digits) {
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10 + static_cast<esn_handle>(c - '0');
    }
    return digits.empty() ? 0 : value;
}

}  // namespace

std::string_view permissionLevelName(const PermissionLevel level)
{
    switch (level) {
    case PermissionLevel::Default: return "default";
    case PermissionLevel::Operator: return "operator";
    case PermissionLevel::Console: return "console";
    }
    return "default";
}

esn_status ApiBridge::getBool(const esn_handle target, const std::string_view name, int *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    {
        Value value;
        if (const auto handled = registryGet(target, name, ValueKind::Bool, value)) {
            if (*handled == ESN_OK) {
                *out = value.boolean;
            }
            return *handled;
        }
    }
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        if (name == "onlineMode") { *out = server->getOnlineMode(); return ESN_OK; }
        if (name == "isPrimaryThread") { *out = server->isPrimaryThread(); return ESN_OK; }
        if (const auto query = suffixOf(name, "isBanned:")) {
            *out = server->getBanList().isBanned(*query, std::nullopt, std::nullopt);
            return ESN_OK;
        }
        if (const auto query = suffixOf(name, "isIpBanned:")) {
            *out = server->getIpBanList().isBanned(*query);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item));
        item && name == "unlimitedLifetime") {
        *out = item->isUnlimitedLifetime();
        return ESN_OK;
    }
    if (auto *source = static_cast<DamageSource *>(resolve(target, Kind::DamageSource))) {
        // True when the responsible actor is not the one that struck, e.g. a shooter and their arrow.
        if (name == "isIndirect") { *out = source->isIndirect(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "isLocked") { *out = map->isLocked(); return ESN_OK; }
        // True when a plugin supplied the lowermost renderer, i.e. the map is not a world map.
        if (name == "isVirtual") { *out = map->isVirtual(); return ESN_OK; }
        if (name == "unlimitedTracking") { *out = map->isUnlimitedTracking(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "visible") { *out = bar->isVisible(); return ESN_OK; }
        if (const auto flag = barFlagFromName(name)) { *out = bar->hasFlag(*flag); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getInt(const esn_handle target, const std::string_view name, std::int64_t *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    {
        Value value;
        if (const auto handled = registryGet(target, name, ValueKind::Int, value)) {
            if (*handled == ESN_OK) {
                *out = value.integer;
            }
            return *handled;
        }
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item))) {
        if (name == "pickupDelay") { *out = item->getPickupDelay(); return ESN_OK; }
        // Absent rather than null when nothing threw it, so `if (item.thrower)` reads naturally.
        if (name == "thrower") {
            const auto thrower = item->getThrower();
            if (!thrower) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            *out = *thrower;
            return ESN_OK;
        }
    }
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        if (name == "port") { *out = server->getPort(); return ESN_OK; }
        if (name == "portV6") { *out = server->getPortV6(); return ESN_OK; }
        if (name == "protocolVersion") { *out = server->getProtocolVersion(); return ESN_OK; }
        if (name == "onlinePlayerCount") {
            *out = static_cast<std::int64_t>(server->getOnlinePlayers().size());
            return ESN_OK;
        }
        if (name == "maxPlayers") { *out = server->getMaxPlayers(); return ESN_OK; }
        // Registry lookups keyed by id, so a plugin can ask about a type it does not hold an instance
        // of - what a level cap or a durability bar needs before the item exists. -1 means unknown id,
        // which is not the same answer as 0.
        if (const auto id = suffixOf(name, "enchantMaxLevel:")) {
            const auto *enchantment = server->getRegistry<Enchantment>().get(EnchantmentId{*id});
            *out = enchantment ? enchantment->getMaxLevel() : -1;
            return ESN_OK;
        }
        if (const auto id = suffixOf(name, "enchantStartLevel:")) {
            const auto *enchantment = server->getRegistry<Enchantment>().get(EnchantmentId{*id});
            *out = enchantment ? enchantment->getStartLevel() : -1;
            return ESN_OK;
        }
        if (const auto id = suffixOf(name, "itemMaxDurability:")) {
            const auto *type = server->getRegistry<ItemType>().get(ItemTypeId{*id});
            *out = type ? type->getMaxDurability() : -1;
            return ESN_OK;
        }
        if (const auto id = suffixOf(name, "itemMaxStackSize:")) {
            const auto *type = server->getRegistry<ItemType>().get(ItemTypeId{*id});
            *out = type ? type->getMaxStackSize() : -1;
            return ESN_OK;
        }
        // Epoch milliseconds, which is what `new Date(n)` takes.
        if (name == "startTime") {
            *out = std::chrono::duration_cast<std::chrono::milliseconds>(
                       server->getStartTime().time_since_epoch())
                       .count();
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *dimension = static_cast<Dimension *>(resolve(target, Kind::Dimension))) {
        if (name == "actorCount") {
            *out = static_cast<std::int64_t>(dimension->getActors().size());
            return ESN_OK;
        }
        // highestBlockYAt:<x>,<z> - just the height, without allocating a Block for it.
        if (const auto request = suffixOf(name, "highestBlockYAt:")) {
            const auto comma = request->find(',');
            if (comma == std::string::npos) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            *out = dimension->getHighestBlockYAt(std::stoi(request->substr(0, comma)),
                                                 std::stoi(request->substr(comma + 1)));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        if (name == "time") { *out = level->getTime(); return ESN_OK; }
        if (name == "actorCount") { *out = static_cast<std::int64_t>(level->getActors().size()); return ESN_OK; }
        if (name == "dimensionCount") { *out = static_cast<std::int64_t>(level->getDimensions().size()); return ESN_OK; }
        if (name == "seed") { *out = level->getSeed(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "id") { *out = map->getId(); return ESN_OK; }
        if (name == "centerX") { *out = map->getCenterX(); return ESN_OK; }
        if (name == "centerZ") { *out = map->getCenterZ(); return ESN_OK; }
        if (name == "scale") { *out = static_cast<std::int64_t>(map->getScale()); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "playerCount") { *out = static_cast<std::int64_t>(bar->getPlayers().size()); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getDouble(const esn_handle target, const std::string_view name, double *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    {
        Value value;
        if (const auto handled = registryGet(target, name, ValueKind::Double, value)) {
            if (*handled == ESN_OK) {
                *out = value.real;
            }
            return *handled;
        }
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "progress") { *out = bar->getProgress(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        // Performance counters. "current" is the last few seconds; "average" is since start-up.
        if (name == "currentTicksPerSecond") { *out = server->getCurrentTicksPerSecond(); return ESN_OK; }
        if (name == "averageTicksPerSecond") { *out = server->getAverageTicksPerSecond(); return ESN_OK; }
        if (name == "currentMillisecondsPerTick") { *out = server->getCurrentMillisecondsPerTick(); return ESN_OK; }
        if (name == "averageMillisecondsPerTick") { *out = server->getAverageMillisecondsPerTick(); return ESN_OK; }
        if (name == "currentTickUsage") { *out = server->getCurrentTickUsage(); return ESN_OK; }
        if (name == "averageTickUsage") { *out = server->getAverageTickUsage(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getString(const esn_handle target, const std::string_view name, char *buf, const std::size_t cap,
                                std::size_t *needed)
{
    {
        Value value;
        if (const auto handled = registryGet(target, name, ValueKind::String, value)) {
            return *handled == ESN_OK ? emitString(value.text, buf, cap, needed) : *handled;
        }
    }
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        if (name == "name") { return emitString(server->getName(), buf, cap, needed); }
        if (name == "version") { return emitString(server->getVersion(), buf, cap, needed); }
        if (name == "minecraftVersion") { return emitString(server->getMinecraftVersion(), buf, cap, needed); }
        if (name == "locale") { return emitString(server->getLanguage().getLocale(), buf, cap, needed); }
        // blockStates: one "key\x1ftype\x1fvalue" per line - the map's values are a variant, so the type
        // has to travel with them or the runtime could not tell "true" the string from true the boolean.
        if (const auto request = suffixOf(name, "blockStates:")) {
            const auto data = server->createBlockData(*request);
            return emitString(data ? blockStatesRecord(*data) : std::string{}, buf, cap, needed);
        }
        if (const auto request = suffixOf(name, "blockRuntimeId:")) {
            // Emitted as text because get_int is tried after get_string and this shares the prefix path.
            const auto data = server->createBlockData(*request);
            return emitString(data ? std::to_string(data->getRuntimeId()) : std::string{}, buf, cap, needed);
        }
        // translate:<locale>\x1f<text>[\x1f<param>...] - the whole request rides the accessor name
        // because a method call cannot return a string. An empty locale means the server's own.
        if (const auto request = suffixOf(name, "translate:")) {
            std::vector<std::string> parts;
            for (std::size_t start = 0;;) {
                const auto end = request->find(kUnitSeparator, start);
                parts.push_back(request->substr(start, end == std::string::npos ? end : end - start));
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            if (parts.size() < 2) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            const auto &locale = parts[0];
            const auto &text = parts[1];
            const std::vector<std::string> params{parts.begin() + 2, parts.end()};
            auto &language = server->getLanguage();
            const auto translated = locale.empty() ? language.translate(text, params)
                                                   : language.translate(text, params, locale);
            return emitString(translated, buf, cap, needed);
        }
        // Ban entries: one 0x1f-delimited record, or empty when the target is not banned.
        if (const auto query = suffixOf(name, "ban:")) {
            const auto entry = server->getBanList().getBanEntry(*query, std::nullopt, std::nullopt);
            return emitString(entry ? banRecord(*entry.get()) : std::string{}, buf, cap, needed);
        }
        if (const auto query = suffixOf(name, "ipBan:")) {
            const auto entry = server->getIpBanList().getBanEntry(*query);
            return emitString(entry ? banRecord(*entry.get()) : std::string{}, buf, cap, needed);
        }
        // Whole lists: one record per line, so a plugin can show them without a call per entry.
        if (name == "banList" || name == "ipBanList") {
            std::string joined;
            const auto append = [&](const std::string &record) {
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += record;
            };
            if (name == "banList") {
                for (const auto &entry : server->getBanList().getEntries()) {
                    append(banRecord(*entry));
                }
            }
            else {
                for (const auto &entry : server->getIpBanList().getEntries()) {
                    append(banRecord(*entry));
                }
            }
            return emitString(joined, buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *board = static_cast<Scoreboard *>(resolve(target, Kind::Scoreboard))) {
        // "name\x1fdisplayName\x1fmodifiable" per objective. Keyed by name everywhere else, so this is
        // the one way to discover what a scoreboard already has.
        if (name == "objectiveList") {
            std::string joined;
            for (const auto &objective : board->getObjectives()) {
                if (!objective) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '\n';
                }
                const auto slot = objective->getDisplaySlot();
                const auto order = objective->getSortOrder();
                joined += objective->getName() + kUnitSeparator + objective->getDisplayName() +
                          kUnitSeparator + (objective->isModifiable() ? "1" : "0") + kUnitSeparator +
                          std::string{slot ? displaySlotName(*slot) : std::string_view{}} + kUnitSeparator +
                          (order ? (*order == ObjectiveSortOrder::Ascending ? "ascending" : "descending") : "");
            }
            return emitString(joined, buf, cap, needed);
        }
        if (name == "entryList") {
            std::string joined;
            for (const auto &entry : board->getEntries()) {
                if (!joined.empty()) {
                    joined += '\n';
                }
                // A ScoreEntry is a player, an actor or a plain name; only the name form is exposed.
                if (const auto *by_name = std::get_if<std::string>(&entry)) {
                    joined += *by_name;
                }
            }
            return emitString(joined, buf, cap, needed);
        }
        // scores:<entry> - "objective\x1fvalue" per line, i.e. every score that entry holds.
        if (const auto request = suffixOf(name, "scores:")) {
            std::string joined;
            for (const auto &score : board->getScores(ScoreEntry{*request})) {
                if (!score || !score->isScoreSet()) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += score->getObjective().getName() + kUnitSeparator +
                          std::to_string(score->getValue());
            }
            return emitString(joined, buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "title") { return emitString(bar->getTitle(), buf, cap, needed); }
        if (name == "color") { return emitString(std::string{barColorName(bar->getColor())}, buf, cap, needed); }
        if (name == "style") { return emitString(std::string{barStyleName(bar->getStyle())}, buf, cap, needed); }
        // Viewers cross as names, one per line: handing out Player handles would tie the bar's lifetime
        // to a dispatch scope, and a name is what a plugin wants for a lookup anyway.
        if (name == "playerNameList") {
            std::string joined;
            for (const auto *viewer : bar->getPlayers()) {
                if (!viewer) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += viewer->getName();
            }
            return emitString(joined, buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *dimension = static_cast<Dimension *>(resolve(target, Kind::Dimension))) {
        if (name == "name") { return emitString(dimension->getName(), buf, cap, needed); }
        // The enum rather than the name, so a plugin does not have to string-match a display name.
        if (name == "type") {
            switch (dimension->getType()) {
            case Dimension::Type::Overworld: return emitString("overworld", buf, cap, needed);
            case Dimension::Type::Nether: return emitString("nether", buf, cap, needed);
            case Dimension::Type::TheEnd: return emitString("theEnd", buf, cap, needed);
            default: return emitString("custom", buf, cap, needed);
            }
        }
        // "x,z" per line: a list cannot cross as an array, and the runtime turns this into objects.
        if (name == "loadedChunkList") {
            std::string joined;
            for (const auto &chunk : dimension->getLoadedChunks()) {
                if (!chunk) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += std::to_string(chunk->getX()) + "," + std::to_string(chunk->getZ());
            }
            return emitString(joined, buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        if (name == "name") { return emitString(level->getName(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *plugin = static_cast<Plugin *>(resolve(target, Kind::Plugin))) {
        const auto &description = plugin->getDescription();
        if (name == "name") { return emitString(description.getName(), buf, cap, needed); }
        if (name == "version") { return emitString(description.getVersion(), buf, cap, needed); }
        if (name == "fullName") { return emitString(description.getFullName(), buf, cap, needed); }
        if (name == "description") { return emitString(description.getDescription(), buf, cap, needed); }
        if (name == "website") { return emitString(description.getWebsite(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *source = static_cast<DamageSource *>(resolve(target, Kind::DamageSource))) {
        // The cause, e.g. "entity_attack", "fall", "lava".
        if (name == "type") { return emitString(source->getType(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

// Packet payloads are bytes, not text, so they get their own accessor rather than riding getString.
// Handing them through the text path meant the host built the JavaScript string with
// napi_create_string_utf8, and any byte >= 0x80 that did not happen to form valid UTF-8 was replaced
// with U+FFFD - so a payload was silently corrupted on the way out while sendPacket, which reads
// latin1, was correct on the way in.

esn_status ApiBridge::getBytes(const esn_handle target, const std::string_view name, char *buf,
                               const std::size_t cap, std::size_t *needed)
{
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setBytes(const esn_handle target, const std::string_view name, const std::string_view value)
{
    if (auto *canvas = static_cast<MapCanvas *>(resolve(target, Kind::MapCanvas))) {
        // A whole frame in one crossing. 128x128 RGBA, row-major - setting pixels one at a time would
        // cross the ABI 16384 times for every draw, for every viewer.
        if (name == "pixels") {
            constexpr int kMapSize = 128;
            const auto expected = static_cast<std::size_t>(kMapSize) * kMapSize * 4;
            if (value.size() < expected) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            const auto *bytes = reinterpret_cast<const unsigned char *>(value.data());
            for (int y = 0; y < kMapSize; ++y) {
                for (int x = 0; x < kMapSize; ++x) {
                    const auto at = (static_cast<std::size_t>(y) * kMapSize + x) * 4;
                    canvas->setPixelColor(x, y, Color{bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]});
                }
            }
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getHandle(const esn_handle target, const std::string_view name, esn_handle *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    {
        Value value;
        if (const auto handled = registryGet(target, name, ValueKind::Handle, value)) {
            if (*handled == ESN_OK) {
                *out = value.handle;
            }
            return *handled;
        }
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "dimension") {
            auto *dimension = map->getDimension();
            *out = dimension ? track(dimension, Kind::Dimension, true) : 0;
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *source = static_cast<DamageSource *>(resolve(target, Kind::DamageSource))) {
        // getActor() is who is responsible; getDamagingActor() is what actually struck, e.g. an arrow.
        if (name == "actor") { *out = trackActor(source->getActor()); return ESN_OK; }
        if (name == "damagingActor") { *out = trackActor(source->getDamagingActor()); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        // The main scoreboard lives as long as the server, so the handle is persistent.
        if (name == "scoreboard") {
            auto *board = server->getScoreboard();
            *out = board ? track(board, Kind::Scoreboard, true) : 0;
            return ESN_OK;
        }
        if (name == "level") {
            auto *level = server->getLevel();
            *out = level ? track(level, Kind::Level, true) : 0;
            return ESN_OK;
        }
        // player:<name or uuid> - the only way to reach a player outside an event. Dispatch-scoped, as
        // any player handle must be: the object is only valid while they are online.
        if (const auto query = suffixOf(name, "player:")) {
            const auto uuid = uuidFromString(*query);
            auto *found = uuid ? server->getPlayer(*uuid) : server->getPlayer(*query);
            *out = found ? track(found, Kind::Player) : 0;
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item)); item && name == "itemStack") {
        // getItemStack hands back a copy, so it is paired with a writeback like an inventory slot.
        *out = trackOwnedItem(item->getItemStack(),
                              [item](const endstone::ItemStack &changed) { item->setItemStack(changed); });
        return ESN_OK;
    }
    if (auto *dimension = static_cast<Dimension *>(resolve(target, Kind::Dimension))) {
        if (name == "level") { *out = track(&dimension->getLevel(), Kind::Level, true); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setBool(const esn_handle target, const std::string_view name, const bool value)
{
    {
        Value incoming;
        incoming.kind = ValueKind::Bool;
        incoming.boolean = value;
        if (const auto handled = registrySet(target, name, ValueKind::Bool, incoming)) {
            return *handled;
        }
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "isLocked") { map->setLocked(value); return ESN_OK; }
        if (name == "unlimitedTracking") { map->setUnlimitedTracking(value); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "visible") { bar->setVisible(value); return ESN_OK; }
        if (const auto flag = barFlagFromName(name)) {
            if (value) {
                bar->addFlag(*flag);
            }
            else {
                bar->removeFlag(*flag);
            }
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item)); item && name == "unlimitedLifetime") {
        item->setUnlimitedLifetime(value);
        return ESN_OK;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setInt(const esn_handle target, const std::string_view name, const std::int64_t value)
{
    {
        Value incoming;
        incoming.kind = ValueKind::Int;
        incoming.integer = value;
        if (const auto handled = registrySet(target, name, ValueKind::Int, incoming)) {
            return *handled;
        }
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "centerX") { map->setCenterX(static_cast<int>(value)); return ESN_OK; }
        if (name == "centerZ") { map->setCenterZ(static_cast<int>(value)); return ESN_OK; }
        // 0 (closest) to 4 (furthest); anything else would be an out-of-range enum value.
        if (name == "scale") {
            if (value < 0 || value > 4) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            map->setScale(static_cast<MapView::Scale>(value));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item))) {
        if (name == "pickupDelay") { item->setPickupDelay(static_cast<int>(value)); return ESN_OK; }
        if (name == "thrower") { item->setThrower(value); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
        if (name == "heldItemSlot") { player_inventory->setHeldItemSlot(static_cast<int>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        if (name == "maxPlayers") { server->setMaxPlayers(static_cast<int>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        if (name == "time") { level->setTime(static_cast<int>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setDouble(const esn_handle target, const std::string_view name, const double value)
{
    {
        Value incoming;
        incoming.kind = ValueKind::Double;
        incoming.real = value;
        if (const auto handled = registrySet(target, name, ValueKind::Double, incoming)) {
            return *handled;
        }
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        // Endstone clamps nothing, and a value outside 0..1 makes the client draw a bar wider than its
        // frame, so it is clamped here rather than in JavaScript where a plugin could skip it.
        if (name == "progress") {
            bar->setProgress(static_cast<float>(value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setString(const esn_handle target, const std::string_view name, const std::string_view value)
{
    {
        Value incoming;
        incoming.kind = ValueKind::String;
        incoming.text = std::string{value};
        if (const auto handled = registrySet(target, name, ValueKind::String, incoming)) {
            return *handled;
        }
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "title") { bar->setTitle(std::string{value}); return ESN_OK; }
        if (name == "color") {
            const auto color = barColorFromName(value);
            if (!color) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            bar->setColor(*color);
            return ESN_OK;
        }
        if (name == "style") {
            const auto style = barStyleFromName(value);
            if (!style) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            bar->setStyle(*style);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::invoke(const esn_handle target, const std::string_view name,
                             const char *const *strings, const std::size_t string_count, const double *numbers,
                             const std::size_t number_count, const esn_handle *handles,
                             const std::size_t handle_count, esn_handle *out_handle)
{
    const auto number = [&](const std::size_t index, const double fallback = 0.0) {
        return index < number_count ? numbers[index] : fallback;
    };
    const auto str = [&](const std::size_t index) -> std::string {
        return index < string_count && strings[index] ? std::string{strings[index]} : std::string{};
    };
    // An object argument, e.g. bar.addPlayer(player) or stack.isSimilar(other). 0 when absent, which
    // every caller has to treat as a bad argument rather than as a null object.
    const auto handle_at = [&](const std::size_t index) -> esn_handle {
        return index < handle_count && handles ? handles[index] : 0;
    };
    const auto text = str(0);

    {
        const Args args{*this,      target,       strings,      string_count, numbers,
                        number_count, handles,    handle_count};
        if (const auto handled = registryCall(target, name, args, out_handle)) {
            return *handled;
        }
    }

    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        // Runs a command as the console, which is how a plugin drives vanilla commands.
        if (name == "dispatchCommand") {
            (void)server->dispatchCommand(server->getCommandSender(), str(0));
            return ESN_OK;
        }
        // createBossBar(title, color, style). The bar is owned here and its handle is persistent, so
        // JavaScript can keep it and drive it from a timer; bar.remove() is what frees it.
        if (name == "reloadData") { server->reloadData(); return ESN_OK; }
        // The only sanctioned way for a plugin to stop the server: process.exit is neutralised.
        if (name == "shutdown") { server->shutdown(); return ESN_OK; }
        if (name == "reload") { server->reload(); return ESN_OK; }
        // getMap(id) - the counterpart to createMap, so an id kept across a restart is usable again.
        if (name == "getMap" && out_handle) {
            auto *map = server->getMap(static_cast<std::int64_t>(number(0)));
            *out_handle = map ? track(map, Kind::MapView, true) : 0;
            return ESN_OK;
        }
        // createMap(dimension) - the map is owned by the server, so its handle is persistent.
        if (name == "createMap") {
            auto *level = server->getLevel();
            if (!level) {
                return ESN_ERR_NOT_INITIALIZED;
            }
            const auto requested = str(0);
            auto *dimension = level->getDimension(requested.empty() ? "overworld" : requested);
            if (!dimension) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            auto &map = server->createMap(*dimension);
            if (out_handle) {
                *out_handle = track(&map, Kind::MapView, true);
            }
            return ESN_OK;
        }
        // A fresh scoreboard, for a sidebar only some players see. Owned here; see owned_scoreboards_.
        if (name == "createScoreboard") {
            auto board = server->createScoreboard();
            if (!board) {
                return ESN_ERR_INTERNAL;
            }
            auto *raw = board.get();
            owned_scoreboards_.push_back(std::move(board));
            if (out_handle) {
                *out_handle = track(raw, Kind::Scoreboard, true);
            }
            return ESN_OK;
        }
        // getOnlinePlayer(index) - the counterpart to onlinePlayerCount, so JavaScript can walk the
        // player list. Indexed rather than returning an array because an accessor carries one value;
        // the runtime turns the pair back into server.onlinePlayers. Same shape as Dimension.getActor.
        if (name == "getOnlinePlayer" && out_handle) {
            const auto players = server->getOnlinePlayers();
            const auto index = static_cast<std::size_t>(number(0));
            *out_handle = index < players.size() && players[index] ? track(players[index], Kind::Player) : 0;
            return ESN_OK;
        }
        // broadcast(message, permission) - only players holding the permission see it.
        if (name == "broadcast") {
            server->broadcast(Message{str(0)}, str(1));
            return ESN_OK;
        }
        // banPlayer(target, reason, source, durationSeconds) / banIp(...). A duration of 0 is permanent.
        if (name == "banPlayer" || name == "banIp") {
            const auto target_name = str(0);
            if (target_name.empty()) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            const auto reason = str(1);
            const auto source = str(2);
            const auto seconds = static_cast<std::int64_t>(number(0, 0));
            const auto optional_of = [](const std::string &value) {
                return value.empty() ? std::nullopt : std::optional{value};
            };
            if (name == "banIp") {
                auto &list = server->getIpBanList();
                if (seconds > 0) {
                    (void)list.addBan(target_name, optional_of(reason), std::chrono::seconds{seconds},
                                      optional_of(source));
                }
                else {
                    (void)list.addBan(target_name, optional_of(reason), std::nullopt, optional_of(source));
                }
                return ESN_OK;
            }
            auto &list = server->getBanList();
            if (seconds > 0) {
                (void)list.addBan(target_name, std::nullopt, std::nullopt, optional_of(reason),
                                  std::chrono::seconds{seconds}, optional_of(source));
            }
            else {
                (void)list.addBan(target_name, std::nullopt, std::nullopt, optional_of(reason), std::nullopt,
                                  optional_of(source));
            }
            return ESN_OK;
        }
        if (name == "unbanPlayer") {
            server->getBanList().removeBan(str(0), std::nullopt, std::nullopt);
            return ESN_OK;
        }
        if (name == "unbanIp") {
            server->getIpBanList().removeBan(str(0));
            return ESN_OK;
        }
        if (name == "createBossBar") {
            const auto color = barColorFromName(str(1));
            const auto style = barStyleFromName(str(2));
            if (!color || !style) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            auto bar = server->createBossBar(str(0), *color, *style);
            if (!bar) {
                return ESN_ERR_INTERNAL;
            }
            auto *raw = bar.get();
            owned_boss_bars_.push_back(std::move(bar));
            if (out_handle) {
                *out_handle = track(raw, Kind::BossBar, true);
            }
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        return bossBarInvoke(target, *bar, name, str, handle_at);
    }
    if (auto *board = static_cast<Scoreboard *>(resolve(target, Kind::Scoreboard))) {
        return scoreboardInvoke(*board, name, str, number, number_count, out_handle);
    }
    if (auto *dimension = static_cast<Dimension *>(resolve(target, Kind::Dimension))) {
        const auto keepBlock = [&](std::unique_ptr<Block> block) -> esn_status {
            if (!block || !out_handle) {
                if (out_handle) { *out_handle = 0; }
                return ESN_OK;
            }
            auto *raw = block.get();
            owned_blocks_.push_back(std::move(block));
            *out_handle = track(raw, Kind::Block);
            return ESN_OK;
        };
        // getBlockAt(location) - the vector is flattened to x, y, z by the runtime.
        if (name == "getBlockAt") {
            return keepBlock(dimension->getBlockAt(static_cast<int>(number(0)), static_cast<int>(number(1)),
                                                   static_cast<int>(number(2))));
        }
        // The topmost non-air block at a column, and just its height.
        if (name == "getHighestBlockAt") {
            return keepBlock(dimension->getHighestBlockAt(static_cast<int>(number(0)), static_cast<int>(number(1))));
        }
        if (name == "spawnActor" && out_handle) {
            Location where{*dimension, static_cast<float>(number(0)), static_cast<float>(number(1)),
                           static_cast<float>(number(2))};
            auto *spawned = dimension->spawnActor(where, std::string{text});
            *out_handle = spawned ? trackActor(spawned) : 0;
            return ESN_OK;
        }
        // dropItem(item, location): a loose item stack in the world.
        if (name == "dropItem") {
            const auto type = str(0);
            if (type.empty()) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            endstone::ItemStack item{type, static_cast<int>(number(3, 1)), static_cast<int>(number(4, 0))};
            Location where{*dimension, static_cast<float>(number(0)), static_cast<float>(number(1)),
                           static_cast<float>(number(2))};
            auto &dropped = dimension->dropItem(where, item);
            if (out_handle) {
                *out_handle = trackActor(&dropped);
            }
            return ESN_OK;
        }
        if (name == "getActor" && out_handle) {
            const auto actors = dimension->getActors();
            const auto index = static_cast<std::size_t>(number(0));
            *out_handle = index < actors.size() && actors[index] ? trackActor(actors[index]) : 0;
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        // Dimensions live as long as the level, so the handle is persistent like the level's own.
        if (name == "getDimension" && out_handle) {
            auto *dimension = level->getDimension(std::string{text});
            *out_handle = dimension ? track(dimension, Kind::Dimension, true) : 0;
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *canvas = static_cast<MapCanvas *>(resolve(target, Kind::MapCanvas))) {
        // setPixel(x, y, r, g, b, a) - for a renderer that touches a handful of pixels. Anything
        // drawing a whole frame should assign canvas.pixels instead.
        if (name == "setPixel") {
            canvas->setPixelColor(static_cast<int>(number(0)), static_cast<int>(number(1)),
                                  Color{static_cast<std::uint8_t>(number(2)), static_cast<std::uint8_t>(number(3)),
                                        static_cast<std::uint8_t>(number(4)),
                                        static_cast<std::uint8_t>(number(5, 255))});
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

namespace {

constexpr char kLineFeed = static_cast<char>(10);

char kindLetter(const ValueKind kind)
{
    switch (kind) {
    case ValueKind::Bool: return 'b';
    case ValueKind::Int: return 'i';
    case ValueKind::Double: return 'd';
    case ValueKind::String: return 's';
    case ValueKind::Handle: return 'h';
    case ValueKind::Bytes: return 'y';
    case ValueKind::None: break;
    }
    return '-';
}

}  // namespace

esn_status ApiBridge::describe(char *buf, const std::size_t cap, std::size_t *needed)
{
    std::string out;
    for (const auto *desc : allTypes()) {
        const auto base = desc->base_event.empty() && desc->base != Kind::None
                              ? std::string{findType(desc->base) ? findType(desc->base)->name : std::string{}}
                              : desc->base_event;
        out += "T";
        out += kUnitSeparator;
        out += desc->name;
        out += kUnitSeparator;
        out += base;
        out += kLineFeed;
        for (const auto &[name, member] : desc->members) {
            std::string flags;
            if (member.get) {
                flags += 'r';
            }
            if (member.set) {
                flags += 'w';
            }
            if (member.call) {
                flags += 'c';
            }
            out += "M";
            out += kUnitSeparator;
            out += name;
            out += kUnitSeparator;
            out += kindLetter(member.kind);
            out += kUnitSeparator;
            out += flags;
            out += kLineFeed;
        }
        for (const auto &entry : desc->dynamic) {
            out += "D";
            out += kUnitSeparator;
            out += entry.prefix;
            out += kUnitSeparator;
            out += kindLetter(entry.kind);
            out += kLineFeed;
        }
    }
    return emitString(out, buf, cap, needed);
}

esn_status ApiBridge::typeName(const esn_handle target, char *buf, const std::size_t cap, std::size_t *needed)
{
    // A migrated type already carries its name in its descriptor, so there is nothing to keep in step.
    if (const auto *entry = find(target)) {
        if (entry->kind == Kind::Event) {
            return emitString(static_cast<Event *>(entry->ptr)->getEventName(), buf, cap, needed);
        }
        if (const auto *desc = findType(entry->kind)) {
            return emitString(desc->name, buf, cap, needed);
        }
    }
    if (resolve(target, Kind::Player)) {
        return emitString("Player", buf, cap, needed);
    }
    if (resolve(target, Kind::Mob)) {
        return emitString("Mob", buf, cap, needed);
    }
    if (resolve(target, Kind::Item)) {
        return emitString("Item", buf, cap, needed);
    }
    if (resolve(target, Kind::Actor)) {
        return emitString("Actor", buf, cap, needed);
    }
    if (resolve(target, Kind::Block)) {
        return emitString("Block", buf, cap, needed);
    }
    if (resolve(target, Kind::Level)) {
        return emitString("Level", buf, cap, needed);
    }
    if (resolve(target, Kind::DamageSource)) {
        return emitString("DamageSource", buf, cap, needed);
    }
    if (resolve(target, Kind::ItemStack)) {
        return emitString("ItemStack", buf, cap, needed);
    }
    if (resolve(target, Kind::BossBar)) {
        return emitString("BossBar", buf, cap, needed);
    }
    if (resolve(target, Kind::BlockData)) {
        return emitString("BlockData", buf, cap, needed);
    }
    if (resolve(target, Kind::BlockState)) {
        return emitString("BlockState", buf, cap, needed);
    }
    if (resolve(target, Kind::Location)) {
        return emitString("Location", buf, cap, needed);
    }
    if (resolve(target, Kind::Vector)) {
        return emitString("Vector", buf, cap, needed);
    }
    if (resolve(target, Kind::CommandSender)) {
        return emitString("CommandSender", buf, cap, needed);
    }
    if (resolve(target, Kind::PlayerInventory)) {
        return emitString("PlayerInventory", buf, cap, needed);
    }
    if (resolve(target, Kind::Inventory)) {
        return emitString("Inventory", buf, cap, needed);
    }
    if (resolve(target, Kind::Plugin)) {
        return emitString("Plugin", buf, cap, needed);
    }
    if (resolve(target, Kind::MapView)) {
        return emitString("MapView", buf, cap, needed);
    }
    if (resolve(target, Kind::MapCanvas)) {
        return emitString("MapCanvas", buf, cap, needed);
    }
    if (resolve(target, Kind::Server)) {
        return emitString("Server", buf, cap, needed);
    }
    if (resolve(target, Kind::Dimension)) {
        return emitString("Dimension", buf, cap, needed);
    }
    if (resolve(target, Kind::Scoreboard)) {
        return emitString("Scoreboard", buf, cap, needed);
    }
    return ESN_ERR_STALE_HANDLE;
}

// --- ABI trampolines -----------------------------------------------------------------------------

namespace {

ApiBridge &bridge(void *context)
{
    return *static_cast<ApiBridge *>(context);
}

// Every trampoline swallows exceptions: the return path runs through libnode and V8 frames compiled
// without exception support.
#define ESN_GUARD(expr)                                                                                                \
    try {                                                                                                              \
        return (expr);                                                                                                 \
    }                                                                                                                  \
    catch (...) {                                                                                                       \
        return ESN_ERR_INTERNAL;                                                                                        \
    }

esn_status ESN_CALL tGetBool(void *c, esn_handle t, const char *n, int *o)
{
    ESN_GUARD(bridge(c).getBool(t, n ? n : "", o))
}
esn_status ESN_CALL tGetInt(void *c, esn_handle t, const char *n, std::int64_t *o)
{
    ESN_GUARD(bridge(c).getInt(t, n ? n : "", o))
}
esn_status ESN_CALL tGetDouble(void *c, esn_handle t, const char *n, double *o)
{
    ESN_GUARD(bridge(c).getDouble(t, n ? n : "", o))
}
esn_status ESN_CALL tGetString(void *c, esn_handle t, const char *n, char *b, std::size_t cap, std::size_t *need)
{
    ESN_GUARD(bridge(c).getString(t, n ? n : "", b, cap, need))
}
esn_status ESN_CALL tGetHandle(void *c, esn_handle t, const char *n, esn_handle *o)
{
    ESN_GUARD(bridge(c).getHandle(t, n ? n : "", o))
}
esn_status ESN_CALL tGetBytes(void *c, esn_handle t, const char *n, char *b, std::size_t cap, std::size_t *need)
{
    ESN_GUARD(bridge(c).getBytes(t, n ? n : "", b, cap, need))
}
esn_status ESN_CALL tSetBytes(void *c, esn_handle t, const char *n, const char *v, std::size_t len)
{
    ESN_GUARD(bridge(c).setBytes(t, n ? n : "", std::string_view(v ? v : "", v ? len : 0)))
}
esn_status ESN_CALL tSetBool(void *c, esn_handle t, const char *n, int v)
{
    ESN_GUARD(bridge(c).setBool(t, n ? n : "", v != 0))
}
esn_status ESN_CALL tSetInt(void *c, esn_handle t, const char *n, std::int64_t v)
{
    ESN_GUARD(bridge(c).setInt(t, n ? n : "", v))
}
esn_status ESN_CALL tSetDouble(void *c, esn_handle t, const char *n, double v)
{
    ESN_GUARD(bridge(c).setDouble(t, n ? n : "", v))
}
esn_status ESN_CALL tSetString(void *c, esn_handle t, const char *n, const char *v, std::size_t len)
{
    ESN_GUARD(bridge(c).setString(t, n ? n : "", std::string_view(v ? v : "", v ? len : 0)))
}
esn_status ESN_CALL tInvoke(void *c, esn_handle t, const char *n, const char *const *strs, std::size_t str_count,
                            const double *nums, std::size_t num_count, const esn_handle *handles,
                            std::size_t handle_count, esn_handle *out)
{
    ESN_GUARD(bridge(c).invoke(t, n ? n : "", strs, str_count, nums, num_count, handles, handle_count, out))
}
esn_status ESN_CALL tTypeName(void *c, esn_handle t, char *b, std::size_t cap, std::size_t *need)
{
    ESN_GUARD(bridge(c).typeName(t, b, cap, need))
}
esn_status ESN_CALL tDescribe(void *c, char *b, std::size_t cap, std::size_t *need)
{
    ESN_GUARD(bridge(c).describe(b, cap, need))
}
esn_status ESN_CALL tSubscribe(void *c, const char *name, int priority, int ignore_cancelled, std::uint32_t *out)
{
    ESN_GUARD(bridge(c).subscribe(name ? name : "", priority, ignore_cancelled != 0, out))
}
esn_status ESN_CALL tUnsubscribe(void *c, std::uint32_t subscription)
{
    ESN_GUARD(bridge(c).unsubscribe(subscription))
}

#undef ESN_GUARD

// The server accessors return sizes or void rather than a status, so they guard by hand.
std::size_t ESN_CALL tServerName(void *c, char *b, std::size_t cap)
{
    try {
        return bridge(c).serverName(b, cap);
    }
    catch (...) {
        return 0;
    }
}
std::size_t ESN_CALL tServerVersion(void *c, char *b, std::size_t cap)
{
    try {
        return bridge(c).serverVersion(b, cap);
    }
    catch (...) {
        return 0;
    }
}
std::size_t ESN_CALL tServerMinecraftVersion(void *c, char *b, std::size_t cap)
{
    try {
        return bridge(c).serverMinecraftVersion(b, cap);
    }
    catch (...) {
        return 0;
    }
}
int ESN_CALL tServerProtocolVersion(void *c)
{
    try {
        return bridge(c).serverProtocolVersion();
    }
    catch (...) {
        return -1;
    }
}
int ESN_CALL tServerOnlinePlayerCount(void *c)
{
    try {
        return bridge(c).serverOnlinePlayerCount();
    }
    catch (...) {
        return -1;
    }
}
esn_status ESN_CALL tServerSelf(void *c, esn_handle *out)
{
    try {
        return bridge(c).serverSelf(out);
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}
esn_status ESN_CALL tServerLevel(void *c, esn_handle *out)
{
    try {
        return bridge(c).serverLevel(out);
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}
void ESN_CALL tBroadcastMessage(void *c, const char *m, std::size_t len)
{
    try {
        bridge(c).broadcastMessage(std::string_view(m ? m : "", m ? len : 0));
    }
    catch (...) {
    }
}
esn_status ESN_CALL tSendForm(void *c, esn_handle player, std::uint32_t form_id, const char *spec, std::size_t len)
{
    try {
        return bridge(c).sendForm(player, form_id, std::string_view(spec ? spec : "", spec ? len : 0));
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}
void ESN_CALL tCloseForm(void *c, esn_handle player)
{
    try {
        (void)bridge(c).closeForm(player);
    }
    catch (...) {
    }
}
esn_status ESN_CALL tSendPacket(void *c, esn_handle player, int packet_id, const char *payload, std::size_t len)
{
    try {
        return bridge(c).sendPacket(player, packet_id, std::string_view(payload ? payload : "", payload ? len : 0));
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}
void ESN_CALL tUpdateCommands(void *c)
{
    try {
        bridge(c).updateCommands();
    }
    catch (...) {
    }
}
esn_status ESN_CALL tScheduleTask(void *c, std::uint32_t delay, std::uint32_t period, std::uint32_t *out)
{
    try {
        return bridge(c).scheduleTask(delay, period, out);
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}
esn_status ESN_CALL tAddMapRenderer(void *c, esn_handle map, std::uint32_t renderer)
{
    try {
        return bridge(c).addMapRenderer(map, renderer);
    }
    catch (...) {
        return ESN_ERR_INTERNAL;
    }
}
void ESN_CALL tCancelTask(void *c, std::uint32_t task)
{
    try {
        bridge(c).cancelTask(task);
    }
    catch (...) {
    }
}
void ESN_CALL tLog(void *c, int level, const char *m, std::size_t len)
{
    try {
        bridge(c).log(level, std::string_view(m ? m : "", m ? len : 0));
    }
    catch (...) {
    }
}

}  // namespace

void ApiBridge::fill(esn_endstone_api &api)
{
    api.abi_version = ESN_ABI_VERSION;
    api.context = this;
    api.server_name = tServerName;
    api.server_version = tServerVersion;
    api.server_minecraft_version = tServerMinecraftVersion;
    api.server_protocol_version = tServerProtocolVersion;
    api.server_online_player_count = tServerOnlinePlayerCount;
    api.server_level = tServerLevel;
    api.server_self = tServerSelf;
    api.broadcast_message = tBroadcastMessage;
    api.log = tLog;
    api.accessors.get_bool = tGetBool;
    api.accessors.get_int = tGetInt;
    api.accessors.get_double = tGetDouble;
    api.accessors.get_string = tGetString;
    api.accessors.get_handle = tGetHandle;
    api.accessors.get_bytes = tGetBytes;
    api.accessors.set_bool = tSetBool;
    api.accessors.set_int = tSetInt;
    api.accessors.set_double = tSetDouble;
    api.accessors.set_string = tSetString;
    api.accessors.set_bytes = tSetBytes;
    api.accessors.invoke = tInvoke;
    api.accessors.type_name = tTypeName;
    api.accessors.describe = tDescribe;
    api.subscribe = tSubscribe;
    api.unsubscribe = tUnsubscribe;
    api.send_form = tSendForm;
    api.close_form = tCloseForm;
    api.send_packet = tSendPacket;
    api.update_commands = tUpdateCommands;
    api.schedule_task = tScheduleTask;
    api.cancel_task = tCancelTask;
    api.add_map_renderer = tAddMapRenderer;
}

}  // namespace endstone::node
