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

// Binds include/endstone/actor/actor.h. Actor derives from CommandSender upstream, so messaging and
// permissions come from there rather than being repeated here.

#include <endstone/actor/actor.h>
#include <endstone/level/dimension.h>
#include <endstone/level/level.h>

#include "types/bind.h"

namespace endstone::node {

ESN_SUBTYPE(Actor, Actor, CommandSender)
{
    b.ro("type", &Actor::getType);

    b.ro("isOnGround", &Actor::isOnGround);
    b.ro("isInWater", &Actor::isInWater);
    b.ro("isInLava", &Actor::isInLava);
    b.ro("isDead", &Actor::isDead);
    b.ro("isValid", &Actor::isValid);

    // Runtime ids exceed what a double represents exactly, so this is exposed as a number only
    // because Bedrock keeps them small in practice; prefer id for anything persistent.
    b.ro("runtimeId", [](const Actor &actor) { return static_cast<std::int64_t>(actor.getRuntimeId()); });
    // The persistent id, the one that survives a reload.
    b.ro("id", &Actor::getId);

    b.rw("nameTag", &Actor::getNameTag, &Actor::setNameTag);
    b.rw("scoreTag", &Actor::getScoreTag, &Actor::setScoreTag);
    b.rw("isNameTagVisible", &Actor::isNameTagVisible, &Actor::setNameTagVisible);
    b.rw("isNameTagAlwaysVisible", &Actor::isNameTagAlwaysVisible, &Actor::setNameTagAlwaysVisible);

    b.ro("dimension", [](const Actor &actor) { return actor.getDimension().getName(); });
    b.ro("level", &Actor::getLevel);
    b.ro("location", &Actor::getLocation);
    // The rotation rides on the location, which carries pitch and yaw; the runtime reads them off it.
    b.ro("rotation", &Actor::getLocation);
    b.ro("velocity", &Actor::getVelocity);

    // A vector of strings crosses newline-joined, which the binding does on its own.
    b.ro("scoreboardTagList", &Actor::getScoreboardTags);
    b.method("addScoreboardTag", &Actor::addScoreboardTag);
    b.method("removeScoreboardTag", &Actor::removeScoreboardTag);

    // Safe to expose here: EndstonePlayer overrides it to log "use Player::kick instead" and do nothing.
    b.method("remove", &Actor::remove);

    // On a player this moves only the server-side rotation - the client owns its camera, so turning a
    // player's view takes a teleport.
    b.method("setRotation", &Actor::setRotation);

    // teleport(dimension, x, y, z, yaw, pitch); rotation and dimension default to the current ones.
    b.method("teleport", [](Actor &actor, const Args &args, esn_handle *) {
        const auto current = actor.getLocation();
        auto *dimension = &actor.getDimension();
        if (const auto requested = args.str(0); !requested.empty()) {
            dimension = actor.getLevel().getDimension(requested);
            if (dimension == nullptr) {
                return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
            }
        }
        actor.teleport(Location{*dimension, static_cast<float>(args.number(0, 0)),
                                static_cast<float>(args.number(1, 0)), static_cast<float>(args.number(2, 0)),
                                static_cast<float>(args.number(4, current.getPitch())),
                                static_cast<float>(args.number(3, current.getYaw()))});
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
