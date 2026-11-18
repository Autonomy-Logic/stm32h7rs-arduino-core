# SPDX-License-Identifier: Apache-2.0

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_mcuboot_size as cms  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def fixture(name):
    return os.path.join(FIXTURES, name)


def write_bin(size):
    handle = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
    handle.write(b"\xff" * size)
    handle.close()
    return handle.name


class CheckMcubootSizeTest(unittest.TestCase):
    def test_bin_at_limit_and_good_map_pass(self):
        path = write_bin(65536)
        try:
            errors = cms.check(path, fixture("mcuboot_good.map"))
        finally:
            os.unlink(path)
        self.assertEqual(errors, [])

    def test_bin_one_byte_over_limit_fails(self):
        path = write_bin(65537)
        try:
            errors = cms.check(path, fixture("mcuboot_good.map"))
        finally:
            os.unlink(path)
        self.assertTrue(any("65537" in e for e in errors), errors)

    def test_section_past_internal_flash_fails(self):
        path = write_bin(1024)
        try:
            errors = cms.check(path, fixture("mcuboot_overflow.map"))
        finally:
            os.unlink(path)
        self.assertTrue(any("0x08010000" in e.lower() for e in errors), errors)

    def test_load_address_crossing_boundary_fails(self):
        path = write_bin(1024)
        try:
            errors = cms.check(path, fixture("mcuboot_lma_over.map"))
        finally:
            os.unlink(path)
        self.assertTrue(any(".data" in e for e in errors), errors)

    def test_ram_sections_are_ignored(self):
        sections = cms.parse_map(fixture("mcuboot_good.map"))
        names = [s.name for s in sections if cms.in_internal_flash(s.load)]
        self.assertIn("text", names)
        self.assertNotIn("bss", names)

    def test_main_returns_nonzero_on_failure(self):
        path = write_bin(70000)
        try:
            code = cms.main(["--bin", path, "--map", fixture("mcuboot_good.map")])
        finally:
            os.unlink(path)
        self.assertNotEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
