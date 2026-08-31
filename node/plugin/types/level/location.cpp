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

// Binds include/endstone/level/location.h. Location does not derive from Vector upstream - it carries
// its own components plus a rotation and a dimension - so it declares them itself.

#include <endstone/block/block.h>
#include <endstone/level/dimension.h>
#include <endstone/level/location.h>

#include "types/bind.h"

namespace endstone::node {

ESN_TYPE(Location, Location)
{
    b.ro("x", &Location::getX);
    b.ro("y", &Location::getY);
    b.ro("z", &Location::getZ);

    b.ro("blockX", &Location::getBlockX);
    b.ro("blockY", &Location::getBlockY);
    b.ro("blockZ", &Location::getBlockZ);

    b.ro("pitch", &Location::getPitch);
    b.ro("yaw", &Location::getYaw);

    // The unit vector this rotation faces, which is what an eye-line or a projectile needs.
    b.ro("direction", &Location::getDirection);

    b.ro("dimension", [](const Location &location) { return location.getDimension().getName(); });
    b.ro("block", &Location::getBlock);
}

}  // namespace endstone::node
