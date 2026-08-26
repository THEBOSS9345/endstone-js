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
/**
 * @brief The actor an event is about, keeping the Mob type when the event has one.
 *
 * `out_mob` receives the pointer as a Mob rather than the caller narrowing an Actor* later. The handle
 * table stores void*, and recovering a Mob* from a void* that was written as an Actor* is a downcast
 * through void - sound only while Mob's single base sits at offset zero. Passing the already-correct
 * static type costs nothing and does not depend on that layout holding.
 */
Actor *eventActor(Event *event, Mob **out_mob)
{
    *out_mob = nullptr;
    const auto *trait = traitFor(event);
    if (!trait) {
        return nullptr;
    }
    if (trait->mob) {
        *out_mob = trait->mob(event);
        return *out_mob;
    }
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

// Same reasoning as gameModeName: magic_enum is core-only, so these are mapped by hand. The names are
// lowercase so JavaScript compares them without worrying about casing.
std::string_view blockFaceName(const BlockFace face)
{
    switch (face) {
    case BlockFace::Down: return "down";
    case BlockFace::Up: return "up";
    case BlockFace::North: return "north";
    case BlockFace::South: return "south";
    case BlockFace::West: return "west";
    case BlockFace::East: return "east";
    }
    return "down";
}

std::optional<BlockFace> blockFaceFromName(const std::string_view name)
{
    if (name == "down") return BlockFace::Down;
    if (name == "up") return BlockFace::Up;
    if (name == "north") return BlockFace::North;
    if (name == "south") return BlockFace::South;
    if (name == "west") return BlockFace::West;
    if (name == "east") return BlockFace::East;
    return std::nullopt;
}

std::string_view interactActionName(const PlayerInteractEvent::Action action)
{
    switch (action) {
    case PlayerInteractEvent::Action::LeftClickBlock: return "leftClickBlock";
    case PlayerInteractEvent::Action::RightClickBlock: return "rightClickBlock";
    case PlayerInteractEvent::Action::LeftClickAir: return "leftClickAir";
    case PlayerInteractEvent::Action::RightClickAir: return "rightClickAir";
    }
    return "rightClickBlock";
}

std::string_view equipmentSlotName(const EquipmentSlot slot)
{
    switch (slot) {
    case EquipmentSlot::Hand: return "hand";
    case EquipmentSlot::OffHand: return "offHand";
    case EquipmentSlot::Feet: return "feet";
    case EquipmentSlot::Legs: return "legs";
    case EquipmentSlot::Chest: return "chest";
    case EquipmentSlot::Head: return "head";
    default: break;
    }
    return "hand";
}

// Custom item data rides the name-keyed accessors: a property called "tag:endstone:timer" addresses
// the key "endstone:timer" in the item's own NBT. That keeps arbitrary per-item data out of the ABI,
// which carries only scalars, and means adding it needed no new entry points.
constexpr std::string_view kTagPrefix = "tag:";

std::optional<std::string> tagKeyOf(const std::string_view name)
{
    if (name.size() > kTagPrefix.size() && name.substr(0, kTagPrefix.size()) == kTagPrefix) {
        return std::string{name.substr(kTagPrefix.size())};
    }
    return std::nullopt;
}

/** The tag stored under `key`, or nullptr when the item has no such key. */
const nbt::Tag *findTag(const CompoundTag &nbt, const std::string &key)
{
    return nbt.contains(key) ? &nbt.at(key) : nullptr;
}

/** Applies `edit` to a copy of the item's NBT and writes it back. */
template <typename Edit>
void editNbt(ItemStack &stack, Edit &&edit)
{
    auto nbt = stack.getNbt();
    edit(nbt);
    stack.setNbt(nbt);
}

/**
 * @brief Reads then writes back an item's metadata - display name, lore, damage, enchantments.
 *
 * getItemMeta() hands out a copy, so a change only reaches the item once setItemMeta puts it back. Same
 * shape as editNbt, and like it the caller must follow with persistItem() so the stack itself is saved
 * back to the slot it came from.
 */
template <typename Edit>
void editMeta(ItemStack &stack, Edit &&edit)
{
    auto meta = stack.getItemMeta();
    if (!meta) {
        return;
    }
    edit(*meta);
    (void)stack.setItemMeta(meta.get());
}

/**
 * @brief The item's metadata as a subclass, or nullptr when it is not that kind of item.
 *
 * NOT dynamic_cast - the plugin's type_info is a different object from the runtime's, so it silently
 * fails across the library boundary. getType() is a virtual call and always works, and once the kind
 * is known by name a static_cast is safe and needs no RTTI. Same discipline as the event traits.
 */
template <typename Meta>
Meta *metaAs(const std::unique_ptr<ItemMeta> &meta)
{
    return (meta && meta->getType() == Meta::MetaType) ? static_cast<Meta *>(meta.get()) : nullptr;
}

/** Applies `edit` to an item's metadata as `Meta`, writing it back. False when the kind is wrong. */
template <typename Meta, typename Edit>
bool editMetaAs(ItemStack &stack, Edit &&edit)
{
    auto meta = stack.getItemMeta();
    auto *typed = metaAs<Meta>(meta);
    if (!typed) {
        return false;
    }
    edit(*typed);
    (void)stack.setItemMeta(meta.get());
    return true;
}

std::string_view generationName(const BookMeta::Generation generation)
{
    switch (generation) {
    case BookMeta::Generation::Original: return "original";
    case BookMeta::Generation::CopyOfOriginal: return "copyOfOriginal";
    case BookMeta::Generation::CopyOfCopy: return "copyOfCopy";
    }
    return "original";
}

std::optional<BookMeta::Generation> generationFromName(const std::string_view name)
{
    if (name == "original") return BookMeta::Generation::Original;
    if (name == "copyOfOriginal" || name == "copyoforiginal") return BookMeta::Generation::CopyOfOriginal;
    if (name == "copyOfCopy" || name == "copyofcopy") return BookMeta::Generation::CopyOfCopy;
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
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        it->second(*stack);
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

std::vector<std::string> splitOn(const std::string_view text, const char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto at = text.find(separator, start);
        if (at == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, at - start));
        start = at + 1;
    }
}

/** Field `index` of a record, or an empty string. Missing fields are normal: they mean "default". */
std::string fieldAt(const std::vector<std::string> &fields, const std::size_t index)
{
    return index < fields.size() ? fields[index] : std::string{};
}

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

constexpr char kUnitSeparator = '\x1f';

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
std::string blockStatesRecord(const BlockData &data)
{
    std::string out;
    for (const auto &[key, value] : data.getBlockStates()) {
        if (!out.empty()) {
            out += '\n';
        }
        out += key;
        out += kUnitSeparator;
        std::visit(
            [&](const auto &held) {
                using Held = std::decay_t<decltype(held)>;
                if constexpr (std::is_same_v<Held, bool>) {
                    out += "b";
                    out += kUnitSeparator;
                    out += held ? "1" : "0";
                }
                else if constexpr (std::is_same_v<Held, int>) {
                    out += "i";
                    out += kUnitSeparator;
                    out += std::to_string(held);
                }
                else {
                    out += "s";
                    out += kUnitSeparator;
                    out += held;
                }
            },
            value);
    }
    return out;
}

/**
 * @brief Reads the "key\x1ftype\x1fvalue" records the runtime sends back, onto `states`.
 *
 * Overlaid rather than replacing: a plugin that sets one state means "change this one", so the rest
 * of the block's palette entry has to survive. The tag decides the variant arm, which is why it
 * travels with the value in both directions - "true" the string and true the boolean are different
 * states and Bedrock cares.
 */
void parseBlockStates(const std::string_view text, BlockStates &states)
{
    if (text.empty()) {
        return;
    }
    for (const auto &line : splitOn(text, '\n')) {
        const auto fields = splitOn(line, kUnitSeparator);
        if (fields.size() < 3 || fields[0].empty()) {
            continue;
        }
        const auto &key = fields[0];
        const auto &value = fields[2];
        if (fields[1] == "b") {
            states[key] = value == "1";
        }
        else if (fields[1] == "i") {
            try {
                states[key] = std::stoi(value);
            }
            catch (...) {
                continue;
            }
        }
        else {
            states[key] = value;
        }
    }
}

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
    // Runtime ids exceed what a double represents exactly, so this is the one id exposed as a number
    // only because Bedrock keeps them small in practice; prefer uniqueId for anything persistent.
    if (name == "runtimeId") { *out = static_cast<std::int64_t>(actor.getRuntimeId()); return ESN_OK; }
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

/**
 * @brief The methods every actor has, including players.
 *
 * Player's own branch falls back to this, so anything Endstone puts on Actor is reachable on a Player
 * too - the types say `Player extends Actor` and this is what makes that true. `remove` is safe to
 * expose here: EndstonePlayer overrides it to log "use Player::kick instead" and do nothing.
 */
esn_status actorInvoke(Actor &actor, const std::string_view name, const std::string &text,
                       const std::function<std::string(std::size_t)> &str,
                       const std::function<double(std::size_t, double)> &number)
{
    // Actor derives from CommandSender, so both of its messaging methods belong here too.
    if (name == "sendMessage") { actor.sendMessage(Message{text}); return ESN_OK; }
    if (name == "sendErrorMessage") { actor.sendErrorMessage(Message{text}); return ESN_OK; }
    if (name == "remove") { actor.remove(); return ESN_OK; }
    if (name == "addScoreboardTag") { (void)actor.addScoreboardTag(text); return ESN_OK; }
    if (name == "removeScoreboardTag") { (void)actor.removeScoreboardTag(text); return ESN_OK; }
    // Backend for the `rotation` property. On a player this moves only the server-side rotation - the
    // client owns its camera, so turning a player's view takes a teleport.
    if (name == "setRotation") {
        actor.setRotation(static_cast<float>(number(0, 0)), static_cast<float>(number(1, 0)));
        return ESN_OK;
    }
    // teleport(location, { rotation, dimension }); rotation and dimension default to the current ones.
    if (name == "teleport") {
        const auto current = actor.getLocation();
        auto *dimension = &actor.getDimension();
        if (const auto requested = str(0); !requested.empty()) {
            dimension = actor.getLevel().getDimension(requested);
            if (!dimension) {
                return ESN_ERR_BAD_ARGUMENT;
            }
        }
        actor.teleport(Location{*dimension, static_cast<float>(number(0, 0)), static_cast<float>(number(1, 0)),
                                static_cast<float>(number(2, 0)), static_cast<float>(number(4, current.getPitch())),
                                static_cast<float>(number(3, current.getYaw()))});
        return ESN_OK;
    }
    return ESN_ERR_NO_SUCH_MEMBER;
}

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

// Permissions live on Permissible, which CommandSender and therefore every Actor derives from, so one
// helper serves players, actors and the console alike. The node rides the accessor name the way a
// custom NBT key does, because the accessors carry no argument of their own.
esn_status permissibleGetBool(Permissible &who, const std::string_view name, int *out)
{
    if (const auto node = suffixOf(name, "permission:")) {
        *out = who.hasPermission(*node);
        return ESN_OK;
    }
    if (const auto node = suffixOf(name, "permissionSet:")) {
        *out = who.isPermissionSet(*node);
        return ESN_OK;
    }
    return ESN_ERR_NO_SUCH_MEMBER;
}

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
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "isOp") { *out = player->isOp(); return ESN_OK; }
        if (name == "isSneaking") { *out = player->isSneaking(); return ESN_OK; }
        if (name == "isSprinting") { *out = player->isSprinting(); return ESN_OK; }
        if (name == "isFlying") { *out = player->isFlying(); return ESN_OK; }
        if (name == "allowFlight") { *out = player->getAllowFlight(); return ESN_OK; }
        if (name == "isGliding") { *out = player->isGliding(); return ESN_OK; }
        if (const auto status = permissibleGetBool(*player, name, out); status != ESN_ERR_NO_SUCH_MEMBER) {
            return status;
        }
        return actorGetBool(*player, name, out);
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item));
        item && name == "unlimitedLifetime") {
        *out = item->isUnlimitedLifetime();
        return ESN_OK;
    }
    if (auto *actor = resolveActor(target)) {
        if (const auto status = permissibleGetBool(*actor, name, out); status != ESN_ERR_NO_SUCH_MEMBER) {
            return status;
        }
        return actorGetBool(*actor, name, out);
    }
    if (auto *source = static_cast<DamageSource *>(resolve(target, Kind::DamageSource))) {
        // True when the responsible actor is not the one that struck, e.g. a shooter and their arrow.
        if (name == "isIndirect") { *out = source->isIndirect(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *inventory = resolveInventory(target)) {
        if (name == "isEmpty") { *out = inventory->isEmpty(); return ESN_OK; }
        // Matching against a whole stack rather than a type id, so NBT, enchantments and custom names
        // count. The stack rides the accessor name because these take an object, as similarTo: does.
        if (const auto request = suffixOf(name, "containsStack:")) {
            const auto comma = request->find(',');
            const auto other = parseHandleSuffix(request->substr(0, comma));
            const auto *against = static_cast<const ItemStack *>(resolve(other, Kind::ItemStack));
            if (!against) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            if (comma == std::string::npos) {
                *out = inventory->contains(*against);
                return ESN_OK;
            }
            *out = inventory->containsAtLeast(*against, std::stoi(request->substr(comma + 1)));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *map = static_cast<MapView *>(resolve(target, Kind::MapView))) {
        if (name == "isLocked") { *out = map->isLocked(); return ESN_OK; }
        // True when a plugin supplied the lowermost renderer, i.e. the map is not a world map.
        if (name == "isVirtual") { *out = map->isVirtual(); return ESN_OK; }
        if (name == "unlimitedTracking") { *out = map->isUnlimitedTracking(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "hasItemMeta") { *out = stack->hasItemMeta(); return ESN_OK; }
        if (name == "hasMapId" || name == "hasMapView" || name == "hasTitle" || name == "hasAuthor" ||
            name == "hasGeneration" || name == "hasPages" || name == "hasChargedProjectiles") {
            const auto meta = stack->getItemMeta();
            if (const auto *map = metaAs<MapMeta>(meta)) {
                if (name == "hasMapId") { *out = map->hasMapId(); return ESN_OK; }
                if (name == "hasMapView") { *out = map->hasMapView(); return ESN_OK; }
            }
            if (const auto *book = metaAs<BookMeta>(meta)) {
                if (name == "hasTitle") { *out = book->hasTitle(); return ESN_OK; }
                if (name == "hasAuthor") { *out = book->hasAuthor(); return ESN_OK; }
                if (name == "hasGeneration") { *out = book->hasGeneration(); return ESN_OK; }
            }
            if (const auto *writable = metaAs<WritableBookMeta>(meta); writable && name == "hasPages") {
                *out = writable->hasPages();
                return ESN_OK;
            }
            if (const auto *crossbow = metaAs<CrossbowMeta>(meta); crossbow && name == "hasChargedProjectiles") {
                *out = crossbow->hasChargedProjectiles();
                return ESN_OK;
            }
            *out = 0;  // right question, wrong kind of item - absent rather than an error
            return ESN_OK;
        }
        if (name == "unbreakable") {
            const auto meta = stack->getItemMeta();
            *out = meta && meta->isUnbreakable();
            return ESN_OK;
        }
        // enchant:<id> - whether the item carries that enchantment at all.
        if (const auto id = suffixOf(name, "enchant:")) {
            const auto meta = stack->getItemMeta();
            *out = meta && meta->hasEnchant(EnchantmentId{*id});
            return ESN_OK;
        }
        // Whether that enchantment fights one the item already has, e.g. sharpness against smite.
        if (const auto id = suffixOf(name, "conflicts:")) {
            const auto meta = stack->getItemMeta();
            *out = meta && meta->hasConflictingEnchant(EnchantmentId{*id});
            return ESN_OK;
        }
        // similarTo:<handle> - invoke cannot return a bool, so the other stack rides the accessor name
        // the same way a custom NBT key does.
        if (name.starts_with("similarTo:")) {
            const auto other = parseHandleSuffix(name.substr(10));
            const auto *against = static_cast<const ItemStack *>(resolve(other, Kind::ItemStack));
            if (!against) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            *out = stack->isSimilar(*against);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "visible") { *out = bar->isVisible(); return ESN_OK; }
        if (const auto flag = barFlagFromName(name)) { *out = bar->hasFlag(*flag); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *sender = static_cast<CommandSender *>(resolve(target, Kind::CommandSender))) {
        // There is no isOp on CommandSender; the console reports Operator or Console here.
        if (name == "isOp") {
            *out = sender->getPermissionLevel() != PermissionLevel::Default;
            return ESN_OK;
        }
        if (name == "isConsole") { *out = sender->asConsole() != nullptr; return ESN_OK; }
        return permissibleGetBool(*sender, name, out);
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        const auto ev = event->getEventName();
        if (ev == "PlayerInteractEvent") {
            auto *interact = static_cast<PlayerInteractEvent *>(event);
            if (name == "hasBlock") { *out = interact->hasBlock(); return ESN_OK; }
            if (name == "hasItem") { *out = interact->hasItem(); return ESN_OK; }
        }
        if (name == "isMuted" && ev == "PlayerEmoteEvent") {
            *out = static_cast<PlayerEmoteEvent *>(event)->isMuted();
            return ESN_OK;
        }
        // The state being changed *to*, which is the only thing either weather event carries: without
        // it a handler cannot tell rain starting from rain stopping.
        if (name == "raining" && ev == "WeatherChangeEvent") {
            *out = static_cast<WeatherChangeEvent *>(event)->toWeatherState();
            return ESN_OK;
        }
        if (name == "thundering" && ev == "ThunderChangeEvent") {
            *out = static_cast<ThunderChangeEvent *>(event)->toThunderState();
            return ESN_OK;
        }
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
    if (auto *server = static_cast<Server *>(resolve(target, Kind::Server))) {
        if (name == "port") { *out = server->getPort(); return ESN_OK; }
        if (name == "portV6") { *out = server->getPortV6(); return ESN_OK; }
        if (name == "protocolVersion") { *out = server->getProtocolVersion(); return ESN_OK; }
        if (name == "onlinePlayerCount") {
            *out = static_cast<std::int64_t>(server->getOnlinePlayers().size());
            return ESN_OK;
        }
        if (name == "maxPlayers") { *out = server->getMaxPlayers(); return ESN_OK; }
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
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "amount") { *out = stack->getAmount(); return ESN_OK; }
        if (name == "data") { *out = stack->getData(); return ESN_OK; }
        if (name == "maxStackSize") { *out = stack->getMaxStackSize(); return ESN_OK; }
        // Metadata that reads as a number. All three are 0 on an item with no metadata at all.
        if (name == "damage" || name == "repairCost") {
            const auto meta = stack->getItemMeta();
            *out = !meta ? 0 : name == "damage" ? meta->getDamage() : meta->getRepairCost();
            return ESN_OK;
        }
        // enchantLevel:<id> - 0 when the item does not have it, which is what getEnchantLevel returns.
        if (const auto id = suffixOf(name, "enchantLevel:")) {
            const auto meta = stack->getItemMeta();
            *out = meta ? meta->getEnchantLevel(EnchantmentId{*id}) : 0;
            return ESN_OK;
        }
        if (const auto key = tagKeyOf(name)) {
            const auto nbt = stack->getNbt();
            const auto *tag = findTag(nbt, *key);
            if (!tag) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            // NBT has no boolean, so a byte covers both flags and small integers; it reads back as a
            // number, which means a JavaScript `true` returns as 1.
            switch (tag->type()) {
            case nbt::Type::Byte:
                *out = tag->get<ByteTag>().value();
                return ESN_OK;
            case nbt::Type::Short:
                *out = tag->get<ShortTag>().value();
                return ESN_OK;
            case nbt::Type::Int:
                *out = tag->get<IntTag>().value();
                return ESN_OK;
            case nbt::Type::Long:
                *out = tag->get<LongTag>().value();
                return ESN_OK;
            default:
                return ESN_ERR_WRONG_TYPE;
            }
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack));
        stack && (name == "mapId" || name == "pageCount" || name == "chargedProjectileCount")) {
        const auto meta = stack->getItemMeta();
        if (const auto *map = metaAs<MapMeta>(meta); map && name == "mapId") {
            *out = map->hasMapId() ? map->getMapId() : 0;
            return ESN_OK;
        }
        if (const auto *writable = metaAs<WritableBookMeta>(meta); writable && name == "pageCount") {
            *out = writable->getPageCount();
            return ESN_OK;
        }
        if (const auto *crossbow = metaAs<CrossbowMeta>(meta); crossbow && name == "chargedProjectileCount") {
            *out = static_cast<std::int64_t>(crossbow->getChargedProjectiles().size());
            return ESN_OK;
        }
        *out = 0;
        return ESN_OK;
    }
    if (auto *inventory = resolveInventory(target)) {
        if (name == "size") { *out = inventory->getSize(); return ESN_OK; }
        if (name == "maxStackSize") { *out = inventory->getMaxStackSize(); return ESN_OK; }
        if (name == "firstEmpty") { *out = inventory->firstEmpty(); return ESN_OK; }
        if (const auto request = suffixOf(name, "firstStack:")) {
            const auto *against =
                static_cast<const ItemStack *>(resolve(parseHandleSuffix(*request), Kind::ItemStack));
            if (!against) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            *out = inventory->first(*against);
            return ESN_OK;
        }
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
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "playerCount") { *out = static_cast<std::int64_t>(bar->getPlayers().size()); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        // The network id of this block's palette entry - what an UpdateBlockPacket carries.
        if (name == "runtimeId") {
            const auto data = block->getData();
            *out = data ? static_cast<std::int64_t>(data->getRuntimeId()) : 0;
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player)); player && name.starts_with("skin")) {
        // Skin and cape pixel data is an Image and stays on the C++ side - a JS plugin cannot render it -
        // but the dimensions are worth having, and 0 distinguishes "no cape" from a 0x0 one.
        const auto skin = player->getSkin();
        if (name == "skinWidth") { *out = skin.getImage().getWidth(); return ESN_OK; }
        if (name == "skinHeight") { *out = skin.getImage().getHeight(); return ESN_OK; }
        if (name == "skinCapeWidth") {
            *out = skin.getCapeImage() ? skin.getCapeImage()->getWidth() : 0;
            return ESN_OK;
        }
        if (name == "skinCapeHeight") {
            *out = skin.getCapeImage() ? skin.getCapeImage()->getHeight() : 0;
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        const auto ev = event->getEventName();
        if (name == "previousSlot" && ev == "PlayerItemHeldEvent") {
            *out = static_cast<PlayerItemHeldEvent *>(event)->getPreviousSlot();
            return ESN_OK;
        }
        if (name == "newSlot" && ev == "PlayerItemHeldEvent") {
            *out = static_cast<PlayerItemHeldEvent *>(event)->getNewSlot();
            return ESN_OK;
        }
        if (name == "blockCount") {
            if (ev == "ActorExplodeEvent") {
                *out = static_cast<std::int64_t>(static_cast<ActorExplodeEvent *>(event)->getBlockList().size());
                return ESN_OK;
            }
            if (ev == "BlockExplodeEvent") {
                *out = static_cast<std::int64_t>(static_cast<BlockExplodeEvent *>(event)->getBlockList().size());
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (ev == "ServerListPingEvent") {
            auto *ping = static_cast<ServerListPingEvent *>(event);
            if (name == "numPlayers") { *out = ping->getNumPlayers(); return ESN_OK; }
            if (name == "maxPlayers") { *out = ping->getMaxPlayers(); return ESN_OK; }
            if (name == "localPort") { *out = ping->getLocalPort(); return ESN_OK; }
            if (name == "localPortV6") { *out = ping->getLocalPortV6(); return ESN_OK; }
            if (name == "protocolVersion") { *out = ping->getNetworkProtocolVersion(); return ESN_OK; }
        }
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
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        if (name == "progress") { *out = bar->getProgress(); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (const auto key = tagKeyOf(name)) {
            const auto nbt = stack->getNbt();
            const auto *tag = findTag(nbt, *key);
            if (!tag) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            if (tag->type() == nbt::Type::Float) { *out = tag->get<FloatTag>().value(); return ESN_OK; }
            if (tag->type() == nbt::Type::Double) { *out = tag->get<DoubleTag>().value(); return ESN_OK; }
            return ESN_ERR_WRONG_TYPE;
        }
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
    if (auto *inventory = resolveInventory(target)) {
        // allStacks:<handle> - every slot holding a stack that matches by metadata, one per line.
        if (const auto request = suffixOf(name, "allStacks:")) {
            const auto *against =
                static_cast<const ItemStack *>(resolve(parseHandleSuffix(*request), Kind::ItemStack));
            if (!against) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            std::string joined;
            for (const auto &[slot, stack] : inventory->all(*against)) {
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += std::to_string(slot);
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
        if (name == "permissionLevel") {
            return emitString(permissionLevelName(player->getPermissionLevel()), buf, cap, needed);
        }
        // The skin's identity, not its pixels: an Image cannot cross the ABI usefully.
        if (name == "skinId") { return emitString(player->getSkin().getId(), buf, cap, needed); }
        if (name == "capeId") {
            const auto &cape = player->getSkin().getCapeId();
            return emitString(cape.value_or(std::string{}), buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *actor = resolveActor(target)) {
        if (name == "type") { return emitString(actor->getType(), buf, cap, needed); }
        if (name == "nameTag") { return emitString(actor->getNameTag(), buf, cap, needed); }
        if (name == "scoreTag") { return emitString(actor->getScoreTag(), buf, cap, needed); }
        if (name == "dimension") { return emitString(actor->getDimension().getName(), buf, cap, needed); }
        // A list cannot cross the ABI as an array, so the tags arrive newline-joined and the runtime
        // splits them into `scoreboardTags`.
        if (name == "scoreboardTagList") {
            std::string joined;
            for (const auto &tag : actor->getScoreboardTags()) {
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += tag;
            }
            return emitString(joined, buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "type") { return emitString(block->getType(), buf, cap, needed); }
        if (name == "dimension") { return emitString(block->getDimension().getName(), buf, cap, needed); }
        // This block's own palette entry, rather than a type's default.
        if (name == "blockStatesList") {
            const auto data = block->getData();
            return emitString(data ? blockStatesRecord(*data) : std::string{}, buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *state = static_cast<BlockState *>(resolve(target, Kind::BlockState))) {
        if (name == "type") { return emitString(state->getType(), buf, cap, needed); }
        if (name == "dimension") { return emitString(state->getDimension().getName(), buf, cap, needed); }
        if (name == "blockStatesList") {
            const auto data = state->getData();
            return emitString(data ? blockStatesRecord(*data) : std::string{}, buf, cap, needed);
        }
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
        if (name == "permissionLevel") {
            return emitString(permissionLevelName(sender->getPermissionLevel()), buf, cap, needed);
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "type") { return emitString(std::string{stack->getType().getId()}, buf, cap, needed); }
        if (name == "translationKey") { return emitString(stack->getTranslationKey(), buf, cap, needed); }
        // Written-book and writable-book metadata. A wrong-kind read is empty rather than an error, so
        // `item.title || item.type` reads naturally.
        if (name == "title" || name == "author" || name == "generation" || name == "pageList") {
            const auto meta = stack->getItemMeta();
            if (const auto *book = metaAs<BookMeta>(meta)) {
                if (name == "title") {
                    return emitString(book->hasTitle() ? book->getTitle() : "", buf, cap, needed);
                }
                if (name == "author") {
                    return emitString(book->hasAuthor() ? book->getAuthor() : "", buf, cap, needed);
                }
                if (name == "generation") {
                    const auto generation = book->getGeneration();
                    return emitString(generation ? generationName(*generation) : "", buf, cap, needed);
                }
            }
            if (const auto *writable = metaAs<WritableBookMeta>(meta); writable && name == "pageList") {
                std::string joined;
                for (const auto &page : writable->getPages()) {
                    if (!joined.empty()) {
                        joined += '\n';
                    }
                    joined += page;
                }
                return emitString(joined, buf, cap, needed);
            }
            return emitString("", buf, cap, needed);
        }
        // Metadata that reads as text. An item with no metadata reads as empty rather than failing, so
        // `item.displayName || item.type` is the natural idiom.
        if (name == "displayName") {
            const auto meta = stack->getItemMeta();
            return emitString(meta && meta->hasDisplayName() ? meta->getDisplayName() : "", buf, cap, needed);
        }
        // Lore and the enchantment list are newline-joined; the runtime splits them.
        if (name == "loreList") {
            const auto meta = stack->getItemMeta();
            std::string joined;
            if (meta && meta->hasLore()) {
                for (const auto &line : meta->getLore()) {
                    if (!joined.empty()) {
                        joined += '\n';
                    }
                    joined += line;
                }
            }
            return emitString(joined, buf, cap, needed);
        }
        // "<id>,<level>" per line.
        if (name == "enchantList") {
            const auto meta = stack->getItemMeta();
            std::string joined;
            if (meta && meta->hasEnchants()) {
                for (const auto &[enchantment, level] : meta->getEnchants()) {
                    if (!enchantment) {
                        continue;
                    }
                    if (!joined.empty()) {
                        joined += '\n';
                    }
                    joined += static_cast<std::string>(enchantment->getId()) + "," + std::to_string(level);
                }
            }
            return emitString(joined, buf, cap, needed);
        }
        // The keys the item carries, newline-joined; the runtime splits them into an array.
        if (name == "tagKeyList") {
            std::string joined;
            for (const auto &[key, value] : stack->getNbt()) {
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += key;
            }
            return emitString(joined, buf, cap, needed);
        }
        if (const auto key = tagKeyOf(name)) {
            const auto nbt = stack->getNbt();
            const auto *tag = findTag(nbt, *key);
            if (!tag) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            if (tag->type() == nbt::Type::String) {
                return emitString(tag->get<StringTag>().value(), buf, cap, needed);
            }
            // Compounds, lists and arrays have no scalar form, so they come back as SNBT text - enough
            // to inspect data an addon or another plugin wrote, without a tag-tree walker here.
            switch (tag->type()) {
            case nbt::Type::Compound:
            case nbt::Type::List:
            case nbt::Type::ByteArray:
            case nbt::Type::IntArray:
                return emitString(std::format("{}", *tag), buf, cap, needed);
            default:
                break;
            }
            // Numeric: let the int or double probe answer instead.
            return ESN_ERR_WRONG_TYPE;
        }
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
        if (name == "format" && event->getEventName() == "PlayerChatEvent") {
            return emitString(static_cast<PlayerChatEvent *>(event)->getFormat(), buf, cap, needed);
        }
        // Who will see the message, as names: getRecipients hands back a copy, so the list is an
        // observation rather than something a handler can filter.
        if (name == "recipientNameList" && event->getEventName() == "PlayerChatEvent") {
            std::string joined;
            for (const auto *recipient : static_cast<PlayerChatEvent *>(event)->getRecipients()) {
                if (!recipient) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += recipient->getName();
            }
            return emitString(joined, buf, cap, needed);
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
        const auto ev = event->getEventName();
        // --- strings on specific events ---------------------------------------------------------
        if (name == "action" && ev == "PlayerInteractEvent") {
            return emitString(interactActionName(static_cast<PlayerInteractEvent *>(event)->getAction()), buf, cap,
                              needed);
        }
        if (name == "blockFace" && ev == "PlayerInteractEvent") {
            return emitString(blockFaceName(static_cast<PlayerInteractEvent *>(event)->getBlockFace()), buf, cap,
                              needed);
        }
        if (name == "direction" && (ev == "BlockPistonExtendEvent" || ev == "BlockPistonRetractEvent")) {
            return emitString(blockFaceName(static_cast<BlockPistonEvent *>(event)->getDirection()), buf, cap, needed);
        }
        if (name == "hand" && ev == "PlayerItemConsumeEvent") {
            return emitString(equipmentSlotName(static_cast<PlayerItemConsumeEvent *>(event)->getHand()), buf, cap,
                              needed);
        }
        if (name == "newGameMode" && ev == "PlayerGameModeChangeEvent") {
            return emitString(gameModeName(static_cast<PlayerGameModeChangeEvent *>(event)->getNewGameMode()), buf,
                              cap, needed);
        }
        if (name == "emoteId" && ev == "PlayerEmoteEvent") {
            return emitString(static_cast<PlayerEmoteEvent *>(event)->getEmoteId(), buf, cap, needed);
        }
        if (name == "kickMessage" && ev == "PlayerLoginEvent") {
            return emitString(static_cast<PlayerLoginEvent *>(event)->getKickMessage(), buf, cap, needed);
        }
        if (name == "reason" && ev == "PlayerKickEvent") {
            return emitString(static_cast<PlayerKickEvent *>(event)->getReason(), buf, cap, needed);
        }
        if (name == "loadType" && ev == "ServerLoadEvent") {
            const auto type = static_cast<ServerLoadEvent *>(event)->getType();
            return emitString(type == ServerLoadEvent::LoadType::Reload ? "reload" : "startup", buf, cap, needed);
        }
        if (name == "messageId" && ev == "ScriptMessageEvent") {
            return emitString(static_cast<ScriptMessageEvent *>(event)->getMessageId(), buf, cap, needed);
        }
        if (name == "scriptMessage" && ev == "ScriptMessageEvent") {
            return emitString(static_cast<ScriptMessageEvent *>(event)->getMessage(), buf, cap, needed);
        }
        if (name == "message" && ev == "BroadcastMessageEvent") {
            return emitString(messageToString(static_cast<BroadcastMessageEvent *>(event)->getMessage()), buf, cap,
                              needed);
        }
        // --- the server-list entry, every field writable ----------------------------------------
        if (ev == "ServerListPingEvent") {
            auto *ping = static_cast<ServerListPingEvent *>(event);
            if (name == "motd") { return emitString(ping->getMotd(), buf, cap, needed); }
            if (name == "levelName") { return emitString(ping->getLevelName(), buf, cap, needed); }
            if (name == "serverGuid") { return emitString(ping->getServerGuid(), buf, cap, needed); }
            if (name == "gameMode") { return emitString(gameModeName(ping->getGameMode()), buf, cap, needed); }
            if (name == "minecraftVersion") { return emitString(ping->getMinecraftVersionNetwork(), buf, cap, needed); }
            if (name == "address") { return emitString(ping->getAddress().getHostname(), buf, cap, needed); }
        }
        if (name == "deathMessage" && event->getEventName() == "PlayerDeathEvent") {
            const auto message = static_cast<PlayerDeathEvent *>(event)->getDeathMessage();
            return emitString(message ? messageToString(*message) : std::string{}, buf, cap, needed);
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

// Packet payloads are bytes, not text, so they get their own accessor rather than riding getString.
// Handing them through the text path meant the host built the JavaScript string with
// napi_create_string_utf8, and any byte >= 0x80 that did not happen to form valid UTF-8 was replaced
// with U+FFFD - so a payload was silently corrupted on the way out while sendPacket, which reads
// latin1, was correct on the way in.

esn_status ApiBridge::getBytes(const esn_handle target, const std::string_view name, char *buf,
                               const std::size_t cap, std::size_t *needed)
{
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "payload") {
            const auto ev = event->getEventName();
            if (ev == "PacketReceiveEvent") {
                return emitString(static_cast<PacketReceiveEvent *>(event)->getPayload(), buf, cap, needed);
            }
            if (ev == "PacketSendEvent") {
                return emitString(static_cast<PacketSendEvent *>(event)->getPayload(), buf, cap, needed);
            }
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

esn_status ApiBridge::setBytes(const esn_handle target, const std::string_view name, const std::string_view value)
{
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (name == "payload") {
            const auto ev = event->getEventName();
            if (ev == "PacketReceiveEvent") {
                static_cast<PacketReceiveEvent *>(event)->setPayload(value);
                return ESN_OK;
            }
            if (ev == "PacketSendEvent") {
                static_cast<PacketSendEvent *>(event)->setPayload(value);
                return ESN_OK;
            }
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
            Mob *mob = nullptr;
            if (auto *actor = eventActor(event, &mob)) {
                *out = mob ? track(mob, Kind::Mob) : track(actor, Kind::Actor);
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
        const auto ev = event->getEventName();
        // Blocks carried by events other than the plain BlockEvent ones.
        if (name == "toBlock" && ev == "BlockFromToEvent") {
            *out = track(&static_cast<BlockFromToEvent *>(event)->getToBlock(), Kind::Block);
            return ESN_OK;
        }
        if (name == "bed") {
            if (ev == "PlayerBedEnterEvent") {
                *out = track(&static_cast<PlayerBedEnterEvent *>(event)->getBed(), Kind::Block);
                return ESN_OK;
            }
            if (ev == "PlayerBedLeaveEvent") {
                *out = track(&static_cast<PlayerBedLeaveEvent *>(event)->getBed(), Kind::Block);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "blockAgainst" && ev == "BlockPlaceEvent") {
            *out = track(&static_cast<BlockPlaceEvent *>(event)->getBlockAgainst(), Kind::Block);
            return ESN_OK;
        }
        if (name == "blockReplaced" && ev == "BlockPlaceEvent") {
            *out = track(&static_cast<BlockPlaceEvent *>(event)->getBlockReplaced(), Kind::Block);
            return ESN_OK;
        }
        // Where an event happened, or where it is going.
        if (name == "from" || name == "to") {
            const auto copyLocation = [&](const Location &location) {
                owned_locations_.push_back(location);
                *out = track(&owned_locations_.back(), Kind::Location);
                return ESN_OK;
            };
            if (ev == "PlayerMoveEvent" || ev == "PlayerJumpEvent" || ev == "PlayerTeleportEvent" ||
                ev == "PlayerPortalEvent") {
                auto *move = static_cast<PlayerMoveEvent *>(event);
                return copyLocation(name == "from" ? move->getFrom() : move->getTo());
            }
            if (ev == "ActorTeleportEvent") {
                auto *teleport = static_cast<ActorTeleportEvent *>(event);
                return copyLocation(name == "from" ? teleport->getFrom() : teleport->getTo());
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        if (name == "location" && ev == "ActorExplodeEvent") {
            owned_locations_.push_back(static_cast<ActorExplodeEvent *>(event)->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        // Items an event hands out. These are live references, so changes reach the world directly.
        if (name == "item") {
            if (ev == "PlayerPickupItemEvent") {
                *out = track(&static_cast<PlayerPickupItemEvent *>(event)->getItem(), Kind::Item);
                return ESN_OK;
            }
            if (ev == "PlayerDropItemEvent") {
                auto &drop = const_cast<ItemStack &>(static_cast<PlayerDropItemEvent *>(event)->getItem());
                *out = track(&drop, Kind::ItemStack);
                return ESN_OK;
            }
            if (ev == "PlayerItemConsumeEvent") {
                auto &item = const_cast<ItemStack &>(static_cast<PlayerItemConsumeEvent *>(event)->getItem());
                *out = track(&item, Kind::ItemStack);
                return ESN_OK;
            }
            if (ev == "PlayerInteractEvent") {
                auto *interact = static_cast<PlayerInteractEvent *>(event);
                if (!interact->hasItem()) {
                    return ESN_ERR_NO_SUCH_MEMBER;  // absent, so it reads as undefined
                }
                auto &item = const_cast<ItemStack &>(*interact->getItem());
                *out = track(&item, Kind::ItemStack);
                return ESN_OK;
            }
        }
        if (ev == "BlockCookEvent") {
            auto *cook = static_cast<BlockCookEvent *>(event);
            if (name == "source") {
                *out = track(&const_cast<ItemStack &>(cook->getSource()), Kind::ItemStack);
                return ESN_OK;
            }
            if (name == "result") {
                // Write-through: setting a property puts the changed stack back as the cooked result.
                *out = trackOwnedItem(cook->getResult(),
                                      [cook](const ItemStack &changed) { cook->setResult(changed); });
                return ESN_OK;
            }
        }
        // The actor on the far side of an interaction, and who caused a knockback.
        if (name == "actor" && ev == "PlayerInteractActorEvent") {
            *out = trackActor(&static_cast<PlayerInteractActorEvent *>(event)->getActor());
            return ESN_OK;
        }
        if (name == "source" && ev == "ActorKnockbackEvent") {
            auto *source = static_cast<ActorKnockbackEvent *>(event)->getSource();
            *out = source ? trackActor(source) : 0;
            return ESN_OK;
        }
        if (name == "knockback" && ev == "ActorKnockbackEvent") {
            owned_vectors_.push_back(static_cast<ActorKnockbackEvent *>(event)->getKnockback());
            *out = track(&owned_vectors_.back(), Kind::Vector);
            return ESN_OK;
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
        // What the player was holding as the action happened. Events do not carry it, so it is read
        // from their main hand at dispatch time - which, because handlers run synchronously before the
        // action is applied, is the item that was in use. Absent rather than null when the hand is
        // empty, so `if (event.heldItem)` reads naturally.
        if (name == "heldItem") {
            auto *player = eventPlayer(event);
            if (!player) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            auto &inventory = player->getInventory();
            auto item = inventory.getItemInMainHand();
            if (!item) {
                return ESN_ERR_NO_SUCH_MEMBER;
            }
            *out = trackOwnedItem(std::move(*item), [&inventory](const endstone::ItemStack &changed) {
                inventory.setItemInMainHand(changed);
            });
            return ESN_OK;
        }
        if (name == "map" && event->getEventName() == "MapInitializeEvent") {
            *out = track(&static_cast<MapInitializeEvent *>(event)->getMap(), Kind::MapView);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
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
    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
        if (name == "level") { *out = track(&player->getLevel(), Kind::Level); return ESN_OK; }
        // A player's own scoreboard, which may differ from the server's main one.
        if (name == "scoreboard") { *out = track(&player->getScoreboard(), Kind::Scoreboard, true); return ESN_OK; }
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
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item)); item && name == "itemStack") {
        // getItemStack hands back a copy, so it is paired with a writeback like an inventory slot.
        *out = trackOwnedItem(item->getItemStack(),
                              [item](const endstone::ItemStack &changed) { item->setItemStack(changed); });
        return ESN_OK;
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "location") {
            owned_locations_.push_back(block->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *state = static_cast<BlockState *>(resolve(target, Kind::BlockState))) {
        if (name == "location") {
            owned_locations_.push_back(state->getLocation());
            *out = track(&owned_locations_.back(), Kind::Location);
            return ESN_OK;
        }
        // The live block at the snapshot's position, which is not necessarily what the snapshot holds.
        if (name == "block") {
            auto live = state->getBlock();
            if (!live) {
                *out = 0;
                return ESN_OK;
            }
            auto *raw = live.get();
            owned_blocks_.push_back(std::move(live));
            *out = track(raw, Kind::Block);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *dimension = static_cast<Dimension *>(resolve(target, Kind::Dimension))) {
        if (name == "level") { *out = track(&dimension->getLevel(), Kind::Level, true); return ESN_OK; }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *inventory = resolveInventory(target)) {
        // The equipment slots, which only a player inventory has.
        auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory));
        if (player_inventory) {
            using Setter = void (PlayerInventory::*)(std::optional<endstone::ItemStack>);
            const auto equipment = [&](std::optional<endstone::ItemStack> item, Setter setter) -> esn_status {
                if (!item) {
                    *out = 0;  // an empty slot reads as null
                    return ESN_OK;
                }
                *out = trackOwnedItem(std::move(*item),
                                      [player_inventory, setter](const endstone::ItemStack &changed) {
                                          (player_inventory->*setter)(changed);
                                      });
                return ESN_OK;
            };
            if (name == "helmet") { return equipment(player_inventory->getHelmet(), &PlayerInventory::setHelmet); }
            if (name == "chestplate") {
                return equipment(player_inventory->getChestplate(), &PlayerInventory::setChestplate);
            }
            if (name == "leggings") {
                return equipment(player_inventory->getLeggings(), &PlayerInventory::setLeggings);
            }
            if (name == "boots") { return equipment(player_inventory->getBoots(), &PlayerInventory::setBoots); }
            if (name == "itemInMainHand") {
                return equipment(player_inventory->getItemInMainHand(), &PlayerInventory::setItemInMainHand);
            }
            if (name == "itemInOffHand") {
                return equipment(player_inventory->getItemInOffHand(), &PlayerInventory::setItemInOffHand);
            }
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
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "unbreakable") {
            editMeta(*stack, [&](ItemMeta &meta) { meta.setUnbreakable(value); });
            persistItem(target);
            return ESN_OK;
        }
        if (const auto key = tagKeyOf(name)) {
            // NBT has no boolean type; a byte is the convention, and reads back as 0 or 1.
            editNbt(*stack, [&](CompoundTag &nbt) {
                nbt[*key] = ByteTag{static_cast<std::uint8_t>(value ? 1 : 0)};
            });
            persistItem(target);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *item = static_cast<Item *>(resolve(target, Kind::Item)); item && name == "unlimitedLifetime") {
        item->setUnlimitedLifetime(value);
        return ESN_OK;
    }
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
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        if (event->getEventName() == "ServerListPingEvent") {
            auto *ping = static_cast<ServerListPingEvent *>(event);
            if (name == "numPlayers") { ping->setNumPlayers(static_cast<int>(value)); return ESN_OK; }
            if (name == "maxPlayers") { ping->setMaxPlayers(static_cast<int>(value)); return ESN_OK; }
            if (name == "localPort") { ping->setLocalPort(static_cast<int>(value)); return ESN_OK; }
            if (name == "localPortV6") { ping->setLocalPortV6(static_cast<int>(value)); return ESN_OK; }
        }
        return ESN_ERR_NO_SUCH_MEMBER;
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
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        // Writes reach the world only for a stack the server handed out live, such as the one on
        // PlayerDropItemEvent. A stack read out of an inventory is a copy - change the inventory.
        if (name == "mapId") {
            if (!editMetaAs<MapMeta>(*stack, [&](MapMeta &map) { map.setMapId(value); })) {
                return ESN_ERR_WRONG_TYPE;
            }
            persistItem(target);
            return ESN_OK;
        }
        if (name == "amount") { stack->setAmount(static_cast<int>(value)); persistItem(target); return ESN_OK; }
        if (name == "data") { stack->setData(static_cast<int>(value)); persistItem(target); return ESN_OK; }
        if (name == "damage" || name == "repairCost") {
            const auto amount = static_cast<int>(value);
            editMeta(*stack, [&](ItemMeta &meta) {
                if (name == "damage") {
                    meta.setDamage(amount);
                }
                else {
                    meta.setRepairCost(amount);
                }
            });
            persistItem(target);
            return ESN_OK;
        }
        if (const auto key = tagKeyOf(name)) {
            // Stored as a long so a JavaScript integer never silently narrows.
            editNbt(*stack, [&](CompoundTag &nbt) { nbt[*key] = LongTag{value}; });
            persistItem(target);
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
    if (auto *bar = static_cast<BossBar *>(resolve(target, Kind::BossBar))) {
        // Endstone clamps nothing, and a value outside 0..1 makes the client draw a bar wider than its
        // frame, so it is clamped here rather than in JavaScript where a plugin could skip it.
        if (name == "progress") {
            bar->setProgress(static_cast<float>(value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value));
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (const auto key = tagKeyOf(name)) {
            editNbt(*stack, [&](CompoundTag &nbt) { nbt[*key] = DoubleTag{value}; });
            persistItem(target);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
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
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        if (name == "type") { stack->setType(std::string{value}); persistItem(target); return ESN_OK; }
        // An empty string clears the custom name, which is what setDisplayName(nullopt) does.
        if (name == "displayName") {
            editMeta(*stack, [&](ItemMeta &meta) {
                meta.setDisplayName(value.empty() ? std::nullopt : std::optional{std::string{value}});
            });
            persistItem(target);
            return ESN_OK;
        }
        // Newline-joined on the way in as well; empty clears it.
        if (name == "loreList") {
            std::vector<std::string> lines;
            for (std::size_t start = 0; start <= value.size() && !value.empty();) {
                const auto end = value.find('\n', start);
                lines.emplace_back(value.substr(start, end == std::string_view::npos ? end : end - start));
                if (end == std::string_view::npos) {
                    break;
                }
                start = end + 1;
            }
            editMeta(*stack, [&](ItemMeta &meta) {
                meta.setLore(lines.empty() ? std::nullopt : std::optional{lines});
            });
            persistItem(target);
            return ESN_OK;
        }
        // Written-book metadata. An empty string clears the field, as displayName does.
        if (name == "title" || name == "author" || name == "generation") {
            const auto text = std::string{value};
            const auto changed = editMetaAs<BookMeta>(*stack, [&](BookMeta &book) {
                if (name == "title") {
                    book.setTitle(text.empty() ? std::nullopt : std::optional{text});
                }
                else if (name == "author") {
                    book.setAuthor(text.empty() ? std::nullopt : std::optional{text});
                }
                else {
                    book.setGeneration(generationFromName(text));
                }
            });
            if (!changed) {
                return ESN_ERR_WRONG_TYPE;
            }
            persistItem(target);
            return ESN_OK;
        }
        if (name == "pageList") {
            std::vector<std::string> pages;
            if (!value.empty()) {
                for (const auto &page : splitOn(value, '\n')) {
                    pages.push_back(page);
                }
            }
            if (!editMetaAs<WritableBookMeta>(*stack, [&](WritableBookMeta &book) { book.setPages(pages); })) {
                return ESN_ERR_WRONG_TYPE;
            }
            persistItem(target);
            return ESN_OK;
        }
        if (const auto key = tagKeyOf(name)) {
            editNbt(*stack, [&](CompoundTag &nbt) { nbt[*key] = StringTag{std::string{value}}; });
            persistItem(target);
            return ESN_OK;
        }
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
        if (name == "format" && event->getEventName() == "PlayerChatEvent") {
            static_cast<PlayerChatEvent *>(event)->setFormat(std::string{value});
            return ESN_OK;
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
        const auto ev = event->getEventName();
        if (name == "kickMessage" && ev == "PlayerLoginEvent") {
            static_cast<PlayerLoginEvent *>(event)->setKickMessage(std::string{value});
            return ESN_OK;
        }
        if (name == "reason" && ev == "PlayerKickEvent") {
            static_cast<PlayerKickEvent *>(event)->setReason(std::string{value});
            return ESN_OK;
        }
        if (name == "message" && ev == "BroadcastMessageEvent") {
            static_cast<BroadcastMessageEvent *>(event)->setMessage(Message{std::string{value}});
            return ESN_OK;
        }
        if (ev == "ServerListPingEvent") {
            auto *ping = static_cast<ServerListPingEvent *>(event);
            if (name == "motd") { ping->setMotd(std::string{value}); return ESN_OK; }
            if (name == "levelName") { ping->setLevelName(std::string{value}); return ESN_OK; }
            if (name == "serverGuid") { ping->setServerGuid(std::string{value}); return ESN_OK; }
            if (name == "minecraftVersion") {
                ping->setMinecraftVersionNetwork(std::string{value});
                return ESN_OK;
            }
            if (name == "gameMode") {
                const auto mode = gameModeFromName(value);
                if (!mode) {
                    return ESN_ERR_BAD_ARGUMENT;
                }
                ping->setGameMode(*mode);
                return ESN_OK;
            }
        }
        if (name == "deathMessage" && event->getEventName() == "PlayerDeathEvent") {
            // An empty string suppresses the announcement, matching join and quit messages.
            auto *death = static_cast<PlayerDeathEvent *>(event);
            death->setDeathMessage(value.empty() ? std::optional<Message>{} : Message{std::string{value}});
            return ESN_OK;
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
        if (name == "blockStatesList") {
            const auto current = block->getData();
            BlockStates states = current ? current->getBlockStates() : BlockStates{};
            parseBlockStates(value, states);
            const auto data = plugin_.getServer().createBlockData(block->getType(), states);
            if (!data) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            block->setData(*data);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *state = static_cast<BlockState *>(resolve(target, Kind::BlockState))) {
        // Only changes the snapshot; update() is what writes it to the world.
        if (name == "type") { state->setType(std::string{value}); return ESN_OK; }
        if (name == "blockStatesList") {
            const auto current = state->getData();
            BlockStates states = current ? current->getBlockStates() : BlockStates{};
            parseBlockStates(value, states);
            const auto data = plugin_.getServer().createBlockData(state->getType(), states);
            if (!data) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            state->setData(*data);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    return find(target) ? ESN_ERR_NO_SUCH_MEMBER : ESN_ERR_STALE_HANDLE;
}

/**
 * @brief addPermission(node, value) / removePermission(node) / recalculatePermissions().
 *
 * A granted permission is an attachment owned by the permissible, and Endstone hands its pointer
 * back. That pointer is not retained here: keeping one past the callback that made it is exactly the
 * lifetime mistake handles exist to prevent, and a player who disconnects would leave it dangling.
 * removePermission therefore overrides the node to false rather than detaching, which is what gating
 * actually needs; the attachment itself goes when the player does.
 */
esn_status permissibleInvoke(Plugin &owner, Permissible &who, const std::string_view name,
                             const std::string &text, const std::function<double(std::size_t, double)> &number)
{
    if (name == "addPermission" || name == "removePermission") {
        if (text.empty()) {
            return ESN_ERR_BAD_ARGUMENT;
        }
        const bool value = name == "addPermission" ? number(0, 1) != 0 : false;
        (void)who.addAttachment(owner, text, value);
        who.recalculatePermissions();
        return ESN_OK;
    }
    if (name == "recalculatePermissions") {
        who.recalculatePermissions();
        return ESN_OK;
    }
    return ESN_ERR_NO_SUCH_MEMBER;
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

    if (auto *player = static_cast<Player *>(resolve(target, Kind::Player))) {
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
        // Both take another object, which is what handle arguments are for.
        if (name == "sendMap") {
            auto *map = static_cast<MapView *>(resolve(handle_at(0), Kind::MapView));
            if (!map) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            player->sendMap(*map);
            return ESN_OK;
        }
        // Backend for the `scoreboard` property: the ABI's setters carry only scalars, so a
        // handle-valued write goes through a method the same way `rotation` does.
        if (name == "setScoreboard") {
            auto *board = static_cast<Scoreboard *>(resolve(handle_at(0), Kind::Scoreboard));
            if (!board) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            player->setScoreboard(*board);
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
        if (const auto status = permissibleInvoke(plugin_, *player, name, text, number);
            status != ESN_ERR_NO_SUCH_MEMBER) {
            return status;
        }
        // Everything Actor offers, so `Player extends Actor` in the types is not a lie.
        return actorInvoke(*player, name, text, str, number);
    }
    if (auto *event = static_cast<Event *>(resolve(target, Kind::Event))) {
        const auto ev = event->getEventName();
        // Backend for `event.knockback = { x, y, z }`, routed as a method because the ABI's setters
        // only carry scalars.
        if (name == "setKnockback" && ev == "ActorKnockbackEvent") {
            static_cast<ActorKnockbackEvent *>(event)->setKnockback(
                Vector{number(0), number(1), number(2)});
            return ESN_OK;
        }
        // setFrom / setTo - where a move, teleport or portal is coming from and going to. Routed as a
        // method because a Location is six numbers plus a dimension and the ABI's setters carry one
        // scalar each. Anything not supplied keeps the event's current value, so assigning a bare
        // { x, y, z } redirects the destination without disturbing facing or dimension.
        if (name == "setFrom" || name == "setTo") {
            const auto place = [&](const Location &current) -> std::optional<Location> {
                auto *dimension = &current.getDimension();
                if (const auto requested = str(0); !requested.empty()) {
                    dimension = current.getDimension().getLevel().getDimension(requested);
                    if (!dimension) {
                        return std::nullopt;
                    }
                }
                return Location{*dimension,
                                static_cast<float>(number(0, current.getX())),
                                static_cast<float>(number(1, current.getY())),
                                static_cast<float>(number(2, current.getZ())),
                                static_cast<float>(number(4, current.getPitch())),
                                static_cast<float>(number(3, current.getYaw()))};
            };
            // PlayerJumpEvent, PlayerTeleportEvent and PlayerPortalEvent all derive from
            // PlayerMoveEvent, which is what the matching getters already rely on.
            if (ev == "PlayerMoveEvent" || ev == "PlayerJumpEvent" || ev == "PlayerTeleportEvent" ||
                ev == "PlayerPortalEvent") {
                auto *move = static_cast<PlayerMoveEvent *>(event);
                const auto updated = place(name == "setFrom" ? move->getFrom() : move->getTo());
                if (!updated) {
                    return ESN_ERR_BAD_ARGUMENT;
                }
                if (name == "setFrom") {
                    move->setFrom(*updated);
                }
                else {
                    move->setTo(*updated);
                }
                return ESN_OK;
            }
            if (ev == "ActorTeleportEvent") {
                auto *teleport = static_cast<ActorTeleportEvent *>(event);
                const auto updated = place(name == "setFrom" ? teleport->getFrom() : teleport->getTo());
                if (!updated) {
                    return ESN_ERR_BAD_ARGUMENT;
                }
                if (name == "setFrom") {
                    teleport->setFrom(*updated);
                }
                else {
                    teleport->setTo(*updated);
                }
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        // The blocks an explosion is about to destroy, by index. Removing one from the list is not
        // exposed; cancel the event instead.
        if (name == "getExplodedBlock" && out_handle) {
            const auto index = static_cast<std::size_t>(number(0));
            if (ev == "ActorExplodeEvent") {
                auto &blocks = static_cast<ActorExplodeEvent *>(event)->getBlockList();
                if (index >= blocks.size() || !blocks[index]) {
                    *out_handle = 0;
                    return ESN_OK;
                }
                *out_handle = track(blocks[index].get(), Kind::Block);
                return ESN_OK;
            }
            if (ev == "BlockExplodeEvent") {
                auto &blocks = static_cast<BlockExplodeEvent *>(event)->getBlockList();
                if (index >= blocks.size() || !blocks[index]) {
                    *out_handle = 0;
                    return ESN_OK;
                }
                *out_handle = track(blocks[index].get(), Kind::Block);
                return ESN_OK;
            }
            return ESN_ERR_NO_SUCH_MEMBER;
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
    if (auto *sender = static_cast<CommandSender *>(resolve(target, Kind::CommandSender))) {
        if (name == "sendMessage") { sender->sendMessage(Message{std::string{text}}); return ESN_OK; }
        if (name == "sendErrorMessage") { sender->sendErrorMessage(Message{std::string{text}}); return ESN_OK; }
        return permissibleInvoke(plugin_, *sender, name, text, number);
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
            const auto slot = static_cast<int>(number(0));
            auto item = inventory->getItem(slot);
            if (!item) {
                *out_handle = 0;
                return ESN_OK;
            }
            *out_handle = trackOwnedItem(std::move(*item), [inventory, slot](const endstone::ItemStack &changed) {
                inventory->setItem(slot, changed);
            });
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
        if (name == "removeStack") {
            auto *against = static_cast<ItemStack *>(resolve(handle_at(0), Kind::ItemStack));
            if (!against) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            inventory->remove(*against);
            return ESN_OK;
        }
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
        // Everything below needs a PlayerInventory, so it all lives under the one guard - the nesting
        // is what says which type owns a method, and scripts/check_methods.py reads it that way.
        if (auto *player_inventory = static_cast<PlayerInventory *>(resolve(target, Kind::PlayerInventory))) {
            if (name == "setHeldItemSlot") {
                player_inventory->setHeldItemSlot(static_cast<int>(number(0)));
                return ESN_OK;
            }
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
        if (const auto status = permissibleInvoke(plugin_, *actor, name, text, number);
            status != ESN_ERR_NO_SUCH_MEMBER) {
            return status;
        }
        return actorInvoke(*actor, name, text, str, number);
    }
    if (auto *block = static_cast<Block *>(resolve(target, Kind::Block))) {
        if (name == "getRelative" && out_handle) {
            // A face plus an optional distance, or a plain offset. The interact event already reports
            // a face as a string, so the natural block.getRelative(event.blockFace) works.
            auto relative = !text.empty() ? [&] {
                const auto face = blockFaceFromName(text);
                return face ? block->getRelative(*face, static_cast<int>(number(0, 1)))
                            : std::unique_ptr<Block>{};
            }()
                                          : block->getRelative(static_cast<int>(number(0)),
                                                               static_cast<int>(number(1)),
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
        // A snapshot of this position, detached from the world. Change its type, then update() to write
        // it back - which is how you restore a block later, or stage a change and apply it.
        // A second handle on the same position, released with this dispatch like any other Block.
        if (name == "clone" && out_handle) {
            auto copy = block->clone();
            if (!copy) {
                return ESN_ERR_INTERNAL;
            }
            auto *raw = copy.get();
            owned_blocks_.push_back(std::move(copy));
            *out_handle = track(raw, Kind::Block);
            return ESN_OK;
        }
        if (name == "captureState" && out_handle) {
            auto state = block->captureState();
            if (!state) {
                return ESN_ERR_INTERNAL;
            }
            auto *raw = state.get();
            owned_block_states_.push_back(std::move(state));
            *out_handle = track(raw, Kind::BlockState);
            return ESN_OK;
        }
        return ESN_ERR_NO_SUCH_MEMBER;
    }
    if (auto *state = static_cast<BlockState *>(resolve(target, Kind::BlockState))) {
        // update(force, applyPhysics) - writes the snapshot back to the world. Without force it only
        // applies if the position is still the type it was captured as, so a racing change is not
        // clobbered; applyPhysics lets neighbours react, e.g. sand falling.
        if (name == "update") {
            const auto force = number(0, 0) != 0;
            const auto physics = number(1, 0) != 0;
            (void)state->update(force, physics);
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
    if (auto *stack = static_cast<ItemStack *>(resolve(target, Kind::ItemStack))) {
        // Deleting a custom data key. Unlike reading and writing one this cannot ride the accessor
        // name, because there is no "set to nothing" - so it is the one tag operation that is a method.
        if (name == "removeTag") {
            editNbt(*stack, [&](CompoundTag &nbt) { (void)nbt.erase(text); });
            persistItem(target);
            return ESN_OK;
        }
        // addEnchant(id, level) - forced, so a level above vanilla's maximum is allowed, which is the
        // point of doing it from a plugin rather than an anvil.
        if (name == "addEnchant") {
            if (text.empty()) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            const auto level = static_cast<int>(number(0, 1));
            editMeta(*stack, [&](ItemMeta &meta) { (void)meta.addEnchant(EnchantmentId{text}, level, true); });
            persistItem(target);
            return ESN_OK;
        }
        if (name == "removeEnchant") {
            editMeta(*stack, [&](ItemMeta &meta) { (void)meta.removeEnchant(EnchantmentId{text}); });
            persistItem(target);
            return ESN_OK;
        }
        if (name == "removeEnchants") {
            editMeta(*stack, [&](ItemMeta &meta) { meta.removeEnchants(); });
            persistItem(target);
            return ESN_OK;
        }
        // setMapView(map) - what turns a blank map item into the one server.createMap() made. Takes a
        // handle rather than an id because that is the object a plugin already has.
        if (name == "setMapView") {
            auto *map = static_cast<MapView *>(resolve(handle_at(0), Kind::MapView));
            if (!map) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            if (!editMetaAs<MapMeta>(*stack, [&](MapMeta &meta) { meta.setMapView(map); })) {
                return ESN_ERR_WRONG_TYPE;
            }
            persistItem(target);
            return ESN_OK;
        }
        if (name == "addPage") {
            if (!editMetaAs<WritableBookMeta>(*stack,
                                              [&](WritableBookMeta &book) { book.addPage({text}); })) {
                return ESN_ERR_WRONG_TYPE;
            }
            persistItem(target);
            return ESN_OK;
        }
        // addChargedProjectile(type, amount, data) - described like any other item argument.
        if (name == "addChargedProjectile") {
            if (text.empty()) {
                return ESN_ERR_BAD_ARGUMENT;
            }
            const endstone::ItemStack projectile{text, static_cast<int>(number(0, 1)),
                                                 static_cast<int>(number(1, 0))};
            if (!editMetaAs<CrossbowMeta>(
                    *stack, [&](CrossbowMeta &crossbow) { crossbow.addChargedProjectile(projectile); })) {
                return ESN_ERR_WRONG_TYPE;
            }
            persistItem(target);
            return ESN_OK;
        }
        // A detached copy: no writeback, so changing it cannot reach the slot the original came from.
        // That is the point - it is how you keep an item's contents past the callback.
        if (name == "clone" && out_handle) {
            *out_handle = trackOwnedItem(*stack, nullptr);
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
    if (resolve(target, Kind::Server)) {
        return emitString("Server", buf, cap, needed);
    }
    if (resolve(target, Kind::Dimension)) {
        return emitString("Dimension", buf, cap, needed);
    }
    if (resolve(target, Kind::Scoreboard)) {
        return emitString("Scoreboard", buf, cap, needed);
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
    api.subscribe = tSubscribe;
    api.unsubscribe = tUnsubscribe;
    api.send_form = tSendForm;
    api.close_form = tCloseForm;
    api.send_packet = tSendPacket;
    api.update_commands = tUpdateCommands;
    api.schedule_task = tScheduleTask;
    api.cancel_task = tCancelTask;
}

}  // namespace endstone::node
