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
[Nodejs] Node host ABI version 13
[Nodejs] loaded 3/3 JavaScript plugin(s)
[Nodejs] testkit ready: 11 commands, schema r26_u4
```

If the ABI line says anything other than 13, the plugin and host halves are out of step — rebuild.

---

## 1. Commands and autocomplete

| Do | Expect |
| --- | --- |
| Type `/js` in chat | The client offers `jstest`, `jsserver`, `jsdim`, … — **this is the thing to check first.** If they do not autocomplete, real registration is broken and everything below still works only by interception. |
| `/jsspawn ` then tab | Completes the `type` argument as a string |
| `/jsform ` then tab | Offers `action`, `message`, `modal` — a **custom enum** in the client |
| `/jsboard` as a non-op | Refused with a permission message, and the refusal appears in the log |
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

## 6. Packets

`/jspacket` — encodes `AnvilDamagePacket`, decodes it back, and compares. Should print the byte count
and `complete=true`; any mismatch prints `§cROUND-TRIP MISMATCH`. It also decodes a `TextPacket` to show
the honest-partial path: `complete=false` with a reason.

`/jstap` — logs the next 20 inbound packets, decoded. Expect a mix of `complete=true` and
`complete=false` with reasons. **Only 143 of 229 packets decode end to end**; the rest stop at the first
optional field, union, map or NBT field, because the schema does not describe how presence is signalled.

`/jstap` again to stop.

## 7. Events

These fire without a command:

| Action | Log line |
| --- | --- |
| Break a block | `break <type> with <item or "bare hands">` — proves `heldItem` |
| Right-click a block | `interact rightClickBlock hasBlock=true block=… face=… item=…` |
| Scroll the hotbar | `held slot N -> M` |
| Die | `death: <name> by <cause>`, and the death message is rewritten |
| Look at the server in your server list | MOTD reads `testkit - N online` |

---

## What is *not* covered

Not implemented, so nothing to test:

- **Ban lists, bossbars, `itemFactory`, `sendMap`, `Inventory.setContents`, `ItemStack.isSimilar`.**
  `isSimilar` needs handle arguments in `invoke`, which is an ABI change I have not made.
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
