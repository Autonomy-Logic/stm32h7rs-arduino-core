# SPDX-License-Identifier: Apache-2.0
# Copyright (c) Autonomy Logic
"""Writes a synthetic merged image: an empty 1 KB header area, then a vector table and filler bytes."""

import struct
import sys

HEADER_SIZE = 0x400
SLOT_BASE = 0x70000000


def main() -> None:
    body = bytearray(struct.pack("<II", 0x24072000, SLOT_BASE + HEADER_SIZE + 0x201))
    body += bytes((i * 31 + 7) & 0xFF for i in range(0x3000))
    with open(sys.argv[1], "wb") as out:
        out.write(bytes(HEADER_SIZE) + body)


if __name__ == "__main__":
    main()
