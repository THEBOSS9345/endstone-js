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
#include <string>
#include <variant>

#include <endstone/message.h>

namespace endstone::node {

/**
 * @brief The literal text of a message, or empty.
 *
 * Message is a variant of a plain string and a translatable; only the former has a literal form, so a
 * translatable reads as empty rather than as its key.
 */
inline std::string optionalMessageText(const std::optional<Message> &message)
{
    if (!message.has_value()) {
        return {};
    }
    if (const auto *text = std::get_if<std::string>(&message.value())) {
        return *text;
    }
    return {};
}

/** The other direction. An empty string is no message at all, which suppresses an announcement. */
inline std::optional<Message> optionalMessage(const std::string &text)
{
    return text.empty() ? std::optional<Message>{} : Message{text};
}

}  // namespace endstone::node
