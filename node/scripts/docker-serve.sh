#!/usr/bin/env bash
# Compiles both halves, stages them, and runs the server in the foreground so a real Minecraft client
# can join.
#
# The toolchain, libnode and Endstone all come baked into the image, so this script only ever builds
# what is under node/. /build is a volume, so ninja is incremental: the second run recompiles nothing
# it does not have to.
set -uo pipefail

SERVER=${SERVER_DIR:-/server}
LIBNODE=${ENDSTONE_NODE_LIBNODE_ROOT:-/libnode}
LINK_EXAMPLES=${LINK_EXAMPLES:-1}

echo "=== building (incremental) ==="
# Configure only when the build tree is not there yet; otherwise go straight to the compile.
if [ ! -f /build/host/build.ninja ]; then
    cmake -S /src/node -B /build/host -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DENDSTONE_NODE_BUILD_PLUGIN=OFF -DENDSTONE_NODE_LIBNODE_ROOT="$LIBNODE" \
        -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ >/dev/null || exit 1
fi
cmake --build /build/host || exit 1

if [ ! -f /build/plugin/build.ninja ]; then
    cmake -S /src/node -B /build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DENDSTONE_NODE_BUILD_HOST=OFF \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ >/dev/null || exit 1
fi
cmake --build /build/plugin || exit 1

echo "=== staging ==="
mkdir -p "$SERVER/plugins/nodejs"
cp /build/plugin/plugin/endstone_nodejs.so   "$SERVER/plugins/"
cp /build/host/host/libendstone_node_host.so "$SERVER/plugins/nodejs/"
cp "$LIBNODE"/lib/libnode.so.*               "$SERVER/plugins/nodejs/"
# The decoder reads this at runtime to know each packet's field layout.
if [ -f /src/node/protocol/protocol.json ]; then
    cp /src/node/protocol/protocol.json      "$SERVER/plugins/nodejs/"
fi

# Symlinked, not copied: a copy goes stale the moment you edit the repo, so /jsreload would faithfully
# reload the old code and report success. Through the link the served file IS the repo file, so an edit
# plus /jsreload takes effect immediately. Set LINK_EXAMPLES=0 to serve only your own plugins.
if [ "$LINK_EXAMPLES" = "1" ]; then
    for example in /src/node/examples/*/; do
        name=$(basename "$example")
        # Install dependencies if package.json has them and node_modules is missing.
        # Uses a shared cache across plugins to save disk and download time.
        if grep -q '"dependencies"' "$example/package.json" 2>/dev/null && [ ! -d "$example/node_modules" ]; then
            echo "  installing deps for $name..."
            npm install --prefix "$example" --cache /tmp/npm-cache --prefer-offline 2>&1 | sed 's/^/    /'
        fi
        rm -rf "$SERVER/plugins/$name"
        ln -sfn "/src/node/examples/$name" "$SERVER/plugins/$name"
        echo "  linked $name"
    done
fi

# Also install deps for any user plugins in /server/plugins that were not symlinked.
for plugin in "$SERVER"/plugins/*/; do
    [ -d "$plugin" ] || continue
    name=$(basename "$plugin")
    # Skip if already symlinked (handled above) or no package.json
    [ -L "$SERVER/plugins/$name" ] && continue
    [ -f "$plugin/package.json" ] || continue
    if grep -q '"dependencies"' "$plugin/package.json" 2>/dev/null && [ ! -d "$plugin/node_modules" ]; then
        echo "  installing deps for user plugin $name..."
        npm install --prefix "$plugin" --cache /tmp/npm-cache --prefer-offline 2>&1 | sed 's/^/    /'
    fi
done

echo "=== server.properties ==="
PROPS="$SERVER/server.properties"
# Written before the first start too, so the allow-list is never on even briefly: Endstone keeps any
# entries that already exist and only fills in the ones it is missing.
mkdir -p "$SERVER"
touch "$PROPS"
if grep -qE '^allow-list=' "$PROPS"; then
    sed -i 's/^allow-list=.*/allow-list=false/' "$PROPS"
else
    echo 'allow-list=false' >> "$PROPS"
fi
grep -E '^(allow-list|online-mode|server-port|server-portv6|level-name)=' "$PROPS" | sed 's/^/  /'

echo
echo "==============================================================="
echo " Connect a Bedrock client on UDP 19132."
echo " Built-in command:  /jsreload [plugin]   (operators only)"
echo " Drop your plugins into $SERVER/plugins - a folder with a"
echo " package.json, or a single .js file - then /jsreload."
echo "==============================================================="
echo

exec endstone -s "$SERVER" -y --no-interactive
