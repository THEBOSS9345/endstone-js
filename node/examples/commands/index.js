// Slash commands, registered from JavaScript.
//
// Every command here is registered at the TOP LEVEL of the module, not in onEnable. That is what gets
// them into Bedrock's command registry, so the client lists them, completes their arguments, and
// offers the enum values in `usages`. Registering in onEnable still works, but it is too late for the
// client to learn about the command - the runtime warns when that happens.
//
// In game, try:  /whereami   /warp shop   /heal 10   /gm creative   /jscommands
// Every one of them should autocomplete as you type.

import { commands, events, server, logger } from "@endstone-js/server";

const WARPS = {
    spawn: { x: 0, y: 80, z: 0 },
    shop: { x: 64, y: 72, z: 64 },
    arena: { x: -128, y: 70, z: 128 },
};

// No arguments, so `usages` can be left out entirely - a bare /whereami is implied.
commands.register("whereami", (sender) => {
    if (sender.isConsole) {
        sender.sendMessage("The console is not anywhere in particular.");
        return;
    }
    const { x, y, z, dimension } = sender.location;
    sender.sendMessage(`§7You are at §f${x.toFixed(1)}, ${y.toFixed(1)}, ${z.toFixed(1)} §7in §f${dimension}`);
}, { description: "Tells you where you are." });

// A custom enum: the client completes spawn|shop|arena and rejects anything else, so the handler does
// not have to validate args[0] itself.
commands.register("warp", (sender, args) => {
    if (sender.isConsole) {
        sender.sendMessage("Only a player can warp.");
        return;
    }
    const target = WARPS[args[0]];
    if (!target) {
        sender.sendErrorMessage(`Unknown warp '${args[0]}'.`);
        return;
    }
    sender.teleport(target, { rotation: { yaw: 0, pitch: 0 } });
    sender.sendMessage(`§aWarped you to ${args[0]}.`);
}, {
    description: "Warps you to a named place.",
    usages: ["/warp <spawn|shop|arena>"],
    aliases: ["w"],
});

// An optional typed argument. `int` means the client only accepts whole numbers, but it still arrives
// as a string, so it needs converting.
commands.register("heal", (sender, args) => {
    if (sender.isConsole) {
        sender.sendMessage("The console has no health to restore.");
        return;
    }
    const amount = args.length ? Number(args[0]) : sender.maxHealth;
    sender.health = Math.min(sender.health + amount, sender.maxHealth);
    sender.sendMessage(`§aHealed to ${sender.health}/${sender.maxHealth}.`);
}, {
    description: "Restores your health.",
    usages: ["/heal [amount: int]"],
    op: true,
});

// Two usages for one command: with and without a target. Both autocomplete.
commands.register("gm", (sender, args) => {
    const mode = args[0];
    const targetName = args[1];
    if (targetName) {
        // No player lookup in the API yet, so hand the work to the server's own command.
        if (sender.isConsole) {
            sender.sendMessage("Run this in-game, or use the vanilla /gamemode.");
            return;
        }
        sender.performCommand(`gamemode ${mode} "${targetName}"`);
        sender.sendMessage(`§aSet ${targetName} to ${mode}.`);
        return;
    }
    if (sender.isConsole) {
        sender.sendMessage("The console is not a player; name a target.");
        return;
    }
    sender.gameMode = mode;
    sender.sendMessage(`§aYou are now in ${sender.gameMode}.`);
}, {
    description: "Changes a game mode.",
    usages: [
        "/gm <survival|creative|adventure|spectator>",
        "/gm <survival|creative|adventure|spectator> <target: string>",
    ],
    op: true,
});

// commands.list() is how a plugin builds its own help. `declared` tells you whether the client knows
// about a command - false means it was registered too late and only works by interception.
commands.register("jscommands", (sender) => {
    const all = commands.list();
    sender.sendMessage(`§7${all.length} command(s) registered from JavaScript:`);
    for (const command of all) {
        const flags = [command.op ? "op" : null, command.declared ? null : "not in client list"]
            .filter(Boolean)
            .join(", ");
        sender.sendMessage(`§f/${command.name}§7${flags ? ` (${flags})` : ""} - ${command.description}`);
    }
}, { description: "Lists the commands this server's JavaScript plugins registered." });

// --- inventory ---------------------------------------------------------------------------------
// An item is a plain object, or just a type string. Nothing constructs an ItemStack.
commands.register("kit", (sender, args) => {
    if (sender.isConsole) {
        sender.sendMessage("Only a player has an inventory.");
        return;
    }
    const inventory = sender.inventory;

    if (args[0] === "clear") {
        inventory.clear();
        sender.sendMessage("§aCleared your inventory.");
        return;
    }

    inventory.setHelmet({ type: "minecraft:diamond_helmet" });
    inventory.setChestplate({ type: "minecraft:diamond_chestplate" });
    inventory.setLeggings({ type: "minecraft:diamond_leggings" });
    inventory.setBoots({ type: "minecraft:diamond_boots" });
    inventory.setItemInMainHand({ type: "minecraft:diamond_sword" });
    inventory.addItem({ type: "minecraft:golden_apple", amount: 8 });
    inventory.setItem(8, "minecraft:torch");   // a bare string means amount 1
    sender.sendMessage("§aKitted out.");
}, {
    description: "Gives you a diamond kit.",
    usages: ["/kit [clear]"],
    op: true,
});

// contents/countOf/first are plain JavaScript over size and getItem, so they cost nothing extra.
commands.register("inv", (sender) => {
    if (sender.isConsole) {
        sender.sendMessage("Only a player has an inventory.");
        return;
    }
    const inventory = sender.inventory;
    const used = inventory.contents().filter(Boolean).length;
    sender.sendMessage(`§7${used}/${inventory.size} slots used, first empty is ${inventory.firstEmpty}`);
    sender.sendMessage(`§7holding slot ${inventory.heldItemSlot}: §f${inventory.itemInMainHand?.type ?? "nothing"}`);
    sender.sendMessage(`§7helmet: §f${inventory.helmet?.type ?? "none"}`);
    sender.sendMessage(`§7torches: §f${inventory.countOf("minecraft:torch")}`);
    if (inventory.contains("minecraft:diamond_sword")) {
        sender.sendMessage(`§7sword is in slot §f${inventory.first("minecraft:diamond_sword")}`);
    }
}, { description: "Describes your inventory." });

export default {
    onEnable() {
        // Commands are registered above, at the top level - see the note at the top of this file.
        // Events are fine to subscribe here.

        // playerDeath is the only death event carrying the announcement, and it is writable.
        events.playerDeath((event) => {
            const cause = event.damageSource.type;
            const killer = event.damageSource.actor;
            logger.info(`[commands] ${event.player.name} died of ${cause}` +
                        (killer ? ` (by ${killer.type})` : "") +
                        ` - "${event.deathMessage}"`);
            event.deathMessage = `§c${event.player.name} §7was undone by §f${cause}`;
        });

        events.pluginEnable((event) => {
            logger.info(`[commands] plugin enabled: ${event.plugin.fullName}`);
        });

        logger.info(`commands example ready on ${server.minecraftVersion}`);
    },
};
