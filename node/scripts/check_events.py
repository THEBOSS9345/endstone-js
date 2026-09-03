#!/usr/bin/env python3
"""Checks that every event Endstone declares upstream is bound.

Parity with upstream is the point of node/, and events are where upstream keeps adding: a release can
introduce one and nothing would notice until someone asked for it by name. Because the bindings mirror
upstream's own folder layout, "what are we missing" is a directory diff, which is what this is.

It reports by name and by header, so closing a gap is a matter of adding one ESN_EVENT block to the
file matching the folder the header lives in.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Not events, so they have nothing to bind:
#   ActorEvent      a template - ActorEvent<Mob> and ActorEvent<Actor> - so `actor` is declared on
#                   each concrete event instead, which also gets the living/non-living kind right.
#   Cancellable     the mechanism behind b.cancellable(), not an event in its own right.
#   ICancellable    its interface.
#   EventHandler    internal dispatch machinery.
#   HandlerList     the same.
NOT_EVENTS = {"ActorEvent", "Cancellable", "ICancellable", "EventHandler", "HandlerList"}

DECLARED = re.compile("^ESN_EVENT(?:_BASE|_ROOT)?[(]([A-Za-z0-9_]+)", re.M)
UPSTREAM = re.compile("^class ([A-Za-z0-9_]+)", re.M)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bindings", type=Path, required=True, help="plugin/types/events")
    parser.add_argument("--headers", type=Path, required=True, help="include/endstone/event")
    args = parser.parse_args()

    declared: set[str] = set()
    for source in sorted(args.bindings.glob("*.cpp")):
        declared |= set(DECLARED.findall(source.read_text(encoding="utf-8")))

    upstream: dict[str, str] = {}
    for header in sorted(args.headers.rglob("*.h")):
        for name in UPSTREAM.findall(header.read_text(encoding="utf-8")):
            if name not in NOT_EVENTS:
                upstream[name] = header.as_posix()

    missing = {name: path for name, path in upstream.items() if name not in declared}
    if missing:
        print(
            "Endstone declares events that are not bound, so a JavaScript plugin cannot use them:\n"
            + "\n".join(f"  {name:<34} {path}" for name, path in sorted(missing.items()))
            + "\n\nAdd an ESN_EVENT block to plugin/types/events/<folder>.cpp, matching the folder the"
            "\nheader lives in. If it is not an event, add it to NOT_EVENTS in this script.",
            file=sys.stderr,
        )
        return 1

    print(f"events agree: {len(upstream)} bound, none missing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
