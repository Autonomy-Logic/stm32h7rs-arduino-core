# SPDX-License-Identifier: Apache-2.0

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import gen_index  # noqa: E402


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


class GenIndexTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        write(os.path.join(self.dir, "zephyr_h7rs-0.1.0.tar.bz2"), b"core")
        for host in gen_index.HOSTS:
            write(os.path.join(self.dir, gen_index.tool_archive_name("0.1.0", host)), host.encode())

    def build(self, base="https://example.com/rel"):
        return gen_index.build_index(self.dir, "0.1.0", base, ["STM32H7S7 (generic)", "Synergy Logic SL-VISU-5R"])

    def test_platform_entry(self):
        platform = self.build()["packages"][0]["platforms"][0]
        self.assertEqual(platform["architecture"], "zephyr_h7rs")
        self.assertEqual(platform["version"], "0.1.0")
        self.assertEqual(platform["url"], "https://example.com/rel/zephyr_h7rs-0.1.0.tar.bz2")
        self.assertEqual(platform["size"], "4")
        self.assertTrue(platform["checksum"].startswith("SHA-256:"))
        self.assertEqual([b["name"] for b in platform["boards"]][1], "Synergy Logic SL-VISU-5R")

    def test_tool_dependencies_include_own_tool_and_toolchain(self):
        deps = self.build()["packages"][0]["platforms"][0]["toolsDependencies"]
        names = {(d["packager"], d["name"], d["version"]) for d in deps}
        self.assertIn(("AutonomyLogic", "stm32h7rs-tool", "0.1.0"), names)
        self.assertIn(("zephyr", "arm-zephyr-eabi", "1.0.1"), names)
        self.assertNotIn("imgtool", {d["name"] for d in deps})

    def test_tool_has_every_host(self):
        tool = self.build()["packages"][0]["tools"][0]
        self.assertEqual(tool["name"], "stm32h7rs-tool")
        self.assertEqual(sorted(s["host"] for s in tool["systems"]), sorted(gen_index.HOSTS))
        for system in tool["systems"]:
            self.assertTrue(system["url"].startswith("https://example.com/rel/"))

    def test_file_base_url(self):
        url = self.build("file:///tmp/assets")["packages"][0]["platforms"][0]["url"]
        self.assertEqual(url, "file:///tmp/assets/zephyr_h7rs-0.1.0.tar.bz2")

    def test_missing_archive_fails(self):
        os.remove(os.path.join(self.dir, "zephyr_h7rs-0.1.0.tar.bz2"))
        with self.assertRaises(FileNotFoundError):
            self.build()

    def test_output_is_json_serialisable(self):
        json.dumps(self.build())


if __name__ == "__main__":
    unittest.main()
