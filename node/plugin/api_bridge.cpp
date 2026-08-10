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

#include <cstring>
#include <optional>
#include <utility>

#include <endstone/actor/actor.h>
#include <endstone/actor/mob.h>
#include <endstone/block/block.h>
#include <endstone/command/command_sender.h>
#include <endstone/command/console_command_sender.h>
#include <endstone/damage/damage_source.h>
#include <endstone/permissions/permission_level.h>
#include <endstone/game_mode.h>
#include <endstone/inventory/inventory.h>
#include <endstone/inventory/item_stack.h>
#include <endstone/inventory/player_inventory.h>
#include <endstone/map/map_view.h>
#include <endstone/util/socket_address.h>
#include <endstone/event/actor/actor_damage_event.h>
#include <endstone/event/actor/actor_death_event.h>
#include <endstone/event/actor/player_death_event.h>
#include <endstone/event/server/map_initialize_event.h>
#include <endstone/event/server/packet_receive_event.h>
#include <endstone/event/server/packet_send_event.h>
#include <endstone/event/server/plugin_disable_event.h>
#include <endstone/event/server/plugin_enable_event.h>
#include <endstone/event/actor/actor_explode_event.h>
#include <endstone/event/actor/actor_knockback_event.h>
#include <endstone/event/actor/actor_remove_event.h>
#include <endstone/event/actor/actor_spawn_event.h>
#include <endstone/event/actor/actor_teleport_event.h>
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
#include <endstone/event/server/script_message_event.h>
#include <endstone/event/server/server_command_event.h>
#include <endstone/event/server/server_list_ping_event.h>
#include <endstone/event/weather/thunder_change_event.h>
#include <endstone/event/weather/weather_change_event.h>
#include <endstone/level/dimension.h>
#include <endstone/level/level.h>
#include <endstone/level/location.h>
#include <endstone/player.h>
#include <endstone/plugin/plugin_manager.h>
#include <endstone/server.h>
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

std::string messageToString(const std::optional<Message> &message)
{
    if (!message.has_value()) {
        return {};
    }
    // Message is a variant of a plain string and a translatable; only the former has a literal form.
    if (const auto *text = std::get_if<std::string>(&message.value())) {
        return *text;
    }
    return {};
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

/**
 * One row per Endstone event. Data rather than code, so adding an event is a line here and a line in
 * the host's JavaScript, and the same static_cast-by-name discipline is applied uniformly.
 *
 * Subscription does not need this table - any event name reaches Endstone's PluginManager - so an event
 * missing from here can still be listened to; it just has no `player`, `actor`, `block` or `cancelled`.
 */
struct EventTrait {
    std::string_view name;
    Player *(*player)(Event *){nullptr};
    Actor *(*actor)(Event *){nullptr};
    Mob *(*mob)(Event *){nullptr};
    Block *(*block)(Event *){nullptr};
    ICancellable *(*cancellable)(Event *){nullptr};
};

#define ESN_PLAYER_EVENT(E)   {#E, playerOf<E>, nullptr, nullptr, nullptr, nullptr}
#define ESN_PLAYER_EVENT_C(E) {#E, playerOf<E>, nullptr, nullptr, nullptr, cancellableOf<E>}
#define ESN_ACTOR_EVENT(E)    {#E, nullptr, actorOf<E>, nullptr, nullptr, nullptr}
#define ESN_MOB_EVENT(E)      {#E, nullptr, nullptr, mobOf<E>, nullptr, nullptr}
#define ESN_ACTOR_EVENT_C(E)  {#E, nullptr, actorOf<E>, nullptr, nullptr, cancellableOf<E>}
#define ESN_MOB_EVENT_C(E)    {#E, nullptr, nullptr, mobOf<E>, nullptr, cancellableOf<E>}
#define ESN_BLOCK_EVENT_C(E)  {#E, nullptr, nullptr, nullptr, blockOf<E>, cancellableOf<E>}
// Block events that also name the player responsible.
#define ESN_BLOCK_PLAYER_EVENT_C(E) {#E, playerOf<E>, nullptr, nullptr, blockOf<E>, cancellableOf<E>}
// A player death is an ActorEvent<Mob> that also knows it is a player, so both accessors work.
#define ESN_PLAYER_MOB_EVENT(E) {#E, playerOf<E>, nullptr, mobOf<E>, nullptr, nullptr}
#define ESN_PLAIN_EVENT_C(E)  {#E, nullptr, nullptr, nullptr, nullptr, cancellableOf<E>}

const EventTrait kEventTraits[] = {
    // Player
    ESN_PLAYER_EVENT(PlayerJoinEvent),
    ESN_PLAYER_EVENT(PlayerQuitEvent),
    ESN_PLAYER_EVENT(PlayerBedLeaveEvent),
    ESN_PLAYER_EVENT(PlayerDimensionChangeEvent),
    ESN_PLAYER_EVENT(PlayerRespawnEvent),
    ESN_PLAYER_EVENT_C(PlayerChatEvent),
    ESN_PLAYER_EVENT_C(PlayerCommandEvent),
    ESN_PLAYER_EVENT_C(PlayerBedEnterEvent),
    ESN_PLAYER_EVENT_C(PlayerDropItemEvent),
    ESN_PLAYER_EVENT_C(PlayerEmoteEvent),
    ESN_PLAYER_EVENT_C(PlayerGameModeChangeEvent),
    ESN_PLAYER_EVENT_C(PlayerInteractEvent),
    ESN_PLAYER_EVENT_C(PlayerInteractActorEvent),
    ESN_PLAYER_EVENT_C(PlayerItemConsumeEvent),
    ESN_PLAYER_EVENT_C(PlayerItemHeldEvent),
    ESN_PLAYER_EVENT_C(PlayerKickEvent),
    ESN_PLAYER_EVENT_C(PlayerLoginEvent),
    ESN_PLAYER_EVENT_C(PlayerMoveEvent),
    ESN_PLAYER_EVENT_C(PlayerJumpEvent),
    ESN_PLAYER_EVENT_C(PlayerTeleportEvent),
    ESN_PLAYER_EVENT_C(PlayerPortalEvent),
    ESN_PLAYER_EVENT_C(PlayerPickupItemEvent),
    ESN_PLAYER_EVENT_C(PlayerSkinChangeEvent),
    // Actor
    ESN_MOB_EVENT(ActorDeathEvent),
    ESN_PLAYER_MOB_EVENT(PlayerDeathEvent),
    ESN_ACTOR_EVENT(ActorRemoveEvent),
    ESN_MOB_EVENT_C(ActorDamageEvent),
    ESN_ACTOR_EVENT_C(ActorExplodeEvent),
    ESN_MOB_EVENT_C(ActorKnockbackEvent),
    ESN_ACTOR_EVENT_C(ActorSpawnEvent),
    ESN_ACTOR_EVENT_C(ActorTeleportEvent),
    // Block
    ESN_BLOCK_PLAYER_EVENT_C(BlockBreakEvent),
    ESN_BLOCK_PLAYER_EVENT_C(BlockPlaceEvent),
    ESN_BLOCK_EVENT_C(BlockCookEvent),
    ESN_BLOCK_EVENT_C(BlockExplodeEvent),
    ESN_BLOCK_EVENT_C(BlockFromToEvent),
    ESN_BLOCK_EVENT_C(BlockGrowEvent),
    ESN_BLOCK_EVENT_C(BlockFormEvent),
    ESN_BLOCK_EVENT_C(BlockPistonExtendEvent),
    ESN_BLOCK_EVENT_C(BlockPistonRetractEvent),
    ESN_BLOCK_EVENT_C(LeavesDecayEvent),
    // Server and world
    ESN_PLAIN_EVENT_C(BroadcastMessageEvent),
    ESN_PLAIN_EVENT_C(ServerCommandEvent),
    ESN_PLAIN_EVENT_C(ServerListPingEvent),
    ESN_PLAIN_EVENT_C(ScriptMessageEvent),
    ESN_PLAIN_EVENT_C(ThunderChangeEvent),
    ESN_PLAIN_EVENT_C(WeatherChangeEvent),
};

#undef ESN_PLAYER_EVENT
#undef ESN_PLAYER_EVENT_C
#undef ESN_ACTOR_EVENT
#undef ESN_MOB_EVENT
#undef ESN_MOB_EVENT_C
#undef ESN_ACTOR_EVENT_C
#undef ESN_BLOCK_EVENT_C
#undef ESN_BLOCK_PLAYER_EVENT_C
#undef ESN_PLAIN_EVENT_C

const EventTrait *traitFor(Event *event)
{
    const auto name = event->getEventName();
    for (const auto &trait : kEventTraits) {
        if (trait.name == name) {
            return &trait;
        }
    }
    return nullptr;
}

Player *eventPlayer(Event *event)
{
    const auto *trait = traitFor(event);
    return trait && trait->player ? trait->player(event) : nullptr;
}

/** Returns the actor plus whether it is known to be living, so the caller tracks the right kind. */
Actor *eventActor(Event *event, bool *is_mob)
{
    const auto *trait = traitFor(event);
    if (!trait) {
        return nullptr;
    }
    if (trait->mob) {
        *is_mob = true;
        return trait->mob(event);
    }
    *is_mob = false;
    return trait->actor ? trait->actor(event) : nullptr;
}

Block *eventBlock(Event *event)
{
    const auto *trait = traitFor(event);
    return trait && trait->block ? trait->block(event) : nullptr;
}

ICancellable *eventCancellable(Event *event)
{
    const auto *trait = traitFor(event);
    return trait && trait->cancellable ? trait->cancellable(event) : nullptr;
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

std::optional<GameMode> gameModeFromName(std::string_view name)
{
    for (const auto &[value, candidate] : kGameModes) {
        if (candidate == name) {
            return value;
        }
    }
    return std::nullopt;
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
    const auto handle = next_handle_++;
    handles_[handle] = Entry{ptr, kind, persistent};
    return handle;
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
    // Mob derives from Actor, so either kind answers an Actor question.
    if (auto *mob = resolve(handle, Kind::Mob)) {
        return static_cast<Mob *>(mob);
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

esn_status ApiBridge::subscribe(const std::string_view event_name, const int priority, const bool ignore_cancelled,
                                std::uint32_t *out)
{
    if (event_name.empty() || !out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    const auto subscription = next_subscription_++;
    subscriptions_.emplace(subscription, std::string{event_name});

    // Endstone refuses to register a listener while the owning plugin is not enabled, and every
    // JavaScript subscription is registered in this plugin's name. Plugins subscribe during load -
    // before this plugin is enabled - so those are queued and registered on enable. The subscription
    // id is minted here either way, so JavaScript never has to know which path was taken.
    if (plugin_.isEnabled()) {
        registerWithEndstone(subscription, event_name, priority, ignore_cancelled);
    }
    else {
        pending_.push_back({subscription, std::string{event_name}, priority, ignore_cancelled});
    }

    *out = subscription;
    return ESN_OK;
}

void ApiBridge::registerWithEndstone(const std::uint32_t subscription, const std::string_view event_name,
                                     const int priority, const bool ignore_cancelled)
{
    plugin_.getServer().getPluginManager().registerEvent(
        std::string{event_name},
        [this, subscription](Event &event) {
            if (subscriptions_.contains(subscription)) {
                dispatch(subscription, event);
            }
        },
        toPriority(priority), plugin_, ignore_cancelled);
}

void ApiBridge::flushPendingSubscriptions()
{
    const auto pending = std::move(pending_);
    pending_.clear();
    for (const auto &entry : pending) {
        // Anything unsubscribed before the flush is skipped rather than registered and dropped.
        if (subscriptions_.contains(entry.subscription)) {
            registerWithEndstone(entry.subscription, entry.event_name, entry.priority, entry.ignore_cancelled);
        }
    }
}

esn_status ApiBridge::unsubscribe(const std::uint32_t subscription)
{
    // Endstone has no per-handler unregister, so the handler stays wired and becomes a no-op. Cheap,
    // and it keeps unsubscribe honest from JavaScript's point of view.
    return subscriptions_.erase(subscription) > 0 ? ESN_OK : ESN_ERR_BAD_ARGUMENT;
}

void ApiBridge::dispatch(const std::uint32_t subscription, Event &event)
{
    if (!event_sink_) {
        return;
    }
    // Everything minted from here on belongs to this dispatch only.
    const auto scope_start = next_handle_;
    const auto handle = track(&event, Kind::Event);

    event_sink_(subscription, handle);

    releaseScope(scope_start);
}

void ApiBridge::releaseScope(const esn_handle scope_start)
{
    for (auto it = handles_.begin(); it != handles_.end();) {
        const bool expired = it->first >= scope_start && !it->second.persistent;
        it = expired ? handles_.erase(it) : std::next(it);
    }
    // Anything the bridge had to own for the duration goes with them.
    owned_blocks_.clear();
    owned_locations_.clear();
    owned_vectors_.clear();
    owned_items_.clear();
}

Inventory *ApiBridge::resolveInventory(const esn_handle target)
{
    if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
        return player_inventory;
    }
    return static_cast<Inventory *>(resolve(target, Kind::Inventory));
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
    command_scope_start_ = next_handle_;
    // A player gets the full Player surface; the console and anything else gets the sender surface.
    if (auto *player = sender.asPlayer()) {
        return track(player, Kind::Player);
    }
    return track(&sender, Kind::CommandSender);
}

void ApiBridge::releaseDispatch()
{
    releaseScope(command_scope_start_);
}

void ApiBridge::shutdown()
{
    subscriptions_.clear();
    handles_.clear();
    event_sink_ = nullptr;
}

// --- property dispatch ---------------------------------------------------------------------------
//
// String comparison per access is deliberate: it keeps the ABI fixed and makes adding a property a
// one-line change. If a hot path ever shows up in a profile, give that specific property its own
// entry point rather than redesigning this.

namespace {

// Player derives from Mob derives from Actor, so the shared members live in helpers that each kind's
// dispatch falls through to. Adding a property to Actor therefore reaches Player for free.

esn_status actorGetBool(Actor &actor, const std::string_view name, int *out)
{
    if (name == "isOnGround") { *out = actor.isOnGround(); return ESN_OK; }
    if (name == "isInWater") { *out = actor.isInWater(); return ESN_OK; }
    if (name == "isInLava") { *out = actor.isInLava(); return ESN_OK; }
    if (name == "isDead") { *out = actor.isDead(); return ESN_OK; }
    if (name == "isValid") { *out = actor.isValid(); return ESN_OK; }
    if (name == "isNameTagVisible") { *out = actor.isNameTagVisible(); return ESN_OK; }
    if (name == "isNameTagAlwaysVisible") { *out = actor.isNameTagAlwaysVisible(); return ESN_OK; }
    return ESN_ERR_NO_SUCH_MEMBER;
}

esn_status actorGetInt(Actor &actor, const std::string_view name, std::int64_t *out)
{
    (void)actor;
    (void)name;
    (void)out;
    return ESN_ERR_NO_SUCH_MEMBER;
}

esn_status actorSetBool(Actor &actor, const std::string_view name, const bool value)
{
    if (name == "isNameTagVisible") { actor.setNameTagVisible(value); return ESN_OK; }
    if (name == "isNameTagAlwaysVisible") { actor.setNameTagAlwaysVisible(value); return ESN_OK; }
    return ESN_ERR_NO_SUCH_MEMBER;
}

esn_status actorSetInt(Actor &actor, const std::string_view name, const std::int64_t value)
{
    (void)actor;
    (void)name;
    (void)value;
    return ESN_ERR_NO_SUCH_MEMBER;
}

}  // namespace

esn_status ApiBridge::getBool(const esn_handle target, const std::string_view name, int *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "isOp") { *out = player->isOp(); return ESN_OK; }
        if (name == "isSneaking") { *out = player->isSneaking(); return ESN_OK; }
        if (name == "isSprinting") { *out = player->isSprinting(); return ESN_OK; }
        if (name == "isFlying") { *out = player->isFlying(); return ESN_OK; }
        if (name == "allowFlight") { *out = player->getAllowFlight(); return ESN_OK; }
        if (name == "isGliding") { *out = player->isGliding(); return ESN_OK; }
        return actorGetBool(*player, name, out);
    }
    if (auto *actor = resolveActor(target)) {
        return actorGetBool(*actor, name, out);
    }
    if (auto *source = static_cast<DamageSource *>(resolve(target, Kind::DamageSource))) {
        // True when the responsible actor is not the one that struck, e.g. a shooter and their arrow.
        if (name == "isIndirect") { *out = source->isIndirect(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *inventory = resolveInventory(target)) {
        if (name == "isEmpty") { *out = inventory->isEmpty(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "isLocked") { *out = map->isLocked(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "hasItemMeta") { *out = stack->hasItemMeta(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *sender = static_cast<CommandSender *>(resolve(target, Kind::CommandSender))) {
        // There is no isOp on CommandSender; the console reports Operator or Console here.
        if (name == "isOp") {
            *out = sender->getPermissionLevel() != PermissionLevel::Default;
            return ESN_OK;
        }
        if (name == "isConsole") { *out = sender->asConsole() != nullptr; return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        // Event::isCancellable() is private to EventHandler; the ICancellable cast is the public test.
        if (name == "isCancellable") { *out = eventCancellable(event) != nullptr; return ESN_OK; }
        if (name == "isAsynchronous") { *out = event->isAsynchronous(); return ESN_OK; }
        if (name == "cancelled") {
            const auto *cancellable = eventCancellable(event);
            if (!cancellable) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            *out = cancellable->isCancelled();
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getInt(const esn_handle target, const std::string_view name, std::int64_t *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        // Player derives from Mob, so these need no cast - and therefore no RTTI.
        if (name == "health") { *out = player->getHealth(); return ESN_OK; }
        if (name == "maxHealth") { *out = player->getMaxHealth(); return ESN_OK; }
        if (name == "ping") { *out = player->getPing().count(); return ESN_OK; }
        if (name == "expLevel") { *out = player->getExpLevel(); return ESN_OK; }
        if (name == "totalExp") { *out = player->getTotalExp(); return ESN_OK; }
        return actorGetInt(*player, name, out);
    }
    if (auto *mob = resolveMob(target)) {
        if (name == "health") { *out = mob->getHealth(); return ESN_OK; }
        if (name == "maxHealth") { *out = mob->getMaxHealth(); return ESN_OK; }
    }
    if (auto *actor = resolveActor(target)) {
        return actorGetInt(*actor, name, out);
    }
    if (auto *location = static_cast<Location *>(resolve(target, Kind::Location))) {
        if (name == "blockX") { *out = location->getBlockX(); return ESN_OK; }
        if (name == "blockY") { *out = location->getBlockY(); return ESN_OK; }
        if (name == "blockZ") { *out = location->getBlockZ(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *vector = static_cast<Vector *>(resolve(target, Kind::Vector))) {
        if (name == "blockX") { *out = vector->getBlockX(); return ESN_OK; }
        if (name == "blockY") { *out = vector->getBlockY(); return ESN_OK; }
        if (name == "blockZ") { *out = vector->getBlockZ(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        if (name == "time") { *out = level->getTime(); return ESN_OK; }
        if (name == "actorCount") { *out = static_cast<std::int64_t>(level->getActors().size()); return ESN_OK; }
        if (name == "dimensionCount") { *out = static_cast<std::int64_t>(level->getDimensions().size()); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "amount") { *out = stack->getAmount(); return ESN_OK; }
        if (name == "data") { *out = stack->getData(); return ESN_OK; }
        if (name == "maxStackSize") { *out = stack->getMaxStackSize(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *inventory = resolveInventory(target)) {
        if (name == "size") { *out = inventory->getSize(); return ESN_OK; }
        if (name == "maxStackSize") { *out = inventory->getMaxStackSize(); return ESN_OK; }
        if (name == "firstEmpty") { *out = inventory->firstEmpty(); return ESN_OK; }
        if (name == "heldItemSlot") {
            if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
                *out = player_inventory->getHeldItemSlot();
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "id") { *out = map->getId(); return ESN_OK; }
        if (name == "centerX") { *out = map->getCenterX(); return ESN_OK; }
        if (name == "centerZ") { *out = map->getCenterZ(); return ESN_OK; }
        if (name == "scale") { *out = static_cast<std::int64_t>(map->getScale()); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "packetId" || name == "subClientId") {
            const auto event_name = event->getEventName();
            if (event_name == "PacketReceiveEvent") {
                auto *packet = static_cast<PacketReceiveEvent *>(event);
                *out = name == "packetId" ? packet->getPacketId() : packet->getSubClientId();
                return ESN_OK;
            }
            if (event_name == "PacketSendEvent") {
                auto *packet = static_cast<PacketSendEvent *>(event);
                *out = name == "packetId" ? packet->getPacketId() : packet->getSubClientId();
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "chunkX" || name == "chunkZ") {
            if (event->getEventName() == "ChunkLoadEvent" || event->getEventName() == "ChunkUnloadEvent") {
                auto &chunk = static_cast<ChunkEvent *>(event)->getChunk();
                *out = name == "chunkX" ? chunk.getX() : chunk.getZ();
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getDouble(const esn_handle target, const std::string_view name, double *out)
{
    if (!out) {
        return ESN_ERR_BAD_ARGUMENT;
    }
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "expProgress") { *out = player->getExpProgress(); return ESN_OK; }
        if (name == "flySpeed") { *out = player->getFlySpeed(); return ESN_OK; }
        if (name == "walkSpeed") { *out = player->getWalkSpeed(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *location = static_cast<Location *>(resolve(target, Kind::Location))) {
        if (name == "x") { *out = location->getX(); return ESN_OK; }
        if (name == "y") { *out = location->getY(); return ESN_OK; }
        if (name == "z") { *out = location->getZ(); return ESN_OK; }
        if (name == "pitch") { *out = location->getPitch(); return ESN_OK; }
        if (name == "yaw") { *out = location->getYaw(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *vector = static_cast<Vector *>(resolve(target, Kind::Vector))) {
        if (name == "x") { *out = vector->getX(); return ESN_OK; }
        if (name == "y") { *out = vector->getY(); return ESN_OK; }
        if (name == "z") { *out = vector->getZ(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "damage" && event->getEventName() == "ActorDamageEvent") {
            *out = static_cast<ActorDamageEvent *>(event)->getDamage();
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::getString(const esn_handle target, const std::string_view name, char *buf, const std::size_t cap,
                                std::size_t *needed)
{
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "name") { return emitString(player->getName(), buf, cap, needed); }
        if (name == "uniqueId") { return emitString(player->getUniqueId().str(), buf, cap, needed); }
        if (name == "xuid") { return emitString(player->getXuid(), buf, cap, needed); }
        if (name == "locale") { return emitString(player->getLocale(), buf, cap, needed); }
        if (name == "deviceOs") { return emitString(player->getDeviceOS(), buf, cap, needed); }
        if (name == "deviceId") { return emitString(player->getDeviceId(), buf, cap, needed); }
        if (name == "gameVersion") { return emitString(player->getGameVersion(), buf, cap, needed); }
        if (name == "address") { return emitString(player->getAddress().getHostname(), buf, cap, needed); }
        if (name == "type") { return emitString(player->getType(), buf, cap, needed); }
        if (name == "nameTag") { return emitString(player->getNameTag(), buf, cap, needed); }
        if (name == "scoreTag") { return emitString(player->getScoreTag(), buf, cap, needed); }
        if (name == "dimension") { return emitString(player->getDimension().getName(), buf, cap, needed); }
        if (name == "gameMode") { return emitString(gameModeName(player->getGameMode()), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *actor = resolveActor(target)) {
        if (name == "type") { return emitString(actor->getType(), buf, cap, needed); }
        if (name == "nameTag") { return emitString(actor->getNameTag(), buf, cap, needed); }
        if (name == "scoreTag") { return emitString(actor->getScoreTag(), buf, cap, needed); }
        if (name == "dimension") { return emitString(actor->getDimension().getName(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "type") { return emitString(block->getType(), buf, cap, needed); }
        if (name == "dimension") { return emitString(block->getDimension().getName(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *location = static_cast<Location *>(resolve(target, Kind::Location))) {
        if (name == "dimension") { return emitString(location->getDimension().getName(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        if (name == "name") { return emitString(level->getName(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *sender = static_cast<CommandSender *>(resolve(target, Kind::CommandSender))) {
        if (name == "name") { return emitString(sender->getName(), buf, cap, needed); }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "type") { return emitString(std::string{stack->getType().getId()}, buf, cap, needed); }
        if (name == "translationKey") { return emitString(stack->getTranslationKey(), buf, cap, needed); }
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
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "eventName") { return emitString(event->getEventName(), buf, cap, needed); }
        if (name == "joinMessage") {
            if (event->getEventName() == "PlayerJoinEvent") {
                auto *join = static_cast<PlayerJoinEvent *>(event);
                return emitString(messageToString(join->getJoinMessage()), buf, cap, needed);
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "quitMessage") {
            if (event->getEventName() == "PlayerQuitEvent") {
                auto *quit = static_cast<PlayerQuitEvent *>(event);
                return emitString(messageToString(quit->getQuitMessage()), buf, cap, needed);
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "message") {
            if (event->getEventName() == "PlayerChatEvent") {
                auto *chat = static_cast<PlayerChatEvent *>(event);
                return emitString(chat->getMessage(), buf, cap, needed);
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "command") {
            if (event->getEventName() == "PlayerCommandEvent") {
                auto *command = static_cast<PlayerCommandEvent *>(event);
                return emitString(command->getCommand(), buf, cap, needed);
            }
            if (event->getEventName() == "ServerCommandEvent") {
                auto *command = static_cast<ServerCommandEvent *>(event);
                return emitString(command->getCommand(), buf, cap, needed);
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "deathMessage" && event->getEventName() == "PlayerDeathEvent") {
            const auto message = static_cast<PlayerDeathEvent *>(event)->getDeathMessage();
            return emitString(message ? messageToString(*message) : std::string{}, buf, cap, needed);
        }
        // The payload is raw packet bytes, so it can contain NUL and is not text. It crosses as a
        // sized string and JavaScript should treat it as binary.
        if (name == "payload") {
            if (event->getEventName() == "PacketReceiveEvent") {
                const auto payload = static_cast<PacketReceiveEvent *>(event)->getPayload();
                return emitString(std::string{payload}, buf, cap, needed);
            }
            if (event->getEventName() == "PacketSendEvent") {
                const auto payload = static_cast<PacketSendEvent *>(event)->getPayload();
                return emitString(std::string{payload}, buf, cap, needed);
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "address") {
            if (event->getEventName() == "PacketReceiveEvent") {
                return emitString(static_cast<PacketReceiveEvent *>(event)->getAddress().getHostname(), buf, cap,
                                  needed);
            }
            if (event->getEventName() == "PacketSendEvent") {
                return emitString(static_cast<PacketSendEvent *>(event)->getAddress().getHostname(), buf, cap, needed);
            }
            return ESN_ERR_NO_SUCH_MEMBER;
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
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "player") {
            if (auto *player = eventPlayer(event)) {
                *out = track(player, Kind::Player);
                return ESN_OK;
            }
            // Packet events carry a Player* that is genuinely null before login, so they cannot go in
            // the trait table - it dereferences. A null player reads as null, not as a missing member.
            const auto event_name = event->getEventName();
            if (event_name == "PacketReceiveEvent" || event_name == "PacketSendEvent") {
                auto *player = event_name == "PacketReceiveEvent"
                                   ? static_cast<PacketReceiveEvent *>(event)->getPlayer()
                                   : static_cast<PacketSendEvent *>(event)->getPlayer();
                *out = player ? track(player, Kind::Player) : 0;
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "actor") {
            bool is_mob = false;
            if (auto *actor = eventActor(event, &is_mob)) {
                *out = track(actor, is_mob ? Kind::Mob : Kind::Actor);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "damageSource") {
            const auto event_name = event->getEventName();
            if (event_name == "ActorDamageEvent") {
                *out = track(&static_cast<ActorDamageEvent *>(event)->getDamageSource(), Kind::DamageSource);
                return ESN_OK;
            }
            // What killed them, which is the useful half of a death event.
            if (event_name == "ActorDeathEvent") {
                *out = track(&static_cast<ActorDeathEvent *>(event)->getDamageSource(), Kind::DamageSource);
                return ESN_OK;
            }
            if (event_name == "PlayerDeathEvent") {
                *out = track(&static_cast<PlayerDeathEvent *>(event)->getDamageSource(), Kind::DamageSource);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "block") {
            if (auto *block = eventBlock(event)) {
                *out = track(block, Kind::Block);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "item") {
            if (event->getEventName() == "PlayerDropItemEvent") {
                auto &drop = const_cast<ItemStack &>(static_cast<PlayerDropItemEvent *>(event)->getItem());
                *out = track(&drop, Kind::ItemStack);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "plugin") {
            const auto event_name = event->getEventName();
            if (event_name == "PluginEnableEvent") {
                *out = track(&static_cast<PluginEnableEvent *>(event)->getPlugin(), Kind::Plugin);
                return ESN_OK;
            }
            if (event_name == "PluginDisableEvent") {
                *out = track(&static_cast<PluginDisableEvent *>(event)->getPlugin(), Kind::Plugin);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "map" && event->getEventName() == "MapInitializeEvent") {
            *out = track(&static_cast<MapInitializeEvent *>(event)->getMap(), Kind::MapView);
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
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "level") { *out = track(&player->getLevel(), Kind::Level); return ESN_OK; }
        if (name == "inventory") {
            *out = track(&player->getInventory(), Kind::PlayerInventory);
            return ESN_OK;
        }
        if (name == "enderChest") {
            *out = track(&player->getEnderChest(), Kind::Inventory);
            return ESN_OK;
        }
        if (name == "location") {
            owned_locations_.push_back(player->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        if (name == "rotation") {
            owned_locations_.push_back(player->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        if (name == "velocity") {
            owned_vectors_.push_back(player->getVelocity());
            *out = track(&owned_vectors_.back(), Kind::Vector);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *actor = resolveActor(target)) {
        if (name == "level") { *out = track(&actor->getLevel(), Kind::Level); return ESN_OK; }
        if (name == "location") {
            owned_locations_.push_back(actor->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        if (name == "rotation") {
            owned_locations_.push_back(actor->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        if (name == "velocity") {
            owned_vectors_.push_back(actor->getVelocity());
            *out = track(&owned_vectors_.back(), Kind::Vector);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "location") {
            owned_locations_.push_back(block->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *inventory = resolveInventory(target)) {
        // The equipment slots, which only a player inventory has.
        auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory));
        if (player_inventory) {
            const auto equipment = [&](std::optional<endstone::ItemStack> item) -> esn_status {
                if (!item) {
                    *out = 0;  // an empty slot reads as null
                    return ESN_OK;
                }
                owned_items_.push_back(std::make_unique<endstone::ItemStack>(std::move(*item)));
                *out = track(owned_items_.back().get(), Kind::ItemStack);
                return ESN_OK;
            };
            if (name == "helmet") { return equipment(player_inventory->getHelmet()); }
            if (name == "chestplate") { return equipment(player_inventory->getChestplate()); }
            if (name == "leggings") { return equipment(player_inventory->getLeggings()); }
            if (name == "boots") { return equipment(player_inventory->getBoots()); }
            if (name == "itemInMainHand") { return equipment(player_inventory->getItemInMainHand()); }
            if (name == "itemInOffHand") { return equipment(player_inventory->getItemInOffHand()); }
        }
        (void)inventory;
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *location = static_cast<Location *>(resolve(target, Kind::Location))) {
        if (name == "block") {
            auto block = location->getBlock();
            if (!block) {
                return ESN_ERR_INTERNAL;
            }
            auto *raw = block.get();
            owned_blocks_.push_back(std::move(block));
            *out = track(raw, Kind::Block);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setBool(const esn_handle target, const std::string_view name, const bool value)
{
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "isOp") { player->setOp(value); return ESN_OK; }
        if (name == "isSneaking") { player->setSneaking(value); return ESN_OK; }
        if (name == "isSprinting") { player->setSprinting(value); return ESN_OK; }
        if (name == "isFlying") { player->setFlying(value); return ESN_OK; }
        if (name == "allowFlight") { player->setAllowFlight(value); return ESN_OK; }
        return actorSetBool(*player, name, value);
    }
    if (auto *actor = resolveActor(target)) {
        return actorSetBool(*actor, name, value);
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "cancelled") {
            auto *cancellable = eventCancellable(event);
            if (!cancellable) {
                return ESN_ERR_WRONG_TYPE;
            }
            cancellable->setCancelled(value);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setInt(const esn_handle target, const std::string_view name, const std::int64_t value)
{
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        // Writes reach the world only for a stack the server handed out live, such as the one on
        // PlayerDropItemEvent. A stack read out of an inventory is a copy - change the inventory.
        if (name == "amount") { stack->setAmount(static_cast<int>(value)); return ESN_OK; }
        if (name == "data") { stack->setData(static_cast<int>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
        if (name == "heldItemSlot") { player_inventory->setHeldItemSlot(static_cast<int>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "health") { player->setHealth(static_cast<int>(value)); return ESN_OK; }
        if (name == "maxHealth") { player->setMaxHealth(static_cast<int>(value)); return ESN_OK; }
        if (name == "expLevel") { player->setExpLevel(static_cast<int>(value)); return ESN_OK; }
        return actorSetInt(*player, name, value);
    }
    if (auto *mob = resolveMob(target)) {
        if (name == "health") { mob->setHealth(static_cast<int>(value)); return ESN_OK; }
        if (name == "maxHealth") { mob->setMaxHealth(static_cast<int>(value)); return ESN_OK; }
    }
    if (auto *actor = resolveActor(target)) {
        return actorSetInt(*actor, name, value);
    }
    if (auto *level = static_cast<Level *>(resolve(target, Kind::Level))) {
        if (name == "time") { level->setTime(static_cast<int>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setDouble(const esn_handle target, const std::string_view name, const double value)
{
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "flySpeed") { player->setFlySpeed(static_cast<float>(value)); return ESN_OK; }
        if (name == "walkSpeed") { player->setWalkSpeed(static_cast<float>(value)); return ESN_OK; }
        if (name == "expProgress") { player->setExpProgress(static_cast<float>(value)); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "damage" && event->getEventName() == "ActorDamageEvent") {
            static_cast<ActorDamageEvent *>(event)->setDamage(static_cast<float>(value));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setString(const esn_handle target, const std::string_view name, const std::string_view value)
{
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "type") { stack->setType(std::string{value}); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "joinMessage") {
            if (event->getEventName() == "PlayerJoinEvent") {
                auto *join = static_cast<PlayerJoinEvent *>(event);
                join->setJoinMessage(Message{std::string{value}});
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "quitMessage") {
            if (event->getEventName() == "PlayerQuitEvent") {
                auto *quit = static_cast<PlayerQuitEvent *>(event);
                quit->setQuitMessage(Message{std::string{value}});
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "message") {
            if (event->getEventName() == "PlayerChatEvent") {
                auto *chat = static_cast<PlayerChatEvent *>(event);
                chat->setMessage(std::string{value});
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "command") {
            if (event->getEventName() == "PlayerCommandEvent") {
                auto *command = static_cast<PlayerCommandEvent *>(event);
                command->setCommand(std::string{value});
                return ESN_OK;
            }
            if (event->getEventName() == "ServerCommandEvent") {
                auto *command = static_cast<ServerCommandEvent *>(event);
                command->setCommand(std::string{value});
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "deathMessage" && event->getEventName() == "PlayerDeathEvent") {
            // An empty string suppresses the announcement, matching join and quit messages.
            auto *death = static_cast<PlayerDeathEvent *>(event);
            death->setDeathMessage(value.empty() ? std::optional<Message>{} : Message{std::string{value}});
            return ESN_OK;
        }
        if (name == "payload") {
            if (event->getEventName() == "PacketReceiveEvent") {
                static_cast<PacketReceiveEvent *>(event)->setPayload(value);
                return ESN_OK;
            }
            if (event->getEventName() == "PacketSendEvent") {
                static_cast<PacketSendEvent *>(event)->setPayload(value);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *actor = resolveActor(target)) {
        if (name == "nameTag") { actor->setNameTag(std::string{value}); return ESN_OK; }
        if (name == "scoreTag") { actor->setScoreTag(std::string{value}); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "nameTag") { player->setNameTag(std::string{value}); return ESN_OK; }
        if (name == "scoreTag") { player->setScoreTag(std::string{value}); return ESN_OK; }
        if (name == "gameMode") {
            const auto mode = gameModeFromName(value);
            if (!mode) {
                return ESN_ERR_WRONG_TYPE;
            }
            player->setGameMode(*mode);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "type") { (void)block->setType(std::string{value}); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::invoke(const esn_handle target, const std::string_view name,
                             const char *const *strings, const std::size_t string_count, const double *numbers,
                             const std::size_t number_count, esn_handle *out_handle)
{
    const auto number = [&](const std::size_t index, const double fallback = 0.0) {
        return index < number_count ? numbers[index] : fallback;
    };
    const auto str = [&](const std::size_t index) -> std::string {
        return index < string_count && strings[index] ? std::string{strings[index]} : std::string{};
    };
    const auto text = str(0);

    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "sendMessage") { player->sendMessage(Message{std::string{text}}); return ESN_OK; }
        if (name == "sendErrorMessage") { player->sendErrorMessage(Message{std::string{text}}); return ESN_OK; }
        if (name == "sendPopup") { player->sendPopup(std::string{text}); return ESN_OK; }
        if (name == "sendTip") { player->sendTip(std::string{text}); return ESN_OK; }
        if (name == "kick") { player->kick(std::string{text}); return ESN_OK; }
        if (name == "performCommand") { (void)player->performCommand(std::string{text}); return ESN_OK; }
        if (name == "updateCommands") { player->updateCommands(); return ESN_OK; }
        if (name == "giveExp") { player->giveExp(static_cast<int>(number(0))); return ESN_OK; }
        if (name == "giveExpLevels") { player->giveExpLevels(static_cast<int>(number(0))); return ESN_OK; }
        if (name == "transfer") {
            player->transfer(std::string{text}, static_cast<int>(number(0, 19132)));
            return ESN_OK;
        }
        // teleport(location, { rotation, dimension }). Rotation and dimension default to the
        // player's current ones. Only a teleport can turn a player's view - setRotation moves the
        // server-side rotation but the client owns its camera.
        if (name == "teleport") {
            const auto current = player->getLocation();
            auto *dimension = &player->getDimension();
            if (const auto requested = str(0); !requested.empty()) {
                dimension = player->getLevel().getDimension(requested);
                if (!dimension) {
                    return ESN_ERR_BAD_ARGUMENT;
                }
            }
            player->teleport(Location{*dimension, static_cast<float>(number(0)),
                                      static_cast<float>(number(1)), static_cast<float>(number(2)),
                                      static_cast<float>(number(4, current.getPitch())),
                                      static_cast<float>(number(3, current.getYaw()))});
            return ESN_OK;
        }
        if (name == "playSound") {
            player->playSound(player->getLocation(), std::string{text}, static_cast<float>(number(0, 1.0)),
                              static_cast<float>(number(1, 1.0)));
            return ESN_OK;
        }
        if (name == "stopSound") { player->stopSound(std::string{text}); return ESN_OK; }
        if (name == "stopAllSounds") { player->stopAllSounds(); return ESN_OK; }
        if (name == "resetTitle") { player->resetTitle(); return ESN_OK; }
        if (name == "sendTitle") {
            player->sendTitle(text, str(1), static_cast<int>(number(0, 10)), static_cast<int>(number(1, 70)),
                              static_cast<int>(number(2, 20)));
            return ESN_OK;
        }
        if (name == "sendToast") { player->sendToast(text, str(1)); return ESN_OK; }
        if (name == "spawnParticle") {
            player->spawnParticle(text, static_cast<float>(number(0)), static_cast<float>(number(1)),
                                  static_cast<float>(number(2)),
                                  string_count > 1 ? std::optional<std::string>{str(1)} : std::nullopt);
            return ESN_OK;
        }
        // Backend for the `rotation` property: setRotation(yaw, pitch), matching Endstone. For a
        // player this only moves the server-side rotation; the client owns its own camera.
        if (name == "setRotation") {
            player->setRotation(static_cast<float>(number(0)), static_cast<float>(number(1)));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *sender = static_cast<CommandSender *>(resolve(target, Kind::CommandSender))) {
        if (name == "sendMessage") { sender->sendMessage(Message{std::string{text}}); return ESN_OK; }
        if (name == "sendErrorMessage") { sender->sendErrorMessage(Message{std::string{text}}); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *inventory = resolveInventory(target)) {
        // An item is described by a type plus optional amount and data, so nothing has to construct an
        // ItemStack from JavaScript. An empty type means "no item", which clears the slot.
        const auto described = [&](const std::size_t number_base) -> std::optional<endstone::ItemStack> {
            if (text.empty()) {
                return std::nullopt;
            }
            return endstone::ItemStack{text, static_cast<int>(number(number_base, 1)),
                                      static_cast<int>(number(number_base + 1, 0))};
        };

        if (name == "getItem" && out_handle) {
            auto item = inventory->getItem(static_cast<int>(number(0)));
            if (!item) {
                *out_handle = 0;
                return ESN_OK;
            }
            owned_items_.push_back(std::make_unique<endstone::ItemStack>(std::move(*item)));
            *out_handle = track(owned_items_.back().get(), Kind::ItemStack);
            return ESN_OK;
        }
        // setItem(slot, item): numbers are slot, then the item's amount and data.
        if (name == "setItem") {
            inventory->setItem(static_cast<int>(number(0)), described(1));
            return ESN_OK;
        }
        if (name == "addItem") {
            if (auto item = described(0)) {
                (void)inventory->addItem({*item});
            }
            return ESN_OK;
        }
        if (name == "removeItem") {
            if (auto item = described(0)) {
                (void)inventory->removeItem({*item});
            }
            return ESN_OK;
        }
        if (name == "remove") { inventory->remove(std::string{text}); return ESN_OK; }
        if (name == "clear") {
            // clear() empties everything; clear(slot) empties one slot.
            if (number_count > 0) {
                inventory->clear(static_cast<int>(number(0)));
            }
            else {
                inventory->clear();
            }
            return ESN_OK;
        }
        if (name == "setHeldItemSlot") {
            if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
                player_inventory->setHeldItemSlot(static_cast<int>(number(0)));
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
            if (name == "setHelmet") { player_inventory->setHelmet(described(0)); return ESN_OK; }
            if (name == "setChestplate") { player_inventory->setChestplate(described(0)); return ESN_OK; }
            if (name == "setLeggings") { player_inventory->setLeggings(described(0)); return ESN_OK; }
            if (name == "setBoots") { player_inventory->setBoots(described(0)); return ESN_OK; }
            if (name == "setItemInMainHand") { player_inventory->setItemInMainHand(described(0)); return ESN_OK; }
            if (name == "setItemInOffHand") { player_inventory->setItemInOffHand(described(0)); return ESN_OK; }
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *actor = resolveActor(target)) {
        if (name == "sendMessage") { actor->sendMessage(Message{std::string{text}}); return ESN_OK; }
        if (name == "remove") { actor->remove(); return ESN_OK; }
        if (name == "setRotation") {
            actor->setRotation(static_cast<float>(number(0)), static_cast<float>(number(1)));
            return ESN_OK;
        }
        if (name == "teleport") {
            const auto current = actor->getLocation();
            auto *dimension = &actor->getDimension();
            if (const auto requested = str(0); !requested.empty()) {
                dimension = actor->getLevel().getDimension(requested);
                if (!dimension) {
                    return ESN_ERR_BAD_ARGUMENT;
                }
            }
            actor->teleport(Location{*dimension, static_cast<float>(number(0)),
                                     static_cast<float>(number(1)), static_cast<float>(number(2)),
                                     static_cast<float>(number(4, current.getPitch())),
                                     static_cast<float>(number(3, current.getYaw()))});
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "getRelative" && out_handle) {
            auto relative = block->getRelative(static_cast<int>(number(0)), static_cast<int>(number(1)),
                                               static_cast<int>(number(2)));
            if (!relative) {
                return ESN_ERR_INTERNAL;
            }
            // The Block is created for us, so the bridge owns it until this dispatch ends.
            auto *raw = relative.get();
            owned_blocks_.push_back(std::move(relative));
            *out_handle = track(raw, Kind::Block);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "cancel") {
            auto *cancellable = eventCancellable(event);
            if (!cancellable) {
                return ESN_ERR_WRONG_TYPE;
            }
            cancellable->cancel();
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::typeName(const esn_handle target, char *buf, const std::size_t cap, std::size_t *needed)
{
    if (resolve(target, Kind::Player)) {
        return emitString("Player", buf, cap, needed);
    }
    if (resolve(target, Kind::Mob)) {
        return emitString("Mob", buf, cap, needed);
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
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        return emitString(event->getEventName(), buf, cap, needed);
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
                            const double *nums, std::size_t num_count, esn_handle *out)
{
    ESN_GUARD(bridge(c).invoke(t, n ? n : "", strs, str_count, nums, num_count, out))
}
esn_status ESN_CALL tTypeName(void *c, esn_handle t, char *b, std::size_t cap, std::size_t *need)
{
    ESN_GUARD(bridge(c).typeName(t, b, cap, need))
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
void ESN_CALL tUpdateCommands(void *c)
{
    try {
        bridge(c).updateCommands();
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
    api.broadcast_message = tBroadcastMessage;
    api.log = tLog;
    api.accessors.get_bool = tGetBool;
    api.accessors.get_int = tGetInt;
    api.accessors.get_double = tGetDouble;
    api.accessors.get_string = tGetString;
    api.accessors.get_handle = tGetHandle;
    api.accessors.set_bool = tSetBool;
    api.accessors.set_int = tSetInt;
    api.accessors.set_double = tSetDouble;
    api.accessors.set_string = tSetString;
    api.accessors.invoke = tInvoke;
    api.accessors.type_name = tTypeName;
    api.subscribe = tSubscribe;
    api.unsubscribe = tUnsubscribe;
    api.update_commands = tUpdateCommands;
}

}  // namespace endstone::node
