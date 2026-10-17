#!/usr/bin/env python3
"""Choose an unpublished major.minor release tag without a v prefix.

The input file contains published Release tag names and existing Git tag names,
one per line. Old v-prefixed tags are intentionally ignored.
"""
import argparse
import re
import sys
from pathlib import Path

VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


def choose(tags: list[str], requested: str) -> str:
    existing = [tuple(map(int, matched.groups()))
                for tag in tags if (matched := VERSION.fullmatch(tag.strip()))]
    latest = max(existing, default=None)
    if requested:
        match = VERSION.fullmatch(requested.strip())
        if not match:
            raise ValueError("La versión debe tener formato MAYOR.MENOR, por ejemplo 2.0")
        version = tuple(map(int, match.groups()))
        if latest is not None and version <= latest:
            raise ValueError(f"La versión solicitada debe superar {latest[0]}.{latest[1]}")
    elif latest is None:
        version = (1, 0)
    else:
        version = (latest[0], latest[1] + 1)
    return f"{version[0]}.{version[1]}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tags-file", required=True, type=Path)
    parser.add_argument("--desde-version", default="")
    args = parser.parse_args()
    try:
        version = choose(args.tags_file.read_text(encoding="utf-8").splitlines(),
                         args.desde_version)
    except (ValueError, OSError) as exc:
        print(f"No se pudo elegir una versión: {exc}", file=sys.stderr)
        sys.exit(1)
    print(version)


if __name__ == "__main__":
    main()
