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

// Binds include/endstone/block/block_state.h - a snapshot of one position, detached from the world.

#include <endstone/block/block.h>
#include <endstone/block/block_data.h>
#include <endstone/block/block_state.h>
#include <endstone/level/dimension.h>
#include <endstone/server.h>

#include "types/bind.h"
#include "types/block_states.h"

namespace endstone::node {

ESN_TYPE(BlockState, BlockState)
{
    // Only changes the snapshot; update() is what writes it to the world.
    b.rw("type", &BlockState::getType, &BlockState::setType);

    b.ro("x", &BlockState::getX);
    b.ro("y", &BlockState::getY);
    b.ro("z", &BlockState::getZ);
    b.ro("dimension", [](const BlockState &state) { return state.getDimension().getName(); });
    b.ro("location", &BlockState::getLocation);

    // The live block at the snapshot's position, which is not necessarily what the snapshot holds.
    b.ro("block", &BlockState::getBlock);

    // The palette entry as an object of its own - type plus states, without a position.
    b.ro("data", &BlockState::getData);

    b.rw("blockStatesList", [](const BlockState &state) {
        const auto data = state.getData();
        return data ? blockStatesRecord(*data) : std::string{};
    },
         [](BlockState &state, const Value &in, Binder &binder) {
             // Overlaid on what the snapshot already holds, so setting one state keeps the rest.
             const auto current = state.getData();
             BlockStates states = current ? current->getBlockStates() : BlockStates{};
             parseBlockStates(in.text, states);
             const auto data = binder.server().createBlockData(state.getType(), states);
             if (!data) {
                 return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
             }
             state.setData(*data);
             return static_cast<esn_status>(ESN_OK);
         });

    // update(force, applyPhysics). Without force it only applies if the position is still the type it
    // was captured as, so a racing change is not clobbered; applyPhysics lets neighbours react.
    b.method("update", [](BlockState &state, const Args &args, esn_handle *) {
        (void)state.update(args.number(0, 0) != 0, args.number(1, 0) != 0);
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
