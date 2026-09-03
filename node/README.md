# Node.js scripting layer

> [!WARNING]
> **Early and unstable.** This layer is under heavy development. The API and the plugin/host ABI change
> without notice, coverage of the Endstone API is partial, and it is verified against one BDS version at a
> time. Treat it as a preview - see the root [README](../README.md) for the detail.

The JavaScript and TypeScript plugin support added by
[endstone-js](https://github.com/THEBOSS9345/endstone-js), a fork of
[Endstone](https://github.com/EndstoneMC/endstone). **This directory is the only part of the fork that
is maintained here**; everything else tracks upstream.

Node.js and V8 run inside the Bedrock server process on the server thread, so JavaScript plugins get the
same synchronous access to the Endstone API - including event cancellation - that Python and C++ plugins
have. Nothing outside `node/` and `recipes/libnode/` is modified: the layer attaches to a stock Endstone
through the public plugin API.

TypeScript definitions are a separate project, published to npm as
[`@endstone-js/server`](https://github.com/THEBOSS9345/endstone-server-types). Its minor version tracks the
Endstone API version, so `0.11.x` targets API `0.11`. Anything bound in `plugin/types/` needs a matching
declaration there; the build fails if one is missing, so the types cannot fall behind the runtime.

## Architecture

Two binaries, because on Linux they cannot share a C++ runtime:

| Binary | Toolchain | Links |
|---|---|---|
| `endstone_nodejs` (plugin) | Endstone's: clang + **libc++** (Linux) / MSVC ABI (Windows) | `endstone::endstone` only |
| `endstone_node_host` (host) | libnode's: gcc + **libstdc++** (Linux) / MSVC ABI (Windows) | `libnode` only |

Endstone forces `-stdlib=libc++` for ABI compatibility with BDS
(`include/CMakeLists.txt`), while `libnode.so` links `libstdc++.so.6`. Node's embedder API is
*not* a C ABI - `node::InitializeOncePerProcess` takes `const std::vector<std::string>&` and returns
`std::shared_ptr<InitializationResult>` - so a libc++ translation unit cannot call it. The two halves
therefore speak only the C ABI in [`include/endstone_node_abi.h`](include/endstone_node_abi.h):
opaque handles, integers, `const char*`, buffer+length, function pointers and error codes.

Verified on Linux:

```
libnode.so                      : [libstdc++.so.6] [libm.so.6] [libgcc_s.so.1] [libc.so.6]
node host (gcc/libstdc++)       : [libnode.so.147] [libstdc++.so.6] [libgcc_s.so.1] [libc.so.6]
endstone plugin (clang/libc++)  : [libm.so.6] [libc.so.6]          <- no libstdc++, no libnode

exported surface of the host: esn_abi_version esn_host_create esn_host_destroy
                              esn_host_pump esn_host_start esn_status_message
                              esn_host_load_plugin esn_plugin_get_meta esn_plugin_invoke
                              esn_plugin_unload esn_plugin_reload esn_host_dispatch_event
mangled C++ symbols on it   : (none)
```

### What each type exposes

The Endstone side is a set of **descriptor tables**, one file per upstream header folder, under
[`plugin/types/`](plugin/types). `include/endstone/block/block.h` is bound by `types/block/block.cpp`,
`include/endstone/event/player/*.h` by `types/events/player.cpp`, and so on. Where a binding goes is
never a judgement call - it goes wherever upstream put the thing it binds, which is what makes "what
are we missing?" a directory diff rather than a memory exercise.

A member is one line, and its C++ signature decides everything else:

| the getter returns | the member becomes |
|---|---|
| `int`, `float`, `bool`, `std::string` | that accessor |
| `std::unique_ptr<T>` | a handle, and the bridge takes ownership |
| `Level &`, `Block *` | a handle, tracked under `T`'s kind |
| `std::vector<std::string>` | one string, newline-joined |
| `std::optional<T>` | `T`, or absent - which reads back as `undefined` |

Inheritance is declared, not repeated: `sendMessage` sits on `CommandSender` and a `Player` reaches it
through `Mob` and `Actor`. Each edge carries a compiler-generated upcast, because a handle holds
`void *` and casting that to a base does no pointer adjustment - `Player` derives from both `Mob` and
`OfflinePlayer`, so it is layout rather than the type system that decides whether getting this wrong
shows up.

Events cannot be resolved to a kind - every one arrives as an `Event` and reports its class through
`getEventName()` - so they are keyed by that name and the downcast it licenses is generated once. See
the `dynamic_cast` warning below for why the name is the only way down.

The runtime needs no copy of any of this: it calls the ABI's `describe` at start-up and builds its
tables from the answer.

The runtime that JavaScript actually sees - the `@endstone-js/server` module, the event and command
registries, the packet codec - is [`host/bootstrap.js`](host/bootstrap.js). It is a real JavaScript file
so it can be linted and syntax-checked, and `scripts/embed_js.py` compiles it into the host as a string
at build time, so there is still no runtime asset to locate. A syntax error there fails the build rather
than the server's first start-up. Editing it rebuilds the host; nothing needs re-staging beyond the DLL.

## The API

`@endstone-js/server` is a **virtual module** served from memory by the host - nothing to install, and it
works identically from CommonJS and ESM. It is shaped after **Endstone's own API**, not Bedrock's
ScriptAPI, so it reads 1:1 with the Endstone documentation: `Server`, `Player`, `Block`, and the
`PlayerXxxEvent` family, rather than `world.afterEvents`.

```js
import { server, logger } from "@endstone-js/server";   // ESM
const { server, logger } = require("@endstone-js/server");  // CommonJS

logger.info(`${server.name} ${server.version} (Minecraft ${server.minecraftVersion})`);
server.broadcastMessage("hello from a JavaScript plugin");
```

| Object | Members |
|---|---|
| `server` | `name`, `version`, `minecraftVersion`, `protocolVersion`, `onlinePlayerCount`, `onlinePlayers`, `maxPlayers`, `port`, `level`, `scoreboard`, `logger`, `broadcastMessage`, `dispatchCommand`, `getPlayer`, `createMap`, `createBossBar`, `createScoreboard`, `banPlayer`, `banIp`, `shutdown`, plus the tick counters and registry lookups |
| `Level` | `name`, `seed`, `time` (writable), `actorCount`, `dimensionCount`, `getDimension(name)` |
| `Dimension` | `name`, `type`, `level`, `actorCount`, `loadedChunks`, `getBlockAt`, `getHighestBlockAt`, `spawnActor`, `dropItem`, `getActor` |
| `CommandSender` | `name`, `isOp`, `isConsole`, `isBlock`, `block`, `permissionLevel`, `sendMessage`, `sendErrorMessage`, `hasPermission`, `isPermissionSet`, `addPermission`, `removePermission` |
| `Actor` | everything on `CommandSender`, plus `type`, `id`, `runtimeId`, `dimension`, `location`, `rotation`, `velocity`, `isOnGround`, `isInWater`, `isInLava`, `isDead`, `isValid`, `level`, `nameTag`, `scoreTag`, `isNameTagVisible`, `scoreboardTags`, `teleport`, `remove()` |
| `Mob` | everything on `Actor`, plus `health`, `maxHealth`, `isGliding` |
| `Player` | everything on `Mob`, plus `uniqueId`, `xuid`, `locale`, `deviceOs`, `deviceId`, `gameVersion`, `address`, `port`, `ping`, `gameMode`, `skin`, `inventory`, `enderChest`, `scoreboard`, `isSneaking`, `isSprinting`, `isFlying`, `allowFlight`, `expLevel`, `expProgress`, `totalExp`, `flySpeed`, `walkSpeed`, `kick`, `performCommand`, `sendPopup`, `sendTip`, `sendTitle`, `sendToast`, `sendForm`, `sendMap`, `transfer`, `giveExp`, `playSound`, `spawnParticle` |
| `Item` | a dropped stack in the world: everything on `Actor`, plus `itemStack`, `pickupDelay`, `unlimitedLifetime`, `thrower` |
| `Block` | `type` (writable), `x`, `y`, `z`, `dimension`, `location`, `data`, `blockStates`, `runtimeId`, `getRelative`, `captureState`, `clone` |
| `BlockData` | `type`, `blockStates`, `runtimeId` - a palette entry with no position |
| `BlockState` | a detached snapshot: `type`, `x`, `y`, `z`, `location`, `data`, `blockStates`, `block`, `update(force, applyPhysics)` |
| `ItemStack` | `type`, `amount`, `data`, `maxStackSize`, `maxDurability`, `displayName`, `lore`, `damage`, `repairCost`, `unbreakable`, `enchants`, custom NBT through `getTag`/`setTag`, and the book, map and crossbow metadata |
| `Inventory` | `size`, `isEmpty`, `maxStackSize`, `firstEmpty`, `getItem`, `setItem`, `addItem`, `removeItem`, `clear` |
| `PlayerInventory` | everything on `Inventory`, plus `heldItemSlot` and the equipment slots - `helmet`, `chestplate`, `leggings`, `boots`, `itemInMainHand`, `itemInOffHand` |
| `Scoreboard` | `objectives`, `entries`, `addObjective`, `setDisplay`, `setDisplaySlot`, `setSortOrder`, `setScore`, `addScore`, `resetScores` |
| `BossBar` | `title`, `color`, `style`, `progress`, `visible`, `addPlayer`, `removePlayer`, `removeAll`, `remove` |
| `MapView` | `id`, `centerX`, `centerZ`, `scale`, `isLocked`, `isVirtual`, `dimension`, `addRenderer(draw)` |
| `MapCanvas` | `pixels` - a whole 128x128 RGBA frame in one write - and `setPixel` |
| `DamageSource` | `type`, `isIndirect`, `actor`, `damagingActor` |
| `Location` | `x`, `y`, `z`, `pitch`, `yaw`, `blockX`/`blockY`/`blockZ`, `dimension`, `block`, `direction` |
| `Vector` | `x`, `y`, `z`, `blockX`/`blockY`/`blockZ`, `length`, `lengthSquared` - what `velocity` and `direction` return |
| `Vector3` | `{ x, y, z }`, the shape you write when passing a position in |
| `logger` | `trace`, `debug`, `info`, `warning`, `error`, `critical` |

Writable members are plain assignment: `player.health = 20`, `level.time = 6000`, `block.type = "minecraft:stone"`.

Implemented with `module.registerHooks()`, which is synchronous and intercepts `require()` as well as
`import()`. The virtual module is a real ES module, so named imports work; CommonJS plugins reach it
through Node's `require(esm)` support.

### Events

Names are the Endstone event class minus `Event`, camelCased, so `PlayerJoinEvent` is
`events.playerJoin`. Handlers run **synchronously on the server thread**, which is exactly what makes
cancellation work:

```js
import { events, logger } from "@endstone-js/server";

events.playerJoin((event) => {
  logger.info(`${event.player.name} joined from ${event.player.address}`);
  event.joinMessage = `${event.player.name} joined the server`;
});

events.playerChat((event) => {
  if (event.message.includes("badword")) {
    event.cancelled = true;                       // blocks the message for real
    event.player.sendMessage("Not allowed here.");
  }
}, { priority: "high", ignoreCancelled: true });
```

`priority` accepts Endstone's ladder (`lowest`…`monitor`), and subscriptions return
`{ unsubscribe() }`. Keep handlers fast: time spent in one is time the server is not ticking.

### Objects and handles

`event.player` is a live `Player`, not a snapshot. Objects are backed by **dispatch-scoped handles**:
valid only inside the callback that produced them. Do not stash a `Player` across ticks - copy out
`name` or `uniqueId` instead. Touching a stale object throws a clear error rather than crashing the
server, because handles are validated through a table rather than being raw pointers.

`server.level` is the exception: it is a *persistent* handle, because the level lives as long as the
server does, so it is safe to keep. Objects reached from it (`level.actorCount`) are still values, not
retained references.

Nested objects work to any depth - `event.player.level.name` is three hops through the accessors - and
the JavaScript side distinguishes a returned object from a plain number by a tag the host attaches,
so no property needs to be special-cased.

> **Extending the bridge: never use `dynamic_cast` on an Endstone type.** It compiles and then silently
> fails. `endstone_add_plugin` builds with hidden visibility and statically links libc++abi, so the
> plugin's `type_info` for e.g. `PlayerEvent` is a different object from the one `endstone_runtime.so`
> used to construct the event, and libc++abi compares `type_info` by pointer identity. Downcast through
> a virtual accessor instead - compare `event->getEventName()` against the class name, then
> `static_cast`. That needs no RTTI and works across the library boundary.

Property access goes through a fixed set of generic typed accessors keyed by name, so exposing more of
Endstone's API is one line in the file named after the upstream header it comes from - never an ABI
change, and never an edit to the runtime, which asks the bridge what exists at start-up. See
[Architecture](#architecture). Name lookup per access is a deliberate trade for that; if a specific
property ever shows up in a profile, give it its own entry point rather than redesigning the scheme.

Everything reaching the API runs on the BDS server thread, so no marshalling is involved. String
getters use a size-then-fetch convention and every entry point catches all exceptions, since the
return path passes through V8 frames compiled without exceptions.

### Types

TypeScript definitions live in a **separate project**, published to npm as
[`@endstone-js/server`](https://www.npmjs.com/package/@endstone-js/server). They are not part of this
repository: the server provides the implementation, the package provides only declarations - the same
split Mojang uses for `@minecraft/server`.

Install them in your plugin folder when you want completions:

```shell
cd plugins/my-plugin
npm install --save-dev @endstone-js/server
```

A `devDependency`, because they are needed to *write* a plugin and never to run one - the runtime module
comes from the host's memory either way. A plugin without them installed behaves identically, and the
missing-`node_modules` warning ignores `devDependencies`, so it stays quiet.

Anything bound in `plugin/types/` needs the matching declaration added there, or the types will
confidently describe an API that does not exist - which `scripts/check_types.py` turns into a build
failure rather than a surprise.

**Coverage:** every event Endstone declares is bound, along with `Server`, `Level`, `Dimension`,
`Block`, `BlockData`, `BlockState`, `Actor`, `Mob`, `Player`, `Item`, `ItemStack`, `Inventory`,
`PlayerInventory`, `Scoreboard`, `BossBar`, `MapView`, `MapCanvas`, `DamageSource`, `Location`,
`Vector` and `CommandSender`. `scripts/check_events.py` fails the build if an upstream release adds an
event that is not bound.

## Writing plugins

Four shapes, all loaded from `plugins/`:

| Shape | Layout | Notes |
|---|---|---|
| CommonJS folder | `plugins/name/package.json` + `main` | `module.exports = { onEnable() {} }` |
| ES module folder | same, plus `"type": "module"` | `export default { onEnable() {} }` |
| Single file | `plugins/name.js` | identity from the filename, no manifest |
| With npm deps | folder + `node_modules` | `require`/`import` resolves per plugin |

`package.json` carries the identity; an `endstone` block carries the server-specific bits:

```json
{
  "name": "my-plugin", "version": "1.0.0", "main": "index.js",
  "dependencies": { "discord.js": "^14.16.3" },
  "endstone": { "apiVersion": "0.11", "load": "postworld", "autoInstall": false }
}
```

The name is normalized to Endstone's `[a-z0-9_]` rule, so an npm scope and dashes are fine
(`@me/my-plugin` becomes `my_plugin`). Lifecycle hooks are `onLoad`, `onEnable`, `onDisable`; they may
be `async`, though the server does not wait for them and a rejection is logged.

**Dependencies** are yours to install - run `npm install` in the plugin folder. If a plugin declares
dependencies with no `node_modules`, the server warns and names them. Setting
`endstone.autoInstall: true` makes the host run `npm install` itself, which is off by default because
it blocks the server thread while it runs.

Both module systems go through `require()`, relying on Node's synchronous `require(esm)` support.
Dynamic `import()` is *not* used to load plugins: the bootstrap has no file identity, so `import()`
from it reaches only `node:` builtins. Inside a plugin, `import()` works normally. A plugin using
**top-level await** cannot be loaded and is reported as such - move awaited work into `onEnable()`.

`process.exit()`, `process.abort()` and `process.reallyExit()` are neutralized: a plugin must not be
able to stop the Minecraft server.

Working examples live in [`examples/`](examples): `hello` (ESM).

## Threading

Node is initialized and JavaScript is executed on the **BDS server thread**, the same thread that
`Plugin::onEnable` and every `Scheduler` sync task run on. The event loop is advanced with
`uv_run(UV_RUN_NOWAIT)` from a `runTaskTimer(..., 1, 1)` task - one pump per tick. No
`napi_threadsafe_function` is involved yet; nothing needs marshalling because nothing runs off-thread.

## Obtaining libnode

Pinned to **Node 26.3.0** (`NODE_MODULE_VERSION 147`). That number is the compatibility contract for
npm native addons later, so treat it as a deliberate choice rather than a default.

There is no official prebuilt libnode - `--shared` is documented as unofficial. Two supported paths:

**1. Conan recipe (source of truth).** `recipes/libnode` builds from the official Node source release
with `./configure --shared` / `vcbuild dll`. Slow (hours) but reproducible and self-owned:

```shell
conan create recipes/libnode/all --version 26.3.0
```

`node/CMakeLists.txt` picks the package up automatically via `find_package(libnode CONFIG)`.

**2. Prebuilt fast path.** Downloads a checksum-pinned build of the same version so a fresh checkout
is productive in minutes:

```shell
python node/scripts/fetch_libnode.py
# -> build/libnode, then pass -DENDSTONE_NODE_LIBNODE_ROOT=build/libnode
```

Prebuilts come from [metacall/libnode](https://github.com/metacall/libnode), which builds Linux with
gcc/libstdc++ and Windows with `vcbuild dll` (/MD) - the toolchains the firewall assumes. Note their
Linux amd64 builds have been failing since v26.5, which is part of why 26.3.0 is the pin and why the
recipe exists.

Passing `ENDSTONE_NODE_LIBNODE_ROOT` explicitly skips the Conan lookup. Expected layout:

```
<libnode-root>/
  include/node/node.h ...
  lib/libnode.lib             (Windows)  or  lib/libnode.so + lib/libnode.so.<ABI>  (Linux)
  bin/libnode.dll             (Windows)
```

## Building

**Windows, one command.** Fetches libnode, builds both halves, and stages them into a server:

```
.\node\scripts\build.ps1 -ServerDir .\bedrock_server
```

Endstone itself is not rebuilt - the Node layer attaches to a stock Endstone through the public plugin
API, so an official wheel (`pip install endstone`) works as-is. Pass `-BuildDir` a **short** path:
FetchContent nests deeply enough that a long build path trips Windows' 260-character limit, and the
script refuses upfront rather than letting ninja fail cryptically.

The manual equivalents follow.

Windows - one configure builds both halves (single MSVC ABI):

```
cmake -S node -B build/node -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DENDSTONE_NODE_LIBNODE_ROOT=<libnode-root>
cmake --build build/node
```

Linux - configure twice, because each half needs its own C++ runtime:

```bash
# host: must match libnode
cmake -S node -B build/node-host -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DENDSTONE_NODE_BUILD_PLUGIN=OFF -DENDSTONE_NODE_LIBNODE_ROOT=<libnode-root> \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build/node-host

# plugin: must match Endstone/BDS
cmake -S node -B build/node-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DENDSTONE_NODE_BUILD_HOST=OFF \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/node-plugin
```

## Running

```
<server>/plugins/endstone_nodejs.dll          (or .so)
<server>/plugins/nodejs/endstone_node_host.dll (or libendstone_node_host.so)
<server>/plugins/nodejs/libnode.dll            (or libnode.so.<ABI>)
<server>/plugins/nodejs/main.js                (created on first run if absent)
```

The plugin logs a warning and stays inert if the host is missing, so a server without libnode still
boots normally.

## Known limitations

- `process.execPath` is the BDS executable, so packages that re-spawn it will not work.
- Node's per-process initialization is not repeatable: exactly one host per process, and
  `/reload` will not re-create it. A JS plugin reloads in place with `!reload [name]` in chat, which
  re-runs its module without touching the host.
- `console.trace` is mapped to a plain debug line rather than printing a stack.
- `worker_threads` is untested.
