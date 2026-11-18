#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks that every project of a resolved west manifest is pinned by a full commit hash (SR-03)."""

import re
import sys
from typing import List, Tuple

_NAME = re.compile(r"^\s*-\s+name:\s*(\S+)\s*$")
_REVISION = re.compile(r"^\s+revision:\s*(\S+)\s*$")
_HASH = re.compile(r"^[0-9a-f]{40}$")


def parse_projects(text: str) -> List[Tuple[str, str]]:
    """Returns (name, revision) for each project of 'west manifest --resolve' output."""
    projects: List[Tuple[str, str]] = []
    current = None
    for line in text.splitlines():
        name = _NAME.match(line)
        if name:
            current = name.group(1)
            projects.append((current, ""))
            continue
        revision = _REVISION.match(line)
        if revision and current is not None and projects and projects[-1][0] == current:
            projects[-1] = (current, revision.group(1).strip("'\""))
    return projects


def check(text: str) -> List[str]:
    """Returns the projects whose revision is not a 40-character commit hash."""
    projects = parse_projects(text)
    if not projects:
        return ["the manifest lists no projects"]
    return [
        f"project {name} is pinned to '{revision}', not a 40-character commit hash"
        for name, revision in projects
        if not _HASH.match(revision)
    ]


def main(argv: List[str]) -> int:
    """Command line entry: reads the resolved manifest from a file or stdin."""
    if argv:
        with open(argv[0], encoding="utf-8") as handle:
            text = handle.read()
    else:
        text = sys.stdin.read()
    errors = check(text)
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    print(f"OK: {len(parse_projects(text))} west projects pinned by commit hash")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
