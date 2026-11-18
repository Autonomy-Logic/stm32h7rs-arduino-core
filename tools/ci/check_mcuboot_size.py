#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks that the MCUboot image fits the 64 KB internal flash (TC12)."""

import argparse
import os
import re
import sys
from typing import List, NamedTuple, Optional

INTERNAL_FLASH_START = 0x08000000
INTERNAL_FLASH_END = 0x08100000
BOUNDARY = 0x08010000
SIZE_LIMIT = BOUNDARY - INTERNAL_FLASH_START

_HEADER = re.compile(r"^(\.?[A-Za-z_][\w.]*)\s*(?:(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)(.*))?$")
_CONT = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)(.*)$")
_LOAD = re.compile(r"load address (0x[0-9a-fA-F]+)")


class Section(NamedTuple):
    """An output section of a GNU ld map: name, run address, size and load address."""

    name: str
    address: int
    size: int
    load: int


def in_internal_flash(address: int) -> bool:
    """True when the address lies in the internal flash address window."""
    return INTERNAL_FLASH_START <= address < INTERNAL_FLASH_END


def _section(name: str, address: str, size: str, rest: str) -> Section:
    run = int(address, 16)
    match = _LOAD.search(rest)
    load = int(match.group(1), 16) if match else run
    return Section(name, run, int(size, 16), load)


def parse_map(path: str) -> List[Section]:
    """Returns the output sections listed after 'Linker script and memory map'."""
    sections: List[Section] = []
    pending: Optional[str] = None
    in_body = False
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line.startswith("Linker script and memory map"):
                in_body = True
                continue
            if not in_body:
                continue
            if pending is not None:
                cont = _CONT.match(line)
                if cont:
                    sections.append(_section(pending, *cont.groups()))
                pending = None
                continue
            if not line or line[0].isspace():
                continue
            header = _HEADER.match(line)
            if not header:
                continue
            name, address, size, rest = header.groups()
            if address is None:
                pending = name
            else:
                sections.append(_section(name, address, size, rest or ""))
    return sections


def check(bin_path: str, map_path: str) -> List[str]:
    """Returns the violations of the 64 KB budget; empty when the image fits."""
    errors: List[str] = []
    size = os.path.getsize(bin_path)
    if size > SIZE_LIMIT:
        errors.append(f"MCUboot binary is {size} bytes, over the {SIZE_LIMIT}-byte limit")
    for section in parse_map(map_path):
        if section.size == 0 or not in_internal_flash(section.load):
            continue
        end = section.load + section.size
        if end > BOUNDARY:
            errors.append(
                f"section {section.name} loads at 0x{section.load:08x}-0x{end:08x}, "
                f"at or above 0x{BOUNDARY:08x}"
            )
    return errors


def main(argv: List[str]) -> int:
    """Command line entry: prints the result and returns the exit code."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True)
    parser.add_argument("--map", required=True)
    args = parser.parse_args(argv)
    errors = check(args.bin, args.map)
    size = os.path.getsize(args.bin)
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print(f"OK: MCUboot is {size} bytes, {SIZE_LIMIT - size} bytes under the {SIZE_LIMIT}-byte limit")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
