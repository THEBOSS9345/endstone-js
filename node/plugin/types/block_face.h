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
#include <string_view>

#include <endstone/block/block_face.h>

namespace endstone::node {

/** The name the runtime uses for a face. Paired with blockFaceFromName, which reads it back. */
inline std::string_view blockFaceName(const BlockFace face)
{
    switch (face) {
    case BlockFace::Down: return "down";
    case BlockFace::Up: return "up";
    case BlockFace::North: return "north";
    case BlockFace::South: return "south";
    case BlockFace::West: return "west";
    case BlockFace::East: return "east";
    }
    return "down";
}

/**
 * @brief A block face by the name the runtime uses for it.
 *
 * Shared because the interact event reports a face as one of these strings and Block::getRelative
 * takes one back, so `block.getRelative(event.blockFace)` has to agree on the spelling.
 */
inline std::optional<BlockFace> blockFaceFromName(const std::string_view name)
{
    if (name == "down") return BlockFace::Down;
    if (name == "up") return BlockFace::Up;
    if (name == "north") return BlockFace::North;
    if (name == "south") return BlockFace::South;
    if (name == "west") return BlockFace::West;
    if (name == "east") return BlockFace::East;
    return std::nullopt;
}

}  // namespace endstone::node
