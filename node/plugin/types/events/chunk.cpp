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

// Binds include/endstone/event/chunk/.

#include <endstone/event/chunk/chunk_event.h>
#include <endstone/event/chunk/chunk_load_event.h>
#include <endstone/event/chunk/chunk_unload_event.h>

#include "types/bind.h"

namespace endstone::node {

// Dimension and level arrive through DimensionEvent, so a multi-world plugin can tell an overworld
// load from a nether one - the coordinates alone are ambiguous.
ESN_EVENT_BASE(ChunkEvent, DimensionEvent)
{
    b.ro("chunkX", [](const ChunkEvent &event) { return event.getChunk().getX(); });
    b.ro("chunkZ", [](const ChunkEvent &event) { return event.getChunk().getZ(); });
}

ESN_EVENT(ChunkLoadEvent, ChunkEvent) {}

ESN_EVENT(ChunkUnloadEvent, ChunkEvent) {}

}  // namespace endstone::node
