#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Assembles a release directory: core archive, tool archives, MCUboot binary, documents, index and SHA-256 list.
# Run from the core root after bootloader/build.sh and extra/build.sh for every board of the core.
# Usage: tools/ci/build_release.sh <version> <base-url> [<output dir>]

set -euo pipefail

VERSION=$1
BASE_URL=$2
OUT=${3:-distrib/release-$VERSION}
GOHOSTS="x86_64-pc-linux-gnu:linux:amd64 aarch64-linux-gnu:linux:arm64 x86_64-apple-darwin:darwin:amd64 arm64-apple-darwin:darwin:arm64 x86_64-mingw32:windows:amd64"

rm -rf "$OUT"
mkdir -p "$OUT"
export PINNED_CORE_VERSION=$VERSION

extra/package_core.sh zephyr_h7rs "$VERSION" "$OUT/zephyr_h7rs-$VERSION.tar.bz2"

STAGE=$(mktemp -d)
for entry in $GOHOSTS; do
	IFS=: read -r host goos goarch <<< "$entry"
	ext=""
	[ "$goos" = windows ] && ext=".exe"
	for tool in stm32h7rs-tool stm32h7rs-abuse; do
		dir="$STAGE/$tool-$host/$tool"
		mkdir -p "$dir"
		(cd tools/stm32h7rs-tool && CGO_ENABLED=0 GOOS=$goos GOARCH=$goarch \
			go build -trimpath -ldflags "-s -w -X main.version=$VERSION" -o "$dir/$tool$ext" ./cmd/$tool)
		tar -czf "$OUT/$tool-$VERSION-$host.tar.gz" -C "$STAGE/$tool-$host" "$tool"
	done
done
rm -rf "$STAGE"

cp build/mcuboot/zephyr/zephyr.bin "$OUT/mcuboot-stm32h7s7-$VERSION.bin"
cp build/mcuboot/zephyr/zephyr.hex "$OUT/mcuboot-stm32h7s7-$VERSION.hex"
for doc in docs/mcuboot-install-requirement.md docs/lab-test-guide.md; do
	[ -f "$doc" ] && cp "$doc" "$OUT/"
done

mapfile -t BOARDS < <(extra/get_board_details.sh | jq -r '.[] | select(.artifact == "zephyr_h7rs") | .name')
BOARD_ARGS=()
for b in "${BOARDS[@]}"; do BOARD_ARGS+=(--board "$b"); done
python3 tools/ci/gen_index.py --dir "$OUT" --version "$VERSION" --base-url "$BASE_URL" "${BOARD_ARGS[@]}" \
	--out "$OUT/package_AutonomyLogic_stm32h7rs_index.json"

(cd "$OUT" && sha256sum -- * > SHA256SUMS.txt)
echo "Release directory: $OUT"
ls -la "$OUT"
