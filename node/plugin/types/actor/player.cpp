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

// Binds include/endstone/player.h. Everything Actor, Mob and CommandSender offer arrives through the
// base chain, so only what Player adds is declared here.

#include <endstone/inventory/player_inventory.h>
#include <endstone/map/map_view.h>
#include <endstone/player.h>
#include <endstone/scoreboard/scoreboard.h>

#include "types/bind.h"

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

}  // namespace

ESN_SUBTYPE(Player, Player, Mob)
{
    b.ro("uniqueId", [](const Player &player) { return player.getUniqueId().str(); });
    b.ro("xuid", &Player::getXuid);
    b.ro("locale", &Player::getLocale);
    b.ro("deviceOs", &Player::getDeviceOS);
    b.ro("deviceId", &Player::getDeviceId);
    b.ro("gameVersion", &Player::getGameVersion);

    // The hostname alone does not separate two players behind one NAT; the port does.
    b.ro("address", [](const Player &player) { return player.getAddress().getHostname(); });
    b.ro("port", [](const Player &player) { return static_cast<std::int64_t>(player.getAddress().getPort()); });
    b.ro("ping", [](const Player &player) { return static_cast<std::int64_t>(player.getPing().count()); });

    // Player has a real isOp, unlike the permission-level reading it inherits from CommandSender.
    b.rw("isOp", &Player::isOp, &Player::setOp);
    b.rw("isSneaking", &Player::isSneaking, &Player::setSneaking);
    b.rw("isSprinting", &Player::isSprinting, &Player::setSprinting);
    b.rw("isFlying", &Player::isFlying, &Player::setFlying);
    b.rw("allowFlight", &Player::getAllowFlight, &Player::setAllowFlight);

    b.rw("flySpeed", &Player::getFlySpeed, &Player::setFlySpeed);
    b.rw("walkSpeed", &Player::getWalkSpeed, &Player::setWalkSpeed);
    b.rw("expProgress", &Player::getExpProgress, &Player::setExpProgress);
    b.rw("expLevel", &Player::getExpLevel, &Player::setExpLevel);
    b.ro("totalExp", &Player::getTotalExp);

    b.rw("gameMode", [](const Player &player) { return std::string{gameModeName(player.getGameMode())}; },
         [](Player &player, const Value &in, Binder &) {
             const auto mode = gameModeFromName(in.text);
             if (!mode) {
                 return static_cast<esn_status>(ESN_ERR_WRONG_TYPE);
             }
             player.setGameMode(*mode);
             return static_cast<esn_status>(ESN_OK);
         });

    // The identity of a skin, not its pixels: an Image cannot cross the ABI usefully. The dimensions
    // are worth having, and 0 distinguishes "no cape" from a 0x0 one.
    b.ro("skinId", [](const Player &player) { return player.getSkin().getId(); });
    b.ro("capeId", [](const Player &player) { return player.getSkin().getCapeId().value_or(std::string{}); });
    b.ro("skinWidth", [](const Player &player) { return player.getSkin().getImage().getWidth(); });
    b.ro("skinHeight", [](const Player &player) { return player.getSkin().getImage().getHeight(); });
    b.ro("skinCapeWidth", [](const Player &player) {
        const auto *cape = player.getSkin().getCapeImage();
        return cape ? cape->getWidth() : 0;
    });
    b.ro("skinCapeHeight", [](const Player &player) {
        const auto *cape = player.getSkin().getCapeImage();
        return cape ? cape->getHeight() : 0;
    });

    b.ro("inventory", [](Player &player) { return &player.getInventory(); });
    b.ro("enderChest", [](Player &player) { return &player.getEnderChest(); });
    // The scoreboard this player sees, which may differ from the main one.
    b.ro("scoreboard", [](Player &player) { return &player.getScoreboard(); });

    b.method("sendPopup", &Player::sendPopup);
    b.method("sendTip", &Player::sendTip);
    b.method("kick", &Player::kick);
    b.method("performCommand", &Player::performCommand);
    b.method("updateCommands", &Player::updateCommands);
    b.method("giveExp", &Player::giveExp);
    b.method("giveExpLevels", &Player::giveExpLevels);
    b.method("stopSound", &Player::stopSound);
    b.method("stopAllSounds", &Player::stopAllSounds);
    b.method("resetTitle", &Player::resetTitle);

    b.method("transfer", [](Player &player, const Args &args, esn_handle *) {
        player.transfer(args.str(0), static_cast<int>(args.number(0, 19132)));
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("sendTitle", [](Player &player, const Args &args, esn_handle *) {
        player.sendTitle(args.str(0), args.str(1), static_cast<int>(args.number(0, 10)),
                         static_cast<int>(args.number(1, 70)), static_cast<int>(args.number(2, 20)));
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("sendToast", [](Player &player, const Args &args, esn_handle *) {
        player.sendToast(args.str(0), args.str(1));
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("playSound", [](Player &player, const Args &args, esn_handle *) {
        player.playSound(player.getLocation(), args.str(0), static_cast<float>(args.number(0, 1.0)),
                         static_cast<float>(args.number(1, 1.0)));
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("spawnParticle", [](Player &player, const Args &args, esn_handle *) {
        player.spawnParticle(args.str(0), static_cast<float>(args.number(0)), static_cast<float>(args.number(1)),
                             static_cast<float>(args.number(2)),
                             args.string_count > 1 ? std::optional<std::string>{args.str(1)} : std::nullopt);
        return static_cast<esn_status>(ESN_OK);
    });

    // Both take another object, which is what handle arguments are for.
    b.method("sendMap", [](Player &player, const Args &args, esn_handle *) {
        auto *map = static_cast<MapView *>(args.binder.resolve(args.handleAt(0), Kind::MapView));
        if (map == nullptr) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        player.sendMap(*map);
        return static_cast<esn_status>(ESN_OK);
    });
    // Backend for the scoreboard property: the setters carry only scalars, so a handle-valued write
    // goes through a method the same way rotation does.
    b.method("setScoreboard", [](Player &player, const Args &args, esn_handle *) {
        auto *board = static_cast<Scoreboard *>(args.binder.resolve(args.handleAt(0), Kind::Scoreboard));
        if (board == nullptr) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        player.setScoreboard(*board);
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
