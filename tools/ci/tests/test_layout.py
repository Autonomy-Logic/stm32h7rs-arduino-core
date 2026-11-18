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
        self.assertEqual(layout["retention"]["parent"], "zephyr,retained-ram")
        self.assertEqual(layout["retention"]["prefix"], "yes")
        self.assertEqual(layout["retention"]["checksum"], "yes")

    def test_nested_retention_is_recorded(self):
        with open(os.path.join(FIXTURES, "layout_good.dts"), encoding="utf-8") as handle:
            text = handle.read()
        nested = text.replace(
            "\t\t\tboot_mode0: boot_mode@0 {",
            "\t\t\tretention0: retention@0 {\n\t\t\t\tcompatible = \"zephyr,retention\";\n"
            "\t\t\t\treg = < 0x0 0x100 >;\n\t\t\tboot_mode0: boot_mode@0 {",
        ).replace("\t\t\t};\n\t\t};\n\t};\n};", "\t\t\t};\n\t\t\t};\n\t\t};\n\t};\n};")
        layout = gl.layout_from_dts(nested)
        self.assertEqual(layout["retention"]["parent"], "zephyr,retention")

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


def with_loader(layout):
    layout = copy.deepcopy(layout)
    layout["partitions"]["loader_partition"] = {
        "device": "ext_flash", "offset": "0x00000000", "start": "0x70000000", "size": "0x00080000",
    }
    layout["partitions"]["user_sketch"] = {
        "device": "ext_flash", "offset": "0x00080000", "start": "0x70080000", "size": "0x00780000",
    }
    layout["image"] = {"header_size": "0x00000400"}
    return layout


BOARDS = """
sl_visu_5r.build.image.sketch_offset=0x00080000
sl_visu_5r.build.image.header_size=0x400
sl_visu_5r.build.image.slot_size=0x00800000
"""


class LoaderLayoutTest(unittest.TestCase):
    def test_loader_layout_passes(self):
        self.assertEqual(cl.check(with_loader(good_layout())), [])

    def test_user_sketch_overlapping_loader_fails(self):
        layout = with_loader(good_layout())
        layout["partitions"]["user_sketch"]["offset"] = "0x00070000"
        layout["partitions"]["user_sketch"]["start"] = "0x70070000"
        self.assertTrue(any("user_sketch" in e for e in cl.check(layout)))

    def test_loader_not_at_slot_start_fails(self):
        layout = with_loader(good_layout())
        layout["partitions"]["loader_partition"]["offset"] = "0x00001000"
        layout["partitions"]["loader_partition"]["start"] = "0x70001000"
        self.assertTrue(any("loader_partition" in e for e in cl.check(layout)))

    def test_boards_values_match(self):
        self.assertEqual(cl.check_boards(with_loader(good_layout()), BOARDS, "sl_visu_5r"), [])

    def test_boards_sketch_offset_mismatch_fails(self):
        boards = BOARDS.replace("sketch_offset=0x00080000", "sketch_offset=0x00180000")
        errors = cl.check_boards(with_loader(good_layout()), boards, "sl_visu_5r")
        self.assertTrue(any("sketch_offset" in e for e in errors), errors)

    def test_boards_header_mismatch_fails(self):
        boards = BOARDS.replace("header_size=0x400", "header_size=0x200")
        errors = cl.check_boards(with_loader(good_layout()), boards, "sl_visu_5r")
        self.assertTrue(any("header_size" in e for e in errors), errors)

    def test_boards_missing_board_fails(self):
        self.assertTrue(cl.check_boards(with_loader(good_layout()), BOARDS, "other"))


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

    def test_retention_under_another_retention_node_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["retention"]["parent"] = "zephyr,retention"
        self.assertTrue(any("retained-ram" in e for e in cl.check(layout)))

    def test_retention_without_checksum_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["retention"]["checksum"] = "no"
        self.assertTrue(any("checksum" in e for e in cl.check(layout)))

    def test_compare_finds_slot_difference(self):
        a = good_layout()
        b = copy.deepcopy(a)
        b["partitions"]["slot0_partition"]["size"] = "0x00080000"
        errors = cl.compare({"mcuboot": a, "loader": b})
        self.assertTrue(any("slot0_partition" in e for e in errors), errors)

    def test_compare_ignores_loader_only_partitions(self):
        a = good_layout()
        b = with_loader(a)
        self.assertEqual(cl.compare({"mcuboot": a, "loader": b}), [])

    def test_compare_finds_retention_difference(self):
        a = good_layout()
        b = copy.deepcopy(a)
        b["retention"]["start"] = "0x24071e00"
        self.assertTrue(cl.compare({"mcuboot": a, "loader": b}))

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
