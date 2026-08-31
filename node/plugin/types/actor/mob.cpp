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

// Binds include/endstone/actor/mob.h - a living actor. Health lives here rather than on Actor because
// only living things have it, which is why Endstone's actor events are templated on one or the other.

#include <endstone/actor/mob.h>

#include "types/bind.h"

namespace endstone::node {

ESN_SUBTYPE(Mob, Mob, Actor)
{
    b.rw("health", &Mob::getHealth, &Mob::setHealth);
    b.rw("maxHealth", &Mob::getMaxHealth, &Mob::setMaxHealth);
    // Was bound on Player only, though every mob can glide.
    b.ro("isGliding", &Mob::isGliding);
}

}  // namespace endstone::node
