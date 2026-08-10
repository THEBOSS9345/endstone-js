# Node.js embedding spike (Milestone 0)

Proves that `BDS -> Endstone -> embedded Node.js -> JavaScript` works inside a single process.
Nothing more: **no Minecraft API, no TypeScript, no event system, no packet API, no npm integration.**
The only thing JavaScript can do here is `console.log`.

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
mangled C++ symbols on it   : (none)
```

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

## Testing without BDS

`endstone_node_host_smoke` is a **C** program that drives the host directly - if it links, the
boundary really needs no C++ runtime agreement:

```
./endstone_node_host_smoke node/js/main.js
```

It pumps 60 times at 50 ms to imitate the tick loop.

## Known limitations

- `process.execPath` is the BDS executable, so packages that re-spawn it will not work.
- Node's per-process initialization is not repeatable: exactly one host per process, and
  `/reload` will not re-create it.
- `console.trace` is mapped to a plain debug line rather than printing a stack.
- `worker_threads` is untested.
