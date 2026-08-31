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

// Binds include/endstone/command/command_sender.h, and the Permissible it derives from. Everything
// here reaches Actor and Player too, which is what makes them senders in the types.

#include <endstone/block/block.h>
#include <endstone/command/block_command_sender.h>
#include <endstone/command/command_sender.h>
#include <endstone/permissions/permissible.h>

#include "types/bind.h"

namespace endstone::node {

namespace {

std::string_view permissionLevelName(const PermissionLevel level)
{
    switch (level) {
    case PermissionLevel::Default: return "default";
    case PermissionLevel::Operator: return "operator";
    case PermissionLevel::Console: return "console";
    }
    return "default";
}

}  // namespace

ESN_TYPE(CommandSender, CommandSender)
{
    b.ro("name", &CommandSender::getName);
    // There is no isOp on CommandSender; the console reports Operator or Console here.
    b.ro("isOp", [](const CommandSender &sender) { return sender.getPermissionLevel() != PermissionLevel::Default; });
    b.ro("isConsole", [](const CommandSender &sender) { return sender.asConsole() != nullptr; });
    b.ro("isBlock", [](const CommandSender &sender) { return sender.asBlock() != nullptr; });
    b.ro("permissionLevel",
         [](const CommandSender &sender) { return std::string{permissionLevelName(sender.getPermissionLevel())}; });

    // A command block reaches a handler with nothing to say where it is. This is that position.
    b.ro("block", [](const CommandSender &sender) {
        auto *as_block = sender.asBlock();
        return as_block ? as_block->getBlock() : std::unique_ptr<Block>{};
    });

    // The node rides the accessor name, so a permission check is a read rather than a call.
    b.dynamic("permission:", ValueKind::Bool,
              [](CommandSender &sender, const std::string_view node, Binder &, Value &out) {
                  out.kind = ValueKind::Bool;
                  out.boolean = sender.hasPermission(std::string{node});
                  return static_cast<esn_status>(ESN_OK);
              });
    // Whether the node is set either way, as opposed to falling back to its default.
    b.dynamic("permissionSet:", ValueKind::Bool,
              [](CommandSender &sender, const std::string_view node, Binder &, Value &out) {
                  out.kind = ValueKind::Bool;
                  out.boolean = sender.isPermissionSet(std::string{node});
                  return static_cast<esn_status>(ESN_OK);
              });

    b.method("sendMessage", [](CommandSender &sender, const Args &args, esn_handle *) {
        sender.sendMessage(Message{args.str(0)});
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("sendErrorMessage", [](CommandSender &sender, const Args &args, esn_handle *) {
        sender.sendErrorMessage(Message{args.str(0)});
        return static_cast<esn_status>(ESN_OK);
    });

    // removePermission overrides the node to false rather than detaching the grant, which is what
    // gating needs: detaching would mean holding an attachment past the callback that created it.
    b.method("addPermission", [](CommandSender &sender, const Args &args, esn_handle *) {
        const auto node = args.str(0);
        if (node.empty()) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        (void)sender.addAttachment(args.binder.owner(), node, args.number(0, 1) != 0);
        sender.recalculatePermissions();
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("removePermission", [](CommandSender &sender, const Args &args, esn_handle *) {
        const auto node = args.str(0);
        if (node.empty()) {
            return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
        }
        (void)sender.addAttachment(args.binder.owner(), node, false);
        sender.recalculatePermissions();
        return static_cast<esn_status>(ESN_OK);
    });
    b.method("recalculatePermissions", &CommandSender::recalculatePermissions);
}

}  // namespace endstone::node
