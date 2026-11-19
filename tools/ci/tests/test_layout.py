# SPDX-License-Identifier: Apache-2.0

import contextlib
import copy
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_layout as cl  # noqa: E402
import gen_layout as gl  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CONTRACT = os.path.join(ROOT, "variants", "stm32h7s7_core_common", "stm32h7rs_boot.h")
COMMITTED = os.path.join(ROOT, "tools", "ci", "layout.json")


def good_layout():
    with open(os.path.join(FIXTURES, "layout_good.dts"), encoding="utf-8") as handle:
        return gl.layout_from_dts(handle.read())


class GenLayoutTest(unittest.TestCase):
    def test_partitions_have_absolute_addresses(self):
        layout = good_layout()
        parts = layout["partitions"]
        self.assertEqual(parts["boot_partition"]["start"], "0x08000000")
        self.assertEqual(parts["boot_partition"]["size"], "0x0000e000")
        self.assertEqual(parts["boot_partition"]["device"], "flash0")
        self.assertEqual(parts["netcfg_partition"]["start"], "0x0800e000")
        self.assertEqual(parts["netcfg_partition"]["offset"], "0x0000e000")
        self.assertEqual(parts["netcfg_partition"]["size"], "0x00002000")
        self.assertEqual(parts["netcfg_partition"]["device"], "flash0")
        self.assertEqual(parts["slot0_partition"]["start"], "0x70000000")
        self.assertEqual(parts["slot0_partition"]["device"], "ext_flash")
        self.assertEqual(parts["hmi_store_partition"]["start"], "0x70800000")
        self.assertEqual(parts["hmi_store_partition"]["offset"], "0x00800000")

    def test_devices_carry_erase_block(self):
        layout = good_layout()
        self.assertEqual(layout["devices"]["ext_flash"]["erase_block"], "0x00001000")
        self.assertEqual(layout["devices"]["flash0"]["start"], "0x08000000")

    def test_mailbox(self):
        layout = good_layout()
        self.assertEqual(layout["mailbox"], {"start": "0x24071f00", "size": "0x00000100"})

    def test_missing_mailbox_node_is_not_recorded(self):
        with open(os.path.join(FIXTURES, "layout_good.dts"), encoding="utf-8") as handle:
            text = handle.read().replace("boot_mailbox: memory@24071f00", "other: memory@24071f00")
        self.assertNotIn("mailbox", gl.layout_from_dts(text))

    def test_committed_layout_matches_generator_output_for_the_loader_shape(self):
        layout = with_loader(good_layout())
        with open(COMMITTED, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), gl.dumps(layout))

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
        layout["partitions"]["boot_partition"]["size"] = "0x00010000"
        errors = cl.check(layout)
        self.assertTrue(any("boot_partition" in e for e in errors), errors)

    def test_netcfg_partition_must_be_exact(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["netcfg_partition"]["offset"] = "0x0000c000"
        layout["partitions"]["netcfg_partition"]["start"] = "0x0800c000"
        errors = cl.check(layout)
        self.assertTrue(any("netcfg_partition" in e for e in errors), errors)

    def test_missing_netcfg_partition_fails(self):
        layout = copy.deepcopy(good_layout())
        del layout["partitions"]["netcfg_partition"]
        errors = cl.check(layout)
        self.assertTrue(any("netcfg_partition" in e for e in errors), errors)

    def test_netcfg_on_another_device_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["netcfg_partition"]["device"] = "ext_flash"
        errors = cl.check(layout)
        self.assertTrue(any("same internal flash" in e for e in errors), errors)

    def test_third_internal_partition_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["partitions"]["storage_partition"] = {
            "device": "flash0", "offset": "0x0000c000", "start": "0x0800c000", "size": "0x00002000",
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

    def test_mailbox_at_wrong_address_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["mailbox"]["start"] = "0x24071e00"
        self.assertTrue(any("mailbox" in e for e in cl.check(layout)))

    def test_mailbox_with_wrong_size_fails(self):
        layout = copy.deepcopy(good_layout())
        layout["mailbox"]["size"] = "0x00000080"
        self.assertTrue(any("mailbox" in e for e in cl.check(layout)))

    def test_missing_mailbox_fails(self):
        layout = copy.deepcopy(good_layout())
        del layout["mailbox"]
        self.assertTrue(any("mailbox" in e for e in cl.check(layout)))

    def test_compare_finds_slot_difference(self):
        a = good_layout()
        b = copy.deepcopy(a)
        b["partitions"]["slot0_partition"]["size"] = "0x00080000"
        errors = cl.compare({"ext_flash_app": a, "sl_visu_5r": b})
        self.assertTrue(any("slot0_partition" in e for e in errors), errors)

    def test_compare_ignores_loader_only_partitions(self):
        a = good_layout()
        b = with_loader(a)
        self.assertEqual(cl.compare({"ext_flash_app": a, "sl_visu_5r": b}), [])

    def test_compare_finds_netcfg_difference(self):
        a = good_layout()
        b = copy.deepcopy(a)
        b["partitions"]["netcfg_partition"]["size"] = "0x00001000"
        errors = cl.compare({"ext_flash_app": a, "sl_visu_5r": b})
        self.assertTrue(any("netcfg_partition" in e for e in errors), errors)

    def test_compare_finds_mailbox_difference(self):
        a = good_layout()
        b = copy.deepcopy(a)
        b["mailbox"]["start"] = "0x24071e00"
        errors = cl.compare({"ext_flash_app": a, "sl_visu_5r": b})
        self.assertTrue(any("mailbox" in e for e in errors), errors)

    def test_freshness_detects_difference(self):
        layout = good_layout()
        stale = copy.deepcopy(layout)
        stale["partitions"]["slot0_partition"]["size"] = "0x00400000"
        self.assertFalse(cl.same(gl.dumps(layout), gl.dumps(stale)))
        self.assertTrue(cl.same(gl.dumps(layout), gl.dumps(copy.deepcopy(layout))))


def contract():
    with open(CONTRACT, encoding="utf-8") as handle:
        return cl.parse_contract(handle.read())


class ContractTest(unittest.TestCase):
    def test_parse_contract_reads_hex_and_decimal_literals(self):
        values = cl.parse_contract(
            "#define STM32H7RS_BOOT_BASE           0x08000000u\n"
            "#define STM32H7RS_NETCFG_WRITE_BLOCK  32u\n"
            "#define STM32H7RS_IMAGE_MIN_SIZE      (STM32H7RS_IMAGE_HEADER_SIZE + 8u)\n"
            "#define OTHER_VALUE 0x1u\n"
        )
        self.assertEqual(values, {"BOOT_BASE": 0x08000000, "NETCFG_WRITE_BLOCK": 32})

    def test_real_contract_matches_loader_layout(self):
        self.assertEqual(cl.check_contract(with_loader(good_layout()), contract()), [])

    def test_real_contract_matches_committed_layout(self):
        with open(COMMITTED, encoding="utf-8") as handle:
            committed = cl.json.loads(handle.read())
        self.assertEqual(cl.check_contract(committed, contract()), [])

    def test_each_contract_value_is_checked(self):
        for name in (
            "BOOT_BASE", "BOOT_SIZE", "NETCFG_ADDR", "NETCFG_SECTOR_SIZE", "SLOT_BASE", "SLOT_SIZE",
            "MAILBOX_ADDR", "MAILBOX_SIZE", "IMAGE_HEADER_SIZE", "EXT_ERASE_SIZE",
        ):
            values = contract()
            values[name] += 0x100
            errors = cl.check_contract(with_loader(good_layout()), values)
            self.assertTrue(any(f"STM32H7RS_{name} " in e for e in errors), (name, errors))

    def test_missing_contract_value_fails(self):
        values = contract()
        del values["MAILBOX_ADDR"]
        errors = cl.check_contract(with_loader(good_layout()), values)
        self.assertTrue(any("STM32H7RS_MAILBOX_ADDR is missing" in e for e in errors), errors)

    def test_layout_without_header_size_fails(self):
        errors = cl.check_contract(good_layout(), contract())
        self.assertTrue(any("IMAGE_HEADER_SIZE" in e for e in errors), errors)

    def test_command_line_contract_check(self):
        with open(CONTRACT, encoding="utf-8") as handle:
            text = handle.read()
        changed = text.replace("STM32H7RS_SLOT_SIZE           0x00800000u", "STM32H7RS_SLOT_SIZE           0x00400000u")
        self.assertNotEqual(text, changed)
        with tempfile.TemporaryDirectory() as tmp:
            layout_path = os.path.join(tmp, "layout.json")
            header_path = os.path.join(tmp, "stm32h7rs_boot.h")
            with open(layout_path, "w", encoding="utf-8") as handle:
                handle.write(gl.dumps(with_loader(good_layout())))
            with open(header_path, "w", encoding="utf-8") as handle:
                handle.write(changed)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cl.main([layout_path, "--contract", CONTRACT]), 0)
                self.assertEqual(cl.main([layout_path, "--contract", header_path]), 1)
            self.assertIn("STM32H7RS_SLOT_SIZE", out.getvalue())


if __name__ == "__main__":
    unittest.main()
