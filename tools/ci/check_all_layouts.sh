#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Checks the committed layout.json against the MCUboot build and every loader build: rules, freshness, agreement
# between builds on every shared partition and the retention region, and the boards.txt image values.
# Run from the core root after bootloader/build.sh and extra/build.sh for every board.

set -euo pipefail

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HS=$(grep '^CONFIG_ROM_START_OFFSET=' firmwares/zephyr-stm32h7s7_core_stm32h7s7xx_sl_visu_5r.config | cut -d= -f2)

python3 tools/ci/gen_layout.py build/mcuboot/zephyr/zephyr.dts --out "$TMP/mcuboot.json"
COMPARE=(--compare "$TMP/mcuboot.json")
for v in stm32h7s7_core_stm32h7s7xx_ext_flash_app stm32h7s7_core_stm32h7s7xx_sl_visu_5r; do
	python3 tools/ci/gen_layout.py "firmwares/zephyr-$v.dts" --header-size "$HS" --out "$TMP/$v.json"
	python3 tools/ci/gen_layout.py build/mcuboot/zephyr/zephyr.dts "firmwares/zephyr-$v.dts" --header-size "$HS" \
		--out "$TMP/$v.merged.json"
	python3 tools/ci/check_layout.py tools/ci/layout.json --fresh "$TMP/$v.merged.json"
	COMPARE+=(--compare "$TMP/$v.json")
done
python3 tools/ci/check_layout.py tools/ci/layout.json "${COMPARE[@]}" --boards boards.txt --board stm32h7s7 --board sl_visu_5r
