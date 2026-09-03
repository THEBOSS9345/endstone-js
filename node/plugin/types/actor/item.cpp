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

// Binds include/endstone/actor/item.h - a dropped stack lying in the world, which is an actor rather
// than a stack. ItemStack is the stack itself.

#include <endstone/actor/item.h>
#include <endstone/inventory/item_stack.h>

#include "types/bind.h"

namespace endstone::node {

ESN_SUBTYPE(Item, Item, Actor)
{
    b.rw("pickupDelay", &Item::getPickupDelay, &Item::setPickupDelay);
    b.rw("unlimitedLifetime", &Item::isUnlimitedLifetime, &Item::setUnlimitedLifetime);
    // Absent rather than null when nothing threw it, so `if (item.thrower)` reads naturally.
    b.rw("thrower", &Item::getThrower, &Item::setThrower);
    // getItemStack hands back a copy, so it is paired with a writeback like an inventory slot.
    b.handle("itemStack", [](Item &item, Binder &binder) {
        return binder.ownItem(item.getItemStack(),
                              [&item](const ItemStack &changed) { item.setItemStack(changed); });
    });
}

}  // namespace endstone::node
