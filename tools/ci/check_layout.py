#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks the flash layout rules on layout.json, its agreement with the boot contract header, boards.txt and
other builds, and that the committed file matches a fresh one."""

import argparse
import json
import re
import sys
from typing import Dict, List, Optional, Tuple

BOOT_START = 0x08000000
BOOT_END = 0x0800E000
NETCFG_START = 0x0800E000
NETCFG_END = 0x08010000
INTERNAL_PARTITIONS = ("boot_partition", "netcfg_partition")
MAILBOX_START = 0x24071F00
MAILBOX_SIZE = 0x100

_DEFINE = re.compile(r"^\s*#define\s+STM32H7RS_(\w+)\s+(0[xX][0-9a-fA-F]+|\d+)[uU]?\b")

Layout = Dict[str, Dict[str, Dict[str, str]]]


def _span(part: Dict[str, str]) -> Tuple[int, int]:
    start = int(part["start"], 16)
    return start, start + int(part["size"], 16)


def _overlaps(a: Tuple[int, int], b: Tuple[int, int]) -> bool:
    return a[0] < b[1] and b[0] < a[1]


def check(layout: Layout) -> List[str]:
    """Returns every flash layout and mailbox rule the layout breaks."""
    errors: List[str] = []
    parts = layout.get("partitions", {})
    devices = layout.get("devices", {})
    for required in ("boot_partition", "netcfg_partition", "slot0_partition", "hmi_store_partition"):
        if required not in parts:
            errors.append(f"{required} is missing")
    if errors:
        return errors
    for name, want in (("boot_partition", (BOOT_START, BOOT_END)), ("netcfg_partition", (NETCFG_START, NETCFG_END))):
        got = _span(parts[name])
        if got != want:
            errors.append(
                f"{name} is 0x{got[0]:08x}-0x{got[1]:08x}, must be exactly 0x{want[0]:08x}-0x{want[1]:08x}"
            )
    internal = {parts[name]["device"] for name in INTERNAL_PARTITIONS}
    if len(internal) != 1:
        errors.append("boot_partition and netcfg_partition must be on the same internal flash device")
    for name, part in parts.items():
        if name not in INTERNAL_PARTITIONS and part["device"] in internal:
            errors.append(
                f"{name} is in internal flash; boot_partition and netcfg_partition must be the only internal partitions"
            )
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
    if "loader_partition" in parts:
        loader = _span(parts["loader_partition"])
        if loader[0] != _span(slot0)[0]:
            errors.append("loader_partition must start at the start of slot0_partition")
        if "user_sketch" in parts and _span(parts["user_sketch"])[0] < loader[1]:
            errors.append("user_sketch must start at or after the end of loader_partition")
    mailbox = layout.get("mailbox")
    if mailbox is None:
        errors.append("the boot RAM mailbox (boot_mailbox node) is missing")
    else:
        start = int(mailbox.get("start", "-0x1"), 16)  # type: ignore[union-attr]
        size = int(mailbox.get("size", "-0x1"), 16)  # type: ignore[union-attr]
        if (start, size) != (MAILBOX_START, MAILBOX_SIZE):
            errors.append(
                f"the boot RAM mailbox is 0x{start:08x} size 0x{size:x}, "
                f"must be 0x{MAILBOX_START:08x} size 0x{MAILBOX_SIZE:x}"
            )
    return errors


def parse_contract(text: str) -> Dict[str, int]:
    """Returns the integer literals of the STM32H7RS_<NAME> defines of the boot contract header, by NAME."""
    values: Dict[str, int] = {}
    for line in text.splitlines():
        match = _DEFINE.match(line)
        if match:
            values[match.group(1)] = int(match.group(2), 0)
    return values


def _field(layout: Layout, section: str, name: Optional[str], key: str) -> Optional[int]:
    entry = layout.get(section, {})
    if name is not None:
        entry = entry.get(name, {})  # type: ignore[assignment]
    raw = entry.get(key)  # type: ignore[union-attr]
    return int(raw, 16) if isinstance(raw, str) else None


def check_contract(layout: Layout, contract: Dict[str, int]) -> List[str]:
    """Returns every boot contract value the layout disagrees with."""
    erase_device = layout.get("partitions", {}).get("slot0_partition", {}).get("device")
    pairs = (
        ("BOOT_BASE", _field(layout, "partitions", "boot_partition", "start"), "boot_partition start"),
        ("BOOT_SIZE", _field(layout, "partitions", "boot_partition", "size"), "boot_partition size"),
        ("NETCFG_ADDR", _field(layout, "partitions", "netcfg_partition", "start"), "netcfg_partition start"),
        ("NETCFG_SECTOR_SIZE", _field(layout, "partitions", "netcfg_partition", "size"), "netcfg_partition size"),
        ("SLOT_BASE", _field(layout, "partitions", "slot0_partition", "start"), "slot0_partition start"),
        ("SLOT_SIZE", _field(layout, "partitions", "slot0_partition", "size"), "slot0_partition size"),
        ("MAILBOX_ADDR", _field(layout, "mailbox", None, "start"), "mailbox start"),
        ("MAILBOX_SIZE", _field(layout, "mailbox", None, "size"), "mailbox size"),
        ("IMAGE_HEADER_SIZE", _field(layout, "image", None, "header_size"), "image header_size"),
        (
            "EXT_ERASE_SIZE",
            _field(layout, "devices", erase_device, "erase_block") if erase_device else None,
            "slot0_partition device erase block",
        ),
    )
    errors: List[str] = []
    for name, got, what in pairs:
        want = contract.get(name)
        if want is None:
            errors.append(f"STM32H7RS_{name} is missing from the boot contract header")
        elif got is None:
            errors.append(f"the layout has no {what} to check against STM32H7RS_{name}")
        elif got != want:
            errors.append(f"{what} is 0x{got:x}, the boot contract STM32H7RS_{name} is 0x{want:x}")
    return errors


def compare(layouts: Dict[str, Layout]) -> List[str]:
    """Returns every partition or mailbox value that differs between builds sharing the layout."""
    errors: List[str] = []
    names = sorted(layouts)
    for i, first in enumerate(names):
        for second in names[i + 1:]:
            a, b = layouts[first], layouts[second]
            shared = set(a.get("partitions", {})) & set(b.get("partitions", {}))
            for part in sorted(shared):
                if a["partitions"][part] != b["partitions"][part]:
                    errors.append(f"{part} differs between {first} and {second}")
            if a.get("mailbox") != b.get("mailbox"):
                errors.append(f"the boot RAM mailbox differs between {first} and {second}")
    return errors


def _board_value(boards: str, board: str, key: str) -> str:
    prefix = f"{board}.build.image.{key}="
    for line in boards.splitlines():
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    return ""


def check_boards(layout: Layout, boards: str, board: str) -> List[str]:
    """Returns the boards.txt image values of a board that disagree with the layout."""
    errors: List[str] = []
    parts = layout.get("partitions", {})
    expected = {
        "sketch_offset": int(parts.get("user_sketch", {}).get("offset", "-0x1"), 16),
        "slot_size": int(parts.get("slot0_partition", {}).get("size", "-0x1"), 16),
        "header_size": int(layout.get("image", {}).get("header_size", "-0x1"), 16),  # type: ignore[union-attr]
    }
    for key, want in expected.items():
        raw = _board_value(boards, board, key)
        if not raw:
            errors.append(f"{board}.build.image.{key} is missing from boards.txt")
            continue
        if int(raw, 0) != want:
            errors.append(f"{board}.build.image.{key}={raw} does not match the layout (0x{want:x})")
    return errors


def same(committed: str, fresh: str) -> bool:
    """True when the committed layout.json text equals the freshly generated one."""
    return json.loads(committed) == json.loads(fresh)


def main(argv: List[str]) -> int:
    """Command line entry: checks layout.json and, per option, freshness, other builds, boards.txt, boot contract."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layout")
    parser.add_argument("--fresh", help="layout generated from the current build")
    parser.add_argument("--boards", help="boards.txt whose image values must match the layout")
    parser.add_argument("--board", action="append", default=[], help="board ID to check in boards.txt")
    parser.add_argument("--compare", action="append", default=[], help="layout of another build that must agree")
    parser.add_argument("--contract", help="boot contract header (stm32h7rs_boot.h) whose values must match the layout")
    args = parser.parse_args(argv)
    with open(args.layout, encoding="utf-8") as handle:
        committed = handle.read()
    errors = check(json.loads(committed))
    if args.fresh:
        with open(args.fresh, encoding="utf-8") as handle:
            if not same(committed, handle.read()):
                errors.append(f"{args.layout} is stale: regenerate it with gen_layout.py")
    if args.compare:
        layouts = {args.layout: json.loads(committed)}
        for path in args.compare:
            with open(path, encoding="utf-8") as handle:
                layouts[path] = json.loads(handle.read())
        errors.extend(compare(layouts))
    if args.boards:
        with open(args.boards, encoding="utf-8") as handle:
            boards = handle.read()
        for board in args.board:
            errors.extend(check_boards(json.loads(committed), boards, board))
    if args.contract:
        with open(args.contract, encoding="utf-8") as handle:
            errors.extend(check_contract(json.loads(committed), parse_contract(handle.read())))
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    print("OK: flash layout, boot contract and boards.txt image values agree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
