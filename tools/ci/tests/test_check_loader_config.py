# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_loader_config as clc  # noqa: E402
from check_loader_config import parse_config  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def good():
    with open(os.path.join(FIXTURES, "loader_good.config"), encoding="utf-8") as handle:
        return parse_config(handle.read())


class ParseConfigTest(unittest.TestCase):
    def test_unset_and_blank_lines_are_skipped(self):
        config = parse_config("# CONFIG_A is not set\n\nCONFIG_B=y\nCONFIG_C=\"x=y\"\n")
        self.assertEqual(config, {"CONFIG_B": "y", "CONFIG_C": "\"x=y\""})


class BootSettingsTest(unittest.TestCase):
    def test_missing_netboot_fails(self):
        config = good()
        del config["CONFIG_ARDUINO_STM32H7RS_NETBOOT"]
        self.assertTrue(any("CONFIG_ARDUINO_STM32H7RS_NETBOOT" in e for e in clc.check(config)))

    def test_wrong_rom_start_offset_fails(self):
        config = good()
        config["CONFIG_ROM_START_OFFSET"] = "0x200"
        self.assertTrue(any("CONFIG_ROM_START_OFFSET" in e for e in clc.check(config)))

    def test_missing_rom_start_offset_fails(self):
        config = good()
        del config["CONFIG_ROM_START_OFFSET"]
        self.assertTrue(any("CONFIG_ROM_START_OFFSET" in e for e in clc.check(config)))

    def test_chain_loaded_image_flag_fails(self):
        config = good()
        config["CONFIG_BOOTLOADER_MCUBOOT"] = "y"
        self.assertTrue(any("CONFIG_BOOTLOADER_MCUBOOT" in e for e in clc.check(config)))

    def test_retention_boot_mode_fails(self):
        config = good()
        config["CONFIG_RETENTION_BOOT_MODE"] = "y"
        self.assertTrue(any("CONFIG_RETENTION_BOOT_MODE" in e for e in clc.check(config)))


class NetworkFeaturesTest(unittest.TestCase):
    def test_good_config_passes(self):
        self.assertEqual(clc.check(good()), [])

    def test_ipv6_fails(self):
        config = good()
        config["CONFIG_NET_IPV6"] = "y"
        self.assertTrue(any("CONFIG_NET_IPV6" in e for e in clc.check(config)))

    def test_network_configuration_at_boot_fails(self):
        config = good()
        config["CONFIG_NET_CONFIG_AUTO_INIT"] = "y"
        self.assertTrue(any("CONFIG_NET_CONFIG_AUTO_INIT" in e for e in clc.check(config)))

    def test_missing_dhcp_fails(self):
        config = good()
        del config["CONFIG_NET_DHCPV4"]
        self.assertTrue(any("CONFIG_NET_DHCPV4" in e for e in clc.check(config)))

    def test_shell_fails(self):
        config = good()
        config["CONFIG_SHELL"] = "y"
        self.assertTrue(any("CONFIG_SHELL" in e for e in clc.check(config)))

    def test_without_networking_no_network_rule_applies(self):
        config = {
            "CONFIG_MAIN_THREAD_PRIORITY": "2",
            "CONFIG_ARDUINO_STM32H7RS_NETBOOT": "y",
            "CONFIG_ROM_START_OFFSET": "0x400",
        }
        self.assertEqual(clc.check(config), [])


class ThreadPriorityTest(unittest.TestCase):
    def test_network_thread_at_main_priority_fails(self):
        config = good()
        config["CONFIG_NET_TCP_WORKER_PRIO"] = "2"
        self.assertTrue(any("CONFIG_NET_TCP_WORKER_PRIO" in e for e in clc.check(config)))

    def test_cooperative_network_threads_fail(self):
        config = good()
        del config["CONFIG_NET_TC_THREAD_PREEMPTIVE"]
        self.assertTrue(any("CONFIG_NET_TC_THREAD_PREEMPTIVE" in e for e in clc.check(config)))

    def test_cooperative_ethernet_rx_fails(self):
        config = good()
        del config["CONFIG_ETH_STM32_HAL_RX_THREAD_PREEMPTIVE"]
        self.assertTrue(any("ETH_STM32_HAL_RX_THREAD_PREEMPTIVE" in e for e in clc.check(config)))

    def test_system_workqueue_above_main_fails(self):
        config = good()
        config["CONFIG_SYSTEM_WORKQUEUE_PRIORITY"] = "-1"
        self.assertTrue(any("CONFIG_SYSTEM_WORKQUEUE_PRIORITY" in e for e in clc.check(config)))

    def test_cooperative_main_thread_fails(self):
        config = good()
        config["CONFIG_MAIN_THREAD_PRIORITY"] = "-2"
        self.assertTrue(any("CONFIG_MAIN_THREAD_PRIORITY" in e for e in clc.check(config)))


if __name__ == "__main__":
    unittest.main()
