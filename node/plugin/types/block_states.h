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

#include <string>
#include <string_view>

#include <endstone/block/block_data.h>

namespace endstone::node {

/**
 * @brief A block's states as one "key\x1ftype\x1fvalue" record per line.
 *
 * The tag travels with the value because the map's values are a variant and Bedrock distinguishes
 * the arms: "true" the string and true the boolean are different states.
 */
std::string blockStatesRecord(const BlockData &data);

/**
 * @brief Reads those records back onto `states`.
 *
 * Overlaid rather than replacing: setting one state means "change this one", so the rest of the
 * block's palette entry has to survive - turning a stair round must not reset whether it is upside
 * down.
 */
void parseBlockStates(std::string_view text, BlockStates &states);

}  // namespace endstone::node
