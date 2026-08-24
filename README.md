# endstone-js — a fork of Endstone with JavaScript and TypeScript plugins

> [!WARNING]
> **The JavaScript layer is early and unstable.** It is under active, heavy development: the API changes
> without notice, the ABI between the plugin and the host has been bumped many times already, and
> features land before they are hardened. Treat it as a preview, not something to run a real server on.
>
> Specifically, today:
>
> - **Nothing here is API-stable.** Names, shapes and behaviour change between commits, and there is no
>   deprecation period.
> - **Coverage is partial.** Large parts of the Endstone API are not bound yet - ban lists, effects,
>   attributes, enchantments, looking a player up by name, and more. See [`node/RULES.md`](node/RULES.md)
>   and the types package for what exists.
> - **Only 143 of 229 packets decode end to end**, by design - the protocol schema does not describe how
>   optional fields signal presence, so decoding stops rather than guessing.
> - **Expect breakage on a BDS or Endstone update.** Generated protocol definitions must be regenerated,
>   and the layer is verified against one BDS version at a time.
> - **Testing is thin.** It compiles and typechecks on every change; in-game coverage is manual.
>
> Bug reports and ideas are welcome, but do not depend on it yet.

> [!IMPORTANT]
> **This is a fork.** Upstream is [EndstoneMC/endstone](https://github.com/EndstoneMC/endstone), and its
> README follows below unchanged.
>
> This fork adds one thing: a **Node.js scripting layer**, so plugins can be written in JavaScript or
> TypeScript alongside Endstone's existing Python and C++ support. Node.js and V8 run *inside* the
> Bedrock server process, on the server thread, so a JS handler can cancel an event just like a Python
> or C++ one.
>
> ```js
> import { server, events, logger } from "@endstone-js/server";
>
> export default {
>   onEnable() {
>     logger.info(`running on ${server.minecraftVersion}`);
>     events.playerChat((event) => {
>       if (event.message.includes("spoiler")) event.cancelled = true;
>     }, { priority: "high" });
>   },
> };
> ```
>
> ### What the fork adds
>
> Everything new lives in [`node/`](node) and [`recipes/libnode/`](recipes/libnode). **No file outside
> those two directories is changed**, apart from this notice — the layer attaches to a stock Endstone
> through the public plugin API, so it works against an official `pip install endstone`.
>
> - plugins as a folder with `package.json` or as a single `.js` file, ESM or CommonJS
> - real npm packages, resolved per plugin (discord.js is part of the test suite)
> - the Endstone API exposed to JS: `Server`, `Level`, `Actor`, `Player`, `Block`, `DamageSource`, and
>   all 55 events with working cancellation
> - TypeScript definitions published as [`@endstone-js/server`](https://github.com/THEBOSS9345/endstone-server-types)
>
> See [`node/README.md`](node/README.md) for the architecture and how to build it.
>
> ### What is maintained here
>
> **Only the JavaScript scripting engine.** Everything else tracks upstream and is not maintained in
> this fork. Bugs in the server itself, the Python or C++ API, BDS version support, packaging, or
> anything not under `node/` should go to
> [upstream's issue tracker](https://github.com/EndstoneMC/endstone/issues) — please don't report them
> here, as they will only be fixed upstream and merged down.

---

<div align="center">
  <a href="https://github.com/EndstoneMC/endstone/releases">
    <img src="https://static.wikia.nocookie.net/minecraft_gamepedia/images/4/43/End_Stone_JE3_BE2.png" alt="Logo" width="80" height="80">
  </a>

<h3>Endstone (upstream)</h3>

<p>
  <b>High-performance Minecraft Bedrock server software</b><br>
  Extensible with Python and C++ plugins
</p>

[![Build](https://github.com/EndstoneMC/endstone/actions/workflows/build.yml/badge.svg)](https://github.com/EndstoneMC/endstone/actions/workflows/build.yml)
[![Minecraft](https://img.shields.io/badge/minecraft-v26.44_(Bedrock)-black)](https://feedback.minecraft.net/hc/en-us/sections/360001186971-Release-Changelogs)
[![PyPI - Version](https://img.shields.io/pypi/v/endstone)](https://pypi.org/project/endstone)
[![Python](https://img.shields.io/pypi/pyversions/endstone?logo=python&logoColor=white)](https://www.python.org/)
[![GitHub License](https://img.shields.io/github/license/endstonemc/endstone)](LICENSE)
[![Discord](https://img.shields.io/discord/1230982180742631457?logo=discord&logoColor=white&color=5865F2)](https://discord.gg/xxgPuc2XN9)
[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&logoColor=white)](https://cloudsmith.io/~endstone/repos/conan/packages/)

</div>

## Why Endstone?

Bedrock's official addon and script APIs let you add content, but can hardly modify core gameplay. Custom servers like
PocketMine and Nukkit offer that control, but sacrifice vanilla features. Endstone gives you both: cancellable events,
packet control, and deep gameplay access with full vanilla compatibility. Think of it as Paper for Bedrock. If you've
ever wished Bedrock servers had the same modding power as Java Edition, this is it.

## Quick Start

Get your server running in seconds:

```shell
pip install endstone
endstone
```

Then create your first plugin:

```python
from endstone.plugin import Plugin
from endstone.event import event_handler, PlayerJoinEvent


class MyPlugin(Plugin):
    api_version = "0.10"

    def on_enable(self):
        self.logger.info("MyPlugin enabled!")
        self.register_events(self)

    @event_handler
    def on_player_join(self, event: PlayerJoinEvent):
        event.player.send_message(f"Welcome, {event.player.name}!")
```

**Get started faster with our templates:**
[Python](https://github.com/EndstoneMC/python-plugin-template) | [C++](https://github.com/EndstoneMC/cpp-plugin-template)

## Features

- **Cross-platform** - Runs natively on both Windows and Linux without emulation, making deployment flexible and
  straightforward.

- **Always up-to-date** - Designed to stay compatible with the latest Minecraft Bedrock releases so you're never left
  behind.

- **Python & C++ plugins** - Write plugins in Python for rapid development, or use C++ when you need maximum
  performance. The choice is yours.

- **Powerful API** - A comprehensive API with 60+ events covering players, blocks, actors, and more. Includes commands,
  forms, scoreboards, inventories, and a full permission system.

- **Drop-in replacement** - Works with your existing Bedrock worlds and configurations. Just install and run.

- **Familiar to Bukkit developers** - If you've developed plugins for Java Edition servers, you'll feel right at home
  with Endstone's API design.

## Installation

Requires Python 3.10+ on Windows 10+ or Linux (Ubuntu 22.04+, Debian 12+).

### Using pip (recommended)

```shell
pip install endstone
endstone
```

### Using Docker

```shell
docker pull endstone/endstone
docker run --rm -it -p 19132:19132/udp endstone/endstone
```

### Building from source

```shell
git clone https://github.com/EndstoneMC/endstone.git
cd endstone
pip install .
endstone
```

For detailed installation guides, system requirements, and configuration options, see
our [documentation](https://endstone.dev/).

## Documentation

Visit [endstone.dev](https://endstone.dev/) for comprehensive guides, API reference, and tutorials.

## Contributing

We welcome contributions from the community! Whether it's bug reports, feature requests, or code contributions:

- **Found a bug?** Open an [issue](https://github.com/EndstoneMC/endstone/issues)
- **Want to contribute code?** Submit a [pull request](https://github.com/EndstoneMC/endstone/pulls)
- **Want to support the project?** [Become a sponsor](https://github.com/sponsors/EndstoneMC)

## License

Endstone is licensed under the [Apache-2.0 license](LICENSE).

## Acknowledgements

Endstone is proudly sponsored by [Bisect Hosting](https://bisecthosting.com/endstone), which offers managed Minecraft server hosting.

[![Bisect Hosting](docs/assets/bisecthosting-banner.webp)](https://bisecthosting.com/endstone)

Package repository hosting is graciously provided by [Cloudsmith](https://cloudsmith.com), which offers free package management for open-source projects.

[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith)](https://cloudsmith.com)
