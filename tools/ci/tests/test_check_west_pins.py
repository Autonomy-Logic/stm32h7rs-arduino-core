# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_west_pins as cwp  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def read(name):
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as handle:
        return handle.read()


class CheckWestPinsTest(unittest.TestCase):
    def test_all_hashes_pass(self):
        self.assertEqual(cwp.check(read("west_resolved_good.yml")), [])

    def test_branch_revision_fails(self):
        errors = cwp.check(read("west_resolved_branch.yml"))
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("cannectivity", errors[0])

    def test_short_hash_fails(self):
        text = read("west_resolved_good.yml").replace(
            "a3f2c9e1d0b4a5f6e7d8c9b0a1f2e3d4c5b6a7f8", "a3f2c9e1"
        )
        self.assertTrue(cwp.check(text))

    def test_no_projects_fails(self):
        self.assertTrue(cwp.check("manifest:\n  projects: []\n"))

    def test_parse_projects(self):
        projects = cwp.parse_projects(read("west_resolved_good.yml"))
        self.assertEqual([p[0] for p in projects], ["zephyr", "mcuboot", "hal_stm32"])


if __name__ == "__main__":
    unittest.main()
