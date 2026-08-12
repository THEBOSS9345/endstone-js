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

// --- boss bars -----------------------------------------------------------------------------------
// The bar is kept in a module variable on purpose: unlike a player or a block it is not
// dispatch-scoped, so driving it from a timer is the whole point.
let bar = null;
let barTask = null;
let barLeft = 0;

commands.register("jsbar", (sender, args) => {
    if (!needPlayer(sender)) return;
    if (args[0] === "off") {
        if (barTask) { barTask.cancel(); barTask = null; }
        if (bar) { bar.remove(); bar = null; }
        return say(sender, "§7bar removed");
    }
    if (!bar) {
        bar = server.createBossBar({
            title: "§6Testkit", color: "red", style: "segmented10", darkenSky: false,
        });
    }
    bar.addPlayer(sender);
    say(sender, `§7viewers=${bar.playerCount} [${bar.playerNames.join(", ")}] ` +
                `color=${bar.color} style=${bar.style} progress=${bar.progress}`);
    // Every setter, then read each one back - a silently-dropped write is the failure this catches.
    bar.color = "rebeccaPurple";
    bar.style = "solid";
    bar.createFog = true;
    bar.progress = 5;                       // out of range on purpose: must clamp to 1
    if (bar.color !== "rebeccaPurple" || bar.style !== "solid" || !bar.createFog || bar.progress !== 1) {
        say(sender, `§cSETTER FAILED: color=${bar.color} style=${bar.style} ` +
                    `fog=${bar.createFog} progress=${bar.progress}`);
    }
    bar.createFog = false;
    bar.color = "green";
    bar.style = "segmented10";
    // Count down over 10 seconds, then leave the bar up so /jsbar off has something to remove.
    barLeft = 200;
    if (!barTask) {
        barTask = scheduler.runTimer(() => {
            if (!bar) return;
            barLeft = Math.max(barLeft - 10, 0);
            bar.progress = barLeft / 200;
            bar.title = `§6Testkit §7- ${(barLeft / 20).toFixed(1)}s`;
        }, 10);
    }
    say(sender, "§abar up and counting down - /jsbar off to remove");
}, { description: "Shows a boss bar and drives it from a timer.", usages: ["/jsbar [off]"], op: true });

commands.register("jssimilar", (sender) => {
    if (!needPlayer(sender)) return;
    const held = sender.inventory.itemInMainHand;
    if (!held) return say(sender, "§chold something first");
    // Same slot read twice must be similar; the next slot along usually is not.
    const same = sender.inventory.itemInMainHand;
    const other = sender.inventory.getItem(1);
    say(sender, `§7${held.type} vs itself: ${held.isSimilar(same)} (expected true)`);
    if (!held.isSimilar(same)) say(sender, "§cisSimilar FAILED on an identical stack");
    if (other) say(sender, `§7${held.type} vs slot 1 ${other.type}: ${held.isSimilar(other)}`);
}, { description: "Compares the held item with itself and with slot 1." });

// --- player lookup and bans ----------------------------------------------------------------------
commands.register("jsfind", (sender, args) => {
    const who = args[0] ?? sender.name;
    const target = server.getPlayer(who);
    if (!target) return say(sender, `§7${who} is not online`);
    // Looked up by name, then again by the uuid we just read - both paths through one accessor.
    const again = server.getPlayer(target.uniqueId);
    say(sender, `§a${target.name} at ${Math.round(target.location.x)},${Math.round(target.location.z)} ` +
                `uuid=${target.uniqueId}`);
    if (!again || again.name !== target.name) say(sender, "§cUUID LOOKUP FAILED");
    say(sender, `§7uptime ${Math.round((Date.now() - server.startTime.getTime()) / 1000)}s`);
}, { description: "Looks a player up by name and by UUID.", usages: ["/jsfind [player: string]"] });

commands.register("jsban", (sender, args) => {
    const who = args[0];
    if (!who) return say(sender, "§cusage: /jsban <name>");
    if (args[1] === "off") {
        server.unbanPlayer(who);
        return say(sender, `§7unbanned ${who} - isBanned=${server.isBanned(who)}`);
    }
    // Temporary on purpose, so a mistake here expires on its own.
    server.banPlayer(who, { reason: "testkit", source: sender.name, durationSeconds: 60 });
    const entry = server.getBanEntry(who);
    if (!entry) return say(sender, "§cBAN NOT RECORDED");
    say(sender, `§a${entry.target} banned by ${entry.source}: ${entry.reason}`);
    say(sender, `§7expires ${entry.expires ? entry.expires.toISOString() : "never"} ` +
                `isBanned=${server.isBanned(who)} total=${server.bannedPlayers.length}`);
    say(sender, "§7/jsban <name> off to lift it - it also lapses after 60s");
}, { description: "Bans a player for 60s and reads the entry back.",
     usages: ["/jsban <name: string> [off]"], op: true });

// --- item metadata -------------------------------------------------------------------------------
commands.register("jsmeta", (sender) => {
    if (!needPlayer(sender)) return;
    const item = sender.inventory.itemInMainHand;
    if (!item) return say(sender, "§chold something first");
    item.displayName = "§6Testkit Blade";
    item.lore = ["§7Line one.", `§7Held by ${sender.name}`];
    item.unbreakable = true;
    item.repairCost = 3;
    item.addEnchant("minecraft:sharpness", 7);
    item.addEnchant("minecraft:unbreaking");
    // Re-read off the inventory, which proves the metadata reached the world and not just the copy.
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7name="${again.displayName}" lore=${again.lore.length} unbreakable=${again.unbreakable}`);
    say(sender, `§7enchants=${JSON.stringify(again.enchants)} sharpness=${again.getEnchantLevel("minecraft:sharpness")}`);
    if (again.displayName !== "§6Testkit Blade") say(sender, "§cDISPLAY NAME NOT WRITTEN BACK");
    if (again.lore.length !== 2) say(sender, "§cLORE NOT WRITTEN BACK");
    if (again.getEnchantLevel("minecraft:sharpness") !== 7) say(sender, "§cENCHANT NOT WRITTEN BACK");
    say(sender, "§a/jsmeta clean to strip it all off");
}, { description: "Writes item name, lore and enchantments, then reads them back.", usages: ["/jsmeta [clean]"] });

commands.register("jsclean", (sender) => {
    if (!needPlayer(sender)) return;
    const item = sender.inventory.itemInMainHand;
    if (!item) return say(sender, "§chold something first");
    item.displayName = "";
    item.lore = [];
    item.removeEnchants();
    item.unbreakable = false;
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7stripped: name="${again.displayName}" lore=${again.lore.length} ` +
                `enchants=${Object.keys(again.enchants).length}`);
}, { description: "Strips name, lore and enchantments off the held item." });

commands.register("jscontents", (sender) => {
    if (!needPlayer(sender)) return;
    const inv = sender.inventory;
    const saved = inv.contents();
    const filled = saved.filter((i) => i !== null).length;
    // Round-trip the whole inventory through setContents; nothing should move.
    inv.setContents(saved);
    const after = inv.contents().filter((i) => i !== null).length;
    say(sender, `§7${filled} slots before, ${after} after a setContents round-trip`);
    if (filled !== after) say(sender, "§cSETCONTENTS LOST ITEMS");
    say(sender, `§7stone in slots [${inv.all("minecraft:stone").join(", ")}]`);
}, { description: "Round-trips the inventory through setContents." });

// --- maps, per-player scoreboards, translation ----------------------------------------------------
// Both are server-owned rather than dispatch-scoped, so they are made once and kept.
let sharedMap = null;
let privateBoard = null;

commands.register("jsmap", (sender) => {
    if (!needPlayer(sender)) return;
    if (!sharedMap) {
        sharedMap = server.createMap();
        sharedMap.centerX = Math.round(sender.location.x);
        sharedMap.centerZ = Math.round(sender.location.z);
        sharedMap.scale = 2;
    }
    // Read every property back, including the ones just written.
    say(sender, `§7map id=${sharedMap.id} centre=${sharedMap.centerX},${sharedMap.centerZ} ` +
                `scale=${sharedMap.scale} locked=${sharedMap.isLocked} virtual=${sharedMap.isVirtual} ` +
                `dim=${sharedMap.dimension ? sharedMap.dimension.name : "-"}`);
    if (sharedMap.scale !== 2) say(sender, "§cMAP SETTER FAILED");
    sender.sendMap(sharedMap);
    say(sender, "§asent - a blank map, since drawing needs a MapRenderer that JS cannot supply");
}, { description: "Creates a map, sets its centre and scale, and sends it.", op: true });

commands.register("jsprivate", (sender, args) => {
    if (!needPlayer(sender)) return;
    if (args[0] === "off") {
        sender.scoreboard = server.scoreboard;
        return say(sender, "§7back on the main scoreboard");
    }
    if (!privateBoard) {
        privateBoard = server.createScoreboard();
        privateBoard.addObjective("private", "§bJust For You");
        privateBoard.setDisplay("private", "sidebar", "descending");
    }
    privateBoard.setScore("private", sender.name, Math.round(sender.health));
    sender.scoreboard = privateBoard;
    say(sender, "§aon your own scoreboard - other players still see the main one");
    say(sender, "§7/jsprivate off to go back, /jsboard for the shared sidebar to compare");
}, { description: "Puts you on a scoreboard only you can see.", usages: ["/jsprivate [off]"], op: true });

commands.register("jslang", (sender, args) => {
    const key = args[0] ?? "item.diamond_sword.name";
    const translated = server.translate(key);
    say(sender, `§7locale=${server.locale} "${key}" -> "${translated}"`);
    // Minecraft returns the key unchanged when there is no entry, which is the only failure signal.
    if (translated === key) say(sender, "§7(unchanged - probably not a real translation key)");
    say(sender, `§7with params: "${server.translate("chat.type.text", [sender.name, "hello"])}"`);
}, { description: "Translates a key against the server's language files.",
     usages: ["/jslang [key: string]"] });

// --- block states and snapshots -------------------------------------------------------------------
commands.register("jsblock", (sender) => {
    if (!needPlayer(sender)) return;
    const ground = sender.level.getDimension(sender.dimension.toLowerCase());
    const at = { x: Math.round(sender.location.x), z: Math.round(sender.location.z) };
    const y = ground.getHighestBlockYAt(at);
    const block = ground.getBlockAt({ x: at.x, y, z: at.z });
    say(sender, `§7highest at ${at.x},${at.z} is y=${y}: ${block.type} runtimeId=${block.runtimeId}`);
    say(sender, `§7states=${JSON.stringify(block.blockStates)}`);
    // Snapshot, change, restore - all in one callback, since a state is dispatch-scoped.
    const before = block.captureState();
    const original = before.type;
    block.type = "minecraft:glass";
    say(sender, `§7changed to ${block.type}, snapshot still holds ${before.type}`);
    before.update(true);
    if (ground.getBlockAt({ x: at.x, y, z: at.z }).type !== original) {
        say(sender, "§cSNAPSHOT RESTORE FAILED");
    } else {
        say(sender, `§arestored to ${original} from the snapshot`);
    }
    say(sender, `§7oak_stairs default states=${JSON.stringify(server.blockStates("minecraft:oak_stairs"))}`);
    say(sender, `§7stone runtimeId=${server.blockRuntimeId("minecraft:stone")}`);
}, { description: "Reads block states, snapshots a block, changes it and restores it.", op: true });

commands.register("jsintro", (sender) => {
    // Scoreboard introspection: the only way to discover objective names another plugin created.
    const board = server.scoreboard;
    const names = board.objectives.map((o) => `${o.name}("${o.displayName}")`);
    say(sender, `§7objectives=[${names.join(", ")}] entries=${board.entries.length}`);
    say(sender, `§7your scores=${JSON.stringify(board.getScores(sender.name))}`);
    if (!needPlayer(sender)) return;
    const held = sender.inventory.itemInMainHand;
    if (!held) return say(sender, "§7hold something to test clone/conflicts");
    const copy = held.clone();
    copy.amount = 1;
    const still = sender.inventory.itemInMainHand;
    say(sender, `§7clone amount=${copy.amount}, original still ${still.amount} ` +
                `(a clone must NOT write back)`);
    if (still.amount === 1 && held.amount !== 1) say(sender, "§cCLONE WROTE BACK");
    say(sender, `§7smite conflicts with what you hold: ${held.hasConflictingEnchant("minecraft:smite")}`);
}, { description: "Lists scoreboard objectives and tests item clone/enchant conflicts." });

commands.register("jsskin", (sender) => {
    if (!needPlayer(sender)) return;
    const skin = sender.skin;
    say(sender, `§7skin ${skin.id} ${skin.width}x${skin.height}`);
    say(sender, skin.capeWidth > 0
        ? `§7cape ${skin.capeId} ${skin.capeWidth}x${skin.capeHeight}`
        : "§7no cape");
    if (skin.width === 0) say(sender, "§cSKIN SIZE MISSING");
}, { description: "Reports your skin and cape ids and sizes." });

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

        logger.info(`testkit ready: ${commands.list().length} commands, schema ${packets.schemaVersion}`);
    },

    onDisable() {
        if (sidebarTask) sidebarTask.cancel();
        if (packetTap) packetTap.unsubscribe();
        if (barTask) barTask.cancel();
        if (bar) bar.remove();
        // Maps and scoreboards need no cleanup: the runtime drops them when the plugin unloads, and
        // players on a private scoreboard fall back to the main one.
    },
};
