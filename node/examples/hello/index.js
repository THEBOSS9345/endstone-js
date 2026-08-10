// Folder-style plugin: identity comes from package.json, lifecycle hooks from the exported object.
// No Minecraft API is bound yet, so console output is all a plugin can do at this milestone.

module.exports = {
    onLoad() {
        console.log("[hello] onLoad - folder plugin, loaded from package.json");
    },
    onEnable() {
        console.log(`[hello] onEnable - require resolves from ${__dirname}`);
    },
    onDisable() {
        console.log("[hello] onDisable");
    },
};
