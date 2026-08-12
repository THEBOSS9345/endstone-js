# Node.js Scripting Rules

Rules for writing and extending the JavaScript plugin API for Endstone. Applies to every plugin and
every change to the API surface (the bridge in `plugin/api_bridge.cpp`, the runtime in `host/bootstrap.js`,
and the declarations in `endstone-server-types/index.d.ts`).

## 1. Plugins never touch the engine

The Minecraft server, the Endstone core, and the native host are off-limits to plugins. A plugin:

- imports everything it needs from `@endstone-js/server` and nothing else from the engine,
- never edits, patches, or reaches into the main source code,
- never loads native code or touches the C++ bridge directly.

If the API does not expose something, the API grows — the plugin never works around it.

## 2. The API is as simple as possible to use

Simplicity is a hard requirement, not a nicety. The rules that keep it that way:

- **Positions are vector objects, not flat numbers.** Never `player.x`, `player.y`, `player.z`.
  Read them once as a vector and let the API do the rest:

  ```js
  player.teleport(player.location);
  player.teleport(player.location, { rotation: { yaw: player.location.yaw + 180, pitch: 0 } });
  player.spawnParticle("minecraft:heart_particle", { x: player.location.x, y: player.location.y + 1, z: player.location.z });
  ```

  A method that takes a location takes a `{ x, y, z }` vector (see `Vector3` in the types). The
  runtime flattens it for you; you never split or reassemble coordinates.

- **Facing is one `rotation` object, not two numbers.** Read or assign `{ yaw, pitch }` in a single
  field:

  ```js
  player.rotation = { yaw: 0, pitch: -45 };
  const { yaw, pitch } = player.rotation;
  ```

  For a *player*, assigning `rotation` only moves the server-side facing — the client owns its camera.
  Pass the rotation to `teleport` to actually turn their view.

- **If two values belong together, they arrive together.** No method takes `x, y, z` or `yaw, pitch`
  as separate parameters. No method exposes the engine's argument order.

- **Prefer one well-named method over five specialized ones, and a property over a getter.**
  `player.location`, `block.location`, `player.velocity`. Reads are `readonly`; writes are plain
  assignment.

- **Adding anything to the API requires four edits in the same change:**
  1. the bridge in `node/plugin/api_bridge.cpp`,
  2. the runtime in `node/host/bootstrap.js` (if the runtime must know about it),
  3. the declaration in `endstone-server-types/index.d.ts`,
  4. an example that actually calls it, under `node/examples/`.

  There is no such thing as a runtime feature without types, or a type without an implementation.

- **A method also goes in `METHODS_BY_TYPE`, under the type that really implements it.** The runtime
  will not call a name that is not listed for the handle's type, and listing it under a type the bridge
  does not implement it for gives you a function that throws. `scripts/check_methods.py` compares the
  two and **fails the build** on a disagreement, so this is checked rather than remembered.

  Which type owns a method is decided by *which `resolve(target, Kind::X)` branch it sits in* in the
  bridge - so put it in the right branch rather than guarding inside a wider one, or the check will
  read it as belonging to the wider type.

## 3. Commands are registered at the top level, and they are always `/` commands

There is no chat-prefix convention — no `!heal`. Everything a plugin exposes is a real slash command,
so it behaves like every other command on the server.

**Register at the top level of the module, never in `onEnable`.** Endstone builds a plugin's command
list while the module is loading and hands it to Bedrock right then, so only a command registered at
that point reaches the client's `/` list, autocompletes, and gets its arguments validated. Register
later and the command still runs — it is picked up by watching command lines — but the client knows
nothing about it, so there is no completion and no validation. The runtime logs a warning when this
happens.

```js
import { commands } from "@endstone-js/server";

// Top level. Autocompletes, and the client offers spawn|shop|arena.
commands.register("warp", (sender, args) => {
    sender.teleport(WARPS[args[0]]);
}, { description: "Warps you.", usages: ["/warp <spawn|shop|arena>"], op: true });

export default { onEnable() { /* setup, not registration */ } };
```

`usages` is what buys the autocompletion, so fill it in. Each usage starts with `/` and the command's
own name; `<x>` is required, `[x]` optional, and `<a|b|c>` declares a custom enum whose values the
client completes. Types include `int`, `float`, `string`, `bool`, `player`, `block`, `entity_type` and
`message` — a `message` argument must come last, since it swallows the rest of the line.

Two consequences worth remembering:

- **A registered name shadows the server's own.** Do not register `reload`, `give` or `tp`; the command
  line is intercepted before the server sees it. The built-in reload is `/jsreload` for this reason.
- **A brand-new command name needs a restart.** `/jsreload` can rebind an existing command's handler,
  but a name the server has never seen cannot join the client's list until it restarts.

Handlers receive `(sender, args, raw)`. `sender` is either a player — with the whole `Player` surface —
or the console; test `sender.isConsole` before reaching for anything player-only. `args` are already
split, and always strings even when declared `int`.

## 4. Other npm packages are allowed

A plugin is an ordinary Node.js module. Installing and importing any npm package is supported and
expected — that is the point of running JavaScript instead of C++. No allowlist, no restrictions on
what a plugin may depend on.

Two consequences:

- Plugins bring their own `package.json`; the server does not supply dependencies.
- Heavy or blocking work (network, disk, CPU) must not run on the server thread. Offload it to a
  timer or `await` a promise. Event handlers run synchronously on the server thread — time spent there
  is time the server is not ticking (rule 7).

## 5. Everything is documented

`endstone-server-types/index.d.ts` is the single source of truth for the API. Every function and every
type:

- has a doc comment (not just a name) that says what it does and any defaults or edge cases, and
- is accompanied by an example of real usage somewhere in the repo — the `hello` example plugin, the
  doc comment, or a snippet in this README — so there is always a correct call to copy.

`tsc --noEmit` must pass (`npm test` in `endstone-server-types`). If your change makes the documented
examples not type-check, the change is wrong.

## 6. The runtime is forgiving

- **Unknown members read as `undefined`, never throw.** `player.somethingThatDoesNotExist` is
  `undefined`, not an error. Use that: check `if (value === undefined)` instead of relying on a throw.
- Stale handles (an object kept past the callback that produced it) still throw a clear error.
- Reading a member of the wrong type still throws. Only "does not exist" is silent.

## 7. The server thread is sacred

- Handlers run synchronously on the Minecraft server thread.
- Keep handlers cheap. Cancellation only works because handlers are synchronous — do not `await` in a
  handler and expect to cancel the event afterwards.
- Objects handed to a handler are valid only for that callback. Copy out what you need
  (`name`, `uniqueId`, a copy of `location`) rather than storing a `Player` or `Block` across ticks.

## 8. Style

- No comments that restate the code; comments explain *why* or document API surface.
- Doc comments match the tone and terse style of `index.d.ts`.
- Match the naming of the Endstone API it mirrors: `PlayerJoinEvent` is `events.playerJoin`, camelCased,
  minus the `Event` suffix.
