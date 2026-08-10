// The npm acceptance test: discord.js is a large, real-world package with a deep dependency tree.
// This does not connect to Discord - no token is involved. It only proves the package resolves from
// the plugin's own node_modules and that its classes construct inside the embedded runtime.
//
// Run "npm install" in this folder first, or set endstone.autoInstall to true in package.json.

const { Client, GatewayIntentBits, version } = require("discord.js");

module.exports = {
    onEnable() {
        console.log(`[discord-bot] discord.js v${version} loaded from the plugin's node_modules`);

        const client = new Client({ intents: [GatewayIntentBits.Guilds] });
        console.log(`[discord-bot] constructed a Client, ready=${client.isReady()}`);

        // Deliberately not calling client.login(): the point is that the package works, not that a
        // bot connects. Destroy it so no handles are left holding the event loop open.
        void client.destroy();
        console.log("[discord-bot] client destroyed - npm package works in-process");
    },

    onDisable() {
        console.log("[discord-bot] onDisable");
    },
};
