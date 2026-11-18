#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Builds MCUboot for the STM32H7S7 core into build/mcuboot (zephyr.bin, zephyr.map, .config, zephyr.dts).

set -euo pipefail

CORE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_DIR="$(cd "${CORE_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${CORE_DIR}/build/mcuboot}"
KEY_FILE="${CORE_DIR}/bootloader/keys/stm32h7rs-dev-ecdsa-p256.pem"

west build -p always -b stm32h7s7_core -d "${BUILD_DIR}" "${WS_DIR}/bootloader/mcuboot/boot/zephyr" -- \
	-DBOARD_ROOT="${CORE_DIR}" \
	-DEXTRA_CONF_FILE="${CORE_DIR}/bootloader/mcuboot.conf;${CORE_DIR}/bootloader/usb_id.conf" \
	-DEXTRA_DTC_OVERLAY_FILE="${CORE_DIR}/bootloader/mcuboot.overlay" \
	-DCONFIG_BOOT_SIGNATURE_KEY_FILE=\""${KEY_FILE}"\"

echo "MCUboot output: ${BUILD_DIR}/zephyr"
