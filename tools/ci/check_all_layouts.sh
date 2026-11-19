#!/bin/bash
# Copyright (c) Autonomy Logic
# SPDX-License-Identifier: Apache-2.0
# Checks the committed layout.json against every loader build: rules, freshness against the sl_visu_5r loader,
# agreement between builds on every shared partition and the boot RAM mailbox, the boards.txt image values and
# the boot contract header. Run from the core root after extra/build.sh for every board.

set -euo pipefail

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HS=$(grep '^CONFIG_ROM_START_OFFSET=' firmwares/zephyr-stm32h7s7_core_stm32h7s7xx_sl_visu_5r.config | cut -d= -f2)

COMPARE=()
for v in stm32h7s7_core_stm32h7s7xx_ext_flash_app stm32h7s7_core_stm32h7s7xx_sl_visu_5r; do
	python3 tools/ci/gen_layout.py "firmwares/zephyr-$v.dts" --header-size "$HS" --out "$TMP/$v.json"
	COMPARE+=(--compare "$TMP/$v.json")
done
python3 tools/ci/check_layout.py tools/ci/layout.json --fresh "$TMP/stm32h7s7_core_stm32h7s7xx_sl_visu_5r.json" \
	"${COMPARE[@]}" --boards boards.txt --board stm32h7s7 --board sl_visu_5r \
	--contract variants/stm32h7s7_core_common/stm32h7rs_boot.h
