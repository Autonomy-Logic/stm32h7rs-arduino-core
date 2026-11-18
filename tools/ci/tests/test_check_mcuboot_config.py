# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_mcuboot_config as cmc  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def load(name):
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as handle:
        return cmc.parse_config(handle.read())


class CheckMcubootConfigTest(unittest.TestCase):
    def test_good_config_passes(self):
        self.assertEqual(cmc.check(load("mcuboot_good.config")), [])

    def test_missing_single_slot_fails(self):
        errors = cmc.check(load("mcuboot_no_single_slot.config"))
        self.assertTrue(any("CONFIG_SINGLE_APPLICATION_SLOT" in e for e in errors), errors)

    def test_encryption_enabled_fails(self):
        config = load("mcuboot_good.config")
        config["CONFIG_BOOT_ENCRYPT_IMAGE"] = "y"
        errors = cmc.check(config)
        self.assertTrue(any("CONFIG_BOOT_ENCRYPT_IMAGE" in e for e in errors), errors)

    def test_wrong_signature_type_fails(self):
        config = load("mcuboot_good.config")
        del config["CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256"]
        config["CONFIG_BOOT_SIGNATURE_TYPE_NONE"] = "y"
        errors = cmc.check(config)
        self.assertTrue(any("ECDSA_P256" in e for e in errors), errors)

    def test_wrong_key_file_fails(self):
        config = load("mcuboot_good.config")
        config["CONFIG_BOOT_SIGNATURE_KEY_FILE"] = '"root-ec-p256.pem"'
        errors = cmc.check(config)
        self.assertTrue(any("KEY_FILE" in e for e in errors), errors)

    def test_vendor_id_must_be_set(self):
        config = load("mcuboot_good.config")
        config["CONFIG_BOOT_SERIAL_CDC_ACM_VID"] = "0x2fe4"
        errors = cmc.check(config)
        self.assertTrue(any("VID" in e for e in errors), errors)

    def test_button_entrance_enabled_fails(self):
        config = load("mcuboot_good.config")
        config["CONFIG_BOOT_SERIAL_ENTRANCE_GPIO"] = "y"
        errors = cmc.check(config)
        self.assertTrue(any("CONFIG_BOOT_SERIAL_ENTRANCE_GPIO" in e for e in errors), errors)

    def test_parse_ignores_comments_and_unset(self):
        parsed = cmc.parse_config("# CONFIG_FOO is not set\nCONFIG_BAR=y\n\n")
        self.assertEqual(parsed, {"CONFIG_BAR": "y"})

    def test_main_returns_nonzero_on_failure(self):
        code = cmc.main([os.path.join(FIXTURES, "mcuboot_no_single_slot.config")])
        self.assertNotEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
