#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks the MCUboot .config for the required recovery and signature settings (SR-01)."""

import sys
from typing import Dict, List

KEY_FILE_NAME = "stm32h7rs-dev-ecdsa-p256.pem"
VENDOR_ID = 0x2FE3

REQUIRED_ENABLED = (
    "CONFIG_SINGLE_APPLICATION_SLOT",
    "CONFIG_MCUBOOT_SERIAL",
    "CONFIG_BOOT_SERIAL_CDC_ACM",
    "CONFIG_BOOT_SERIAL_NO_APPLICATION",
    "CONFIG_BOOT_SERIAL_BOOT_MODE",
    "CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256",
)

REQUIRED_DISABLED = ("CONFIG_BOOT_ENCRYPT_IMAGE", "CONFIG_BOOT_SERIAL_ENTRANCE_GPIO")


def parse_config(text: str) -> Dict[str, str]:
    """Parses a Kconfig .config into a symbol to value map, skipping unset symbols."""
    values: Dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def check(config: Dict[str, str]) -> List[str]:
    """Returns every C2 item the configuration does not meet."""
    errors: List[str] = []
    for symbol in REQUIRED_ENABLED:
        if config.get(symbol) != "y":
            errors.append(f"{symbol} must be y")
    for symbol in REQUIRED_DISABLED:
        if config.get(symbol) == "y":
            errors.append(f"{symbol} must not be set")
    key_file = config.get("CONFIG_BOOT_SIGNATURE_KEY_FILE", "").strip('"')
    if not key_file.endswith(KEY_FILE_NAME):
        errors.append(f"CONFIG_BOOT_SIGNATURE_KEY_FILE must name {KEY_FILE_NAME}, got '{key_file}'")
    vid = config.get("CONFIG_BOOT_SERIAL_CDC_ACM_VID", "")
    try:
        vid_value = int(vid, 0)
    except ValueError:
        vid_value = -1
    if vid_value != VENDOR_ID:
        errors.append(f"CONFIG_BOOT_SERIAL_CDC_ACM_VID must be 0x{VENDOR_ID:04x}, got '{vid}'")
    return errors


def main(argv: List[str]) -> int:
    """Command line entry: checks the .config given as the only argument."""
    if len(argv) != 1:
        print("usage: check_mcuboot_config.py <path to .config>")
        return 2
    with open(argv[0], encoding="utf-8") as handle:
        errors = check(parse_config(handle.read()))
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    print("OK: MCUboot configuration meets C2 (single slot, recovery entries, ECDSA-P256, no encryption)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
