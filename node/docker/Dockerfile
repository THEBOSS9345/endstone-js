# syntax=docker/dockerfile:1

# Build-and-serve image for the Node.js scripting layer.
#
# Everything slow is baked into a cached layer - both toolchains, libnode and an Endstone runtime - and
# the source tree is bind-mounted rather than copied. A rebuild therefore recompiles only what changed
# under node/, and none of the setup is repeated.
#
# Layer order is deliberate: libnode is a 100 MB download, so it sits *below* the toolchain. Adding a
# compiler package invalidates the toolchain layer and everything after it, but not the download.
#
# Trixie is used because it ships clang 19 and libc++ in the base repositories, so no apt.llvm.org
# round-trip is needed; Endstone's hard minimum is LLVM 18.

FROM debian:trixie-slim

ARG LLVM_VERSION=19
ARG ENDSTONE_VERSION=0.11.9

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PIP_NO_CACHE_DIR=1

# --- 1. just enough to fetch libnode + npm for plugin dependencies --------------------------------
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends ca-certificates python3 libpython3.13 npm \
    && rm -rf /var/lib/apt/lists/*

# --- 2. libnode, checksum-pinned and baked in ----------------------------------------------------
# Only this script's own contents invalidate the layer, so editing anything else in the repo - or
# adding a package below - never re-downloads 100 MB.
COPY fetch_libnode.py /tmp/fetch_libnode.py
RUN python3 /tmp/fetch_libnode.py --platform linux --dest /libnode --cache /tmp/libnode-cache \
    && rm -rf /tmp/libnode-cache /tmp/fetch_libnode.py
ENV ENDSTONE_NODE_LIBNODE_ROOT=/libnode

# --- 3. the toolchains ----------------------------------------------------------------------------
# Two of them, because on Linux the two halves cannot share a C++ runtime: the Endstone plugin needs
# clang + libc++ to stay ABI-compatible with BDS, the Node host needs gcc + libstdc++ to match libnode.
# git is for the FetchContent that pulls expected-lite; libcurl is what the Bedrock server binary
# itself links against at runtime.
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
        build-essential \
        clang-${LLVM_VERSION} \
        cmake \
        git \
        libc++-${LLVM_VERSION}-dev \
        libc++abi-${LLVM_VERSION}-dev \
        libcurl4t64 \
        ninja-build \
        python3-venv \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${LLVM_VERSION} 100 \
    && rm -rf /var/lib/apt/lists/*

# --- 4. the Endstone runtime that loads the plugin ------------------------------------------------
# Pinned to the version this tree is tagged at, so the plugin's API version matches what the server
# reports and a JS plugin's declared apiVersion is accepted.
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --upgrade pip \
    && /opt/venv/bin/pip install "endstone==${ENDSTONE_VERSION}"
ENV PATH=/opt/venv/bin:$PATH

# /src is the repo (read-only), /build is a volume so ninja stays incremental across runs, /server
# holds the world, plugins and configuration.
WORKDIR /src
ENTRYPOINT ["bash", "/src/node/scripts/docker-serve.sh"]
