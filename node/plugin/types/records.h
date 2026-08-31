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

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace endstone::node {

/**
 * @brief Separates the fields of one record.
 *
 * Anything the ABI carries that is more than a single value travels as text, because the accessors
 * carry one value each. A list is newline-separated and a record's fields are separated by this - a
 * control character, so it cannot collide with a block state name or a player's chat.
 */
constexpr char kUnitSeparator = '\x1f';

inline std::vector<std::string> splitOn(const std::string_view text, const char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto at = text.find(separator, start);
        if (at == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, at - start));
        start = at + 1;
    }
}

/** Field `index` of a record, or an empty string. Missing fields are normal: they mean "default". */
inline std::string fieldAt(const std::vector<std::string> &fields, const std::size_t index)
{
    return index < fields.size() ? fields[index] : std::string{};
}

}  // namespace endstone::node
