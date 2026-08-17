// Copyright (c) 2024, The Endstone Project. (https://endstone.dev) All Rights Reserved.
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

#include "bedrock/core/sem_ver/sem_version.h"

#include <charconv>
#include <regex>
#include <string_view>

namespace {

const std::regex &getSemVerRegex()
{
    static const std::regex regex{
        R"(^(0|[1-9]\d{0,4})(\.(0|[1-9]\d{0,4})(\.(0|[1-9]\d{0,4})(?:\-((?:(?:(?:(?:0[0-9]*[A-Za-z-])|[1-9A-Za-z-])[0-9A-Za-z-]*)|0)(?:\.(?:(?:(?:(?:0[0-9]*[A-Za-z-])|[1-9A-Za-z-])[0-9A-Za-z-]*)|0))*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?)?)?)",
        std::regex::ECMAScript};
    return regex;
}

bool toVersionNumber(const std::string &text, std::uint16_t &value)
{
    const auto *first = text.data();
    const auto *last = first + text.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    return first != last && ptr == last && ec == std::errc{};
}

Bedrock::StaticOptimizedString toOptimizedString(const std::string &text)
{
    return {text, Bedrock::StaticOptimizedString::StorageType::Dynamic};
}

}  // namespace

SemVersion::MatchType SemVersion::fromString(const std::string &src, SemVersion &output, ParseOption parse_option)
{
    if (src.size() == 1 && src.front() == '*') {
        output = SemVersion{AnyVersionConstructor};
        if (parse_option == ParseOption::AllowWildcards) {
            return MatchType::Full;
        }
        output = SemVersion{};
        return MatchType::None;
    }

    if (src == BetaPreReleaseTag) {
        output = SemVersion{BetaVersionNumber, BetaVersionNumber, BetaVersionNumber,
                            Bedrock::StaticOptimizedString{"beta"}};
        return MatchType::Full;
    }

    std::smatch match;
    if (!std::regex_search(src, match, getSemVerRegex())) {
        return MatchType::None;
    }
    if (match[0].length() != static_cast<std::ptrdiff_t>(src.size())) {
        return MatchType::None;
    }

    std::uint16_t major = 0;
    if (!toVersionNumber(match[1].str(), major)) {
        return MatchType::None;
    }

    std::uint16_t minor = 0;
    if (match[3].matched && !toVersionNumber(match[3].str(), minor)) {
        return MatchType::None;
    }

    std::uint16_t patch = 0;
    if (match[5].matched && !toVersionNumber(match[5].str(), patch)) {
        return MatchType::None;
    }

    output = SemVersion{major, minor, patch, toOptimizedString(match[6].str()), toOptimizedString(match[7].str())};
    if (!match[3].matched) {
        return MatchType::Partial;
    }
    return match[5].matched ? MatchType::Full : MatchType::Partial;
}
