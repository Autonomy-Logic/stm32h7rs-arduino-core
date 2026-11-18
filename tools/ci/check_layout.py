#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks the C1 flash layout rules on layout.json and that the committed file matches a fresh one (TC06)."""

import argparse
import json
import sys
from typing import Dict, List, Tuple

BOOT_START = 0x08000000
BOOT_END = 0x08010000

Layout = Dict[str, Dict[str, Dict[str, str]]]


def _span(part: Dict[str, str]) -> Tuple[int, int]:
    start = int(part["start"], 16)
    return start, start + int(part["size"], 16)


def _overlaps(a: Tuple[int, int], b: Tuple[int, int]) -> bool:
    return a[0] < b[1] and b[0] < a[1]


def check(layout: Layout) -> List[str]:
    """Returns every C1 rule the layout breaks."""
    errors: List[str] = []
    parts = layout.get("partitions", {})
    devices = layout.get("devices", {})
    for required in ("boot_partition", "slot0_partition", "hmi_store_partition"):
        if required not in parts:
            errors.append(f"{required} is missing")
    if errors:
        return errors
    boot = _span(parts["boot_partition"])
    if boot != (BOOT_START, BOOT_END):
        errors.append(
            f"boot_partition is 0x{boot[0]:08x}-0x{boot[1]:08x}, must be exactly 0x{BOOT_START:08x}-0x{BOOT_END:08x}"
        )
    boot_device = parts["boot_partition"]["device"]
    for name, part in parts.items():
        if name != "boot_partition" and part["device"] == boot_device:
            errors.append(f"{name} is in internal flash; boot_partition must be the only internal partition")
    slot0 = parts["slot0_partition"]
    if int(slot0["offset"], 16) != 0:
        errors.append(f"slot0_partition must start at external offset 0, got {slot0['offset']}")
    store = parts["hmi_store_partition"]
    if _overlaps(_span(store), _span(slot0)):
        errors.append("hmi_store_partition intersects slot0_partition")
    erase = int(devices.get(store["device"], {}).get("erase_block", "0x0"), 16)
    if erase <= 0 or int(store["offset"], 16) % erase or int(store["size"], 16) % erase:
        errors.append(f"hmi_store_partition is not aligned to the {store['device']} erase block (0x{erase:x})")
    if "user_sketch" in parts and not (
        _span(slot0)[0] <= _span(parts["user_sketch"])[0] and _span(parts["user_sketch"])[1] <= _span(slot0)[1]
    ):
        errors.append("user_sketch is not inside slot0_partition")
    if "retention" not in layout:
        errors.append("the boot mode retention region is missing")
    return errors


def same(committed: str, fresh: str) -> bool:
    """True when the committed layout.json text equals the freshly generated one."""
    return json.loads(committed) == json.loads(fresh)


def main(argv: List[str]) -> int:
    """Command line entry: checks layout.json and, with --fresh, its freshness."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layout")
    parser.add_argument("--fresh", help="layout generated from the current build")
    args = parser.parse_args(argv)
    with open(args.layout, encoding="utf-8") as handle:
        committed = handle.read()
    errors = check(json.loads(committed))
    if args.fresh:
        with open(args.fresh, encoding="utf-8") as handle:
            if not same(committed, handle.read()):
                errors.append(f"{args.layout} is stale: regenerate it with gen_layout.py")
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    print("OK: flash layout meets C1")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
