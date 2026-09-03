#!/usr/bin/env python3
"""Checks that every member the bridge binds is declared in the TypeScript definitions.

A member lives in two places once the runtime builds its own tables: the descriptor in plugin/types/,
and the interface in @endstone-js/server. Nothing enforced the second, so a member could be bound and
work at runtime while being invisible to anyone writing a plugin - which is how Block went without
x, y and z for as long as it did.

This compares the two by name. It deliberately does not check types: the .d.ts carries documentation
and precise unions that a ValueKind cannot express, so it stays hand-written. What it cannot do is
silently omit something.

Naming: the bridge speaks in accessor names, several of which the runtime renames on the way through -
a `fooList` crosses newline-joined and reaches JavaScript as `foo`. RENAMED records those.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Accessor name -> the name a plugin actually writes. The runtime does the translation in wrap().
RENAMED = {
    "blockStatesList": "blockStates",
    "loreList": "lore",
    "pageList": "pages",
    "enchantList": "enchants",
    "tagKeyList": "tagKeys",
    "scoreboardTagList": "scoreboardTags",
    "objectiveList": "objectives",
    "recipientNameList": "recipientNames",
    "loadedChunkList": "loadedChunks",
}

# Bound, but deliberately not part of the public surface: backends for a property the runtime exposes
# under another shape, or plumbing a plugin never names.
INTERNAL = {
    "setScoreboard",  # backend for the `scoreboard` property
    "setRotation",  # backend for the `rotation` property
    "setHeldItemSlot",  # backend for the `heldItemSlot` property
    "setHelmet",
    "setChestplate",
    "setLeggings",
    "setBoots",
    "setItemInMainHand",
    "setItemInOffHand",
    # The runtime composes these four into the `skin` object rather than exposing them singly.
    "skinWidth",
    "skinHeight",
    "skinCapeWidth",
    "skinCapeHeight",
}

# The descriptor's type name -> the interfaces that between them declare it. More than one where the
# definitions split what the bridge keeps as one type: a sender is a union of three, and the
# definitions fold Mob into Actor because "only living actors have health" reads better than a second
# interface nobody writes down.
INTERFACE = {
    "CommandSender": ["ConsoleSender", "PlayerSender", "BlockSender"],
    "Mob": ["Actor"],
    # `Plugin` in the definitions is the base class a JS plugin writes; what an event hands over is
    # another plugin's metadata, which is PluginInfo.
    "Plugin": ["PluginInfo"],
}

TYPE_BLOCK = re.compile("^ESN_(?:SUB)?TYPE[(]([A-Za-z0-9_]+),[ ]*[A-Za-z0-9_]+", re.M)
MEMBER = re.compile('b[.](?:ro|rw|handle|method)[(]"([A-Za-z_][A-Za-z0-9_]*)"')


def bound_members(root: Path) -> dict[str, set[str]]:
    """What each bound type exposes, keyed by the C++ type name its ESN_TYPE block names."""
    found: dict[str, set[str]] = {}
    for source in sorted(root.rglob("*.cpp")):
        text = source.read_text(encoding="utf-8")
        marks = [(m.start(), m.group(1)) for m in TYPE_BLOCK.finditer(text)]
        marks.append((len(text), ""))
        for (start, name), (end, _) in zip(marks, marks[1:]):
            if not name:
                continue
            names = {RENAMED.get(n, n) for n in MEMBER.findall(text[start:end])}
            found.setdefault(name, set()).update(names - INTERNAL)
    return found


def interface_bases(definitions: str) -> dict[str, list[str]]:
    """What each interface extends."""
    out: dict[str, list[str]] = {}
    for match in re.finditer(r"^export interface (\w+)\s+extends\s+([^{]+)\{", definitions, re.M):
        out[match.group(1)] = [name.strip() for name in match.group(2).split(",") if name.strip()]
    return out


def declared_members(definitions: str) -> dict[str, set[str]]:
    """Every member of every exported interface, by interface name."""
    out: dict[str, set[str]] = {}
    for match in re.finditer(r"^export interface (\w+)[^{]*\{", definitions, re.M):
        start = match.end()
        depth = 1
        index = start
        while index < len(definitions) and depth:
            if definitions[index] == "{":
                depth += 1
            elif definitions[index] == "}":
                depth -= 1
            index += 1
        body = definitions[start : index - 1]
        # A member is `name:`, `readonly name:`, or `name(`. Nested object literals are members too,
        # which is why this reads the whole brace-matched body rather than line by line.
        names = set(re.findall(r"^\s*(?:readonly\s+)?([A-Za-z_]\w*)\s*[?]?\s*[:(<]", body, re.M))
        out.setdefault(match.group(1), set()).update(names)
    return out


def with_bases(name: str, declared: dict[str, set[str]], bases: dict[str, list[str]]) -> set[str]:
    """Everything an interface offers, following `extends` - the definitions use it where the bridge
    uses a base chain, so Location gets x, y and z from Vector3."""
    seen: set[str] = set()
    members: set[str] = set()
    pending = [name]
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        members |= declared.get(current, set())
        pending.extend(bases.get(current, []))
    return members


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--types", type=Path, required=True, help="plugin/types")
    parser.add_argument("--definitions", type=Path, required=True, help="index.d.ts")
    args = parser.parse_args()

    if not args.definitions.exists():
        # The definitions live in a sibling checkout, which not every build has.
        print(f"{args.definitions} not found - skipping the declaration check")
        return 0

    text = args.definitions.read_text(encoding="utf-8")
    bound = bound_members(args.types)
    declared = declared_members(text)
    bases = interface_bases(text)

    problems: list[str] = []
    for type_name, members in sorted(bound.items()):
        candidates = INTERFACE.get(type_name, [type_name])
        available: set[str] = set()
        for candidate in candidates:
            available |= with_bases(candidate, declared, bases)
        if not available:
            problems.append(f"  {type_name}: no matching interface in the definitions ({candidates})")
            continue
        missing = members - available
        if missing:
            problems.append(f"  {type_name}: bound but not declared: {sorted(missing)}")

    if problems:
        print(
            "the bridge binds members the TypeScript definitions do not declare:\n"
            + "\n".join(problems)
            + "\n\nDeclare them in endstone-server-types/index.d.ts, or - if the runtime exposes one"
            "\nunder another name - record that in RENAMED or INTERNAL in this script.",
            file=sys.stderr,
        )
        return 1

    total = sum(len(v) for v in bound.values())
    print(f"declarations agree: {total} members across {len(bound)} types")
    return 0


if __name__ == "__main__":
    sys.exit(main())
