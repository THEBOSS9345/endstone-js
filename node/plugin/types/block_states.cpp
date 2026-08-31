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

#include "types/block_states.h"

#include <variant>

#include "types/records.h"

namespace endstone::node {

std::string blockStatesRecord(const BlockData &data)
{
    std::string out;
    for (const auto &[key, value] : data.getBlockStates()) {
        if (!out.empty()) {
            out += '\n';
        }
        out += key;
        out += kUnitSeparator;
        std::visit(
            [&](const auto &held) {
                using Held = std::decay_t<decltype(held)>;
                if constexpr (std::is_same_v<Held, bool>) {
                    out += "b";
                    out += kUnitSeparator;
                    out += held ? "1" : "0";
                }
                else if constexpr (std::is_same_v<Held, int>) {
                    out += "i";
                    out += kUnitSeparator;
                    out += std::to_string(held);
                }
                else {
                    out += "s";
                    out += kUnitSeparator;
                    out += held;
                }
            },
            value);
    }
    return out;
}

/**
 * @brief Reads the "key\x1ftype\x1fvalue" records the runtime sends back, onto `states`.
 *
 * Overlaid rather than replacing: a plugin that sets one state means "change this one", so the rest
 * of the block's palette entry has to survive. The tag decides the variant arm, which is why it
 * travels with the value in both directions - "true" the string and true the boolean are different
 * states and Bedrock cares.
 */
void parseBlockStates(const std::string_view text, BlockStates &states)
{
    if (text.empty()) {
        return;
    }
    for (const auto &line : splitOn(text, '\n')) {
        const auto fields = splitOn(line, kUnitSeparator);
        if (fields.size() < 3 || fields[0].empty()) {
            continue;
        }
        const auto &key = fields[0];
        const auto &value = fields[2];
        if (fields[1] == "b") {
            states[key] = value == "1";
        }
        else if (fields[1] == "i") {
            try {
                states[key] = std::stoi(value);
            }
            catch (...) {
                continue;
            }
        }
        else {
            states[key] = value;
        }
    }
}

}  // namespace endstone::node
