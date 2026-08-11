#!/usr/bin/env python3
"""Generate the TypeScript packet definitions from EndstoneMC/protocol-docs.

The schemas are extracted from BDS itself by EndstoneMC's protocol-dumper, so they are the accurate
description of the wire format for one BDS release - far better than hand-written definitions, and the
only realistic way to cover 229 packets. This script turns them into:

    packet.d.ts        the PacketId enum, one interface per packet, and the enum types they reference
    protocol.json      the same field lists in a compact form, for a schema-driven decoder at runtime

Run it against a checkout or let it fetch:

    python node/scripts/generate_protocol.py --ref r26_u4 --out ../endstone-server-types

The version the schemas describe is written into the generated header, because a payload decoded with
the wrong version's schema is silently wrong rather than obviously broken.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO = "EndstoneMC/protocol-docs"

# Wire types, as they appear in the schemas, mapped to what JavaScript actually receives.
# Sizes over 32 bits become bigint: a JavaScript number cannot hold them exactly, and silently
# rounding an entity id or a block position is worse than making the caller deal with a bigint.
WIRE_TO_TS = {
    "bool": "boolean",
    "int8": "number", "uint8": "number",
    "int16": "number", "uint16": "number",
    "int32": "number", "uint32": "number", "int32_be": "number",
    "int64": "bigint", "uint64": "bigint",
    "float": "number", "double": "number", "float32": "number", "float64": "number",
    "varint32": "number", "uvarint32": "number",
    "varint64": "bigint", "uvarint64": "bigint",
    "zigzag32": "number", "zigzag64": "bigint",
    "string": "string",
    "ActorRuntimeID": "bigint", "ActorUniqueID": "bigint",
    "Vec3": "Vector3", "Vec2": "Vector2", "BlockPos": "Vector3", "NetworkBlockPosition": "Vector3",
    "UUID": "string", "mce::UUID": "string",
    # NBT is a tag tree rather than an opaque blob, so it gets a named recursive type. The decoder does
    # not read these yet, but the shape is worth stating.
    "CompoundTag": "NbtCompound", "NBT": "NbtCompound",
}

# Emitted verbatim: the recursive NBT shape, and nothing else needs hand-written declarations.
NBT_PREAMBLE = """/** A value in an NBT tag tree. */
export type NbtValue = number | bigint | string | boolean | NbtValue[] | NbtCompound;

/** An NBT compound: named tags, nested arbitrarily. */
export interface NbtCompound {
    [key: string]: NbtValue;
}
"""

# Templated net ids and cereal wrappers carry no useful shape of their own; they are named per
# instantiation, so matching them by prefix keeps the generator from inventing 40 one-off interfaces.
OPAQUE_PREFIXES = ("TypedServerNetId<", "TypedClientNetId<", "cerealizer<", "cereal::", "brstd::")


def camel(name: str) -> str:
    """"Sender's XUID" -> "senderXuid". Schema field names are prose, so they need normalising."""
    # Drop possessive apostrophes first, or "Sender's" splits into "Sender" + "S".
    name = re.sub(r"'s(?![A-Za-z0-9])", "", name, flags=re.IGNORECASE)
    words = re.findall(r"[A-Za-z0-9]+", name)
    if not words:
        return "field"
    head, *rest = words
    out = head.lower() + "".join(w[:1].upper() + w[1:].lower() for w in rest)
    return out if out[0].isalpha() else "f" + out


def pascal(name: str) -> str:
    return "".join(w[:1].upper() + w[1:] for w in re.findall(r"[A-Za-z0-9]+", name))


def fetch_schemas(ref: str) -> dict[str, dict[str, dict]]:
    """Every schema in the repo, from one tarball request rather than 700 file requests."""
    import io
    import tarfile

    archive = subprocess.run(["gh", "api", f"repos/{REPO}/tarball/{ref}"],
                             capture_output=True, check=True).stdout
    out: dict[str, dict[str, dict]] = {"packets": {}, "types": {}, "enums": {}}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        for member in tar.getmembers():
            if not member.isfile() or not member.name.endswith(".json"):
                continue
            parts = Path(member.name).parts
            # <repo>-<sha>/<directory>/<file>.json
            if len(parts) < 3 or parts[1] not in out:
                continue
            handle = tar.extractfile(member)
            if handle is None:
                continue
            out[parts[1]][Path(member.name).stem] = json.load(handle)
    return out


def flat_key(name: str) -> str:
    """`Foo::Bar` is stored as `Foo__Bar`, which is also how it is named in the output."""
    return name.replace("::", "__")


def ts_type(field_type, known: set[str]) -> str:
    """
    One field's TypeScript type, given its schema `type` (a string, or a switch object).

    Anything that cannot be resolved becomes `unknown` rather than a name that does not exist, so the
    generated file always compiles - a field you have to narrow yourself is far better than a build
    that will not typecheck at all.
    """
    if isinstance(field_type, dict):
        # A map: keyed lookup, so a Record says it better than an opaque blob.
        if "key" in field_type and "value" in field_type:
            key = ts_type(field_type["key"], known)
            value = ts_type(field_type["value"], known)
            index = key if key in ("number", "string") else "string"
            return f"Record<{index}, {value}>"
        # A repeat nested inside the type rather than beside it: still just an array.
        if "repeat" in field_type and "type" in field_type:
            return f"{ts_type(field_type['type'], known)}[]"
        # A tagged union: the discriminant selects which case the body is. A null case carries no
        # payload, so it contributes nothing to the union rather than a bogus type.
        cases = [c for c in field_type.get("cases", []) if isinstance(c, str)]
        named = sorted({pascal(flat_key(c)) for c in cases if flat_key(c) in known})
        return " | ".join(named) if named else "unknown"
    if not isinstance(field_type, str):
        return "unknown"
    if field_type in WIRE_TO_TS:
        return WIRE_TO_TS[field_type]
    if field_type.startswith(OPAQUE_PREFIXES):
        return "unknown"
    key = flat_key(field_type)
    return pascal(key) if key in known else "unknown"


def field_lines(packet: dict, known: set[str]) -> list[str]:
    lines: list[str] = []
    for field in packet.get("fields", []):
        # A field with a fixed `value` and no name is a constant on the wire - it has to be written and
        # skipped when decoding, but it is not something a caller supplies, so it stays out of the type.
        if "name" not in field:
            continue
        name = camel(field["name"])
        declared = ts_type(field.get("type"), known)
        # A numeric field with an `enum` is really that enum, so name it rather than "number".
        if field.get("enum") and flat_key(field["enum"]) in known:
            declared = pascal(flat_key(field["enum"]))
        if field.get("repeat"):
            declared = f"{declared}[]"
        optional = "?" if field.get("optional") else ""
        notes = (field.get("description") or field.get("notes") or "").strip().replace("*/", "*!/")
        doc = notes or (f"`{field['name']}`" if field["name"] != name else "")
        if field.get("deprecated"):
            doc = (doc + " " if doc else "") + "@deprecated no longer used by the client."
        if doc:
            lines.append(f"    /** {doc} */")
        lines.append(f"    {name}{optional}: {declared};")
    return lines or ["    // no fields"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ref", default="r26_u4", help="branch or tag of protocol-docs (default: r26_u4)")
    parser.add_argument("--out", default="../endstone-server-types", help="where to write packet.d.ts")
    parser.add_argument("--vendor", default="node/protocol", help="where to keep the fetched schemas")
    args = parser.parse_args()

    vendor = Path(args.vendor)
    vendor.mkdir(parents=True, exist_ok=True)
    cache = vendor / f"{args.ref}.json"

    if cache.exists():
        print(f"using vendored schemas: {cache}")
        bundle = json.loads(cache.read_text(encoding="utf-8"))
    else:
        print(f"fetching schemas from {REPO}@{args.ref}")
        bundle = {"ref": args.ref, **fetch_schemas(args.ref)}
        cache.write_text(json.dumps(bundle, indent=1, sort_keys=True), encoding="utf-8")
        print(f"vendored to {cache}")

    packets, types, enums = bundle["packets"], bundle["types"], bundle["enums"]
    known = set(types) | set(enums)
    out = [
        "// GENERATED by node/scripts/generate_protocol.py - do not edit by hand.",
        f"// Schemas from https://github.com/{REPO} @ {bundle['ref']}, extracted from BDS by protocol-dumper.",
        "//",
        "// A payload decoded against the wrong release's schema is silently wrong, so regenerate this",
        "// whenever the server's BDS version changes.",
        "",
        'import type { Vector2, Vector3 } from "./index";',
        "",
        NBT_PREAMBLE,
        "/** Every Bedrock packet id, by name. */",
        "export const enum PacketId {",
    ]
    for name, packet in sorted(packets.items(), key=lambda kv: kv[1].get("id", 0)):
        if "id" in packet:
            out.append(f"    {pascal(name)} = {packet['id']},")
    out += ["}", ""]

    # Enums first: packets and composite types both reference them.
    for name, enum in sorted(enums.items()):
        values = enum.get("values") or []
        if not values:
            continue
        out.append(f"/** `{enum.get('name', name)}` */")
        out.append(f"export const enum {pascal(name)} {{")
        seen: set[str] = set()
        for entry in values:
            raw = str(entry.get("name", ""))
            label = raw if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", raw) else pascal(raw)
            if not label or label in seen or not isinstance(entry.get("value"), int):
                continue
            seen.add(label)
            out.append(f"    {label} = {entry['value']},")
        out += ["}", ""]

    # Then the composite types the packets are built from.
    for name, composite in sorted(types.items()):
        out.append(f"/** `{composite.get('name', name)}` */")
        out += [f"export interface {pascal(name)} {{", *field_lines(composite, known), "}", ""]

    for name, packet in sorted(packets.items()):
        notes = (packet.get("notes") or "").strip().replace("*/", "*!/")
        out.append("/**")
        out.append(f" * `{packet.get('name', name)}`" + (f" - id {packet['id']}." if "id" in packet else "."))
        if notes:
            out += [" *", *(f" * {line}" for line in notes.splitlines())]
        out += [" */", f"export interface {pascal(name)} {{", *field_lines(packet, known), "}", ""]

    destination = Path(args.out) / "packet.d.ts"
    destination.write_text("\n".join(out), encoding="utf-8")
    print(f"wrote {destination}: {len(packets)} packets, {len(types)} types, {len(enums)} enums")

    write_runtime_schema(bundle, vendor / "protocol.json")
    return 0


def compact_fields(fields: list[dict]) -> list[dict]:
    """One field list in the smallest form the runtime decoder needs."""
    out = []
    for field in fields:
        entry: dict = {"t": field.get("type")}
        if "name" in field:
            entry["n"] = camel(field["name"])
        if field.get("optional"):
            entry["o"] = 1
        if "value" in field:
            entry["c"] = field["value"]          # a constant on the wire: read and discard
        repeat = field.get("repeat")
        if isinstance(repeat, dict):
            entry["r"] = repeat.get("prefix", "uvarint32")
        elif repeat:
            entry["r"] = "uvarint32"
        out.append(entry)
    return out


def write_runtime_schema(bundle: dict, destination: Path) -> None:
    """
    The decoder's own copy of the schema: packets keyed by id, plus the composite types they use.

    Deliberately not the full vendored bundle - enums are only names for numbers we already decode, and
    the notes and constraints are documentation. Keeping it small matters because this file is read at
    runtime on the server.
    """
    packets = {}
    for name, packet in bundle["packets"].items():
        if "id" not in packet:
            continue
        packets[str(packet["id"])] = {"n": name, "f": compact_fields(packet.get("fields", []))}
    types = {name: compact_fields(t.get("fields", [])) for name, t in bundle["types"].items()}
    payload = {"ref": bundle["ref"], "packets": packets, "types": types}
    destination.write_text(json.dumps(payload, separators=(",", ":"), sort_keys=True), encoding="utf-8")
    print(f"wrote {destination}: {len(packets)} packets, {len(types)} types "
          f"({destination.stat().st_size // 1024} KB, read at runtime by the decoder)")


if __name__ == "__main__":
    sys.exit(main())
