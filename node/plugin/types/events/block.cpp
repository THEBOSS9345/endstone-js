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

// Binds include/endstone/event/block/.

// Several of these headers name Player and ItemStack without declaring them, so order matters.
#include <endstone/inventory/item_stack.h>
#include <endstone/player.h>

#include <endstone/event/block/block_break_event.h>
#include <endstone/event/block/block_cook_event.h>
#include <endstone/event/block/block_event.h>
#include <endstone/event/block/block_explode_event.h>
#include <endstone/event/block/block_form_event.h>
#include <endstone/event/block/block_from_to_event.h>
#include <endstone/event/block/block_grow_event.h>
#include <endstone/event/block/block_piston_event.h>
#include <endstone/event/block/block_piston_extend_event.h>
#include <endstone/event/block/block_piston_retract_event.h>
#include <endstone/event/block/block_place_event.h>
#include <endstone/event/block/leaves_decay_event.h>

#include "types/bind.h"
#include "types/block_face.h"

namespace endstone::node {

namespace {

/** One block out of an explosion's list. Handle 0 past the end, so a loop can just walk it. */
template <typename EventType>
esn_status explodedBlock(EventType &event, const Args &args, esn_handle *out_handle)
{
    if (out_handle == nullptr) {
        return ESN_OK;
    }
    auto &blocks = event.getBlockList();
    const auto index = static_cast<std::size_t>(args.number(0));
    *out_handle = index < blocks.size() && blocks[index]
                      ? args.binder.track(blocks[index].get(), Kind::Block, false)
                      : 0;
    return ESN_OK;
}

}  // namespace

ESN_EVENT_BASE(BlockEvent, Event)
{
    b.ro("block", &BlockEvent::getBlock);
}

ESN_EVENT(BlockBreakEvent, BlockEvent)
{
    b.cancellable();
    b.ro("player", &BlockBreakEvent::getPlayer);
}

ESN_EVENT(BlockPlaceEvent, BlockEvent)
{
    b.cancellable();
    b.ro("player", &BlockPlaceEvent::getPlayer);
    // What the block is going up against, and what it is replacing.
    b.ro("blockAgainst", &BlockPlaceEvent::getBlockAgainst);
    b.ro("blockReplaced", &BlockPlaceEvent::getBlockReplaced);
    b.ro("blockPlacedState", &BlockPlaceEvent::getBlockPlacedState);
}

ESN_EVENT(BlockCookEvent, BlockEvent)
{
    b.cancellable();
    b.ro("source", [](BlockCookEvent &event) { return const_cast<ItemStack *>(&event.getSource()); });
    // Write-through: setting a property on this stack puts it back as the cooked result.
    b.handle("result", [](BlockCookEvent &event, Binder &binder) {
        return binder.ownItem(event.getResult(), [&event](const ItemStack &changed) { event.setResult(changed); });
    });
}

ESN_EVENT(BlockFromToEvent, BlockEvent)
{
    b.cancellable();
    b.ro("toBlock", &BlockFromToEvent::getToBlock);
}

ESN_EVENT(BlockGrowEvent, BlockEvent)
{
    b.cancellable();
    b.ro("newState", &BlockGrowEvent::getNewState);
}

ESN_EVENT(BlockFormEvent, BlockGrowEvent) {}

ESN_EVENT(LeavesDecayEvent, BlockEvent)
{
    b.cancellable();
}

ESN_EVENT_BASE(BlockPistonEvent, BlockEvent)
{
    b.ro("direction", [](const BlockPistonEvent &event) { return std::string{blockFaceName(event.getDirection())}; });
}

ESN_EVENT(BlockPistonExtendEvent, BlockPistonEvent)
{
    b.cancellable();
}

ESN_EVENT(BlockPistonRetractEvent, BlockPistonEvent)
{
    b.cancellable();
}

// The block list is read-only by design upstream, so a handler can see what an explosion will destroy
// but not shorten the list. blockCount plus getExplodedBlock(i) is how it is walked.
ESN_EVENT(BlockExplodeEvent, BlockEvent)
{
    b.cancellable();
    b.ro("blockCount",
         [](BlockExplodeEvent &event) { return static_cast<std::int64_t>(event.getBlockList().size()); });
    b.method("getExplodedBlock", [](BlockExplodeEvent &event, const Args &args, esn_handle *out_handle) {
        return explodedBlock(event, args, out_handle);
    });
}

}  // namespace endstone::node
