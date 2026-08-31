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

// Binds include/endstone/inventory/inventory.h and player_inventory.h.

#include <endstone/inventory/inventory.h>
#include <endstone/inventory/item_stack.h>
#include <endstone/inventory/player_inventory.h>

#include "types/bind.h"

namespace endstone::node {

namespace {

/** A handle that rode in on an accessor name, e.g. "containsStack:41,2". 0 on anything else. */
esn_handle handleFromDigits(const std::string_view digits)
{
    esn_handle handle = 0;
    for (const auto digit : digits) {
        if (digit < '0' || digit > '9') {
            return 0;
        }
        handle = handle * 10 + static_cast<esn_handle>(digit - '0');
    }
    return handle;
}

const ItemStack *stackFromDigits(Binder &binder, const std::string_view digits)
{
    return static_cast<const ItemStack *>(binder.resolve(handleFromDigits(digits), Kind::ItemStack));
}

/**
 * An item as described by its arguments: a type plus optional amount and data, so nothing has to
 * construct an ItemStack from JavaScript. An empty type means "no item", which clears the slot.
 */
std::optional<ItemStack> describedItem(const Args &args, const std::size_t number_base)
{
    const auto type = args.str(0);
    if (type.empty()) {
        return std::nullopt;
    }
    return ItemStack{type, static_cast<int>(args.number(number_base, 1)),
                     static_cast<int>(args.number(number_base + 1, 0))};
}

}  // namespace

ESN_TYPE(Inventory, Inventory)
{
    b.ro("isEmpty", &Inventory::isEmpty);
    b.ro("size", &Inventory::getSize);
    b.ro("maxStackSize", &Inventory::getMaxStackSize);
    b.ro("firstEmpty", &Inventory::firstEmpty);

    // These take another stack, so it rides the accessor name the way a custom NBT key does. Matching
    // is against a whole stack rather than a type id, so NBT, enchantments and custom names all count.
    b.dynamic("containsStack:", ValueKind::Bool,
              [](Inventory &inventory, const std::string_view request, Binder &binder, Value &out) {
                  const auto comma = request.find(',');
                  const auto *against = stackFromDigits(binder, request.substr(0, comma));
                  if (against == nullptr) {
                      return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
                  }
                  out.kind = ValueKind::Bool;
                  out.boolean = comma == std::string_view::npos
                                    ? inventory.contains(*against)
                                    : inventory.containsAtLeast(*against, std::stoi(std::string{request.substr(comma + 1)}));
                  return static_cast<esn_status>(ESN_OK);
              });
    b.dynamic("firstStack:", ValueKind::Int,
              [](Inventory &inventory, const std::string_view request, Binder &binder, Value &out) {
                  const auto *against = stackFromDigits(binder, request);
                  if (against == nullptr) {
                      return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
                  }
                  out.kind = ValueKind::Int;
                  out.integer = inventory.first(*against);
                  return static_cast<esn_status>(ESN_OK);
              });
    // allStacks:<handle> - every slot holding a matching stack, one per line.
    b.dynamic("allStacks:", ValueKind::String,
              [](Inventory &inventory, const std::string_view request, Binder &binder, Value &out) {
                  const auto *against = stackFromDigits(binder, request);
                  if (against == nullptr) {
                      return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
                  }
                  out.kind = ValueKind::String;
                  for (const auto &[slot, stack] : inventory.all(*against)) {
                      if (!out.text.empty()) {
                          out.text += '\n';
                      }
                      out.text += std::to_string(slot);
                  }
                  return static_cast<esn_status>(ESN_OK);
              });

    b.method("getItem", [](Inventory &inventory, const Args &args, esn_handle *out_handle) {
        const auto slot = static_cast<int>(args.number(0));
        auto item = inventory.getItem(slot);
        if (out_handle == nullptr) {
            return static_cast<esn_status>(ESN_OK);
        }
        // Paired with a writeback, so a change to the stack reaches the slot it came from.
        *out_handle = item ? args.binder.ownItem(std::move(*item),
                                                 [&inventory, slot](const ItemStack &changed) {
                                                     inventory.setItem(slot, changed);
                                                 })
                           : 0;
        return static_cast<esn_status>(ESN_OK);
    });
    // setItem(slot, item): numbers are the slot, then the item's amount and data.
    b.method("setItem", [](Inventory &inventory, const Args &args, esn_handle *) {
        inventory.setItem(static_cast<int>(args.number(0)), describedItem(args, 1));
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("addItem", [](Inventory &inventory, const Args &args, esn_handle *) {
        if (auto item = describedItem(args, 0)) {
            (void)inventory.addItem({*item});
        }
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("removeItem", [](Inventory &inventory, const Args &args, esn_handle *) {
        if (auto item = describedItem(args, 0)) {
            (void)inventory.removeItem({*item});
        }
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("remove", [](Inventory &inventory, const Args &args, esn_handle *) {
        inventory.remove(args.str(0));
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("removeStack", [](Inventory &inventory, const Args &args, esn_handle *) {
        auto *against = static_cast<ItemStack *>(args.binder.resolve(args.handleAt(0), Kind::ItemStack));
        if (against == nullptr) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        inventory.remove(*against);
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("clear", [](Inventory &inventory, const Args &args, esn_handle *) {
        // clear() empties everything; clear(slot) empties one slot.
        if (args.number_count > 0) {
            inventory.clear(static_cast<int>(args.number(0)));
        }
        else {
            inventory.clear();
        }
        return static_cast<esn_status>(ESN_OK);
    });
}

ESN_SUBTYPE(PlayerInventory, PlayerInventory, Inventory)
{
    b.ro("heldItemSlot", &PlayerInventory::getHeldItemSlot);
    b.method("setHeldItemSlot", &PlayerInventory::setHeldItemSlot);

    // The equipment slots. Each reads as a stack paired with the setter that puts it back, so
    // `player.inventory.helmet.damage = 10` reaches the armour rather than a copy of it. An empty slot
    // reads as handle 0, which the runtime turns into null.
    using Getter = std::optional<ItemStack> (PlayerInventory::*)() const;
    using Setter = void (PlayerInventory::*)(std::optional<ItemStack>);
    const auto equipment = [&b](const std::string_view name, const Getter getter, const Setter setter) {
        b.handle(name, [getter, setter](PlayerInventory &inventory, Binder &binder) -> esn_handle {
            auto item = (inventory.*getter)();
            if (!item) {
                return 0;
            }
            return binder.ownItem(std::move(*item), [&inventory, setter](const ItemStack &changed) {
                (inventory.*setter)(changed);
            });
        });
    };

    equipment("helmet", &PlayerInventory::getHelmet, &PlayerInventory::setHelmet);
    equipment("chestplate", &PlayerInventory::getChestplate, &PlayerInventory::setChestplate);
    equipment("leggings", &PlayerInventory::getLeggings, &PlayerInventory::setLeggings);
    equipment("boots", &PlayerInventory::getBoots, &PlayerInventory::setBoots);
    equipment("itemInMainHand", &PlayerInventory::getItemInMainHand, &PlayerInventory::setItemInMainHand);
    equipment("itemInOffHand", &PlayerInventory::getItemInOffHand, &PlayerInventory::setItemInOffHand);

    // The write side of those, taking a described item so JavaScript need not build an ItemStack.
    // Written out one by one rather than looped: scripts/check_methods.py reads the literal names, and
    // a method it cannot see is one it cannot keep in step with the runtime.
    const auto setter = [](const Setter set) {
        return [set](PlayerInventory &inventory, const Args &args, esn_handle *) {
            (inventory.*set)(describedItem(args, 0));
            return static_cast<esn_status>(ESN_OK);
        };
    };
    b.method("setHelmet", setter(&PlayerInventory::setHelmet));
    b.method("setChestplate", setter(&PlayerInventory::setChestplate));
    b.method("setLeggings", setter(&PlayerInventory::setLeggings));
    b.method("setBoots", setter(&PlayerInventory::setBoots));
    b.method("setItemInMainHand", setter(&PlayerInventory::setItemInMainHand));
    b.method("setItemInOffHand", setter(&PlayerInventory::setItemInOffHand));
}

}  // namespace endstone::node
