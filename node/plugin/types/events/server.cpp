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

// Binds include/endstone/event/server/.

#include <endstone/player.h>
#include <endstone/plugin/plugin.h>

#include <endstone/event/server/broadcast_message_event.h>
#include <endstone/event/server/map_initialize_event.h>
#include <endstone/event/server/packet_receive_event.h>
#include <endstone/event/server/packet_send_event.h>
#include <endstone/event/server/plugin_disable_event.h>
#include <endstone/event/server/plugin_enable_event.h>
#include <endstone/event/server/script_message_event.h>
#include <endstone/event/server/server_command_event.h>
#include <endstone/event/server/server_event.h>
#include <endstone/event/server/server_list_ping_event.h>
#include <endstone/event/server/server_load_event.h>

#include "types/bind.h"
#include "types/events/message.h"

namespace endstone::node {

namespace {

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

std::optional<GameMode> gameModeFromName(const std::string_view name)
{
    if (name == "survival") return GameMode::Survival;
    if (name == "creative") return GameMode::Creative;
    if (name == "adventure") return GameMode::Adventure;
    if (name == "spectator") return GameMode::Spectator;
    return std::nullopt;
}

/** Packet events share their whole surface, so it is declared once and applied to both. */
template <typename T>
void bindPacketEvent(TypeBuilder<T> &b)
{
    b.cancellable();
    b.ro("packetId", &T::getPacketId);
    b.ro("subClientId", &T::getSubClientId);
    b.ro("address", [](const T &event) { return event.getAddress().getHostname(); });
    b.ro("port", [](const T &event) { return static_cast<std::int64_t>(event.getAddress().getPort()); });
    // Genuinely null before login, so this reads as null rather than as a missing member.
    b.ro("player", &T::getPlayer);
    // A payload is bytes, not text: the generic string accessor would replace every byte that is not
    // valid UTF-8 with U+FFFD, so it crosses on the binary one instead.
    b.bytes("payload", [](const T &event) { return std::string{event.getPayload()}; },
            [](T &event, const std::string &payload) { event.setPayload(payload); });
}

}  // namespace

ESN_EVENT_BASE(ServerEvent, Event) {}

ESN_EVENT(ServerLoadEvent, ServerEvent)
{
    b.ro("loadType", [](const ServerLoadEvent &event) {
        return std::string{event.getType() == ServerLoadEvent::LoadType::Reload ? "reload" : "startup"};
    });
}

ESN_EVENT(ServerCommandEvent, ServerEvent)
{
    b.cancellable();
    b.rw("command", &ServerCommandEvent::getCommand, &ServerCommandEvent::setCommand);
    b.ro("sender", &ServerCommandEvent::getSender);
}

ESN_EVENT(BroadcastMessageEvent, ServerEvent)
{
    b.cancellable();
    // Recipients are a copy upstream, so they cannot be filtered from here; cancel instead.
    b.rw("message", [](const BroadcastMessageEvent &event) { return optionalMessageText(event.getMessage()); },
         [](BroadcastMessageEvent &event, const Value &in, Binder &) {
             event.setMessage(Message{in.text});
             return static_cast<esn_status>(ESN_OK);
         });
    b.ro("recipientCount",
         [](const BroadcastMessageEvent &event) { return static_cast<std::int64_t>(event.getRecipients().size()); });
}

ESN_EVENT(ScriptMessageEvent, ServerEvent)
{
    b.cancellable();
    b.ro("messageId", &ScriptMessageEvent::getMessageId);
    // Named scriptMessage rather than message so it does not collide with the broadcast one.
    b.ro("scriptMessage", &ScriptMessageEvent::getMessage);
    b.ro("sender", [](const ScriptMessageEvent &event) { return const_cast<CommandSender *>(&event.getSender()); });
}

ESN_EVENT(MapInitializeEvent, ServerEvent)
{
    b.ro("map", &MapInitializeEvent::getMap);
}

ESN_EVENT(PluginEnableEvent, ServerEvent)
{
    b.ro("plugin", &PluginEnableEvent::getPlugin);
}

ESN_EVENT(PluginDisableEvent, ServerEvent)
{
    b.ro("plugin", &PluginDisableEvent::getPlugin);
}

ESN_EVENT(PacketReceiveEvent, ServerEvent)
{
    bindPacketEvent(b);
}

ESN_EVENT(PacketSendEvent, ServerEvent)
{
    bindPacketEvent(b);
}

// The entry a client sees in its server list, with every field writable.
ESN_EVENT(ServerListPingEvent, ServerEvent)
{
    b.cancellable();
    b.ro("address", [](const ServerListPingEvent &event) { return event.getAddress().getHostname(); });
    b.ro("protocolVersion", &ServerListPingEvent::getNetworkProtocolVersion);
    b.rw("motd", &ServerListPingEvent::getMotd, &ServerListPingEvent::setMotd);
    b.rw("levelName", &ServerListPingEvent::getLevelName, &ServerListPingEvent::setLevelName);
    b.rw("serverGuid", &ServerListPingEvent::getServerGuid, &ServerListPingEvent::setServerGuid);
    b.rw("minecraftVersion", &ServerListPingEvent::getMinecraftVersionNetwork,
         &ServerListPingEvent::setMinecraftVersionNetwork);
    b.rw("numPlayers", &ServerListPingEvent::getNumPlayers, &ServerListPingEvent::setNumPlayers);
    b.rw("maxPlayers", &ServerListPingEvent::getMaxPlayers, &ServerListPingEvent::setMaxPlayers);
    b.rw("localPort", &ServerListPingEvent::getLocalPort, &ServerListPingEvent::setLocalPort);
    b.rw("localPortV6", &ServerListPingEvent::getLocalPortV6, &ServerListPingEvent::setLocalPortV6);
    b.rw("gameMode",
         [](const ServerListPingEvent &event) { return std::string{gameModeName(event.getGameMode())}; },
         [](ServerListPingEvent &event, const Value &in, Binder &) {
             const auto mode = gameModeFromName(in.text);
             if (!mode) {
                 return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
             }
             event.setGameMode(*mode);
             return static_cast<esn_status>(ESN_OK);
         });
}

}  // namespace endstone::node
