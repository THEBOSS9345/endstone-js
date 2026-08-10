// Folder-style CommonJS plugin, and the API demo. @endstone-js/server is a virtual module served by the
// host - nothing to install - and its types are published to plugins/node_modules/@endstone-js/server so
// editors give you completions.
//
// Join and try: hello !pos !heal !gm !toast !title !particle !vel !spin !time badword
// Then break/place a block, and hit something.

const { server, events, logger } = require("@endstone-js/server");

// Everything goes to the console; anything with a player behind it is also shown in chat. Note that
// only player events have `event.player` - an actor event has `event.actor`, and a lifecycle hook has
// no event at all - so the recipient is passed in explicitly rather than assumed.
function report(text, recipient) {
    logger.info(`[hello] ${text}`);
    if (recipient) {
        recipient.sendMessage(`§7[hello] §f${text}`);
    }
}

module.exports = {
    onLoad() {
        report("onLoad - CommonJS folder plugin");
    },

    onEnable() {
        report(`${server.name} ${server.version} (Minecraft ${server.minecraftVersion})`);
        report(`protocol=${server.protocolVersion} online=${server.onlinePlayerCount}`);

        events.serverLoad(() => {
            const level = server.level;
            if (level) {
                report(`level "${level.name}" time=${level.time} dimensions=${level.dimensionCount}`);
            }
        });

        events.playerJoin((event) => {
            const p = event.player;
            report(`JOIN ${p.name}`);
            report(`  xuid=${p.xuid} device=${p.deviceOs} version=${p.gameVersion} locale=${p.locale}`);
            report(`  address=${p.address} ping=${p.ping}ms op=${p.isOp}`);
            report(`  at ${p.x.toFixed(1)},${p.y.toFixed(1)},${p.z.toFixed(1)} in ${p.dimension}`);
            report(`  health ${p.health}/${p.maxHealth}  level="${p.level.name}"`);

            event.joinMessage = `§a+ §f${p.name}`;
            p.sendMessage(`§bWelcome, ${p.name}!`);
            p.sendMessage("§7Try: §fhello !pos !heal !gm !toast !title !particle !vel !spin !time badword");
        });

        events.playerQuit((event) => {
            report(`QUIT ${event.player.name}`);
            event.quitMessage = `§c- §f${event.player.name}`;
        });

        events.playerChat((event) => {
            const p = event.player;
            const text = event.message;
            report(`CHAT <${p.name}> ${text}`);

            // Cancellation is synchronous on the server thread, so this really does block the message.
            if (text.toLowerCase().includes("badword")) {
                event.cancelled = true;
                p.sendMessage("§cThat word is not allowed here. (message cancelled by a JS plugin)");
                report(`  cancelled the message from ${p.name}`);
                return;
            }

            if (text === "hello") {
                p.sendMessage(`§aHello ${p.name}! This reply came from JavaScript.`);
            } else if (text === "!pos") {
                p.sendMessage(`§7You are at §f${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)} §7in §f${p.dimension}`);
            } else if (text === "!heal") {
                p.health = p.maxHealth;
                p.sendMessage(`§aHealed to ${p.health}/${p.maxHealth} - a JS plugin wrote to player.health`);
            } else if (text === "!gm") {
                p.gameMode = p.gameMode === "creative" ? "survival" : "creative";
                p.sendMessage(`§aGame mode is now §f${p.gameMode}`);
            } else if (text === "!toast") {
                p.sendToast("§aFrom JavaScript", "sendToast takes two strings across the C ABI");
            } else if (text === "!title") {
                p.sendTitle("§bEndstone", "§7running your JS plugin", 5, 40, 10);
            } else if (text === "!particle") {
                p.spawnParticle("minecraft:heart_particle", p.x, p.y + 1.5, p.z);
                p.sendMessage("§aSpawned a particle above you");
            } else if (text === "!vel") {
                p.sendMessage(`§7velocity §f${p.velocityX.toFixed(2)}, ${p.velocityY.toFixed(2)}, ${p.velocityZ.toFixed(2)}`);
            } else if (text === "!spin") {
                // teleport, not setRotation: the client owns its camera, so only a teleport turns it.
                p.teleport(p.x, p.y, p.z, (p.yaw + 180) % 360, p.pitch);
                p.sendMessage("§aTurned you around (teleport with yaw)");
            } else if (text === "!time") {
                const level = server.level;
                p.sendMessage(`§7World time is §f${level.time}§7; setting it to 1000 (morning)`);
                level.time = 1000;
            }
        }, { priority: "high" });

        // Block events carry both the block and the player responsible.
        events.blockBreak((event) => {
            const b = event.block;
            report(`BREAK ${b.type} at ${b.x},${b.y},${b.z} by ${event.player.name}`, event.player);
        });

        events.blockPlace((event) => {
            const b = event.block;
            report(`PLACE ${b.type} at ${b.x},${b.y},${b.z} by ${event.player.name}`, event.player);
        });

        // An actor event: event.actor, not event.player. The damage source says what did it.
        events.actorDamage((event) => {
            const victim = event.actor;
            const src = event.damageSource;

            // src.actor is who is responsible; src.damagingActor is what actually struck. For a bow
            // shot they differ - the shooter versus the arrow.
            const blame = src.actor;
            const weapon = src.damagingActor;
            const who = blame ? `${blame.endstoneType} ${blame.type}` : "nothing";
            const via = weapon && weapon.handle !== (blame && blame.handle) ? ` via ${weapon.type}` : "";

            // Tell whoever is involved, if either side happens to be a player.
            const audience = victim.endstoneType === "Player" ? victim
                : blame && blame.endstoneType === "Player" ? blame
                : null;

            report(`DAMAGE ${victim.type} took ${event.damage.toFixed(1)} (${src.type}) ` +
                   `from ${who}${via} indirect=${src.isIndirect} ` +
                   `health=${victim.health}/${victim.maxHealth}`, audience);

            // Damage is writable, so a plugin can soften or amplify a hit.
            if (src.type === "fall") {
                event.damage = event.damage / 2;
                report("  halved the fall damage", audience);
            }
        });

        report("subscribed to serverLoad, playerJoin/Quit/Chat, blockBreak/Place, actorDamage");
    },

    onDisable() {
        report("onDisable");
    },
};
