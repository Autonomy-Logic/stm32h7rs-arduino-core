#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generates tools/ci/layout.json from a built zephyr.dts: partitions, flash devices and retention region."""

import argparse
import json
import re
import sys
from typing import Dict, List, Optional

PARTITION_LABELS = ("boot_partition", "slot0_partition", "hmi_store_partition", "loader_partition", "user_sketch")
RETENTION_COMPATIBLE = '"zephyr,retention"'

_NODE = re.compile(r"^((?:[A-Za-z_][\w]*:\s*)*)([^\s{]+)\s*\{$")
_PROP = re.compile(r"^([#\w,.\-]+)\s*(?:=\s*(.*))?;$")
_CELLS = re.compile(r"<([^>]*)>")
_COMMENT = re.compile(r"/\*.*?\*/")


class Node:
    """A devicetree node with its labels, raw properties, parent and children."""

    def __init__(self, name: str, labels: List[str], parent: Optional["Node"]) -> None:
        self.name = name
        self.labels = labels
        self.parent = parent
        self.props: Dict[str, str] = {}
        self.children: List["Node"] = []

    def cells(self, prop: str) -> List[int]:
        """Returns the property's cells as integers, all <> groups concatenated."""
        raw = self.props.get(prop)
        if raw is None:
            return []
        values: List[int] = []
        for group in _CELLS.findall(raw):
            values.extend(int(token, 0) for token in group.split() if not token.startswith("&"))
        return values


def parse_dts(text: str) -> Node:
    """Parses the normalised zephyr.dts the Zephyr build writes, one statement per line."""
    root = Node("/", [], None)
    stack = [root]
    for raw in text.splitlines():
        line = _COMMENT.sub("", raw).strip()
        if not line or line.startswith("/dts-v1/") or line.startswith("//") or line.startswith("/*"):
            continue
        if line == "};":
            stack.pop()
            continue
        node = _NODE.match(line)
        if node:
            labels = [label.strip().rstrip(":") for label in node.group(1).split(":") if label.strip()]
            name = node.group(2)
            if name == "/" and len(stack) == 1:
                continue
            child = Node(name, labels, stack[-1])
            stack[-1].children.append(child)
            stack.append(child)
            continue
        prop = _PROP.match(line)
        if prop:
            stack[-1].props[prop.group(1)] = prop.group(2) or ""
    return root


def walk(node: Node) -> List[Node]:
    """Returns the node and all its descendants, depth first."""
    nodes = [node]
    for child in node.children:
        nodes.extend(walk(child))
    return nodes


def absolute(node: Node, address: int) -> int:
    """Translates an address in node's parent bus to the CPU address through every 'ranges'."""
    current = node.parent
    while current is not None and current.name != "/":
        if "ranges" not in current.props:
            break
        ranges = current.cells("ranges")
        if len(ranges) >= 3:
            child, parent, size = ranges[0], ranges[1], ranges[2]
            if child <= address < child + size:
                address = address - child + parent
        current = current.parent
    return address


def _hex(value: int) -> str:
    return f"0x{value:08x}"


def layout_from_dts(text: str) -> Dict[str, Dict[str, Dict[str, str]]]:
    """Builds the layout dictionary written to layout.json."""
    root = parse_dts(text)
    nodes = walk(root)
    partitions: Dict[str, Dict[str, str]] = {}
    devices: Dict[str, Dict[str, str]] = {}
    retention: Optional[Dict[str, str]] = None
    for node in nodes:
        label = next((l for l in node.labels if l in PARTITION_LABELS), None)
        if label is not None and node.parent is not None and node.parent.parent is not None:
            flash = node.parent.parent
            reg = node.cells("reg")
            flash_reg = flash.cells("reg")
            offset = reg[0]
            start = absolute(flash, flash_reg[0] + offset)
            device = flash.labels[0] if flash.labels else flash.name
            partitions[label] = {
                "device": device,
                "offset": _hex(offset),
                "start": _hex(start),
                "size": _hex(reg[1]),
            }
            flash_start = absolute(flash, flash_reg[0])
            erase = flash.cells("erase-block-size")
            devices[device] = {
                "start": _hex(flash_start),
                "erase_block": _hex(erase[0] if erase else 0),
            }
        compatible = node.props.get("compatible", "")
        if retention is None and compatible == RETENTION_COMPATIBLE and node.parent is not None:
            region = node.parent
            while region.parent is not None and not region.cells("reg"):
                region = region.parent
            region_reg = region.cells("reg")
            reg = node.cells("reg")
            if region_reg and reg:
                retention = {
                    "start": _hex(absolute(region, region_reg[0]) + reg[0]),
                    "size": _hex(reg[1]),
                }
    layout: Dict[str, Dict[str, Dict[str, str]]] = {"partitions": partitions, "devices": devices}
    if retention is not None:
        layout["retention"] = retention  # type: ignore[assignment]
    return layout


def dumps(layout: Dict[str, Dict[str, Dict[str, str]]]) -> str:
    """Serialises the layout in a stable form for the freshness check."""
    return json.dumps(layout, indent=2, sort_keys=True) + "\n"


def main(argv: List[str]) -> int:
    """Command line entry: reads one or more zephyr.dts files and writes the merged layout."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dts", nargs="+")
    parser.add_argument("--out", required=True)
    parser.add_argument("--header-size", help="MCUboot image header size (CONFIG_ROM_START_OFFSET of the loader)")
    args = parser.parse_args(argv)
    merged: Dict[str, Dict[str, Dict[str, str]]] = {"partitions": {}, "devices": {}}
    for path in args.dts:
        with open(path, encoding="utf-8") as handle:
            layout = layout_from_dts(handle.read())
        merged["partitions"].update(layout["partitions"])
        merged["devices"].update(layout["devices"])
        if "retention" in layout:
            merged["retention"] = layout["retention"]
    if args.header_size:
        merged["image"] = {"header_size": _hex(int(args.header_size, 0))}  # type: ignore[assignment]
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write(dumps(merged))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
