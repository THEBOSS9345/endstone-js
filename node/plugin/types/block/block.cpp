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

// Binds include/endstone/block/block.h. One file per Endstone header folder, so "where does this go?"
// is answered by where upstream put it - see node/RULES.md.

#include <endstone/block/block.h>
#include <endstone/block/block_data.h>
#include <endstone/block/block_state.h>
#include <endstone/level/dimension.h>

#include <endstone/server.h>

#include "types/bind.h"
#include "types/block_face.h"
#include "types/block_states.h"

namespace endstone::node {

ESN_TYPE(Block, Block, None)
{
    b.rw("type", &Block::getType, static_cast<void (Block::*)(std::string)>(&Block::setType));

    b.ro("x", &Block::getX);
    b.ro("y", &Block::getY);
    b.ro("z", &Block::getZ);

    // The name, not a handle: a block's dimension is worth knowing without being worth keeping, and
    // every other type reports it the same way.
    b.ro("dimension", [](const Block &block) { return block.getDimension().getName(); });

    // Returned by value, so the bridge holds the copy for the rest of the dispatch.
    b.ro("location", &Block::getLocation);

    // The network id of this block's palette entry - what an UpdateBlockPacket carries.
    b.ro("runtimeId", [](const Block &block) -> std::int64_t {
        const auto data = block.getData();
        return data ? static_cast<std::int64_t>(data->getRuntimeId()) : 0;
    });

    // The palette entry as an object of its own - type plus states, without a position.
    b.ro("data", &Block::getData);

    // This block's own palette entry, rather than a type's default. Overlaid on write, so setting one
    // state keeps the rest - turning a stair round must not reset whether it is upside down.
    b.rw("blockStatesList",
         [](const Block &block) {
             const auto data = block.getData();
             return data ? blockStatesRecord(*data) : std::string{};
         },
         [](Block &block, const Value &in, Binder &binder) {
             const auto current = block.getData();
             BlockStates states = current ? current->getBlockStates() : BlockStates{};
             parseBlockStates(in.text, states);
             const auto data = binder.server().createBlockData(block.getType(), states);
             if (!data) {
                 return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
             }
             block.setData(*data);
             return static_cast<esn_status>(ESN_OK);
         });

    // A second handle on the same position, and a snapshot detached from the world. Both come back as
    // unique_ptr, which is all the binding needs to know to take ownership of them.
    b.method("clone", &Block::clone);
    b.method("captureState", &Block::captureState);

    // The one member here that cannot be a signature: three overloads sharing a name, chosen by what
    // the caller passed. A face plus an optional distance, or a plain offset.
    b.method("getRelative", [](Block &block, const Args &args, esn_handle *out_handle) {
        const auto face_name = args.str(0);
        auto relative = [&] {
            if (face_name.empty()) {
                return block.getRelative(static_cast<int>(args.number(0)), static_cast<int>(args.number(1)),
                                         static_cast<int>(args.number(2)));
            }
            const auto face = blockFaceFromName(face_name);
            return face ? block.getRelative(*face, static_cast<int>(args.number(0, 1)))
                        : std::unique_ptr<Block>{};
        }();
        if (!relative) {
            return static_cast<esn_status>(ESN_ERR_INTERNAL);
        }
        if (out_handle != nullptr) {
            *out_handle = args.binder.own(std::move(relative));
        }
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
