// Folder-style CommonJS plugin: identity comes from package.json, lifecycle hooks from the exported
// object. @endstone/server is a virtual module served by the host - there is nothing to install.

const { server, logger } = require("@endstone/server");

module.exports = {
    onLoad() {
        logger.info("[hello] onLoad - CommonJS folder plugin");
    },

    onEnable() {
        logger.info(`[hello] ${server.name} ${server.version} (Minecraft ${server.minecraftVersion})`);
        logger.info(`[hello] protocol=${server.protocolVersion} online=${server.onlinePlayerCount}`);
        server.broadcastMessage("hello from a JavaScript plugin");
        logger.warning("[hello] warning level reaches the Endstone logger");
    },

    onDisable() {
        logger.info("[hello] onDisable");
    },
};
