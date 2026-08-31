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

namespace endstone::node {

/**
 * @brief What a handle points at.
 *
 * Lives here rather than inside ApiBridge because every type descriptor names one, and the
 * descriptors are declared in their own translation units - one per Endstone header folder.
 *
 * Mob is distinct from Actor because only living things have health, and Endstone's actor events are
 * templated on one or the other (ActorEvent<Mob> vs ActorEvent<Actor>).
 */
enum class Kind : std::uint8_t {
    // Item is a dropped item stack in the world, distinct from ItemStack which is the stack itself.
    Player,
    Mob,
    Actor,
    Item,
    Block,
    Level,
    DamageSource,
    ItemStack,
    Location,
    Vector,
    CommandSender,
    // PlayerInventory is distinct from Inventory only so the equipment slots can be reached; every
    // generic inventory operation accepts either.
    Inventory,
    PlayerInventory,
    Plugin,
    MapView,
    MapCanvas,
    Server,
    Dimension,
    Scoreboard,
    Event,
    BossBar,
    // A block's palette entry (type plus its states), and a detached snapshot of one position.
    BlockData,
    BlockState,
    /** Not a real kind: means "this type has no base" in a descriptor. */
    None,
};

}  // namespace endstone::node
