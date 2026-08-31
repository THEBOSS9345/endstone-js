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
#include <string_view>

#include <endstone/inventory/item_stack.h>
#include <endstone/inventory/meta/book_meta.h>
#include <endstone/inventory/meta/crossbow_meta.h>
#include <endstone/inventory/meta/item_meta.h>
#include <endstone/inventory/meta/map_meta.h>
#include <endstone/inventory/meta/writable_book_meta.h>
#include <endstone/nbt/tag.h>

namespace endstone::node {

// Custom item data rides the name-keyed accessors: a property called "tag:endstone:timer" addresses
// the key "endstone:timer" in the item's own NBT. That keeps arbitrary per-item data out of the ABI,
// which carries only scalars, and means adding it needed no new entry points.
constexpr std::string_view kTagPrefix = "tag:";

inline std::optional<std::string> tagKeyOf(const std::string_view name)
{
    if (name.size() > kTagPrefix.size() && name.substr(0, kTagPrefix.size()) == kTagPrefix) {
        return std::string{name.substr(kTagPrefix.size())};
    }
    return std::nullopt;
}

/** The tag stored under `key`, or nullptr when the item has no such key. */
inline const nbt::Tag *findTag(const CompoundTag &nbt, const std::string &key)
{
    return nbt.contains(key) ? &nbt.at(key) : nullptr;
}

/** Applies `edit` to a copy of the item's NBT and writes it back. */
template <typename Edit>
void editNbt(ItemStack &stack, Edit &&edit)
{
    auto nbt = stack.getNbt();
    edit(nbt);
    stack.setNbt(nbt);
}

/**
 * @brief Reads then writes back an item's metadata - display name, lore, damage, enchantments.
 *
 * getItemMeta() hands out a copy, so a change only reaches the item once setItemMeta puts it back. Same
 * shape as editNbt, and like it the caller must follow with persistItem() so the stack itself is saved
 * back to the slot it came from.
 */
template <typename Edit>
void editMeta(ItemStack &stack, Edit &&edit)
{
    auto meta = stack.getItemMeta();
    if (!meta) {
        return;
    }
    edit(*meta);
    (void)stack.setItemMeta(meta.get());
}

/**
 * @brief The item's metadata as a subclass, or nullptr when it is not that kind of item.
 *
 * NOT dynamic_cast - the plugin's type_info is a different object from the runtime's, so it silently
 * fails across the library boundary. getType() is a virtual call and always works, and once the kind
 * is known by name a static_cast is safe and needs no RTTI. Same discipline as the event traits.
 */
inline bool metaTypeMatches(const ItemMeta::Type actual, const ItemMeta::Type wanted)
{
    if (actual == wanted) {
        return true;
    }
    // BookMeta derives from WritableBookMeta, so a written book answers a writable-book request.
    return wanted == ItemMeta::Type::WritableBook && actual == ItemMeta::Type::Book;
}

template <typename Meta>
Meta *metaAs(const std::unique_ptr<ItemMeta> &meta)
{
    return (meta && metaTypeMatches(meta->getType(), Meta::MetaType)) ? static_cast<Meta *>(meta.get())
                                                                     : nullptr;
}

/**
 * @brief Which metadata an item carries, as text.
 *
 * Worth exposing because the server decides this, not the item id: at present it produces MapMeta for
 * minecraft:filled_map and the plain base for everything else, so book and crossbow metadata is
 * declared by the API and never actually instantiated. A plugin can check this rather than discover it
 * as a failed write.
 */
inline std::string_view metaTypeName(const ItemMeta::Type type)
{
    switch (type) {
    case ItemMeta::Type::Item: return "item";
    case ItemMeta::Type::Book: return "book";
    case ItemMeta::Type::CrossBow: return "crossbow";
    case ItemMeta::Type::Map: return "map";
    case ItemMeta::Type::WritableBook: return "writableBook";
    }
    return "item";
}

/** Applies `edit` to an item's metadata as `Meta`, writing it back. False when the kind is wrong. */
template <typename Meta, typename Edit>
bool editMetaAs(ItemStack &stack, Edit &&edit)
{
    auto meta = stack.getItemMeta();
    auto *typed = metaAs<Meta>(meta);
    if (!typed) {
        return false;
    }
    edit(*typed);
    (void)stack.setItemMeta(meta.get());
    return true;
}

inline std::string_view generationName(const BookMeta::Generation generation)
{
    switch (generation) {
    case BookMeta::Generation::Original: return "original";
    case BookMeta::Generation::CopyOfOriginal: return "copyOfOriginal";
    case BookMeta::Generation::CopyOfCopy: return "copyOfCopy";
    }
    return "original";
}

inline std::optional<BookMeta::Generation> generationFromName(const std::string_view name)
{
    if (name == "original") return BookMeta::Generation::Original;
    if (name == "copyOfOriginal" || name == "copyoforiginal") return BookMeta::Generation::CopyOfOriginal;
    if (name == "copyOfCopy" || name == "copyofcopy") return BookMeta::Generation::CopyOfCopy;
    return std::nullopt;
}

}  // namespace endstone::node
