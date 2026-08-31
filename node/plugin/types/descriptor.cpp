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

/** Events are keyed by the name they report rather than by a kind - see declareEvent. */
std::unordered_map<std::string, TypeDesc, StringHash, std::equal_to<>> &eventStorage()
{
    static std::unordered_map<std::string, TypeDesc, StringHash, std::equal_to<>> events;
    return events;
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

TypeDesc &declareType(const Kind kind, const std::string_view name, const Kind base,
                      void *(*to_base)(void *))
{
    auto &desc = storage()[static_cast<std::size_t>(kind)];
    if (desc.kind == Kind::None && kind != Kind::None) {
        registered().push_back(&desc);
    }
    desc.kind = kind;
    desc.name = std::string{name};
    desc.base = base;
    desc.to_base = to_base;
    return desc;
}

const TypeDesc *findType(const Kind kind)
{
    const auto &desc = storage()[static_cast<std::size_t>(kind)];
    return desc.kind == Kind::None ? nullptr : &desc;
}

Lookup findMember(const Kind kind, const std::string_view name, void *self)
{
    // Bounded by the depth of Endstone's own hierarchy - Player is the deepest, at four.
    for (auto current = kind; current != Kind::None;) {
        const auto *desc = findType(current);
        if (desc == nullptr) {
            return {};
        }
        if (const auto found = desc->members.find(name); found != desc->members.end()) {
            return Lookup{&found->second, nullptr, self, {}};
        }
        if (desc->base == Kind::None || desc->to_base == nullptr) {
            return {};
        }
        self = desc->to_base(self);
        current = desc->base;
    }
    return {};
}

Lookup findDynamic(const Kind kind, const std::string_view name, void *self, const ValueKind want)
{
    for (auto current = kind; current != Kind::None;) {
        const auto *desc = findType(current);
        if (desc == nullptr) {
            return {};
        }
        for (const auto &entry : desc->dynamic) {
            if (entry.kind == want && name.size() > entry.prefix.size() && name.starts_with(entry.prefix)) {
                return Lookup{nullptr, &entry, self, name.substr(entry.prefix.size())};
            }
        }
        if (desc->base == Kind::None || desc->to_base == nullptr) {
            return {};
        }
        self = desc->to_base(self);
        current = desc->base;
    }
    return {};
}

TypeDesc &declareEvent(const std::string_view name, const std::string_view base, void *(*from_event)(void *),
                       void *(*to_base)(void *))
{
    auto &desc = eventStorage()[std::string{name}];
    if (desc.name.empty()) {
        registered().push_back(&desc);
    }
    desc.kind = Kind::Event;
    desc.name = std::string{name};
    desc.base_event = std::string{base};
    desc.from_event = from_event;
    desc.to_base = to_base;
    return desc;
}

const TypeDesc *findEvent(const std::string_view name)
{
    const auto &events = eventStorage();
    const auto found = events.find(name);
    return found == events.end() ? nullptr : &found->second;
}

namespace {

/** Walks an event's chain, starting with the downcast its reported name licenses. */
template <typename Match>
Lookup walkEvent(const std::string_view event_name, void *event, Match match)
{
    const auto *desc = findEvent(event_name);
    if (desc == nullptr || desc->from_event == nullptr) {
        return {};
    }
    void *self = desc->from_event(event);
    while (desc != nullptr) {
        if (auto found = match(*desc, self)) {
            return found;
        }
        if (desc->base_event.empty() || desc->to_base == nullptr) {
            return {};
        }
        self = desc->to_base(self);
        desc = findEvent(desc->base_event);
    }
    return {};
}

}  // namespace

Lookup findEventMember(const std::string_view event_name, const std::string_view name, void *event)
{
    return walkEvent(event_name, event, [&](const TypeDesc &desc, void *self) -> Lookup {
        const auto found = desc.members.find(name);
        return found == desc.members.end() ? Lookup{} : Lookup{&found->second, nullptr, self, {}};
    });
}

Lookup findEventDynamic(const std::string_view event_name, const std::string_view name, void *event,
                        const ValueKind want)
{
    return walkEvent(event_name, event, [&](const TypeDesc &desc, void *self) -> Lookup {
        for (const auto &entry : desc.dynamic) {
            if (entry.kind == want && name.size() > entry.prefix.size() && name.starts_with(entry.prefix)) {
                return Lookup{nullptr, &entry, self, name.substr(entry.prefix.size())};
            }
        }
        return {};
    });
}

const std::vector<const TypeDesc *> &allTypes()
{
    return registered();
}

}  // namespace endstone::node
