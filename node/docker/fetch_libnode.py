#!/usr/bin/env python3
"""Fetch a pinned, checksum-verified libnode and lay it out for node/CMakeLists.txt.

The fast path for contributors. Building libnode from source takes hours; the Conan recipe in
recipes/libnode is the source of truth, and this fetches a known-good prebuilt of the same version so
a fresh checkout can build and run the Node host immediately.

    python node/scripts/fetch_libnode.py
    cmake -S node -B build/node -DENDSTONE_NODE_LIBNODE_ROOT=build/libnode ...

Prebuilts come from metacall/libnode, which builds Linux with gcc/libstdc++ and Windows with
`vcbuild dll` (/MD) - exactly the toolchains the ABI firewall assumes. Every archive is pinned by
SHA256; a mismatch is a hard failure.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

# Pinned to the version proven by the Milestone 0/1 spike. NODE_MODULE_VERSION 147.
# Bump these together with recipes/libnode/config.yml, never independently.
DEFAULT_VERSION = "26.3.0"
BASE_URL = "https://github.com/metacall/libnode/releases/download/v{version}"

ARTIFACTS: dict[str, dict[str, dict[str, str]]] = {
    "26.3.0": {
        "headers": {
            "file": "libnode-headers.zip",
            "sha256": "f1c4d294a32016b0fe3b2c25ea9018e0ad09c8ab59ebd1f5a03d702bc0237b46",
        },
        "win32": {
            "file": "libnode-amd64-windows.zip",
            "sha256": "964ed95855d147659b00fd67a1b6299d2d39142047f0e2902906cc22aef37e9c",
        },
        "linux": {
            "file": "libnode-amd64-linux.tar.xz",
            "sha256": "341ae950bb35726fd6d47f56ff10b58365380f23776048d3c1c861e65ed869a5",
        },
    }
}


def download(url: str, dest: Path, expected_sha256: str) -> Path:
    if dest.exists() and sha256(dest) == expected_sha256:
        print(f"  cached   {dest.name}")
        return dest

    print(f"  fetching {url}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(url) as response, tmp.open("wb") as out:
        shutil.copyfileobj(response, out)

    actual = sha256(tmp)
    if actual != expected_sha256:
        tmp.unlink(missing_ok=True)
        raise SystemExit(f"checksum mismatch for {dest.name}\n  expected {expected_sha256}\n  actual   {actual}")
    tmp.replace(dest)
    print(f"  verified {dest.name}")
    return dest


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract(archive: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    if archive.suffixes[-2:] == [".tar", ".xz"] or archive.suffix == ".xz":
        with tarfile.open(archive, "r:xz") as tar:
            tar.extractall(dest, filter="data")
    elif archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(dest)
    else:
        raise SystemExit(f"unsupported archive: {archive.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", default=DEFAULT_VERSION, help=f"libnode version (default: {DEFAULT_VERSION})")
    parser.add_argument("--dest", default="build/libnode", help="output root (default: build/libnode)")
    parser.add_argument("--cache", default="build/libnode-cache", help="download cache directory")
    # Cross-fetching is normal here: a Windows host building for the Linux container needs the .so.
    parser.add_argument("--platform", choices=("win32", "linux"), help="target platform (default: this host)")
    args = parser.parse_args()

    if args.version not in ARTIFACTS:
        raise SystemExit(f"no pinned artifacts for {args.version}; known: {', '.join(ARTIFACTS)}")

    platform = args.platform
    if platform is None:
        platform = "win32" if sys.platform.startswith("win") else "linux" if sys.platform.startswith("linux") else None
    if platform is None:
        raise SystemExit(f"{sys.platform} is not supported; pass --platform")

    pins = ARTIFACTS[args.version]
    base = BASE_URL.format(version=args.version)
    cache = Path(args.cache)
    root = Path(args.dest)
    staging = root / ".staging"

    print(f"libnode {args.version} for {platform}")
    headers = download(f"{base}/{pins['headers']['file']}", cache / pins["headers"]["file"], pins["headers"]["sha256"])
    binaries = download(f"{base}/{pins[platform]['file']}", cache / pins[platform]["file"], pins[platform]["sha256"])

    if staging.exists():
        shutil.rmtree(staging)
    extract(headers, staging / "headers")
    extract(binaries, staging / "binaries")

    # Normalize to the layout node/CMakeLists.txt searches: include/node, lib/, bin/.
    include_src = next(iter(staging.glob("headers/**/node/node.h"))).parent.parent
    include_dst = root / "include"
    if include_dst.exists():
        shutil.rmtree(include_dst)
    shutil.copytree(include_src, include_dst)

    lib = root / "lib"
    bin_dir = root / "bin"
    for directory in (lib, bin_dir):
        directory.mkdir(parents=True, exist_ok=True)

    staged = 0
    for path in staging.glob("binaries/**/*"):
        if not path.is_file():
            continue
        name = path.name
        if name.endswith(".lib") or ".so" in name:
            shutil.copy2(path, lib / name)
            staged += 1
        elif name.endswith(".dll"):
            shutil.copy2(path, bin_dir / name)
            staged += 1
    if staged == 0:
        raise SystemExit("no libnode binaries found in the archive")

    shutil.rmtree(staging)
    print(f"\nlibnode ready: {root.resolve()}")
    print(f"  headers : {include_dst / 'node'}")
    for path in sorted(lib.iterdir()) + sorted(bin_dir.iterdir()):
        print(f"  {path.relative_to(root)}  ({path.stat().st_size // (1 << 20)} MB)")
    print(f"\nconfigure with: -DENDSTONE_NODE_LIBNODE_ROOT={root.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
