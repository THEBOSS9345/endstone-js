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

#include "types/descriptor.h"

#include <array>

namespace endstone::node {

namespace {

constexpr auto kKindCount = static_cast<std::size_t>(Kind::None) + 1;

/**
 * Descriptors live in a function-local array rather than a namespace-scope one so that a type
 * declared in another translation unit cannot run before this is constructed. Static initialisation
 * order across TUs is unspecified, and every ESN_TYPE block runs during it.
 */
std::array<TypeDesc, kKindCount> &storage()
{
    static std::array<TypeDesc, kKindCount> types;
    return types;
}

std::vector<const TypeDesc *> &registered()
{
    static std::vector<const TypeDesc *> types;
    return types;
}

}  // namespace

std::string Args::str(const std::size_t index) const
{
    if (index >= string_count || strings == nullptr || strings[index] == nullptr) {
        return {};
    }
    return std::string{strings[index]};
}

double Args::number(const std::size_t index, const double fallback) const
{
    if (index >= number_count || numbers == nullptr) {
        return fallback;
    }
    return numbers[index];
}

esn_handle Args::handleAt(const std::size_t index) const
{
    if (index >= handle_count || handles == nullptr) {
        return 0;
    }
    return handles[index];
}

TypeDesc &declareType(const Kind kind, const std::string_view name, const Kind base)
{
    auto &desc = storage()[static_cast<std::size_t>(kind)];
    if (desc.kind == Kind::None && kind != Kind::None) {
        registered().push_back(&desc);
    }
    desc.kind = kind;
    desc.name = std::string{name};
    desc.base = base;
    return desc;
}

const TypeDesc *findType(const Kind kind)
{
    const auto &desc = storage()[static_cast<std::size_t>(kind)];
    return desc.kind == Kind::None ? nullptr : &desc;
}

const MemberDesc *findMember(const Kind kind, const std::string_view name)
{
    // Bounded by the depth of Endstone's own hierarchy - Player is the deepest at four.
    for (auto current = kind; current != Kind::None;) {
        const auto *desc = findType(current);
        if (desc == nullptr) {
            return nullptr;
        }
        if (const auto found = desc->members.find(name); found != desc->members.end()) {
            return &found->second;
        }
        current = desc->base;
    }
    return nullptr;
}

const DynamicDesc *findDynamic(const Kind kind, const std::string_view name, std::string_view &suffix)
{
    for (auto current = kind; current != Kind::None;) {
        const auto *desc = findType(current);
        if (desc == nullptr) {
            return nullptr;
        }
        for (const auto &entry : desc->dynamic) {
            if (name.size() > entry.prefix.size() && name.starts_with(entry.prefix)) {
                suffix = name.substr(entry.prefix.size());
                return &entry;
            }
        }
        current = desc->base;
    }
    return nullptr;
}

const std::vector<const TypeDesc *> &allTypes()
{
    return registered();
}

}  // namespace endstone::node
