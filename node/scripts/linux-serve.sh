#!/usr/bin/env bash
# Builds both halves, stages the plugins, turns the allow-list off, and runs the server in the
# foreground so a real Minecraft client can join and exercise the API.
set -uo pipefail

SERVER=/server

echo "=== building ==="
mkdir -p /libnode-root/lib
ln -sfn /libnode/headers/include /libnode-root/include
ln -sfn /libnode/linux/libnode.so /libnode-root/lib/libnode.so
ln -sfn /libnode/linux/libnode.so.147 /libnode-root/lib/libnode.so.147

cmake -S /src/node -B /build/host -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DENDSTONE_NODE_BUILD_PLUGIN=OFF -DENDSTONE_NODE_LIBNODE_ROOT=/libnode-root \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ >/dev/null || exit 1
cmake --build /build/host >/dev/null || exit 1
cmake -S /src/node -B /build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DENDSTONE_NODE_BUILD_HOST=OFF \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ >/dev/null 2>&1 || exit 1
cmake --build /build/plugin >/dev/null || exit 1

echo "=== staging ==="
mkdir -p "$SERVER/plugins/nodejs"
cp /build/plugin/plugin/endstone_nodejs.so   "$SERVER/plugins/"
cp /build/host/host/libendstone_node_host.so "$SERVER/plugins/nodejs/"
cp /libnode/linux/libnode.so.147             "$SERVER/plugins/nodejs/"
# Symlinked, not copied: a copy goes stale the moment you edit the repo, so /jsreload would
# faithfully reload the old code and report success. Through the link the served file IS the repo
# file, so an edit plus /jsreload takes effect immediately. Reload shadow copies are written to
# $SERVER/plugins, which stays writable even though /src is mounted read-only.
#
# A plugin that declares npm dependencies is skipped unless its node_modules is already there, since
# /src is read-only and npm cannot install into it.
for example in /src/node/examples/*/; do
    name=$(basename "$example")
    if grep -q '"dependencies"' "$example/package.json" 2>/dev/null && [ ! -d "$example/node_modules" ]; then
        echo "  skipping $name (declares dependencies, no node_modules)"
        continue
    fi
    rm -rf "$SERVER/plugins/$name"
    ln -sfn "/src/node/examples/$name" "$SERVER/plugins/$name"
    echo "  linked $name"
done

echo "=== server.properties ==="
PROPS="$SERVER/server.properties"
if [ -f "$PROPS" ]; then
    # Allow-list off so you can just join; keep online-mode on so real accounts authenticate.
    sed -i 's/^allow-list=.*/allow-list=false/'                   "$PROPS"
    sed -i 's/^server-name=.*/server-name=Endstone Node API Test/' "$PROPS"
    grep -qE '^allow-list=' "$PROPS" || echo 'allow-list=false' >> "$PROPS"
    grep -E '^(allow-list|online-mode|server-port|server-portv6|server-name|level-name)=' "$PROPS" | sed 's/^/  /'
fi

echo
echo "==============================================================="
echo " Connect a Bedrock client to this machine on UDP port 19132."
echo " Built-in command:  /jsreload [plugin]   (operators only)"
echo " $SERVER is bind-mounted from the host - edit plugins, properties"
echo " and permissions.json directly, then /jsreload."
echo " Ctrl+C here (or docker stop) to shut the server down."
echo "==============================================================="
echo

exec /opt/venv/bin/endstone -s "$SERVER" -y --no-interactive
