#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks a loader .config: limited network features, nothing configured at boot, network below the sketch thread."""

import sys
from typing import Dict, List

from check_mcuboot_config import parse_config

NETWORK_REQUIRED = ("CONFIG_NET_IPV4", "CONFIG_NET_TCP", "CONFIG_NET_UDP", "CONFIG_NET_DHCPV4")

NETWORK_FORBIDDEN = (
    "CONFIG_NET_IPV6",
    "CONFIG_NET_CONFIG_SETTINGS",
    "CONFIG_NET_CONFIG_AUTO_INIT",
    "CONFIG_NET_VLAN",
    "CONFIG_NET_GPTP",
    "CONFIG_NET_LLDP",
    "CONFIG_MDNS_RESPONDER",
    "CONFIG_LLMNR_RESPONDER",
    "CONFIG_HTTP_SERVER",
    "CONFIG_WEBSOCKET_CLIENT",
    "CONFIG_NET_SOCKETS_SOCKS",
    "CONFIG_NET_SHELL",
    "CONFIG_BT",
)

ALWAYS_FORBIDDEN = ("CONFIG_SHELL",)

NETWORK_PREEMPTIVE_FLAGS = ("CONFIG_NET_TC_THREAD_PREEMPTIVE", "CONFIG_NET_TC_THREAD_PRIO_CUSTOM")

NETWORK_THREAD_PRIORITIES = (
    "CONFIG_NET_TC_TX_THREAD_BASE_PRIO",
    "CONFIG_NET_TC_RX_THREAD_BASE_PRIO",
    "CONFIG_NET_TCP_WORKER_PRIO",
    "CONFIG_NET_SOCKETS_SERVICE_THREAD_PRIO",
    "CONFIG_SYSTEM_WORKQUEUE_PRIORITY",
)

ETH_FLAG = "CONFIG_ETH_STM32_HAL_RX_THREAD_PREEMPTIVE"
ETH_PRIORITY = "CONFIG_ETH_STM32_HAL_RX_THREAD_PRIO"


def _enabled(config: Dict[str, str], symbol: str) -> bool:
    return config.get(symbol) == "y"


def _int(config: Dict[str, str], symbol: str) -> int:
    try:
        return int(config.get(symbol, ""), 0)
    except ValueError:
        return -1000


def check(config: Dict[str, str]) -> List[str]:
    """Returns every network-feature and thread-priority rule the loader configuration breaks."""
    errors: List[str] = []
    for symbol in ALWAYS_FORBIDDEN:
        if _enabled(config, symbol):
            errors.append(f"{symbol} must not be set")
    main = _int(config, "CONFIG_MAIN_THREAD_PRIORITY")
    if main < 0:
        errors.append(f"CONFIG_MAIN_THREAD_PRIORITY must be a preemptive priority (>= 0), got {main}")
    if not _enabled(config, "CONFIG_NETWORKING"):
        return errors
    for symbol in NETWORK_REQUIRED:
        if not _enabled(config, symbol):
            errors.append(f"{symbol} must be y")
    for symbol in NETWORK_FORBIDDEN:
        if _enabled(config, symbol):
            errors.append(f"{symbol} must not be set")
    for symbol in NETWORK_PREEMPTIVE_FLAGS:
        if not _enabled(config, symbol):
            errors.append(f"{symbol} must be y so network threads run below the sketch thread")
    priorities = list(NETWORK_THREAD_PRIORITIES)
    if _enabled(config, "CONFIG_ETH_STM32_HAL"):
        if not _enabled(config, ETH_FLAG):
            errors.append(f"{ETH_FLAG} must be y so the Ethernet RX thread runs below the sketch thread")
        priorities.append(ETH_PRIORITY)
    for symbol in priorities:
        value = _int(config, symbol)
        if value <= main:
            errors.append(f"{symbol}={value} must be numerically higher than CONFIG_MAIN_THREAD_PRIORITY={main}")
    return errors


def main(argv: List[str]) -> int:
    """Command line entry: checks the loader .config given as the only argument."""
    if len(argv) != 1:
        print("usage: check_loader_config.py <path to loader .config>")
        return 2
    with open(argv[0], encoding="utf-8") as handle:
        errors = check(parse_config(handle.read()))
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    print("OK: loader network features and thread priorities meet SR-03 and SR-05")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
