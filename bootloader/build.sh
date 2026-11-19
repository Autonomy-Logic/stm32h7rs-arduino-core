#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) Autonomy Logic
# Builds the network bootloader of every device in bootloader/devices (or the devices named as arguments) into
# build/bootloader/<device>/bootloader.{elf,bin,hex,map}, then runs the host tests.
# Needs the west workspace (modules/hal/stm32, modules/hal/cmsis*) and ZEPHYR_SDK_INSTALL_DIR.

set -euo pipefail

CORE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_DIR="$(cd "${CORE_DIR}/.." && pwd)"
CROSS="${ZEPHYR_SDK_INSTALL_DIR:?set ZEPHYR_SDK_INSTALL_DIR}/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-"
HAL_DIR="${WS_DIR}/modules/hal/stm32/stm32cube/stm32h7rsxx"
CMSIS_DIR="${WS_DIR}/modules/hal/cmsis_6/CMSIS/Core"
[ -d "${CMSIS_DIR}" ] || CMSIS_DIR="${WS_DIR}/modules/hal/cmsis/CMSIS/Core"

if [ $# -gt 0 ]; then
	DEVICES=("$@")
else
	DEVICES=()
	for mk in "${CORE_DIR}"/bootloader/devices/*.mk; do
		DEVICES+=("$(basename "${mk}" .mk)")
	done
fi

for device in "${DEVICES[@]}"; do
	make -C "${CORE_DIR}/bootloader" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" DEVICE="${device}" CROSS="${CROSS}" \
		HAL_DIR="${HAL_DIR}" CMSIS_DIR="${CMSIS_DIR}" OUT="${CORE_DIR}/build/bootloader/${device}"
	echo "Bootloader output: ${CORE_DIR}/build/bootloader/${device}"
done

make -C "${CORE_DIR}/bootloader/tests" check-c
