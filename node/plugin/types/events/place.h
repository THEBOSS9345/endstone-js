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

#include <optional>
#include <string>

#include <endstone/level/dimension.h>
#include <endstone/level/level.h>
#include <endstone/level/location.h>

#include "types/bind.h"

namespace endstone::node {

/**
 * @brief Builds a Location from a setFrom/setTo call, keeping whatever the caller left out.
 *
 * Assigning a bare `{ x, y, z }` redirects a move without disturbing facing or dimension, which is
 * what a redirect usually wants. Returns nullopt only when a named dimension does not exist.
 */
inline std::optional<Location> placeFromArgs(const Args &args, const Location &current)
{
    auto *dimension = &current.getDimension();
    if (const auto requested = args.str(0); !requested.empty()) {
        dimension = current.getDimension().getLevel().getDimension(requested);
        if (dimension == nullptr) {
            return std::nullopt;
        }
    }
    return Location{*dimension,
                    static_cast<float>(args.number(0, current.getX())),
                    static_cast<float>(args.number(1, current.getY())),
                    static_cast<float>(args.number(2, current.getZ())),
                    static_cast<float>(args.number(4, current.getPitch())),
                    static_cast<float>(args.number(3, current.getYaw()))};
}

/**
 * Declares setFrom and setTo on an event that has both.
 *
 * Methods rather than setters because a Location is six numbers plus a dimension and the ABI's
 * setters carry one scalar each.
 */
template <typename T, typename Get, typename Set>
void bindPlaceSetters(TypeBuilder<T> &b, Get get_from, Set set_from, Get get_to, Set set_to)
{
    b.method("setFrom", [get_from, set_from](T &event, const Args &args, esn_handle *) {
        const auto updated = placeFromArgs(args, (event.*get_from)());
        if (!updated) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        (event.*set_from)(*updated);
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("setTo", [get_to, set_to](T &event, const Args &args, esn_handle *) {
        const auto updated = placeFromArgs(args, (event.*get_to)());
        if (!updated) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        (event.*set_to)(*updated);
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
