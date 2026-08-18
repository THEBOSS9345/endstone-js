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

    # A syntax error would otherwise surface as a runtime failure at server start-up, long after the
    # build said everything was fine. Skipped silently when node is unavailable: this must not be the
    # thing that stops someone building.
    if args.check and which("node"):
        result = subprocess.run(["node", "--check", str(args.source)], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"{args.source}: not valid JavaScript\n{result.stderr}", file=sys.stderr)
            return 1

    # MSVC limits a single string literal to 65535 bytes. Split conservatively at 60 000
    # characters (always ≤ 60 000 bytes for ASCII / Latin-1, well under the limit).
    MAX_CHARS = 60_000
    total_bytes = len(js.encode("utf-8"))
    if total_bytes <= 65535:
        parts = [js]
    else:
        parts = [js[i : i + MAX_CHARS] for i in range(0, len(js), MAX_CHARS)]

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
