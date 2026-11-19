# SPDX-License-Identifier: Apache-2.0
# Copyright (c) Autonomy Logic
"""Copies a program image with one payload byte flipped, so its payload CRC no longer matches its header."""

import sys


def main() -> None:
    data = bytearray(open(sys.argv[1], "rb").read())
    data[0x400 + 64] ^= 0xFF
    with open(sys.argv[2], "wb") as out:
        out.write(data)


if __name__ == "__main__":
    main()
