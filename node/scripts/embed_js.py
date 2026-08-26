#!/usr/bin/env python3
"""Embeds a JavaScript file into a C++ header as a string literal.

The runtime's bootstrap is JavaScript, but the host has no runtime asset to locate - it is compiled in.
Keeping it as a real .js file rather than a raw string literal inside host.cpp is what makes it
lintable, formattable, syntax-checkable and diffable; this script is the bridge between the two.

Run by CMake on every build, so editing bootstrap.js is enough.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from shutil import which

# Chosen so it cannot appear in JavaScript by accident. Verified rather than assumed.
DELIMITER = "ESNJS"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="the .js file to embed")
    parser.add_argument("--out", type=Path, required=True, help="the .h file to write")
    parser.add_argument("--symbol", default="kBootstrapSource", help="the C++ variable to define")
    parser.add_argument("--check", action="store_true", help="reject a syntax error, if node is on PATH")
    args = parser.parse_args()

    js = args.source.read_bytes().decode("utf-8").replace("\r\n", "\n")

    # A raw string literal ends at its delimiter, so this is the one thing that must not appear.
    if f'){DELIMITER}"' in js:
        print(f"{args.source}: contains the raw-string delimiter ){DELIMITER}\" - rename it", file=sys.stderr)
        return 1

    # The embedded source is read back as a const char*, so a NUL byte truncates everything after
    # it - silently, and only at runtime. node --check accepts one inside a string literal, so the
    # build is the only place this can be caught. Build the character instead, as bootstrap.js does
    # for its other separators: String.fromCharCode(0).
    nul = chr(0)
    if nul in js:
        line = js.count(chr(10), 0, js.index(nul)) + 1
        print(f"{args.source}:{line}: contains a NUL byte, which would truncate the embedded "
              f"source. Use String.fromCharCode(0) instead.", file=sys.stderr)
        return 1

    # A syntax error would otherwise surface as a runtime failure at server start-up, long after the
    # build said everything was fine. Skipped silently when node is unavailable: this must not be the
    # thing that stops someone building.
    if args.check and which("node"):
        result = subprocess.run(["node", "--check", str(args.source)], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"{args.source}: not valid JavaScript\n{result.stderr}", file=sys.stderr)
            return 1

    # MSVC limits a single string literal to 65535 bytes. Split conservatively at line boundaries
    # near 60 000 characters so that no token is broken across parts (an embedded \n between
    # parts would turn a mid-token split into a syntax error at runtime).
    MAX_CHARS = 60_000
    total_bytes = len(js.encode("utf-8"))
    if total_bytes <= 65535:
        parts = [js]
    else:
        parts = []
        i = 0
        while i < len(js):
            end = min(i + MAX_CHARS, len(js))
            if end < len(js):
                nl = js.rfind("\n", i, end)
                if nl > i:
                    end = nl + 1
            parts.append(js[i:end])
            i = end

    concat_parts = " ".join(f'R\"{DELIMITER}(\n{p}){DELIMITER}\"' for p in parts)
    header = (
        f"// Generated from {args.source.name} by scripts/embed_js.py. Do not edit; edit the .js instead.\n"
        "#pragma once\n"
        "\n"
        f"constexpr const char *{args.symbol} =\n    {concat_parts};\n"
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Only rewrite on a real change, so an unchanged bootstrap does not force host.cpp to recompile.
    if not args.out.exists() or args.out.read_text(encoding="utf-8") != header:
        args.out.write_text(header, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
