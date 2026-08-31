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

// Binds include/endstone/block/block_data.h.

#include <endstone/block/block_data.h>

#include "types/bind.h"
#include "types/block_states.h"

namespace endstone::node {

ESN_TYPE(BlockData, BlockData, None)
{
    b.ro("type", &BlockData::getType);
    b.ro("runtimeId", [](const BlockData &data) { return static_cast<std::int64_t>(data.getRuntimeId()); });
    // The palette entry as records; the runtime splits it back into an object.
    b.ro("blockStatesList", [](const BlockData &data) { return blockStatesRecord(data); });
}

}  // namespace endstone::node
