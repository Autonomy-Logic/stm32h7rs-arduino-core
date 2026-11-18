# SPDX-License-Identifier: Apache-2.0

import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_layout as cl  # noqa: E402
import gen_layout as gl  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def good_layout():
    with open(os.path.join(FIXTURES, "layout_good.dts"), encoding="utf-8") as handle:
        return gl.layout_from_dts(handle.read())


class GenLayoutTest(unittest.TestCase):
    def test_partitions_have_absolute_addresses(self):
        layout = good_layout()
        parts = layout["partitions"]
        self.assertEqual(parts["boot_partition"]["start"], "0x08000000")
        self.assertEqual(parts["boot_partition"]["size"], "0x00010000")
        self.assertEqual(parts["boot_partition"]["device"], "flash0")
        self.assertEqual(parts["slot0_partition"]["start"], "0x70000000")
        self.assertEqual(parts["slot0_partition"]["device"], "ext_flash")
        self.assertEqual(parts["hmi_store_partition"]["start"], "0x70800000")
        self.assertEqual(parts["hmi_store_partition"]["offset"], "0x00800000")

    def test_devices_carry_erase_block(self):
        layout = good_layout()
        self.assertEqual(layout["devices"]["ext_flash"]["erase_block"], "0x00001000")
        self.assertEqual(layout["devices"]["flash0"]["start"], "0x08000000")

    def test_retention_region(self):
        layout = good_layout()
        self.assertEqual(layout["retention"]["start"], "0x24071f00")
        self.assertEqual(layout["retention"]["size"], "0x00000100")

    def test_trailing_source_comments_are_ignored(self):
        text = (
            "/ {\n"
            "\tsoc {\n"
            "\t\t#address-cells = < 0x1 >; /* in soc.dtsi:3 */\n"
            "\t\tflash0: flash@8000000 {\n"
            "\t\t\treg = < 0x8000000 0x10000 >;   /* in soc.dtsi:12 */\n"
            "\t\t\terase-block-size = < 0x2000 >; /* in soc.dtsi:13 */\n"
            "\t\t\t/* node '/soc/flash@8000000/partitions' defined in board.dtsi:4 */\n"
            "\t\t\tpartitions {\n"
            "\t\t\t\tranges;                           /* in board.dtsi:5 */\n"
            "\t\t\t\tboot_partition: partition@0 {\n"
            "\t\t\t\t\treg = < 0x0 0x10000 >;        /* in board.dtsi:9 */\n"
            "\t\t\t\t};\n"
            "\t\t\t};\n"
            "\t\t};\n"
            "\t};\n"
            "};\n"
        )
        parts = gl.layout_from_dts(text)["partitions"]
        self.assertEqual(parts["boot_partition"]["start"], "0x08000000")
        self.assertEqual(parts["boot_partition"]["size"], "0x00010000")

    def test_serialisation_is_stable(self):
        self.assertEqual(gl.dumps(good_layout()), gl.dumps(good_layout()))


class CheckLayoutTest(unittest.TestCase):
    def test_good_layout_passes(self):
        self.assertEqual(cl.check(good_layout()), [])

    def test_boot_partition_must_be_exact(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["boot_partition"]["size"] = "0x0000e000"
        errors = cl.check(layout)
        self.assertTrue(any("boot_partition" in e for e in errors), errors)

    def test_second_internal_partition_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["storage_partition"] = {
            "device": "flash0", "offset": "0x0000e000", "start": "0x0800e000", "size": "0x00002000",
        }
        errors = cl.check(layout)
        self.assertTrue(any("storage_partition" in e for e in errors), errors)

    def test_store_overlapping_slot0_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["hmi_store_partition"]["offset"] = "0x007f0000"
        layout["partitions"]["hmi_store_partition"]["start"] = "0x707f0000"
        errors = cl.check(layout)
        self.assertTrue(any("hmi_store_partition" in e for e in errors), errors)

    def test_store_misaligned_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["hmi_store_partition"]["offset"] = "0x00800800"
        layout["partitions"]["hmi_store_partition"]["start"] = "0x70800800"
        errors = cl.check(layout)
        self.assertTrue(any("aligned" in e for e in errors), errors)

    def test_slot0_must_start_at_offset_zero(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["slot0_partition"]["offset"] = "0x00010000"
        layout["partitions"]["slot0_partition"]["start"] = "0x70010000"
        errors = cl.check(layout)
        self.assertTrue(any("slot0_partition" in e for e in errors), errors)

    def test_missing_retention_fails(self):
        layout = copy.deepcopy(good_layout())
        del layout["retention"]
        self.assertTrue(cl.check(layout))

    def test_freshness_detects_difference(self):
        layout = good_layout()
        stale = copy.deepcopy(layout)
        stale["partitions"]["slot0_partition"]["size"] = "0x00400000"
        self.assertFalse(cl.same(gl.dumps(layout), gl.dumps(stale)))
        self.assertTrue(cl.same(gl.dumps(layout), gl.dumps(copy.deepcopy(layout))))


if __name__ == "__main__":
    unittest.main()
