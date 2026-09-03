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

// Binds include/endstone/level/level.h and dimension.h.

#include <endstone/actor/actor.h>
#include <endstone/actor/item.h>
#include <endstone/block/block.h>
#include <endstone/inventory/item_stack.h>
#include <endstone/level/chunk.h>
#include <endstone/level/dimension.h>
#include <endstone/level/level.h>

#include "types/bind.h"

namespace endstone::node {

ESN_TYPE(Level, Level)
{
    b.ro("name", &Level::getName);
    b.ro("seed", &Level::getSeed);
    b.rw("time", &Level::getTime, &Level::setTime);
    b.ro("actorCount", [](const Level &level) { return static_cast<std::int64_t>(level.getActors().size()); });
    b.ro("dimensionCount",
         [](const Level &level) { return static_cast<std::int64_t>(level.getDimensions().size()); });

    // Dimensions live as long as the level, so the handle outlives the callback that asked for it.
    b.method("getDimension", [](Level &level, const Args &args, esn_handle *out_handle) {
        if (out_handle != nullptr) {
            auto *dimension = level.getDimension(args.str(0));
            *out_handle = dimension != nullptr ? args.binder.track(dimension, Kind::Dimension, true) : 0;
        }
        return static_cast<esn_status>(ESN_OK);
    });
}

ESN_TYPE(Dimension, Dimension)
{
    b.ro("name", &Dimension::getName);
    b.ro("level", &Dimension::getLevel);
    b.ro("actorCount",
         [](const Dimension &dimension) { return static_cast<std::int64_t>(dimension.getActors().size()); });

    // The enum rather than the display name, so a plugin does not have to string-match.
    b.ro("type", [](const Dimension &dimension) {
        switch (dimension.getType()) {
        case Dimension::Type::Overworld: return std::string{"overworld"};
        case Dimension::Type::Nether: return std::string{"nether"};
        case Dimension::Type::TheEnd: return std::string{"theEnd"};
        default: return std::string{"custom"};
        }
    });

    // "x,z" per line: a list cannot cross as an array, and the runtime turns this into objects.
    b.ro("loadedChunkList", [](Dimension &dimension) {
        std::string joined;
        for (const auto &chunk : dimension.getLoadedChunks()) {
            if (!chunk) {
                continue;
            }
            if (!joined.empty()) {
                joined += '\n';
            }
            joined += std::to_string(chunk->getX()) + "," + std::to_string(chunk->getZ());
        }
        return joined;
    });

    // highestBlockYAt:<x>,<z> - just the height, without allocating a Block for it.
    b.dynamic("highestBlockYAt:", ValueKind::Int,
              [](Dimension &dimension, const std::string_view request, Binder &, Value &out) {
                  const auto comma = request.find(',');
                  if (comma == std::string_view::npos) {
                      return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
                  }
                  out.kind = ValueKind::Int;
                  out.integer = dimension.getHighestBlockYAt(std::stoi(std::string{request.substr(0, comma)}),
                                                             std::stoi(std::string{request.substr(comma + 1)}));
                  return static_cast<esn_status>(ESN_OK);
              });

    // getBlockAt(location) - the vector is flattened to x, y, z by the runtime.
    b.method("getBlockAt", [](Dimension &dimension, const Args &args, esn_handle *out_handle) {
        auto block = dimension.getBlockAt(static_cast<int>(args.number(0)), static_cast<int>(args.number(1)),
                                          static_cast<int>(args.number(2)));
        if (out_handle != nullptr) {
            *out_handle = block ? args.binder.own(std::move(block)) : 0;
        }
        return static_cast<esn_status>(ESN_OK);
    });
    // The topmost non-air block in a column.
    b.method("getHighestBlockAt", [](Dimension &dimension, const Args &args, esn_handle *out_handle) {
        auto block = dimension.getHighestBlockAt(static_cast<int>(args.number(0)),
                                                 static_cast<int>(args.number(1)));
        if (out_handle != nullptr) {
            *out_handle = block ? args.binder.own(std::move(block)) : 0;
        }
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("spawnActor", [](Dimension &dimension, const Args &args, esn_handle *out_handle) {
        const Location where{dimension, static_cast<float>(args.number(0)), static_cast<float>(args.number(1)),
                             static_cast<float>(args.number(2))};
        auto *spawned = dimension.spawnActor(where, args.str(0));
        if (out_handle != nullptr) {
            *out_handle = spawned != nullptr ? args.binder.trackActor(spawned) : 0;
        }
        return static_cast<esn_status>(ESN_OK);
    });
    // dropItem(item, location): a loose stack lying in the world.
    b.method("dropItem", [](Dimension &dimension, const Args &args, esn_handle *out_handle) {
        const auto type = args.str(0);
        if (type.empty()) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        const ItemStack item{type, static_cast<int>(args.number(3, 1)), static_cast<int>(args.number(4, 0))};
        const Location where{dimension, static_cast<float>(args.number(0)), static_cast<float>(args.number(1)),
                             static_cast<float>(args.number(2))};
        auto &dropped = dimension.dropItem(where, item);
        if (out_handle != nullptr) {
            // The static type is known here, so it is tracked as an Item rather than a plain actor -
            // which is what gives the caller pickupDelay and itemStack.
            *out_handle = args.binder.track(&dropped, Kind::Item, false);
        }
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("getActor", [](Dimension &dimension, const Args &args, esn_handle *out_handle) {
        if (out_handle != nullptr) {
            const auto actors = dimension.getActors();
            const auto index = static_cast<std::size_t>(args.number(0));
            *out_handle = index < actors.size() && actors[index] != nullptr
                              ? args.binder.trackActor(actors[index])
                              : 0;
        }
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
