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

// Binds include/endstone/inventory/item_stack.h and the metadata under inventory/meta/.
//
// The least mechanical type here, and the reason the escape hatches exist. An item stack is a value
// rather than a reference, its metadata is a class hierarchy chosen at runtime, and its custom data is
// arbitrary NBT - none of which a plain getter signature can express.

#include <format>

#include <endstone/enchantments/enchantment.h>
#include <endstone/inventory/item_stack.h>
#include <endstone/inventory/item_type.h>
#include <endstone/map/map_view.h>

#include "types/bind.h"
#include "types/inventory/item_meta.h"
#include "types/records.h"

namespace endstone::node {

namespace {

std::vector<std::string> splitLines(const std::string_view text)
{
    return text.empty() ? std::vector<std::string>{} : splitOn(text, '\n');
}

std::string joinLines(const std::vector<std::string> &lines)
{
    std::string joined;
    for (const auto &line : lines) {
        if (!joined.empty()) {
            joined += '\n';
        }
        joined += line;
    }
    return joined;
}

}  // namespace

ESN_TYPE(ItemStack, ItemStack)
{
    b.rw("type", [](const ItemStack &stack) { return std::string{stack.getType().getId()}; },
         [](ItemStack &stack, const Value &in, Binder &) {
             stack.setType(in.text);
             return static_cast<esn_status>(ESN_OK);
         });
    b.rw("amount", &ItemStack::getAmount, &ItemStack::setAmount);
    b.rw("data", &ItemStack::getData, &ItemStack::setData);
    b.ro("maxStackSize", &ItemStack::getMaxStackSize);
    b.ro("translationKey", &ItemStack::getTranslationKey);
    // 0 for anything that does not wear out. Paired with damage, this is a durability bar.
    b.ro("maxDurability", [](const ItemStack &stack) { return stack.getType().getMaxDurability(); });
    b.ro("hasItemMeta", &ItemStack::hasItemMeta);

    // Which metadata class this item actually has. On a release where the core does not instantiate a
    // subclass the item reports plain "item", which is how a plugin tells "not a book" from "empty".
    b.ro("metaType", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        return std::string{meta ? metaTypeName(meta->getType()) : std::string_view{"item"}};
    });

    // --- metadata that every item has ---------------------------------------------------------------
    // An item with no metadata reads as empty rather than failing, so `item.displayName || item.type`
    // is the natural idiom.
    b.rw("displayName",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             return meta && meta->hasDisplayName() ? meta->getDisplayName() : std::string{};
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             // An empty string clears the custom name, which is what setDisplayName(nullopt) does.
             editMeta(stack, [&](ItemMeta &meta) {
                 meta.setDisplayName(in.text.empty() ? std::nullopt : std::optional{in.text});
             });
             return static_cast<esn_status>(ESN_OK);
         });

    b.rw("unbreakable",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             return meta && meta->isUnbreakable();
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             editMeta(stack, [&](ItemMeta &meta) { meta.setUnbreakable(in.boolean); });
             return static_cast<esn_status>(ESN_OK);
         });

    b.rw("damage",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             return meta ? meta->getDamage() : 0;
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             editMeta(stack, [&](ItemMeta &meta) { meta.setDamage(static_cast<int>(in.integer)); });
             return static_cast<esn_status>(ESN_OK);
         });
    b.rw("repairCost",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             return meta ? meta->getRepairCost() : 0;
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             editMeta(stack, [&](ItemMeta &meta) { meta.setRepairCost(static_cast<int>(in.integer)); });
             return static_cast<esn_status>(ESN_OK);
         });

    // Lore is newline-joined both ways, so a line containing a newline is not representable - which
    // matches the client, where each entry is its own line.
    b.rw("loreList",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             return meta && meta->hasLore() ? joinLines(meta->getLore()) : std::string{};
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             const auto lines = splitLines(in.text);
             editMeta(stack, [&](ItemMeta &meta) {
                 meta.setLore(lines.empty() ? std::nullopt : std::optional{lines});
             });
             return static_cast<esn_status>(ESN_OK);
         });

    // "<id>,<level>" per line.
    b.ro("enchantList", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        std::string joined;
        if (meta && meta->hasEnchants()) {
            for (const auto &[enchantment, level] : meta->getEnchants()) {
                if (enchantment == nullptr) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += static_cast<std::string>(enchantment->getId()) + "," + std::to_string(level);
            }
        }
        return joined;
    });

    // --- metadata that only some items have ---------------------------------------------------------
    // Reading the wrong kind of item answers empty or 0 rather than erroring, so a plugin can ask
    // without first checking metaType. Writing is stricter - see the setters below.
    b.ro("hasMapId", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *map = metaAs<MapMeta>(meta);
        return map != nullptr && map->hasMapId();
    });
    b.ro("hasMapView", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *map = metaAs<MapMeta>(meta);
        return map != nullptr && map->hasMapView();
    });
    b.ro("hasTitle", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *book = metaAs<BookMeta>(meta);
        return book != nullptr && book->hasTitle();
    });
    b.ro("hasAuthor", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *book = metaAs<BookMeta>(meta);
        return book != nullptr && book->hasAuthor();
    });
    b.ro("hasGeneration", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *book = metaAs<BookMeta>(meta);
        return book != nullptr && book->hasGeneration();
    });
    b.ro("hasPages", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *writable = metaAs<WritableBookMeta>(meta);
        return writable != nullptr && writable->hasPages();
    });
    b.ro("hasChargedProjectiles", [](const ItemStack &stack) {
        const auto meta = stack.getItemMeta();
        const auto *crossbow = metaAs<CrossbowMeta>(meta);
        return crossbow != nullptr && crossbow->hasChargedProjectiles();
    });

    // Writes reach the world only for a stack the server handed out live, such as the one on
    // PlayerDropItemEvent. A stack read out of an inventory is a copy - change the inventory.
    // Unlike the reads above, a write to the wrong kind of item is an error rather than a no-op: it
    // means the plugin believed something false about the item.
    const auto bookSetter = [](const auto field) {
        return [field](ItemStack &stack, const Value &in, Binder &) {
            const auto changed = editMetaAs<BookMeta>(stack, [&](BookMeta &book) { field(book, in.text); });
            return static_cast<esn_status>(changed ? ESN_OK : ESN_ERR_WRONG_TYPE);
        };
    };
    b.rw("title",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             const auto *book = metaAs<BookMeta>(meta);
             return book && book->hasTitle() ? book->getTitle() : std::string{};
         },
         bookSetter([](BookMeta &book, const std::string &text) {
             book.setTitle(text.empty() ? std::nullopt : std::optional{text});
         }));
    b.rw("author",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             const auto *book = metaAs<BookMeta>(meta);
             return book && book->hasAuthor() ? book->getAuthor() : std::string{};
         },
         bookSetter([](BookMeta &book, const std::string &text) {
             book.setAuthor(text.empty() ? std::nullopt : std::optional{text});
         }));
    b.rw("generation",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             const auto *book = metaAs<BookMeta>(meta);
             const auto generation = book ? book->getGeneration() : std::nullopt;
             return generation ? std::string{generationName(*generation)} : std::string{};
         },
         bookSetter([](BookMeta &book, const std::string &text) { book.setGeneration(generationFromName(text)); }));

    b.rw("pageList",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             const auto *writable = metaAs<WritableBookMeta>(meta);
             return writable ? joinLines(writable->getPages()) : std::string{};
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             const auto pages = splitLines(in.text);
             const auto changed =
                 editMetaAs<WritableBookMeta>(stack, [&](WritableBookMeta &book) { book.setPages(pages); });
             return static_cast<esn_status>(changed ? ESN_OK : ESN_ERR_WRONG_TYPE);
         });

    b.rw("mapId",
         [](const ItemStack &stack) {
             const auto meta = stack.getItemMeta();
             const auto *map = metaAs<MapMeta>(meta);
             return map && map->hasMapId() ? map->getMapId() : 0;
         },
         [](ItemStack &stack, const Value &in, Binder &) {
             const auto id = static_cast<int>(in.integer);
             const auto changed = editMetaAs<MapMeta>(stack, [&](MapMeta &map) { map.setMapId(id); });
             return static_cast<esn_status>(changed ? ESN_OK : ESN_ERR_WRONG_TYPE);
         });

    // --- methods ------------------------------------------------------------------------------------
    // Writes reach the world only for a stack the server handed out live. A stack read out of an
    // inventory is a copy, and the bridge writes it back through the slot it came from.

    // addEnchant(id, level) - forced, so a level above vanilla's maximum is allowed, which is the point
    // of doing it from a plugin rather than an anvil.
    b.method("addEnchant", [](ItemStack &stack, const Args &args, esn_handle *) {
        const auto id = args.str(0);
        if (id.empty()) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        const auto level = static_cast<int>(args.number(0, 1));
        editMeta(stack, [&](ItemMeta &meta) { (void)meta.addEnchant(EnchantmentId{id}, level, true); });
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("removeEnchant", [](ItemStack &stack, const Args &args, esn_handle *) {
        const auto id = args.str(0);
        editMeta(stack, [&](ItemMeta &meta) { (void)meta.removeEnchant(EnchantmentId{id}); });
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("removeEnchants", [](ItemStack &stack, const Args &, esn_handle *) {
        editMeta(stack, [](ItemMeta &meta) { meta.removeEnchants(); });
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("removeTag", [](ItemStack &stack, const Args &args, esn_handle *) {
        const auto key = args.str(0);
        editNbt(stack, [&](CompoundTag &nbt) { (void)nbt.erase(key); });
        return static_cast<esn_status>(ESN_OK);
    });

    // setMapView(map) - what turns a blank map item into the one server.createMap() made. Takes a
    // handle rather than an id because that is the object a plugin already has.
    b.method("setMapView", [](ItemStack &stack, const Args &args, esn_handle *) {
        auto *map = static_cast<MapView *>(args.binder.resolve(args.handleAt(0), Kind::MapView));
        if (map == nullptr) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        const auto changed = editMetaAs<MapMeta>(stack, [&](MapMeta &meta) { meta.setMapView(map); });
        return static_cast<esn_status>(changed ? ESN_OK : ESN_ERR_WRONG_TYPE);
    });
    b.method("addPage", [](ItemStack &stack, const Args &args, esn_handle *) {
        const auto page = args.str(0);
        const auto changed =
            editMetaAs<WritableBookMeta>(stack, [&](WritableBookMeta &book) { book.addPage({page}); });
        return static_cast<esn_status>(changed ? ESN_OK : ESN_ERR_WRONG_TYPE);
    });
    // addChargedProjectile(type, amount, data) - described like any other item argument.
    b.method("addChargedProjectile", [](ItemStack &stack, const Args &args, esn_handle *) {
        const auto type = args.str(0);
        if (type.empty()) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        const ItemStack projectile{type, static_cast<int>(args.number(0, 1)), static_cast<int>(args.number(1, 0))};
        const auto changed = editMetaAs<CrossbowMeta>(
            stack, [&](CrossbowMeta &crossbow) { crossbow.addChargedProjectile(projectile); });
        return static_cast<esn_status>(changed ? ESN_OK : ESN_ERR_WRONG_TYPE);
    });

    // A detached copy: no writeback, so changing it cannot reach the slot the original came from. That
    // is the point - it is how a plugin keeps an item's contents past the callback.
    b.method("clone", [](ItemStack &stack, const Args &args, esn_handle *out_handle) {
        if (out_handle != nullptr) {
            *out_handle = args.binder.ownItem(stack, nullptr);
        }
        return static_cast<esn_status>(ESN_OK);
    });

    // --- custom NBT ---------------------------------------------------------------------------------
    // The keys the item carries, newline-joined; the runtime splits them into an array.
    b.ro("tagKeyList", [](const ItemStack &stack) {
        std::string joined;
        for (const auto &[key, value] : stack.getNbt()) {
            if (!joined.empty()) {
                joined += '\n';
            }
            joined += key;
        }
        return joined;
    });

    // A property called "tag:endstone:timer" addresses that key in the item's own NBT, which keeps
    // arbitrary per-item data out of the ABI. Each accessor answers the tag types it can carry and
    // defers the rest, so the runtime's probe lands on the right one.
    b.dynamic("tag:", ValueKind::Int, [](ItemStack &stack, const std::string_view key, Binder &, Value &out) {
        const auto nbt = stack.getNbt();
        const auto *tag = findTag(nbt, std::string{key});
        if (tag == nullptr) {
            return static_cast<esn_status>(ESN_ERR_NO_SUCH_MEMBER);
        }
        out.kind = ValueKind::Int;
        // NBT has no boolean, so a byte covers both flags and small integers; it reads back as a
        // number, which means a JavaScript `true` returns as 1.
        switch (tag->type()) {
        case nbt::Type::Byte: out.integer = tag->get<ByteTag>().value(); return static_cast<esn_status>(ESN_OK);
        case nbt::Type::Short: out.integer = tag->get<ShortTag>().value(); return static_cast<esn_status>(ESN_OK);
        case nbt::Type::Int: out.integer = tag->get<IntTag>().value(); return static_cast<esn_status>(ESN_OK);
        case nbt::Type::Long: out.integer = tag->get<LongTag>().value(); return static_cast<esn_status>(ESN_OK);
        default: return static_cast<esn_status>(ESN_ERR_WRONG_TYPE);
        }
    });
    b.dynamic("tag:", ValueKind::Double, [](ItemStack &stack, const std::string_view key, Binder &, Value &out) {
        const auto nbt = stack.getNbt();
        const auto *tag = findTag(nbt, std::string{key});
        if (tag == nullptr) {
            return static_cast<esn_status>(ESN_ERR_NO_SUCH_MEMBER);
        }
        out.kind = ValueKind::Double;
        if (tag->type() == nbt::Type::Float) {
            out.real = tag->get<FloatTag>().value();
            return static_cast<esn_status>(ESN_OK);
        }
        if (tag->type() == nbt::Type::Double) {
            out.real = tag->get<DoubleTag>().value();
            return static_cast<esn_status>(ESN_OK);
        }
        return static_cast<esn_status>(ESN_ERR_WRONG_TYPE);
    });
    b.dynamic("tag:", ValueKind::String, [](ItemStack &stack, const std::string_view key, Binder &, Value &out) {
        const auto nbt = stack.getNbt();
        const auto *tag = findTag(nbt, std::string{key});
        if (tag == nullptr) {
            return static_cast<esn_status>(ESN_ERR_NO_SUCH_MEMBER);
        }
        out.kind = ValueKind::String;
        if (tag->type() == nbt::Type::String) {
            out.text = tag->get<StringTag>().value();
            return static_cast<esn_status>(ESN_OK);
        }
        // Compounds, lists and arrays have no scalar form, so they come back as SNBT text - enough to
        // inspect data an addon or another plugin wrote, without a tag-tree walker here.
        switch (tag->type()) {
        case nbt::Type::Compound:
        case nbt::Type::List:
        case nbt::Type::ByteArray:
        case nbt::Type::IntArray:
            out.text = std::format("{}", *tag);
            return static_cast<esn_status>(ESN_OK);
        default:
            // Numeric: let the int or double probe answer instead.
            return static_cast<esn_status>(ESN_ERR_WRONG_TYPE);
        }
    });

    // enchant:<id> - whether the item carries that enchantment at all.
    b.dynamic("enchant:", ValueKind::Bool, [](ItemStack &stack, const std::string_view id, Binder &, Value &out) {
        const auto meta = stack.getItemMeta();
        out.kind = ValueKind::Bool;
        out.boolean = meta && meta->hasEnchant(EnchantmentId{std::string{id}});
        return static_cast<esn_status>(ESN_OK);
    });
    // enchantLevel:<id> - 0 when the item does not have it, which is what getEnchantLevel returns.
    b.dynamic("enchantLevel:", ValueKind::Int, [](ItemStack &stack, const std::string_view id, Binder &, Value &out) {
        const auto meta = stack.getItemMeta();
        out.kind = ValueKind::Int;
        out.integer = meta ? meta->getEnchantLevel(EnchantmentId{std::string{id}}) : 0;
        return static_cast<esn_status>(ESN_OK);
    });
    // Whether that enchantment fights one the item already has, e.g. sharpness against smite.
    b.dynamic("conflicts:", ValueKind::Bool, [](ItemStack &stack, const std::string_view id, Binder &, Value &out) {
        const auto meta = stack.getItemMeta();
        out.kind = ValueKind::Bool;
        out.boolean = meta && meta->hasConflictingEnchant(EnchantmentId{std::string{id}});
        return static_cast<esn_status>(ESN_OK);
    });
    // similarTo:<handle> - a read cannot take an argument, so the other stack rides the accessor name
    // the same way a custom NBT key does.
    b.dynamic("similarTo:", ValueKind::Bool,
              [](ItemStack &stack, const std::string_view digits, Binder &binder, Value &out) {
                  esn_handle other = 0;
                  for (const auto digit : digits) {
                      if (digit < '0' || digit > '9') {
                          return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
                      }
                      other = other * 10 + static_cast<esn_handle>(digit - '0');
                  }
                  const auto *against = static_cast<const ItemStack *>(binder.resolve(other, Kind::ItemStack));
                  if (against == nullptr) {
                      return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
                  }
                  out.kind = ValueKind::Bool;
                  out.boolean = stack.isSimilar(*against);
                  return static_cast<esn_status>(ESN_OK);
              });
}

}  // namespace endstone::node
