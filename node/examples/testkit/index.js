// A test harness for the whole API surface, driven by slash commands.
//
// Every command reports what it did in chat and in the server log, so a failure shows up as either a
// missing line or an "undefined". Run /jstest for the index once in game.
//
// Commands are registered at the top level so the client autocompletes them - see node/RULES.md rule 3.

import { commands, events, packets, scheduler, server, logger } from "@endstone-js/server";

const say = (sender, text) => {
    logger.info(`[testkit] ${text.replace(/§./g, "")}`);
    sender.sendMessage(text);
};

const needPlayer = (sender) => {
    if (sender.isConsole) {
        sender.sendMessage("Run this in game.");
        return false;
    }
    return true;
};

commands.register("jstest", (sender) => {
    say(sender, "§6testkit commands:");
    for (const c of commands.list()) {
        if (c.name === "jstest") continue;
        say(sender, `  §f/${c.name} §7${c.description}`);
    }
}, { description: "Lists the testkit commands." });

// --- server, level, dimension --------------------------------------------------------------------
commands.register("jsserver", (sender) => {
    say(sender, `§7${server.name} ${server.version} mc=${server.minecraftVersion} protocol=${server.protocolVersion}`);
    say(sender, `§7port=${server.port}/${server.portV6} max=${server.maxPlayers} onlineMode=${server.onlineMode}`);
    say(sender, `§7tps=${server.currentTicksPerSecond.toFixed(2)} avg=${server.averageTicksPerSecond.toFixed(2)}`);
    say(sender, `§7mspt=${server.currentMillisecondsPerTick.toFixed(2)} tickUsage=${server.currentTickUsage.toFixed(1)}%`);
    say(sender, `§7primaryThread=${server.isPrimaryThread} players=${server.onlinePlayerCount}`);
    const level = server.level;
    say(sender, `§7level="${level.name}" seed=${level.seed} time=${level.time} dimensions=${level.dimensionCount}`);
}, { description: "Server, level and tick metrics." });

commands.register("jsdim", (sender) => {
    const dim = server.level.getDimension("overworld");
    if (!dim) return say(sender, "§cgetDimension returned null");
    say(sender, `§7dimension=${dim.name} actors=${dim.actorCount} level=${dim.level.name}`);
    const ground = dim.getHighestBlockAt({ x: 0, z: 0 });
    say(sender, `§7highest at 0,0 = ${ground.type} y=${ground.location.y}`);
    say(sender, `§7block at 0,64,0 = ${dim.getBlockAt({ x: 0, y: 64, z: 0 }).type}`);
    say(sender, `§7loadedChunks=${dim.loadedChunks.length}`);
}, { description: "Dimension reads: blocks, actors, chunks." });

commands.register("jsspawn", (sender, args) => {
    if (!needPlayer(sender)) return;
    const dim = server.level.getDimension(sender.location.dimension) ?? server.level.getDimension("overworld");
    const type = args[0] ?? "minecraft:cow";
    const at = { x: sender.location.x, y: sender.location.y + 1, z: sender.location.z };
    const spawned = dim.spawnActor(type, at);
    if (!spawned) return say(sender, `§cspawnActor("${type}") returned null`);
    spawned.addScoreboardTag("testkit");
    say(sender, `§aspawned ${spawned.type} runtimeId=${spawned.runtimeId} tags=${JSON.stringify(spawned.scoreboardTags)}`);
}, { description: "Spawns an actor at your feet.", usages: ["/jsspawn [type: string]"], op: true });

commands.register("jsdrop", (sender) => {
    if (!needPlayer(sender)) return;
    const dim = server.level.getDimension(sender.location.dimension);
    const dropped = dim.dropItem(sender.location, { type: "minecraft:diamond", amount: 3 });
    say(sender, dropped ? `§adropped ${dropped.type}` : "§cdropItem returned null");
}, { description: "Drops 3 diamonds at your feet.", op: true });

// --- inventory and item data ---------------------------------------------------------------------
commands.register("jsinv", (sender) => {
    if (!needPlayer(sender)) return;
    const inv = sender.inventory;
    say(sender, `§7size=${inv.size} used=${inv.contents().filter(Boolean).length} firstEmpty=${inv.firstEmpty}`);
    say(sender, `§7held slot ${inv.heldItemSlot} = ${inv.itemInMainHand?.type ?? "empty"}`);
    say(sender, `§7helmet=${inv.helmet?.type ?? "none"} chest=${inv.chestplate?.type ?? "none"}`);
    say(sender, `§7enderChest used=${sender.enderChest.contents().filter(Boolean).length}/${sender.enderChest.size}`);
}, { description: "Reports your inventory." });

commands.register("jskit", (sender) => {
    if (!needPlayer(sender)) return;
    const inv = sender.inventory;
    inv.setHelmet({ type: "minecraft:diamond_helmet" });
    inv.setChestplate({ type: "minecraft:diamond_chestplate" });
    inv.setItemInMainHand({ type: "minecraft:diamond_pickaxe" });
    inv.addItem({ type: "minecraft:torch", amount: 16 });
    say(sender, "§akit given - now try /jstag with the pickaxe held");
}, { description: "Gives a diamond kit.", op: true });

commands.register("jstag", (sender) => {
    if (!needPlayer(sender)) return;
    const held = sender.inventory.itemInMainHand;
    if (!held) return say(sender, "§chold something first");
    const uses = Number(held.getTag("testkit:uses") ?? 0) + 1;
    held.setTag("testkit:uses", uses);
    held.setTag("testkit:owner", sender.name);
    held.setTag("testkit:data", JSON.stringify({ at: Date.now() }));
    // Read it back off the inventory, which proves the write reached the world and not just the copy.
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7${held.type}: uses=${again.getTag("testkit:uses")} owner=${again.getTag("testkit:owner")}`);
    say(sender, `§7keys=${JSON.stringify(again.tagKeys())}`);
    if (Number(again.getTag("testkit:uses")) !== uses) say(sender, "§cWRITE-THROUGH FAILED");
}, { description: "Writes and reads custom NBT on the held item." });

// --- forms ---------------------------------------------------------------------------------------
commands.register("jsform", (sender, args) => {
    if (!needPlayer(sender)) return;
    const which = (args[0] ?? "action").toLowerCase();
    if (which === "message") {
        sender.sendForm({
            type: "message", title: "Message form", content: "Pick one.",
            button1: "Left", button2: "Right",
            onSubmit: (i) => say(sender, `§amessage form -> ${i}`),
            onClose: () => say(sender, "§7message form dismissed"),
        });
    } else if (which === "modal") {
        sender.sendForm({
            type: "modal", title: "Modal form", submitButton: "Apply",
            controls: [
                { type: "header", text: "Settings" },
                { type: "toggle", label: "A toggle", defaultValue: true },
                { type: "slider", label: "A slider", min: 0, max: 10, step: 1, defaultValue: 5 },
                { type: "dropdown", label: "A dropdown", options: ["one", "two", "three"], defaultIndex: 1 },
                { type: "stepSlider", label: "A step slider", options: ["low", "high"] },
                { type: "divider" },
                { type: "textInput", label: "Some text", placeholder: "type here", defaultValue: "hello" },
            ],
            onSubmit: (r) => say(sender, `§amodal form -> ${JSON.stringify(r)}`),
            onClose: () => say(sender, "§7modal form dismissed"),
        });
    } else {
        sender.sendForm({
            type: "action", title: "Action form", content: "Buttons and decorations.",
            buttons: [
                { type: "header", text: "Group" },
                "Plain button",
                { text: "With an icon", icon: "textures/items/diamond" },
                { type: "divider" },
                "Last button",
            ],
            onSubmit: (i) => say(sender, `§aaction form -> ${i}`),
            onClose: () => say(sender, "§7action form dismissed"),
        });
    }
}, { description: "Shows a form.", usages: ["/jsform <action|message|modal>"] });

// --- scoreboard ----------------------------------------------------------------------------------
let sidebarTask = null;

commands.register("jsboard", (sender, args) => {
    const board = server.scoreboard;
    if (args[0] === "off") {
        if (sidebarTask) { sidebarTask.cancel(); sidebarTask = null; }
        board.clearSlot("sidebar");
        board.removeObjective("testkit");
        return say(sender, "§7sidebar removed");
    }
    board.addObjective("testkit", "§6Testkit");
    board.setDisplay("testkit", "sidebar", "descending");
    board.setScore("testkit", "Online", server.onlinePlayerCount);
    board.setScore("testkit", "TPS", Math.round(server.currentTicksPerSecond));
    board.addScore("testkit", "Refreshes", 1);
    if (!sidebarTask) {
        sidebarTask = scheduler.runTimer(() => {
            board.setScore("testkit", "Online", server.onlinePlayerCount);
            board.setScore("testkit", "TPS", Math.round(server.currentTicksPerSecond));
            board.addScore("testkit", "Refreshes", 1);
        }, 40);
    }
    say(sender, "§asidebar up, refreshing every 2s - /jsboard off to remove");
}, { description: "Puts a live sidebar up.", usages: ["/jsboard [off]"], op: true });

// --- packets -------------------------------------------------------------------------------------
commands.register("jspacket", (sender) => {
    say(sender, `§7schema=${packets.schemaVersion} id141=${packets.nameOf(141)}`);
    // Round-trip through encode and decode, which needs no client involvement to verify.
    const built = packets.encode(141, { blockPosition: { x: 1, y: 64, z: -2 } });
    if (!built.ok) return say(sender, `§cencode failed: ${built.reason} @ ${built.stoppedAt}`);
    const back = packets.decode(141, built.payload);
    say(sender, `§7encoded ${built.payload.length} bytes -> ${JSON.stringify(back.fields)} complete=${back.complete}`);
    if (JSON.stringify(back.fields.blockPosition) !== JSON.stringify({ x: 1, y: 64, z: -2 })) {
        say(sender, "§cROUND-TRIP MISMATCH");
    }
    // A packet the schema cannot fully describe must report a reason rather than invent values. This
    // line is a PASS when complete=false: TextPacket's body is a tagged union, and the schema does not
    // say which case is on the wire, so stopping is the correct outcome.
    const partial = packets.decode(9, new Uint8Array([1, 0]));
    say(sender, partial.complete
        ? "§cTextPacket decoded fully - unexpected, the schema cannot describe its union body"
        : `§a(expected) TextPacket stops early: ${partial.reason}`);
}, { description: "Encodes and decodes a packet, checking the round-trip." });

let packetTap = null;
commands.register("jstap", (sender) => {
    if (packetTap) {
        packetTap.unsubscribe();
        packetTap = null;
        return say(sender, "§7packet tap off");
    }
    let seen = 0;
    packetTap = events.packetReceive((event) => {
        if (++seen > 20) return;
        const d = packets.decode(event.packetId, event.payload);
        logger.info(`[testkit] <- ${d.name ?? event.packetId} complete=${d.complete} ${
            d.complete ? JSON.stringify(d.fields).slice(0, 120) : d.reason}`);
    });
    say(sender, "§apacket tap on - logs the next 20 inbound packets, /jstap again to stop");
}, { description: "Logs decoded inbound packets.", op: true });

// --- events --------------------------------------------------------------------------------------
export default {
    onEnable() {
        events.playerDeath((event) => {
            logger.info(`[testkit] death: ${event.player.name} by ${event.damageSource.type} - "${event.deathMessage}"`);
            event.deathMessage = `§c${event.player.name} §7died testing`;
        });

        events.blockBreak((event) => {
            const held = event.heldItem;
            logger.info(`[testkit] break ${event.block.type} with ${held ? held.type : "bare hands"}`);
        });

        events.playerInteract((event) => {
            logger.info(`[testkit] interact ${event.action} hasBlock=${event.hasBlock} ` +
                        `block=${event.block?.type ?? "-"} face=${event.blockFace} item=${event.item?.type ?? "-"}`);
        });

        events.playerItemHeld((event) => {
            logger.info(`[testkit] held slot ${event.previousSlot} -> ${event.newSlot}`);
        });

        events.serverListPing((event) => {
            event.motd = `§6testkit §7- §f${server.onlinePlayerCount} online`;
        });

        logger.info(`testkit ready: ${commands.list().length} commands, schema ${packets.schemaVersion}`);
    },

    onDisable() {
        if (sidebarTask) sidebarTask.cancel();
        if (packetTap) packetTap.unsubscribe();
    },
};
