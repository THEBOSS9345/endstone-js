#!/usr/bin/env python3
"""Checks that the runtime's METHODS_BY_TYPE matches the methods ApiBridge::invoke actually implements.

The two halves have to agree: a method in the bridge but not in the table is uncallable from
JavaScript, and one in the table but not in the bridge is a function that throws when called. Neither
shows up until someone hits it at runtime, so this compares them at build time instead.

The parse relies on invoke() keeping its conventional shape - one `resolve(target, Kind::X)` branch per
kind, dispatching on `name == "..."`. If a branch is written differently this reports a divergence; fix
the branch or teach this script about it, but do not just delete the check.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Methods the bridge implements in a helper rather than inside an `if (resolve(...))` branch, and the
# type each helper serves.
HELPERS = {
    "actorInvoke": "Actor",
    "bossBarInvoke": "BossBar",
    "scoreboardInvoke": "Scoreboard",
    # Permissible sits under CommandSender, which every Actor and Player derives from, so the parent
    # chain carries these the rest of the way.
    "permissibleInvoke": "CommandSender",
}

# Names the runtime handles itself and never forwards under that name, so the bridge has no case for
# them. Keeping them listed in the table is still right - it documents what the type answers to.
RUNTIME_ONLY = {"createBossBar"}

BRANCH = re.compile(
    r"^    if \(auto \*\w+ = (?:static_cast<\w+ \*>\()?resolve(Inventory|Actor|Mob)?\(target"
    r"(?:, Kind::(\w+))?\)",
    re.M,
)
NAME = re.compile(r'name == "([A-Za-z_]\w*)"')

# A migrated type declares its members in plugin/types/, one file per Endstone header folder. Methods
# there are `b.method("name", ...)` inside an ESN_TYPE block naming the kind they belong to.
TYPE_BLOCK = re.compile(r"^ESN_TYPE\((\w+),\s*(\w+),\s*(\w+)\)", re.M)
TYPE_METHOD = re.compile('b[.]method[(]"([A-Za-z_][A-Za-z0-9_]*)"')


def descriptor_methods(root: Path) -> dict[str, set[str]]:
    """Methods declared by the descriptor tables, keyed by the kind their ESN_TYPE block names."""
    found: dict[str, set[str]] = {}
    for source in sorted(root.rglob("*.cpp")):
        text = source.read_text(encoding="utf-8")
        marks = [(m.start(), m.group(2)) for m in TYPE_BLOCK.finditer(text)]
        marks.append((len(text), ""))
        for (start, kind), (end, _) in zip(marks, marks[1:]):
            if kind:
                found.setdefault(kind, set()).update(TYPE_METHOD.findall(text[start:end]))
    return {k: v for k, v in found.items() if v}


def enclosing_block(text: str, position: int) -> str | None:
    """The body of the braced block that opens just after `position`."""
    start = text.find("{", position)
    if start < 0:
        return None
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : i]
    return None


def flatten(per_type: dict[str, set[str]], parents: dict[str, str]) -> dict[str, set[str]]:
    """Resolves each type against its parent chain, the way the runtime does at lookup time."""
    out: dict[str, set[str]] = {}
    for type_name in set(per_type) | set(parents):
        names: set[str] = set()
        current: str | None = type_name
        while current:
            names |= per_type.get(current, set())
            current = parents.get(current)
        if names:
            out[type_name] = names
    return out


def bridge_methods(source: str) -> dict[str, set[str]]:
    body = source[source.index("esn_status ApiBridge::invoke") :]
    body = body[: body.index("\nesn_status ApiBridge::typeName")]

    marks: list[tuple[int, str]] = []
    for match in BRANCH.finditer(body):
        kind = match.group(2) or {"Inventory": "Inventory", "Actor": "Actor", "Mob": "Mob"}[match.group(1)]
        marks.append((match.start(), kind))
    marks.append((len(body), ""))

    found: dict[str, set[str]] = {}
    for (start, kind), (end, _) in zip(marks, marks[1:]):
        found.setdefault(kind, set()).update(NAME.findall(body[start:end]))

    # Some inventory methods need a PlayerInventory and sit in blocks guarded on that kind, nested
    # inside the Inventory branch. Brace-matched rather than pattern-matched: the nesting is what
    # decides which type owns the method, so getting it approximately right is not good enough.
    for guard in re.finditer(r"resolve\(target, Kind::PlayerInventory\)", body):
        block = enclosing_block(body, guard.end())
        if block is None:
            continue
        names = set(NAME.findall(block))
        found.setdefault("PlayerInventory", set()).update(names)
        found.setdefault("Inventory", set()).difference_update(names)

    for helper, kind in HELPERS.items():
        chunk = source[source.index(f"esn_status {helper}") if f"esn_status {helper}" in source
                       else source.index(f"ApiBridge::{helper}") :]
        end = chunk.index("\n    return ESN_ERR_NO_SUCH_MEMBER;\n}")
        found.setdefault(kind, set()).update(NAME.findall(chunk[:end]))

    return {k: v for k, v in found.items() if v}


def runtime_methods(source: str) -> tuple[dict[str, set[str]], dict[str, str]]:
    table = source[source.index("const METHODS_BY_TYPE = {") : source.index("const TYPE_PARENT = {")]
    out: dict[str, set[str]] = {}
    for entry in re.finditer(r"^  (\w+): \[(.*?)\],$", table, re.M | re.S):
        out[entry.group(1)] = set(re.findall(r"'([A-Za-z_]\w*)'", entry.group(2)))

    chain = source[source.index("const TYPE_PARENT = {") :]
    chain = chain[: chain.index("};")]
    parents = dict(re.findall(r"(\w+): '(\w+)'", chain))
    return out, parents


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--types", type=Path, help="plugin/types, the descriptor tables")
    args = parser.parse_args()

    raw_bridge = bridge_methods(args.bridge.read_text(encoding="utf-8"))
    # A type part-way through migration has methods on both sides, so the two are merged rather than
    # compared: what matters to the runtime is that the method exists somewhere in the plugin.
    if args.types and args.types.is_dir():
        for kind, names in descriptor_methods(args.types).items():
            raw_bridge.setdefault(kind, set()).update(names)
    raw_runtime, parents = runtime_methods(args.runtime.read_text(encoding="utf-8"))

    # Compared after resolving the parent chain, because that is what each side actually offers: the
    # runtime walks TYPE_PARENT at lookup time, and Player's branch in the bridge falls through to
    # actorInvoke. Comparing the raw per-type lists would flag every inherited method.
    bridge = flatten(raw_bridge, parents)
    runtime = flatten(raw_runtime, parents)

    problems: list[str] = []
    for kind in sorted(set(bridge) | set(runtime)):
        missing = bridge.get(kind, set()) - runtime.get(kind, set())
        extra = runtime.get(kind, set()) - bridge.get(kind, set()) - RUNTIME_ONLY
        if missing:
            problems.append(f"  {kind}: in the bridge but not the table (uncallable): {sorted(missing)}")
        if extra:
            problems.append(f"  {kind}: in the table but not the bridge (throws when called): {sorted(extra)}")

    if problems:
        print(
            f"{args.runtime.name} and {args.bridge.name} disagree about which methods each type has:\n"
            + "\n".join(problems)
            + "\n\nAdd the method to both, or - if the runtime handles it itself under a different"
            "\nname - add it to RUNTIME_ONLY in this script.",
            file=sys.stderr,
        )
        return 1

    total = sum(len(v) for v in raw_bridge.values())
    print(f"methods agree: {total} across {len(raw_bridge)} types")
    return 0


if __name__ == "__main__":
    sys.exit(main())
