// A test harness for the whole API surface, driven by slash commands.
//
// Every command reports what it did in chat and in the server log, so a failure shows up as either a
// missing line or an "undefined". Run /jstest for the index once in game.
//
// Commands are registered at the top level so the client autocompletes them - see node/RULES.md rule 3.

import { commands, events, packets, scheduler, server, logger } from "@endstone-js/server";

const say = (who, text) => {
    logger.info(`[testkit] ${text.replace(/§./g, "")}`);
    const player = typeof who === "string" ? server.getPlayer(who) : who;
    if (player) player.sendMessage(text);
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
    // No need to capture the sender: a form callback is handed a freshly resolved player, because the
    // handle that sent the form is stale by the time the answer arrives.
    if (which === "message") {
        sender.sendForm({
            type: "message", title: "Message form", content: "Pick one.",
            button1: "Left", button2: "Right",
            onSubmit: (i, player) => say(player, `§amessage form -> ${i}`),
            onClose: (player) => say(player, "§7message form dismissed"),
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
            onSubmit: (r, player) => say(player, `§amodal form -> ${JSON.stringify(r)}`),
            onClose: (player) => say(player, "§7modal form dismissed"),
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
            onSubmit: (i, player) => say(player, `§aaction form -> ${i}`),
            onClose: (player) => say(player, "§7action form dismissed"),
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

// --- newly bound surface --------------------------------------------------------------------------
// Everything below covers API added after the first pass. Each command reads back what it wrote where
// it can, so a silently-dropped write shows up as a red line rather than as nothing at all.

commands.register("jsplayers", (sender) => {
    const players = server.onlinePlayers;
    say(sender, `§7onlinePlayers=${players.length} count=${server.onlinePlayerCount}`);
    for (const player of players) {
        say(sender, `  §f${player.name} §7at ${Math.round(player.location.x)},${Math.round(player.location.z)}`);
    }
    if (players.length !== server.onlinePlayerCount) say(sender, "§cLIST DOES NOT MATCH COUNT");
    if (!sender.isConsole && !players.some((p) => p.name === sender.name)) {
        say(sender, "§cYOU ARE MISSING FROM THE LIST");
    }
}, { description: "Lists every online player." });

commands.register("jsperm", (sender, args) => {
    const node = args[0] ?? "testkit.demo";
    say(sender, `§7level=${sender.permissionLevel} isOp=${sender.isOp}`);
    say(sender, `§7before: has=${sender.hasPermission(node)} set=${sender.isPermissionSet(node)}`);
    sender.addPermission(node);
    if (!sender.hasPermission(node)) say(sender, "§cGRANT DID NOT TAKE");
    say(sender, `§7after grant: has=${sender.hasPermission(node)} set=${sender.isPermissionSet(node)}`);
    sender.removePermission(node);
    if (sender.hasPermission(node)) say(sender, "§cDENY DID NOT TAKE");
    say(sender, `§7after deny: has=${sender.hasPermission(node)}`);
}, { description: "Grants and denies a permission node, reading it back.",
     usages: ["/jsperm [node: string]"], op: true });

// Gated on a declared permission rather than on op, which is the path that was never checked before.
commands.register("jsgated", (sender) => {
    say(sender, "§aYou hold testkit.gated - the permission gate let you through.");
    say(sender, "§7Run /jsperm testkit.gated to grant or deny it, then try this again.");
}, { description: "Only runs if you hold testkit.gated.", permissions: ["testkit.gated"] });

commands.register("jsstates", (sender) => {
    if (!needPlayer(sender)) return;
    const dim = server.level.getDimension(sender.dimension.toLowerCase());
    const at = { x: Math.round(sender.location.x), z: Math.round(sender.location.z) };
    const y = dim.getHighestBlockYAt(at);
    const block = dim.getBlockAt({ x: at.x, y, z: at.z });
    const before = block.captureState();
    block.type = "minecraft:oak_stairs";
    say(sender, `§7placed stairs, states=${JSON.stringify(block.blockStates)}`);
    // The write names one state; everything else in the palette entry has to survive it.
    block.blockStates = { weirdo_direction: 2 };
    const after = block.blockStates;
    say(sender, `§7after write: ${JSON.stringify(after)}`);
    if (after.weirdo_direction !== 2) say(sender, "§cBLOCK STATE WRITE FAILED");
    if (!("upside_down_bit" in after)) say(sender, "§cOTHER STATES WERE LOST");
    before.update(true);
    say(sender, "§arestored the original block");
}, { description: "Writes a block state and checks the others survive.", op: true });

commands.register("jsdim2", (sender) => {
    if (!needPlayer(sender)) return;
    const dim = server.level.getDimension(sender.dimension.toLowerCase());
    say(sender, `§7name=${dim.name} type=${dim.type}`);
    const at = { x: Math.round(sender.location.x), z: Math.round(sender.location.z) };
    const ground = dim.getBlockAt({ x: at.x, y: dim.getHighestBlockYAt(at), z: at.z });
    // getRelative by face pairs with the face the interact event already reports.
    const above = ground.getRelative("up");
    const twoUp = ground.getRelative("up", 2);
    say(sender, `§7ground=${ground.type} up=${above.type} up2=${twoUp.type}`);
    if (above.location.y !== ground.location.y + 1) say(sender, "§cgetRelative(face) WRONG OFFSET");
    if (twoUp.location.y !== ground.location.y + 2) say(sender, "§cgetRelative(face, 2) WRONG OFFSET");
}, { description: "Dimension type and getRelative by block face." });

// --- item metadata ---------------------------------------------------------------------------------
let testkitMap = null;

// This server build only produces MapMeta (for minecraft:filled_map); book and crossbow metadata are
// declared by the Endstone API but never instantiated, so writing them fails. Check first and say so.
const needMeta = (sender, item, wanted, what) => {
    if (item.metaType === wanted) return true;
    say(sender, `§eThis server does not provide ${what} metadata: the item reports ` +
                `metaType=§f${item.metaType}§e, not §f${wanted}§e.`);
    say(sender, "§7Endstone declares the type but its core maps only minecraft:filled_map to a");
    say(sender, "§7subclass, so this will start working when upstream wires the rest up.");
    return false;
};

commands.register("jsmapitem", (sender) => {
    if (!needPlayer(sender)) return;
    if (!testkitMap) {
        testkitMap = server.createMap();
        testkitMap.centerX = Math.round(sender.location.x);
        testkitMap.centerZ = Math.round(sender.location.z);
    }
    sender.inventory.setItemInMainHand({ type: "minecraft:filled_map" });
    const held = sender.inventory.itemInMainHand;
    say(sender, `§7metaType=${held.metaType} (expected map)`);
    if (!needMeta(sender, held, "map", "map")) return;
    held.setMapView(testkitMap);
    // Read back off the inventory, not off the copy that was written.
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7hasMapView=${again.hasMapView} hasMapId=${again.hasMapId} mapId=${again.mapId}`);
    if (!again.hasMapView) say(sender, "§cSETMAPVIEW DID NOT PERSIST");
    else say(sender, "§ayou are holding the map this plugin made");
}, { description: "Puts a created map into a filled_map item.", op: true });

commands.register("jsdraw", (sender) => {
    if (!needPlayer(sender)) return;
    // A renderer really is suppliable from JavaScript - Endstone lets a plugin add one, which is how
    // its Python bindings do it too. The map stays blank without one.
    const map = server.createMap();
    map.centerX = Math.round(sender.location.x);
    map.centerZ = Math.round(sender.location.z);

    // A whole 128x128 RGBA frame, built once and assigned in a single crossing. Per-pixel calls would
    // cross the ABI 16384 times for every draw, for every viewer.
    const SIZE = 128;
    const frame = new Uint8Array(SIZE * SIZE * 4);
    for (let y = 0; y < SIZE; ++y) {
        for (let x = 0; x < SIZE; ++x) {
            const at = (y * SIZE + x) * 4;
            const ring = Math.round(Math.hypot(x - 64, y - 64));
            frame[at] = (x * 2) & 0xff;              // red ramps east
            frame[at + 1] = (y * 2) & 0xff;          // green ramps south
            frame[at + 2] = ring < 40 ? 220 : 40;    // a disc in the middle
            frame[at + 3] = 255;
        }
    }

    let draws = 0;
    let reportedThread = false;
    map.addRenderer((canvas, viewer) => {
        draws += 1;
        // The open question was whether a draw happens on the server thread. If this ever reports
        // false, entering JavaScript here is unsafe and the renderer has to go.
        if (!reportedThread) {
            reportedThread = true;
            logger.info(`[testkit] map draw: primaryThread=${server.isPrimaryThread} ` +
                        `viewer=${viewer ? viewer.name : "?"}`);
            if (!server.isPrimaryThread) {
                logger.error("[testkit] MAP DRAW IS OFF THE SERVER THREAD - unsafe, report this");
            }
        }
        canvas.pixels = frame;
    });

    sender.inventory.setItemInMainHand({ type: "minecraft:filled_map" });
    const item = sender.inventory.itemInMainHand;
    if (item.metaType === "map") item.setMapView(map);
    sender.sendMap(map);
    say(sender, "§ahold the map - it should show a red/green gradient with a blue disc");
    say(sender, "§7if it is blank, the renderer never ran; check the log for the draw line");
    scheduler.runLater(() => {
        say(sender, `§7renderer ran ${draws} time(s) in the first 3s`);
        if (draws === 0) say(sender, "§cRENDERER NEVER RAN");
    }, 60);
}, { description: "Draws a real image on a map from JavaScript.", op: true });

commands.register("jsbook", (sender) => {
    if (!needPlayer(sender)) return;
    sender.inventory.setItemInMainHand({ type: "minecraft:written_book" });
    const item = sender.inventory.itemInMainHand;
    say(sender, `§7metaType=${item.metaType} (expected book)`);
    if (!needMeta(sender, item, "book", "written book")) return;
    item.title = "Testkit";
    item.author = sender.name;
    item.generation = "copyOfOriginal";
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7title="${again.title}" author="${again.author}" generation=${again.generation}`);
    if (again.title !== "Testkit") say(sender, "§cBOOK TITLE NOT WRITTEN BACK");
    if (again.author !== sender.name) say(sender, "§cBOOK AUTHOR NOT WRITTEN BACK");
    if (again.generation !== "copyOfOriginal") say(sender, "§cBOOK GENERATION NOT WRITTEN BACK");
}, { description: "Writes a written book's title, author and generation.", op: true });

commands.register("jspages", (sender) => {
    if (!needPlayer(sender)) return;
    sender.inventory.setItemInMainHand({ type: "minecraft:writable_book" });
    const item = sender.inventory.itemInMainHand;
    say(sender, `§7metaType=${item.metaType} (expected writableBook)`);
    if (!needMeta(sender, item, "writableBook", "writable book")) return;
    item.pages = ["First page.", "Second page."];
    sender.inventory.itemInMainHand.addPage("Third page.");
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7pageCount=${again.pageCount} hasPages=${again.hasPages}`);
    say(sender, `§7pages=${JSON.stringify(again.pages)}`);
    if (again.pageCount !== 3) say(sender, "§cPAGE COUNT WRONG - addPage or pages failed");
}, { description: "Writes and appends pages on a writable book.", op: true });

commands.register("jsbow", (sender) => {
    if (!needPlayer(sender)) return;
    sender.inventory.setItemInMainHand({ type: "minecraft:crossbow" });
    const bow = sender.inventory.itemInMainHand;
    say(sender, `§7metaType=${bow.metaType} (expected crossbow)`);
    if (!needMeta(sender, bow, "crossbow", "crossbow")) return;
    bow.addChargedProjectile("minecraft:arrow");
    const again = sender.inventory.itemInMainHand;
    say(sender, `§7charged=${again.hasChargedProjectiles} count=${again.chargedProjectileCount}`);
    if (again.chargedProjectileCount < 1) say(sender, "§cCROSSBOW NOT LOADED");
}, { description: "Loads a crossbow with an arrow.", op: true });

// --- inventory matching -----------------------------------------------------------------------------
commands.register("jsmatch", (sender) => {
    if (!needPlayer(sender)) return;
    const inv = sender.inventory;
    const held = inv.itemInMainHand;
    if (!held) return say(sender, "§chold something first - ideally an enchanted tool");
    // Matching by type id against matching the whole stack. The difference only shows once a stack
    // carries metadata, which is why this asks you to enchant one of two identical items.
    const byType = inv.all(held.type);
    const byStack = inv.all(held);
    say(sender, `§7type "${held.type}" is in slots [${byType.join(", ")}]`);
    say(sender, `§7this exact stack is in slots [${byStack.join(", ")}]`);
    say(sender, `§7contains(type)=${inv.contains(held.type)} contains(stack)=${inv.contains(held)}`);
    say(sender, `§7first(type)=${inv.first(held.type)} first(stack)=${inv.first(held)}`);
    if (!inv.contains(held)) say(sender, "§cSTACK MATCH FAILED against the item you are holding");
    if (byStack.length > byType.length) say(sender, "§cSTACK MATCH IS WIDER THAN TYPE MATCH");
    say(sender, "§7Enchant one of two identical items and run this again: the type list should then");
    say(sender, "§7be longer than the stack list.");
}, { description: "Compares matching by type id with matching a whole stack." });

// --- dropped items -----------------------------------------------------------------------------------
commands.register("jsdropped", (sender) => {
    if (!needPlayer(sender)) return;
    const dim = server.level.getDimension(sender.dimension.toLowerCase());
    const dropped = dim.dropItem(sender.location, { type: "minecraft:diamond", amount: 2 });
    if (!dropped) return say(sender, "§cdropItem returned null");
    say(sender, `§7endstoneType=${dropped.endstoneType} type=${dropped.type}`);
    if (dropped.endstoneType !== "Item") say(sender, "§cDROPPED ITEM IS NOT AN Item HANDLE");
    const stack = dropped.itemStack;
    say(sender, `§7itemStack=${stack ? `${stack.amount} x ${stack.type}` : "none"}`);
    dropped.pickupDelay = 60;
    dropped.unlimitedLifetime = true;
    say(sender, `§7pickupDelay=${dropped.pickupDelay} unlimitedLifetime=${dropped.unlimitedLifetime}`);
    if (dropped.pickupDelay !== 60) say(sender, "§cPICKUP DELAY NOT WRITTEN");
    if (dropped.unlimitedLifetime !== true) say(sender, "§cUNLIMITED LIFETIME NOT WRITTEN");
    say(sender, "§await 3s, pick it up, and watch the log for the pickup event");
}, { description: "Drops an item and reads it back as an Item actor.", op: true });

// --- scoreboard display readback ----------------------------------------------------------------------
// --- identity, address, registries -------------------------------------------------------------------
commands.register("jsids", (sender) => {
    if (!needPlayer(sender)) return;
    // runtimeId is per-session; id survives a restart. They are different numbers, and a plugin that
    // stores the wrong one silently loses its reference on the next boot.
    say(sender, `§7you: id=${sender.id} runtimeId=${sender.runtimeId} uniqueId=${sender.uniqueId}`);
    say(sender, `§7from ${sender.address}:${sender.port} ping=${sender.ping}ms`);
    if (typeof sender.id !== "number") say(sender, "§cACTOR ID MISSING");
    if (typeof sender.port !== "number") say(sender, "§cPORT MISSING");
    if (sender.port === 0) say(sender, "§ePORT IS 0 - suspicious, but not impossible");
}, { description: "Reports actor id, runtime id and the remote address and port." });

commands.register("jsdura", (sender) => {
    if (!needPlayer(sender)) return;
    const item = sender.inventory.itemInMainHand;
    if (!item) return say(sender, "§7hold something first");
    const max = item.maxDurability;
    say(sender, `§7${item.type}: maxStackSize=${item.maxStackSize} maxDurability=${max}`);
    if (max > 0) {
        const damage = item.hasItemMeta ? (item.itemMeta.damage ?? 0) : 0;
        say(sender, `§7durability ${max - damage}/${max} (${Math.round((1 - damage / max) * 100)}%)`);
    } else {
        say(sender, "§7this item does not wear out");
    }
    // The same numbers by id, without holding the item - what a plugin needs before the stack exists.
    for (const id of ["minecraft:diamond_sword", "minecraft:stone", "minecraft:not_a_real_item"]) {
        say(sender, `§7${id}: durability=${server.getItemMaxDurability(id)} ` +
                    `stack=${server.getItemMaxStackSize(id)}`);
    }
    for (const id of ["minecraft:sharpness", "minecraft:mending", "minecraft:not_a_real_enchant"]) {
        say(sender, `§7${id}: start=${server.getEnchantmentStartLevel(id)} ` +
                    `max=${server.getEnchantmentMaxLevel(id)}`);
    }
    if (server.getItemMaxDurability("minecraft:not_a_real_item") !== -1) {
        say(sender, "§cUNKNOWN ITEM ID SHOULD REPORT -1");
    }
    if (server.getEnchantmentMaxLevel("minecraft:sharpness") <= 0) {
        say(sender, "§cENCHANTMENT REGISTRY LOOKUP FAILED");
    }
}, { description: "Reads item durability and looks types and enchantments up by id." });

// --- moving an objective without rebuilding it -----------------------------------------------------------
commands.register("jsslot", (sender) => {
    const board = server.scoreboard;
    if (!board.objectives.some((o) => o.name === "testkit")) {
        return say(sender, "§7no testkit objective - run /jsboard first");
    }
    const read = () => board.objectives.find((o) => o.name === "testkit");
    say(sender, `§7before: slot=${read().displaySlot ?? "none"} order=${read().sortOrder ?? "none"}`);

    board.setSortOrder("testkit", "ascending");
    const ascending = read();
    say(sender, `§7after setSortOrder: slot=${ascending.displaySlot ?? "none"} ` +
                `order=${ascending.sortOrder ?? "none"}`);
    // The point of the individual setters: changing the order must not move it off the sidebar.
    if (ascending.sortOrder !== "ascending") say(sender, "§cSORT ORDER NOT APPLIED");
    if (ascending.displaySlot !== "sidebar") say(sender, "§cSORT ORDER CHANGE DISTURBED THE SLOT");

    board.setDisplaySlot("testkit", "belowName");
    say(sender, `§7after setDisplaySlot: slot=${read().displaySlot ?? "none"}`);
    if (read().displaySlot !== "belowName") say(sender, "§cDISPLAY SLOT NOT APPLIED");

    // No slot at all takes it off the board without unregistering it - the scores must survive.
    board.setDisplaySlot("testkit");
    const hidden = read();
    say(sender, `§7after clearing: slot=${hidden?.displaySlot ?? "none"} ` +
                `stillRegistered=${hidden !== undefined}`);
    if (!hidden) say(sender, "§cCLEARING THE SLOT UNREGISTERED THE OBJECTIVE");

    scheduler.runLater(() => {
        board.setDisplay("testkit", "sidebar", "descending");
        say(sender, "§7restored to the sidebar");
    }, 40);
}, { description: "Moves an objective's slot and sort order independently.", op: true });

// --- who ran this ------------------------------------------------------------------------------------------
commands.register("jssender", (sender) => {
    // Worth running from a command block: that is the branch that had no position until now.
    say(sender, `§7name="${sender.name}" console=${sender.isConsole === true} ` +
                `block=${sender.isBlock === true} op=${sender.isOp}`);
    if (sender.isBlock) {
        const block = sender.block;
        say(sender, `§7command block at ${block.x},${block.y},${block.z} in ${block.dimension} ` +
                    `type=${block.type}`);
        if (!block) say(sender, "§cBLOCK SENDER HAS NO BLOCK");
    } else if (!sender.isConsole) {
        say(sender, "§7run this from a command block to see the block branch");
    }
}, { description: "Reports which kind of sender ran the command, and where a block sits." });

// --- chunk events know their dimension --------------------------------------------------------------------
let chunkTap = null;
commands.register("jschunks", (sender) => {
    if (chunkTap) {
        chunkTap.unsubscribe();
        chunkTap = null;
        return say(sender, "§7chunk logging off");
    }
    let seen = 0;
    const name = sender.name;
    // Very high volume, so this stops itself rather than relying on you to turn it off.
    chunkTap = events.chunkLoad((event) => {
        if (seen >= 5) return;
        seen += 1;
        say(name, `§7chunk ${event.chunkX},${event.chunkZ} in ${event.dimension.name} ` +
                    `of level "${event.level.name}"`);
        if (seen >= 5) {
            say(name, "§7five chunks logged - stopping");
            chunkTap.unsubscribe();
            chunkTap = null;
        }
    });
    say(sender, "§7chunk logging on - walk somewhere new");
}, { description: "Logs which dimension the next few chunk loads belong to.", op: true });

commands.register("jsobj", (sender) => {
    const objectives = server.scoreboard.objectives;
    if (objectives.length === 0) return say(sender, "§7no objectives - run /jsboard first");
    for (const objective of objectives) {
        say(sender, `§7${objective.name}: display=${objective.displaySlot ?? "none"} ` +
                    `order=${objective.sortOrder ?? "none"} modifiable=${objective.modifiable}`);
    }
    const shown = objectives.find((o) => o.name === "testkit");
    if (shown && shown.displaySlot !== "sidebar") {
        say(sender, "§cDISPLAY SLOT READBACK WRONG - /jsboard puts testkit on the sidebar");
    }
}, { description: "Reads back where each objective is displayed." });

// --- server controls -----------------------------------------------------------------------------------
commands.register("jssrv2", (sender) => {
    const before = server.maxPlayers;
    server.maxPlayers = before + 1;
    const after = server.maxPlayers;
    server.maxPlayers = before;
    say(sender, `§7maxPlayers ${before} -> ${after} -> ${server.maxPlayers}`);
    if (after !== before + 1) say(sender, "§cMAXPLAYERS NOT WRITABLE");
    if (testkitMap) {
        const again = server.getMap(testkitMap.id);
        say(sender, `§7getMap(${testkitMap.id}) -> ${again ? `id ${again.id}` : "null"}`);
        if (!again || again.id !== testkitMap.id) say(sender, "§cGETMAP FAILED");
    } else {
        say(sender, "§7run /jsmapitem first to test getMap(id)");
    }
}, { description: "Writes maxPlayers and looks a map up by id.", op: true });

// Behind a literal confirm on purpose: it really does stop the server.
commands.register("jsstop", (sender, args) => {
    if (args[0] !== "confirm") {
        return say(sender, "§eThis stops the server. Run §f/jsstop confirm§e if you mean it.");
    }
    say(sender, "§cshutting down via server.shutdown()");
    server.shutdown();
}, { description: "Stops the server - needs the word confirm.",
     usages: ["/jsstop <confirm>"], op: true });

// --- redirecting a move ----------------------------------------------------------------------------------
let redirect = null;

commands.register("jsredirect", (sender) => {
    if (!needPlayer(sender)) return;
    if (redirect) {
        redirect.unsubscribe();
        redirect = null;
        return say(sender, "§7teleport redirect off");
    }
    const anchor = { x: sender.location.x, y: sender.location.y, z: sender.location.z };
    redirect = events.playerTeleport((event) => {
        // Rewriting the destination rather than cancelling is the whole point of setTo.
        event.to = anchor;
        logger.info(`[testkit] redirected a teleport to ${Math.round(anchor.x)},${Math.round(anchor.z)}`);
    });
    say(sender, "§aany teleport now lands back here - /jsredirect again to stop");
}, { description: "Redirects teleports to where you stood, testing event.to.", op: true });

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

        // Weather events carry exactly one field each, and it says which way the change is going.
        events.weatherChange((event) => {
            logger.info(`[testkit] weather -> ${event.raining ? "raining" : "clear"}`);
        });
        events.thunderChange((event) => {
            logger.info(`[testkit] thunder -> ${event.thundering ? "storm" : "calm"}`);
        });

        // Rewriting the format rather than the message is how a rank prefix is applied.
        events.playerChat((event) => {
            logger.info(`[testkit] chat format="${event.format}" recipients=${event.recipients.length}`);
            event.format = "§7[js]§r " + event.format;
        }, { priority: "monitor" });

        // The pickup event finally knows what was picked up.
        events.playerPickupItem((event) => {
            const stack = event.item.itemStack;
            logger.info(`[testkit] pickup ${stack ? stack.amount + " x " + stack.type : "?"} ` +
                        `delay=${event.item.pickupDelay}`);
        });

        events.playerSkinChange((event) => {
            logger.info(`[testkit] ${event.player.name} skin -> ${event.skinId} ` +
                        `cape="${event.capeId}" message="${event.skinChangeMessage}"`);
        });

        events.playerItemHeld((event) => {
            logger.info(`[testkit] held slot ${event.previousSlot} -> ${event.newSlot}`);
        });

        logger.info(`testkit ready: ${commands.list().length} commands, schema ${packets.schemaVersion}`);
    },

    onDisable() {
        if (redirect) redirect.unsubscribe();
        if (chunkTap) chunkTap.unsubscribe();
        if (sidebarTask) sidebarTask.cancel();
        if (packetTap) packetTap.unsubscribe();
        if (barTask) barTask.cancel();
        if (bar) bar.remove();
        // Maps and scoreboards need no cleanup: the runtime drops them when the plugin unloads, and
        // players on a private scoreboard fall back to the main one.
    },
};
