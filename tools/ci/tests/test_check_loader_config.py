# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_loader_config as clc  # noqa: E402
from check_mcuboot_config import parse_config  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def good():
    with open(os.path.join(FIXTURES, "loader_good.config"), encoding="utf-8") as handle:
        return parse_config(handle.read())


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

    def test_without_networking_only_the_shell_rule_applies(self):
        config = {"CONFIG_MAIN_THREAD_PRIORITY": "2"}
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
