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

// Binds include/endstone/event/actor/.
//
// ActorEvent is a template - ActorEvent<Mob> for the living, ActorEvent<Actor> for anything - so it
// cannot be a base descriptor the way PlayerEvent and BlockEvent are. `actor` is therefore declared on
// each event. That is not a loss: the return type says whether the actor is living, so the binding
// tracks a Mob as a Mob and an Actor as an Actor without being told.

#include <endstone/inventory/item_stack.h>
#include <endstone/player.h>

#include <endstone/event/actor/actor_damage_event.h>
#include <endstone/event/actor/actor_death_event.h>
#include <endstone/event/actor/actor_explode_event.h>
#include <endstone/event/actor/actor_knockback_event.h>
#include <endstone/event/actor/actor_remove_event.h>
#include <endstone/event/actor/actor_spawn_event.h>
#include <endstone/event/actor/actor_teleport_event.h>
#include <endstone/event/actor/player_death_event.h>

#include "types/bind.h"
#include "types/events/message.h"
#include "types/events/place.h"

namespace endstone::node {

ESN_EVENT(ActorSpawnEvent, Event)
{
    b.cancellable();
    b.ro("actor", &ActorSpawnEvent::getActor);
}

ESN_EVENT(ActorRemoveEvent, Event)
{
    b.ro("actor", &ActorRemoveEvent::getActor);
}

ESN_EVENT(ActorDamageEvent, Event)
{
    b.cancellable();
    b.ro("actor", &ActorDamageEvent::getActor);
    b.rw("damage", &ActorDamageEvent::getDamage, &ActorDamageEvent::setDamage);
    b.ro("damageSource", &ActorDamageEvent::getDamageSource);
}

ESN_EVENT(ActorDeathEvent, Event)
{
    b.ro("actor", &ActorDeathEvent::getActor);
    // What killed them, which is the useful half of a death event.
    b.ro("damageSource", &ActorDeathEvent::getDamageSource);
}

ESN_EVENT(PlayerDeathEvent, ActorDeathEvent)
{
    b.ro("player", &PlayerDeathEvent::getPlayer);
    // An empty string suppresses the announcement, matching join and quit messages.
    b.rw("deathMessage",
         [](const PlayerDeathEvent &event) { return optionalMessageText(event.getDeathMessage()); },
         [](PlayerDeathEvent &event, const Value &in, Binder &) {
             event.setDeathMessage(optionalMessage(in.text));
             return static_cast<esn_status>(ESN_OK);
         });
}

ESN_EVENT(ActorKnockbackEvent, Event)
{
    b.cancellable();
    b.ro("actor", &ActorKnockbackEvent::getActor);
    // Null when nothing was responsible, e.g. an explosion the server set off.
    b.ro("source", &ActorKnockbackEvent::getSource);
    b.ro("knockback", &ActorKnockbackEvent::getKnockback);
    // Backend for `event.knockback = { x, y, z }`, routed as a method because the ABI's setters carry
    // one scalar each.
    b.method("setKnockback", [](ActorKnockbackEvent &event, const Args &args, esn_handle *) {
        event.setKnockback(Vector{args.number(0), args.number(1), args.number(2)});
        return static_cast<esn_status>(ESN_OK);
    });
}

ESN_EVENT(ActorTeleportEvent, Event)
{
    b.cancellable();
    b.ro("actor", &ActorTeleportEvent::getActor);
    b.ro("from", &ActorTeleportEvent::getFrom);
    b.ro("to", &ActorTeleportEvent::getTo);
    bindPlaceSetters(b, &ActorTeleportEvent::getFrom, &ActorTeleportEvent::setFrom, &ActorTeleportEvent::getTo,
                     &ActorTeleportEvent::setTo);
}

// The block list is read-only upstream, so a handler can see what will be destroyed but not shorten
// the list. blockCount plus getExplodedBlock(i) is how it is walked.
ESN_EVENT(ActorExplodeEvent, Event)
{
    b.cancellable();
    b.ro("actor", &ActorExplodeEvent::getActor);
    b.ro("location", &ActorExplodeEvent::getLocation);
    b.ro("blockCount",
         [](ActorExplodeEvent &event) { return static_cast<std::int64_t>(event.getBlockList().size()); });
    b.method("getExplodedBlock", [](ActorExplodeEvent &event, const Args &args, esn_handle *out_handle) {
        if (out_handle == nullptr) {
            return static_cast<esn_status>(ESN_OK);
        }
        auto &blocks = event.getBlockList();
        const auto index = static_cast<std::size_t>(args.number(0));
        *out_handle = index < blocks.size() && blocks[index]
                          ? args.binder.track(blocks[index].get(), Kind::Block, false)
                          : 0;
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
