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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "endstone_node_abi.h"
#include "types/kind.h"

namespace endstone {
class Actor;
class Block;
class BlockData;
class BlockState;
class BossBar;
class CommandSender;
class DamageSource;
class Dimension;
class Item;
class ItemStack;
class Level;
class Location;
class MapCanvas;
class MapView;
class Mob;
class Player;
class Scoreboard;
class Server;
class Vector;
}  // namespace endstone

namespace endstone::node {

/**
 * @brief Which of the ABI's typed accessors carries a member.
 *
 * The ABI has one entry point per value kind rather than one per property, so every member declares
 * which one it answers on. A member is deduced from its C++ signature, never written out by hand.
 */
enum class ValueKind : std::uint8_t { None, Bool, Int, Double, String, Handle, Bytes };

/** One value crossing between a binding and an ABI entry point. */
struct Value {
    ValueKind kind{ValueKind::None};
    bool boolean{false};
    std::int64_t integer{0};
    double real{0.0};
    /** Also carries Bytes, which is a string that may contain NULs. */
    std::string text;
    esn_handle handle{0};
};

/**
 * @brief What a binding may ask of the bridge.
 *
 * Bindings live in their own translation units and must not reach into ApiBridge's internals, so
 * everything they need - minting a handle, taking ownership of something Endstone handed back by
 * pointer or by value - arrives through this. ApiBridge is the only implementation.
 */
class Binder {
public:
    virtual ~Binder() = default;

    /**
     * Records a pointer under a kind. The pointer's static type must be the class the kind names -
     * see ApiBridge::track for why a base or derived pointer is a layout bug rather than a type error.
     */
    virtual esn_handle track(void *ptr, Kind kind, bool persistent) = 0;
    /** An actor whose concrete type is not known statically; picks Player when it matches one. */
    virtual esn_handle trackActor(Actor *actor) = 0;
    virtual void *resolve(esn_handle handle, Kind kind) const = 0;

    // Endstone hands these back as unique_ptr or by value, so the bridge owns them until the
    // dispatch scope closes. Each returns the handle for the object it took.
    virtual esn_handle own(std::unique_ptr<Block> block) = 0;
    virtual esn_handle own(std::unique_ptr<BlockData> data) = 0;
    virtual esn_handle own(std::unique_ptr<BlockState> state) = 0;
    virtual esn_handle own(const Location &location) = 0;
    virtual esn_handle own(const Vector &vector) = 0;

    /** Runs an item handle's writeback, if it has one. Item stacks are values, not references. */
    virtual void persistItem(esn_handle target) = 0;
    virtual Server &server() = 0;
};

/**
 * @brief A method's arguments, as they arrive over the ABI.
 *
 * Strings and numbers travel in separate arrays, so a method taking (string, int) reads str(0) and
 * number(0) - the indices count within their own kind, not across the parameter list.
 */
struct Args {
    Binder &binder;
    esn_handle self_handle{0};
    const char *const *strings{nullptr};
    std::size_t string_count{0};
    const double *numbers{nullptr};
    std::size_t number_count{0};
    const esn_handle *handles{nullptr};
    std::size_t handle_count{0};

    [[nodiscard]] std::string str(std::size_t index) const;
    [[nodiscard]] double number(std::size_t index, double fallback = 0.0) const;
    [[nodiscard]] esn_handle handleAt(std::size_t index) const;
};

using GetFn = std::function<esn_status(void *self, Binder &binder, Value &out)>;
using SetFn = std::function<esn_status(void *self, Binder &binder, const Value &in)>;
using CallFn = std::function<esn_status(void *self, const Args &args, esn_handle *out_handle)>;
/** A prefixed accessor, e.g. "tag:<key>" - it receives whatever followed the prefix. */
using DynamicFn = std::function<esn_status(void *self, std::string_view suffix, Binder &binder, Value &out)>;

struct MemberDesc {
    /** Which accessor answers this member. None for a method, which goes through invoke. */
    ValueKind kind{ValueKind::None};
    GetFn get;
    SetFn set;
    CallFn call;
};

struct DynamicDesc {
    std::string prefix;
    ValueKind kind{ValueKind::None};
    DynamicFn get;
};

/** Transparent hashing, so a string_view lookup does not allocate a std::string first. */
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(const std::string_view text) const noexcept
    {
        return std::hash<std::string_view>{}(text);
    }
    std::size_t operator()(const std::string &text) const noexcept
    {
        return std::hash<std::string_view>{}(text);
    }
};

/**
 * @brief Everything one bound type exposes.
 *
 * `base` chains to the type this one derives from, so Player's descriptor holds only what Player adds
 * and a lookup walks up to Mob, Actor and CommandSender. That mirrors Endstone's own hierarchy, which
 * is what keeps a member declared exactly once.
 */
struct TypeDesc {
    Kind kind{Kind::None};
    std::string name;
    Kind base{Kind::None};
    /**
     * Converts a pointer to this type into one to its base.
     *
     * Not decoration: a handle holds `void *`, and casting that straight to a base class does no
     * pointer adjustment. Player inherits from Mob and OfflinePlayer both, so a CommandSender member
     * reached through a Player has to be given a real CommandSender pointer or it reads the wrong
     * bytes - and it is layout, not the type system, that decides whether that shows up.
     */
    void *(*to_base)(void *){nullptr};
    std::unordered_map<std::string, MemberDesc, StringHash, std::equal_to<>> members;
    std::vector<DynamicDesc> dynamic;
};

/** Creates or returns the descriptor for a kind. Called from ESN_TYPE blocks at static-init time. */
TypeDesc &declareType(Kind kind, std::string_view name, Kind base, void *(*to_base)(void *));
const TypeDesc *findType(Kind kind);

/** A member and the pointer to call it with, adjusted for whichever type in the chain declared it. */
struct Lookup {
    const MemberDesc *member{nullptr};
    const DynamicDesc *dynamic{nullptr};
    void *self{nullptr};
    std::string_view suffix;
    explicit operator bool() const { return member != nullptr || dynamic != nullptr; }
};

/** Walks the base chain, adjusting `self` at every edge. */
Lookup findMember(Kind kind, std::string_view name, void *self);
/** The prefixed accessor matching `name`, with the remainder in the result's `suffix`. */
Lookup findDynamic(Kind kind, std::string_view name, void *self);

/** Every registered kind, for the describe() introspection the runtime builds its tables from. */
const std::vector<const TypeDesc *> &allTypes();

}  // namespace endstone::node
