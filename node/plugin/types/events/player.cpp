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

// Binds include/endstone/event/player/ - the biggest folder upstream, and the one that keeps growing.

#include <endstone/actor/item.h>
#include <endstone/inventory/item_stack.h>
#include <endstone/player.h>

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

#include "types/bind.h"
#include "types/block_face.h"
#include "types/events/message.h"
#include "types/events/place.h"

namespace endstone::node {

namespace {

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

std::string_view gameModeName(const GameMode mode)
{
    switch (mode) {
    case GameMode::Survival: return "survival";
    case GameMode::Creative: return "creative";
    case GameMode::Adventure: return "adventure";
    case GameMode::Spectator: return "spectator";
    }
    return "survival";
}

}  // namespace

ESN_EVENT_BASE(PlayerEvent, Event)
{
    b.ro("player", &PlayerEvent::getPlayer);
}

ESN_EVENT(PlayerJoinEvent, PlayerEvent)
{
    // An empty string suppresses the announcement.
    b.rw("joinMessage", [](const PlayerJoinEvent &event) { return optionalMessageText(event.getJoinMessage()); },
         [](PlayerJoinEvent &event, const Value &in, Binder &) {
             event.setJoinMessage(optionalMessage(in.text));
             return static_cast<esn_status>(ESN_OK);
         });
}

ESN_EVENT(PlayerQuitEvent, PlayerEvent)
{
    b.rw("quitMessage", [](const PlayerQuitEvent &event) { return optionalMessageText(event.getQuitMessage()); },
         [](PlayerQuitEvent &event, const Value &in, Binder &) {
             event.setQuitMessage(optionalMessage(in.text));
             return static_cast<esn_status>(ESN_OK);
         });
}

ESN_EVENT(PlayerRespawnEvent, PlayerEvent) {}

ESN_EVENT(PlayerChatEvent, PlayerEvent)
{
    b.cancellable();
    b.rw("message", &PlayerChatEvent::getMessage, &PlayerChatEvent::setMessage);
    // Rewriting the format is how a plugin adds a prefix without cancelling and re-broadcasting.
    b.rw("format", &PlayerChatEvent::getFormat, &PlayerChatEvent::setFormat);
    // Who will see it, as names. getRecipients hands back a copy, so this is an observation rather
    // than something a handler can filter.
    b.ro("recipientNameList", [](const PlayerChatEvent &event) {
        std::string joined;
        for (const auto *recipient : event.getRecipients()) {
            if (recipient == nullptr) {
                continue;
            }
            if (!joined.empty()) {
                joined += '\n';
            }
            joined += recipient->getName();
        }
        return joined;
    });
}

ESN_EVENT(PlayerCommandEvent, PlayerEvent)
{
    b.cancellable();
    b.rw("command", &PlayerCommandEvent::getCommand, &PlayerCommandEvent::setCommand);
}

ESN_EVENT(PlayerKickEvent, PlayerEvent)
{
    b.cancellable();
    b.rw("reason", &PlayerKickEvent::getReason, &PlayerKickEvent::setReason);
}

ESN_EVENT(PlayerLoginEvent, PlayerEvent)
{
    b.cancellable();
    // Setting this and cancelling is how a plugin refuses a join with a reason.
    b.rw("kickMessage", &PlayerLoginEvent::getKickMessage, &PlayerLoginEvent::setKickMessage);
}

ESN_EVENT(PlayerEmoteEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("emoteId", &PlayerEmoteEvent::getEmoteId);
    b.rw("isMuted", &PlayerEmoteEvent::isMuted, &PlayerEmoteEvent::setMuted);
}

ESN_EVENT(PlayerGameModeChangeEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("newGameMode",
         [](const PlayerGameModeChangeEvent &event) { return std::string{gameModeName(event.getNewGameMode())}; });
}

ESN_EVENT(PlayerSkinChangeEvent, PlayerEvent)
{
    b.cancellable();
    // The skin's pixels stay on the C++ side - an Image cannot cross usefully - but the ids identify
    // it, and the broadcast message is the part a plugin usually wants to reword or suppress.
    b.ro("skinId", [](const PlayerSkinChangeEvent &event) { return event.getNewSkin().getId(); });
    b.ro("capeId",
         [](const PlayerSkinChangeEvent &event) { return event.getNewSkin().getCapeId().value_or(std::string{}); });
    b.rw("skinChangeMessage",
         [](const PlayerSkinChangeEvent &event) { return optionalMessageText(event.getSkinChangeMessage()); },
         [](PlayerSkinChangeEvent &event, const Value &in, Binder &) {
             event.setSkinChangeMessage(optionalMessage(in.text));
             return static_cast<esn_status>(ESN_OK);
         });
}

ESN_EVENT(PlayerBedEnterEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("bed", &PlayerBedEnterEvent::getBed);
}

ESN_EVENT(PlayerBedLeaveEvent, PlayerEvent)
{
    b.ro("bed", &PlayerBedLeaveEvent::getBed);
}

ESN_EVENT(PlayerDimensionChangeEvent, PlayerEvent)
{
    b.ro("from", &PlayerDimensionChangeEvent::getFrom);
    b.ro("to", &PlayerDimensionChangeEvent::getTo);
}

ESN_EVENT(PlayerItemHeldEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("previousSlot", &PlayerItemHeldEvent::getPreviousSlot);
    b.ro("newSlot", &PlayerItemHeldEvent::getNewSlot);
}

// The stacks these carry are live references, so a change reaches the world directly.
ESN_EVENT(PlayerDropItemEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("item", [](PlayerDropItemEvent &event) { return const_cast<ItemStack *>(&event.getItem()); });
}

ESN_EVENT(PlayerItemConsumeEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("item", [](PlayerItemConsumeEvent &event) { return const_cast<ItemStack *>(&event.getItem()); });
    b.ro("hand",
         [](const PlayerItemConsumeEvent &event) { return std::string{equipmentSlotName(event.getHand())}; });
}

// A pickup carries the dropped-item actor rather than a bare stack, so the item on the ground can be
// inspected and changed before it is collected.
ESN_EVENT(PlayerPickupItemEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("item", &PlayerPickupItemEvent::getItem);
}

ESN_EVENT(PlayerInteractActorEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("actor", &PlayerInteractActorEvent::getActor);
}

ESN_EVENT(PlayerInteractEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("action", [](const PlayerInteractEvent &event) { return std::string{interactActionName(event.getAction())}; });
    b.ro("hasBlock", &PlayerInteractEvent::hasBlock);
    b.ro("hasItem", &PlayerInteractEvent::hasItem);
    // Null when the click was on air, which reads back as null rather than as a missing member.
    b.ro("block", &PlayerInteractEvent::getBlock);
    // Absent when empty-handed, so it reads as undefined - see the optional rule in bind.h.
    b.ro("item", [](PlayerInteractEvent &event) -> ItemStack * {
        return event.hasItem() ? const_cast<ItemStack *>(&*event.getItem()) : nullptr;
    });
    b.ro("blockFace",
         [](const PlayerInteractEvent &event) { return std::string{blockFaceName(event.getBlockFace())}; });
    b.ro("clickedPosition", &PlayerInteractEvent::getClickedPosition);
}

// Jump, teleport and portal all derive from a move, so from and to are declared once here.
ESN_EVENT(PlayerMoveEvent, PlayerEvent)
{
    b.cancellable();
    b.ro("from", &PlayerMoveEvent::getFrom);
    b.ro("to", &PlayerMoveEvent::getTo);
    bindPlaceSetters(b, &PlayerMoveEvent::getFrom, &PlayerMoveEvent::setFrom, &PlayerMoveEvent::getTo,
                     &PlayerMoveEvent::setTo);
}

ESN_EVENT(PlayerJumpEvent, PlayerMoveEvent) {}

ESN_EVENT(PlayerTeleportEvent, PlayerMoveEvent) {}

ESN_EVENT(PlayerPortalEvent, PlayerTeleportEvent) {}

}  // namespace endstone::node
