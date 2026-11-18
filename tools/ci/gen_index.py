#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Writes the board manager index of the STM32H7S7 core for one release directory."""

import argparse
import hashlib
import json
import os
import sys
from typing import Dict, List, Union

PACKAGER = "AutonomyLogic"
ARCHITECTURE = "zephyr_h7rs"
TOOL = "stm32h7rs-tool"

HOSTS = (
    "x86_64-pc-linux-gnu",
    "aarch64-linux-gnu",
    "x86_64-apple-darwin",
    "arm64-apple-darwin",
    "x86_64-mingw32",
)

UPSTREAM_TOOLS = (
    ("zephyr", "arm-zephyr-eabi", "1.0.1"),
    ("arduino", "zephyr-sketch-tool", "0.4.2"),
    ("arduino", "gen-rodata-ld", "0.1.1"),
    ("arduino", "zephyr-check-size", "0.1.0"),
)

Json = Dict[str, Union[str, List["Json"], "Json"]]


def core_archive_name(version: str) -> str:
    """Name of the core archive package_core.sh writes."""
    return f"{ARCHITECTURE}-{version}.tar.bz2"


def tool_archive_name(version: str, host: str) -> str:
    """Name of the upload tool archive for one host."""
    return f"{TOOL}-{version}-{host}.tar.gz"


def _file_entry(directory: str, name: str, base_url: str) -> Dict[str, str]:
    path = os.path.join(directory, name)
    with open(path, "rb") as handle:
        data = handle.read()
    return {
        "url": f"{base_url.rstrip('/')}/{name}",
        "archiveFileName": name,
        "checksum": "SHA-256:" + hashlib.sha256(data).hexdigest(),
        "size": str(len(data)),
    }


def build_index(directory: str, version: str, base_url: str, boards: List[str]) -> Json:
    """Builds the index for the archives in directory, published under base_url."""
    platform: Json = {
        "name": "STM32H7S7 core",
        "architecture": ARCHITECTURE,
        "version": version,
        "category": "Contributed",
        "help": {"online": "https://github.com/Autonomy-Logic/stm32h7rs-arduino-core"},
        "boards": [{"name": b} for b in boards],
        "toolsDependencies": [
            {"packager": p, "name": n, "version": v} for p, n, v in UPSTREAM_TOOLS
        ] + [{"packager": PACKAGER, "name": TOOL, "version": version}],
    }
    platform.update(_file_entry(directory, core_archive_name(version), base_url))
    systems = []
    for host in HOSTS:
        entry: Json = {"host": host}
        entry.update(_file_entry(directory, tool_archive_name(version, host), base_url))
        systems.append(entry)
    return {
        "packages": [
            {
                "name": PACKAGER,
                "maintainer": "Autonomy Logic",
                "websiteURL": "https://github.com/Autonomy-Logic/stm32h7rs-arduino-core",
                "email": "",
                "help": {"online": "https://github.com/Autonomy-Logic/stm32h7rs-arduino-core"},
                "platforms": [platform],
                "tools": [{"name": TOOL, "version": version, "systems": systems}],
            }
        ]
    }


def main(argv: List[str]) -> int:
    """Command line entry: writes the index JSON for a release directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", required=True, help="directory holding the core and tool archives")
    parser.add_argument("--version", required=True)
    parser.add_argument("--base-url", required=True, help="URL the archives are published under")
    parser.add_argument("--board", action="append", required=True, help="board name shown by the board manager")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    index = build_index(args.dir, args.version, args.base_url, args.board)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(index, handle, indent=2)
        handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
