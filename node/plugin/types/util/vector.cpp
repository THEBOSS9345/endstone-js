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

// Binds include/endstone/util/vector.h.

#include <endstone/util/vector.h>

#include "types/bind.h"

namespace endstone::node {

ESN_TYPE(Vector, Vector)
{
    b.ro("x", &Vector::getX);
    b.ro("y", &Vector::getY);
    b.ro("z", &Vector::getZ);

    // The containing block, i.e. each component floored. Negative coordinates round the way Minecraft
    // does rather than the way a cast to int does.
    b.ro("blockX", &Vector::getBlockX);
    b.ro("blockY", &Vector::getBlockY);
    b.ro("blockZ", &Vector::getBlockZ);

    // Properties rather than calls: they take no arguments and read as magnitudes. Squared is there
    // because comparing distances does not need the square root.
    b.ro("length", &Vector::length);
    b.ro("lengthSquared", &Vector::lengthSquared);
}

}  // namespace endstone::node
