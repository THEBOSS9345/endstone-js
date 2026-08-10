// ES module plugin. "type": "module" in package.json selects ESM; the default export supplies the
// lifecycle hooks. The same @endstone/server module works here with real named imports.

import { server, logger } from "@endstone/server";

export default {
    onLoad() {
        logger.info("[esm-hello] onLoad - loaded as an ES module");
    },

    // Hooks may be async. The server does not wait for them, but a rejection is reported.
    async onEnable() {
        const os = await import("node:os");
        logger.info(`[esm-hello] onEnable - async hook, ${os.cpus().length} cpus`);
        logger.info(`[esm-hello] same API from ESM: ${server.name} ${server.version}`);
    },

    onDisable() {
        logger.info("[esm-hello] onDisable");
    },
};
