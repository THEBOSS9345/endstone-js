// Folder-style CommonJS plugin, and the API demo. @endstone/server is a virtual module served by the
// host - nothing to install - and its types are published to plugins/node_modules/@endstone/server so
// editors give you completions.
//
// Join the server and try: "hello", "!pos", "!heal", "!time", "badword".

const { server, events, logger } = require("@endstone/server");

module.exports = {
    onLoad() {
        logger.info("[hello] onLoad - CommonJS folder plugin");
    },

    onEnable() {
        logger.info(`[hello] ${server.name} ${server.version} (Minecraft ${server.minecraftVersion})`);
        logger.info(`[hello] protocol=${server.protocolVersion} online=${server.onlinePlayerCount}`);

        events.serverLoad(() => {
            const level = server.level;
            if (level) {
                logger.info(`[hello] level "${level.name}" time=${level.time} dimensions=${level.dimensionCount}`);
            }
        });

        events.playerJoin((event) => {
            const p = event.player;
            logger.info(`[hello] JOIN ${p.name}`);
            logger.info(`[hello]   xuid=${p.xuid} device=${p.deviceOs} version=${p.gameVersion} locale=${p.locale}`);
            logger.info(`[hello]   address=${p.address} ping=${p.ping}ms op=${p.isOp}`);
            logger.info(`[hello]   at ${p.x.toFixed(1)},${p.y.toFixed(1)},${p.z.toFixed(1)} in ${p.dimension}`);
            logger.info(`[hello]   health ${p.health}/${p.maxHealth}  level="${p.level.name}"`);

            event.joinMessage = `§a+ §f${p.name}`;
            p.sendMessage(`§bWelcome, ${p.name}! §7Try: hello, !pos, !heal, !time, badword`);
        });

        events.playerQuit((event) => {
            logger.info(`[hello] QUIT ${event.player.name}`);
            event.quitMessage = `§c- §f${event.player.name}`;
        });

        events.playerChat((event) => {
            const p = event.player;
            const text = event.message;
            logger.info(`[hello] CHAT <${p.name}> ${text}`);

            // Cancellation is synchronous on the server thread, so this really does block the message.
            if (text.toLowerCase().includes("badword")) {
                event.cancelled = true;
                p.sendMessage("§cThat word is not allowed here. (message cancelled by a JS plugin)");
                logger.info(`[hello]   cancelled the message from ${p.name}`);
                return;
            }

            if (text === "hello") {
                p.sendMessage(`§aHello ${p.name}! This reply came from JavaScript.`);
            } else if (text === "!pos") {
                p.sendMessage(`§7You are at §f${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)} §7in §f${p.dimension}`);
            } else if (text === "!heal") {
                p.health = p.maxHealth;
                p.sendMessage(`§aHealed to ${p.health}/${p.maxHealth} - a JS plugin wrote to player.health`);
            } else if (text === "!time") {
                const level = server.level;
                p.sendMessage(`§7World time is §f${level.time}§7; setting it to 1000 (morning)`);
                level.time = 1000;
            }
        }, { priority: "high" });

        logger.info("[hello] subscribed to serverLoad, playerJoin, playerQuit and playerChat");
    },

    onDisable() {
        logger.info("[hello] onDisable");
    },
};
