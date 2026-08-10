// Single-file plugin: dropped straight into plugins/ with no manifest. Name and version are derived
// from the filename, so there is no apiVersion to check and the server logs a warning about that.

module.exports = {
    onLoad() {
        console.log("[standalone] onLoad - single .js file, no package.json");
    },
    onEnable() {
        console.log("[standalone] onEnable");
    },
    onDisable() {
        console.log("[standalone] onDisable");
    },
};
