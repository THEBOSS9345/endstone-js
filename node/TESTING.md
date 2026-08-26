# Test plan

Everything below is **compile-verified only** — the C++ builds with no errors or warnings and
`tsc --noEmit` passes, but none of it has run against a live server. That's the split: I compile, you
test in game.

Start the server, then in chat run `/jstest` for the command index. The `testkit` example plugin
(`node/examples/testkit/`) drives every item here, and each command echoes to chat *and* the server log
— so a failure shows up as a missing line or an `undefined`, not silence.

```bash
node\scripts\test-endstone-node.ps1
```

`linux-serve.sh` symlinks every example, so `testkit` loads automatically. Expect at start-up:

```
[Nodejs] Node host ABI version 16
[Nodejs] loaded 3/3 JavaScript plugin(s)
[Nodejs] testkit ready: 49 commands, schema r26_u4
```

If the ABI line says anything other than 16, the plugin and host halves are out of step — rebuild.

---

## 1. Commands and autocomplete

| Do | Expect |
| --- | --- |
| Type `/js` in chat | The client offers `jstest`, `jsserver`, `jsdim`, … — **this is the thing to check first.** If they do not autocomplete, real registration is broken and everything below still works only by interception. |
| `/jsspawn ` then tab | Completes the `type` argument as a string |
| `/jsform ` then tab | Offers `action`, `message`, `modal` — a **custom enum** in the client |
| `/jsboard` as a non-op | Refused with a permission message, and the refusal appears in the log |
| `/jsbar ` then tab | Completes `off` |
| `/jsreload` | Reloads all plugins; the testkit re-registers |

**Known limitation:** a *new* command name needs a server restart, not just `/jsreload`.

## 2. Server, level, dimension

`/jsserver` — every field should have a plausible value. Watch for `tps` near 20 and `mspt` under 50.
`primaryThread=true` confirms handlers run on the server thread.

`/jsdim` — `highest at 0,0` should name a real block, `loadedChunks` should be non-zero with a player
online.

`/jsspawn` and `/jsspawn minecraft:zombie` — the mob appears at your feet, and the log shows a
`runtimeId` and `tags=["testkit"]`.

`/jsdrop` — three diamonds land at your feet.

## 3. Inventory and custom item data

1. `/jskit` — armour and a diamond pickaxe.
2. `/jsinv` — slot counts, held item, armour, ender chest all reported.
3. Hold the pickaxe and run `/jstag` several times.

**`uses` must increment across calls.** It reads the value back off the inventory rather than the copy
it just wrote, so if write-through is broken you get `§cWRITE-THROUGH FAILED`. This is the one to watch:
it proves custom NBT persists on the item.

Then drop the pickaxe, pick it up, and `/jstag` again — the count should carry over, since the data
lives in the item's own NBT.

## 4. Forms

`/jsform message`, `/jsform action`, `/jsform modal`.

- Each should render, and pressing a button reports the index in chat.
- Dismissing with Escape should report "dismissed" — that is the `onClose` path.
- The modal form should show a header, toggle, slider, dropdown, step slider, divider and text input,
  and submitting should report a JSON array of the values in order.

**Note on action-form indices:** decorations (header, divider) count towards the reported index, exactly
as Endstone reports them. The example deliberately mixes them so you can see the numbering.

## 5. Scoreboard

`/jsboard` — a sidebar titled `Testkit` with `Online`, `TPS` and `Refreshes`. `Refreshes` should tick up
every 2 seconds, which also proves the tick scheduler is running. `/jsboard off` removes it.

## 6. Boss bars

`/jsbar` — a bar across the top of the screen titled `Testkit`, counting down from 10 seconds. The
countdown is what proves a bar survives the callback that made it: it is updated from a timer, long
after the command returned.

The command also writes every property and reads it straight back, so it prints `§cSETTER FAILED` with
the offending values if any write is dropped. That includes `progress = 5`, which must clamp to `1`
rather than drawing a bar wider than its frame.

| Do | Expect |
| --- | --- |
| `/jsbar` | Bar appears, green, ten notches, counting down |
| `/jsbar` again | `viewers=1 [<your name>]` — adding the same player twice does not duplicate them |
| `/jsbar` with a second player, then `/jsbar` as them | `viewers=2` with both names |
| `/jsbar off` | Bar disappears for **everyone**, not just you |
| Reload with `/jsreload` while a bar is up | Bar disappears — `onDisable` removes it |

`/jssimilar` — hold something and run it. `vs itself: true` is the check; anything else prints
`§cisSimilar FAILED`. Holding a stack of the same item that differs only in count should still be
similar, since `isSimilar` ignores the amount.

## 7. Player lookup, bans, item metadata

`/jsfind` and `/jsfind <player>` — looks you up by name, then looks the result up again by the UUID it
just read. `§cUUID LOOKUP FAILED` means only the name path works. Also prints server uptime.

`/jsban <name>` — bans for **60 seconds only**, so a mistake lapses on its own, then reads the entry back
and prints who/why/when. `/jsban <name> off` lifts it early. Ban yourself if you like; you will need a
second account or a 60-second wait to get back in. Note a ban does **not** kick anyone already connected.

`/jsmeta` — hold a tool. Sets a custom name, two lore lines, unbreakable, repair cost and two
enchantments, then re-reads all of it *off the inventory* rather than off the copy it wrote. Any
`§c... NOT WRITTEN BACK` line means metadata write-through is broken. Sharpness 7 is above the vanilla
maximum on purpose — plugin enchanting is not anvil-limited.

`/jsclean` — strips all of that off again.

`/jscontents` — reads the whole inventory, writes it straight back with `setContents`, and compares the
filled-slot count. `§cSETCONTENTS LOST ITEMS` is the failure.

## 8. Maps, private scoreboards, translation

`/jsmap` — creates a map centred on you at scale 2, reads every property back, then sends it. It will be
**blank**: drawing needs a `MapRenderer`, which is a C++ interface JavaScript cannot implement. What is
testable is that it arrives, and that `§cMAP SETTER FAILED` does not appear.

`/jsprivate` — puts you on a scoreboard only you can see, titled `Just For You`. **The test needs two
players:** run `/jsboard` first so the shared sidebar is up, then `/jsprivate` — yours should change while
the other player's stays on `Testkit`. `/jsprivate off` puts you back.

`/jslang` and `/jslang <key>` — resolves a translation key. `item.diamond_sword.name` should come back as
`Diamond Sword`. Minecraft returns the key unchanged when there is no entry, so that is the only failure
signal there is.

## 9. Block states, snapshots, skin, introspection

`/jsblock` — reads the block under you: type, `runtimeId`, and its states. Then it **snapshots the block,
turns it to glass, and restores it from the snapshot**, all in one callback. `§cSNAPSHOT RESTORE FAILED`
is the failure. Also prints `oak_stairs` default states and stone's runtime id, both read without a block
in the world.

`/jsintro` — lists the main scoreboard's objectives (run `/jsboard` first so there is one), your scores,
and then tests that `item.clone()` is genuinely detached: `§cCLONE WROTE BACK` means a clone reached the
inventory slot, which it must never do.

`/jsskin` — your skin and cape ids and texture sizes. `64x64` vs `64x32` distinguishes the skin layout;
no cape reports as `no cape` rather than `0x0`.

## 10. Packets

`/jspacket` — encodes `AnvilDamagePacket`, decodes it back, and compares. Should print the byte count
and `complete=true`; any mismatch prints `§cROUND-TRIP MISMATCH`. It also decodes a `TextPacket` to show
the honest-partial path: `complete=false` with a reason.

`/jstap` — logs the next 20 inbound packets, decoded. Expect a mix of `complete=true` and
`complete=false` with reasons. **Only 143 of 229 packets decode end to end**; the rest stop at the first
optional field, union, map or NBT field, because the schema does not describe how presence is signalled.

`/jstap` again to stop.

## 11. Events

These fire without a command:

| Action | Log line |
| --- | --- |
| Break a block | `break <type> with <item or "bare hands">` — proves `heldItem` |
| Right-click a block | `interact rightClickBlock hasBlock=true block=… face=… item=…` |
| Scroll the hotbar | `held slot N -> M` |
| Die | `death: <name> by <cause>`, and the death message is rewritten |

**Not testable:** `serverListPing` no longer reaches JavaScript. Endstone fires it asynchronously from
RakNet's network thread, and delivering it would enter V8 off-thread and race the handle table - which is
exactly what the `handles must not be kept past the callback` errors on `motd` were. The runtime now drops
it and warns once at start-up.

---

## 12. Regression checks

These cover defects that were fixed after the sections above were written. Each one failed silently
before, which is why they are called out rather than left to the general passes.

| Do | Expect | Was |
| --- | --- | --- |
| Right-click a block holding an item | The `interact` log line ends `item=minecraft:<something>`, not `item=-` | `event.item` was always `undefined` on `playerInteract` and `playerItemConsume` — the branch that read it was unreachable |
| `/jstap`, then move around | Decoded packets whose payloads contain bytes ≥ 0x80 now decode; the `complete=true` share should be visibly higher than before | Payloads crossed as UTF-8, so every byte that was not valid UTF-8 became U+FFFD and then `0xFD` |
| `/jsboard`, then `/jsintro` repeatedly (20+ times) | Steady memory and no slowdown | Every read of `server.scoreboard`, `player.scoreboard`, `level.getDimension()` minted a *persistent* handle that was never released, and each event dispatch walks the whole handle table |
| `/jstap` on and off 20+ times, then break a block | The break still logs exactly once | Each `unsubscribe` left its Endstone listener wired forever, so listeners accumulated |
| From a command handler, call something that fires an event (e.g. `/gm` in the `commands` example, which calls `performCommand`) | The command finishes normally | A nested dispatch used to free the outer dispatch's `Location`/`ItemStack`/`Block` objects while the outer handler was still holding them |

The nested-dispatch case is the one worth being deliberate about: it is a use-after-free, so the old
behaviour was undefined rather than reliably wrong. It may have looked fine and corrupted memory anyway.

## 13. Newly bound API

Everything here was added after the sections above. Each command reads back what it wrote where it
can, so a dropped write shows up as a red `§c` line rather than as nothing at all.

### Players and permissions

| Do | Expect |
| --- | --- |
| `/jsplayers` | Every online player listed, and the count matching the list length. This is the loop that was impossible to write before — `onlinePlayerCount` existed, nothing iterated. |
| `/jsperm` | `has=false` → grant → `has=true` → deny → `has=false`. Any `§c` line means a write was dropped. |
| `/jsgated` | Refused until you run `/jsperm testkit.gated`, then allowed. This is the **declared-permission** path, which used to be unenforced — a command registered too late for Bedrock's registry was open to everyone. |

### Blocks and dimensions

| Do | Expect |
| --- | --- |
| `/jsstates` | Places stairs, writes one state, and reports the rest. **`§cOTHER STATES WERE LOST` is the failure to watch** — only the named state should change. Restores the original block afterwards. |
| `/jsdim2` | `type=overworld` (the enum, not the display name), and `getRelative("up")` / `getRelative("up", 2)` landing exactly 1 and 2 blocks higher. |

### Item metadata

| Do | Expect |
| --- | --- |
| `/jsmapitem` | You end up holding a `filled_map` pointing at the map the plugin made. This is the gap where `createMap()` and `sendMap()` both worked and there was no way to put a map *into an item*. |
| `/jsbook` | **A yellow "this server does not provide written book metadata" notice** — see below. |
| `/jspages` | The same notice for writable books. |
| `/jsbow` | The same notice for crossbows. |

**Only map metadata actually exists on this build.** Endstone declares `BookMeta`,
`WritableBookMeta` and `CrossbowMeta` in its public headers, but its core maps only
`minecraft:air` and `minecraft:filled_map` to a metadata subclass — everything else gets the plain
base. So those three are bound here and cannot succeed until upstream instantiates them. `item.metaType`
reports what an item really carries, which is the check to make before writing any of these fields.

This is worth re-testing after each upstream merge: the bindings are already in place, so they will
start working the moment the core wires the types up.

### Inventory matching

`/jsmatch` — hold a tool. It compares matching by **type id** with matching the **whole stack**.

The difference only appears once metadata is involved, so the real test is: put two identical items in
your inventory, enchant one, hold the enchanted one, and run it again. The type list should then be
**longer** than the stack list. Before this, `first("minecraft:diamond_pickaxe")` matched any pickaxe,
so a plugin could not tell one specific item from another of the same kind.

### Dropped items

`/jsdropped` — drops two diamonds and reads them back as an `Item` actor: `endstoneType=Item`, its
`itemStack`, and a `pickupDelay` and `unlimitedLifetime` that both persist. Then pick it up and watch
the log for the pickup line.

**`/jsdropped` sets `unlimitedLifetime`, so the drop will not despawn** — pick it up or it stays.

### Scoreboard, chat, server

| Do | Expect |
| --- | --- |
| `/jsboard`, then `/jsobj` | `testkit: display=sidebar order=descending`. Reading back *where* an objective is shown was previously impossible — only setting it worked. |
| Say anything in chat | The log reports the chat `format` and recipient count, and your messages gain a `[js]` prefix — applied by rewriting the format, not by cancelling and re-broadcasting. |
| `/jssrv2` | `maxPlayers` written and restored, and `getMap(id)` returning the map from `/jsmapitem`. |
| `/jsredirect`, then teleport | Every teleport lands you back where you stood. `/jsredirect` again to stop. This is `event.to` — before it, a teleport could only be cancelled outright, never redirected. |

### Drawing on a map

`/jsdraw` — creates a map, attaches a JavaScript renderer, and puts it in your hand. You should see a
red/green gradient with a blue disc in the middle. A blank map means the renderer never ran.

**This was previously documented as impossible.** It is not: Endstone lets a plugin supply a
`MapRenderer`, and its own Python bindings do exactly that through a trampoline. The JS side now
subclasses it and forwards the draw call.

The one thing to watch is the log line the first draw prints:

```
[testkit] map draw: primaryThread=true viewer=<you>
```

**If `primaryThread` is ever `false`, stop and report it** — the draw happens on the packet-send path,
and entering JavaScript from a non-server thread is unsafe for the same reason `serverListPing` had to
be dropped. The command shouts `MAP DRAW IS OFF THE SERVER THREAD` if it sees that.

A whole frame is assigned at once (`canvas.pixels = frame`) rather than pixel by pixel: a map is
128×128, so per-pixel writes would cross into the server 16384 times per draw, per viewer.

### Weather

Run `/weather rain` and `/weather thunder`. The log should report `weather -> raining` / `-> clear` and
`thunder -> storm` / `-> calm`. Both events fired before this but carried none of their state, so a
handler could not tell rain starting from rain stopping.

### Stopping the server

`/jsstop` warns; `/jsstop confirm` actually stops it via `server.shutdown()`. That is the only
sanctioned way for a plugin to stop the server — `process.exit()` is neutralised deliberately, so
before this there was no way at all.

---

## What is *not* covered

Not implemented, so nothing to test:

- **`itemFactory`** — its whole surface takes a detached `ItemMeta*`, which would need a new handle
  kind to expose something already reachable as properties on an item.
- **Custom dimensions** (`DimensionCreator`) — develop only.
- **Effects and attributes** — deliberately left out: Endstone is adding them on its `develop` branch with
  different types, so binding them against `main` would mean writing them twice.
- **Asynchronous events**, i.e. `serverListPing`. They fire off the server thread and cannot reach
  JavaScript at all; see section 8.
- **NBT decoding.** `packets.decode` stops at an NBT field; item tags are scalar-valued (nest with
  `JSON.stringify` into one key).
- **`pluginEnable`, `pluginDisable`, `blockGrow`, `blockForm`** — declared by Endstone but never fired,
  so a handler can never run.
- **51 packet fields typed `unknown`** — `cerealizer<NetworkItemStackDescriptor>::SerializedData` and
  friends, which protocol-docs names but does not describe. That is upstream, not fixable here.

## If something is wrong

- `protocol.json` missing → `/jspacket` throws a message telling you to run
  `python node/scripts/generate_protocol.py`. It is staged automatically by `linux-serve.sh`.
- After a BDS version bump, regenerate with `--ref <new branch>`; decoding against the wrong schema is
  silently wrong, which is why `packets.schemaVersion` is printed at start-up.
- A command that runs but does not autocomplete was registered too late — it must be at the top level of
  the module, not in `onEnable`. The runtime logs a warning when that happens.
