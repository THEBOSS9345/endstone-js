// Single-file plugin: dropped straight into plugins/ with no manifest. Name and version are derived
// from the filename, so there is no apiVersion to check and the server logs a warning about that.

module.exports = {
    onLoad() {
        console.log("[standalone] onLoad - single .js file, no package.json");
    },

    onEnable() {
        console.log("[standalone] onEnable");

        // process.exit() is neutralized by the host: a plugin must not be able to stop the Minecraft
        // server. Expect a warning here and for the server to keep running.
        process.exit(1);
        console.log("[standalone] still alive after process.exit(1) - the guard works");
    },

    onDisable() {
        console.log("[standalone] onDisable");
    },
};
