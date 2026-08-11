// Folder-style ES module plugin, and the API demo. @endstone-js/server is a virtual module served by
// the host - nothing to install - and its types are published to plugins/node_modules/@endstone-js/server
// so editors give you completions.
//
// Join and try: hello !pos !heal !gm !toast !title !particle !vel !spin !time badword
// Then break/place a block, and hit something.

import { commands } from "@endstone-js/server";
import { server, events, logger } from "@endstone-js/server";

// Everything goes to the console; anything with a player behind it is also shown in chat. Note that
// only player events have `event.player` - an actor event has `event.actor`, and a lifecycle hook has
// no event at all - so the recipient is passed in explicitly rather than assumed.
function report(text, recipient) {
    logger.info(`[hello] ${text}`);
    if (recipient) {
        recipient.sendMessage(`§7[hello] §f${text}`);
    }
}

export default {
    onLoad() {
        report("onLoad - ES module folder plugin");
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
            report(`  at ${p.location.x.toFixed(1)},${p.location.y.toFixed(1)},${p.location.z.toFixed(1)} in ${p.dimension}`);
            report(`  health ${p.health}/${p.maxHealth}  level="${p.level.name}"`);

            event.joinMessage = `§a+ §f${p.name}`;
            p.sendMessage(`§bWelcome, ${p.name}!`);
            p.sendMessage("§7Try: §fhello !pos !heal !gm !toast !title !particle !vel !spin !time badword");
        });

      events.playerQuit((event) => {
            report(`QUIT ${event.player.name}`);
            event.quitMessage = `§c- §f${event.player.name}`;
        });

      events.playerDropItem((event) => {
        const item = event.item;

        if (item.type === "minecraft:diamond") {
          event.cancel()
          return
        }

        report(`DROP ${item.amount} x ${item.type} (max stack ${item.maxStackSize})`, event.player);
        });

        events.playerChat((event) => {
            const p = event.player;
          const text = event.message;

          p.teleport({
            x: 1,
            y: 100,
            z: 1
          })

          p.isOp = true
            // Cancellation is synchronous on the server thread, so this really does block the message.
            if (text.toLowerCase().includes("badword")) {
                event.cancelled = true;
                p.sendMessage("§cThat word is not allowed here. (message cancelled by a JS plugin)");
                report(`  cancelled the message from ${p.name}`);
                return;
            }

        }, { priority: "high" });


        // Block events carry both the block and the player responsible.
      events.blockBreak((event) => {
        const b = event.block;
        report(`BREAK ${b.type} atss ${b.location.x},${b.location.y},${b.location.z} by ${event.player.name}`, event.player);
        });

        events.blockPlace((event) => {
          const b = event.block;

            report(`PLACE ${b.type} at ${b.location.x},${b.location.y},${b.location.z} by ${event.player.name}`, event.player);
        });

      report("subscribed to serverLoad, playerJoin/Quit/DropItem/Chat, blockBreak/Place, actorDamage");
    },

    onDisable() {
        report("onDisable");
    },
};
