// SPDX-License-Identifier: Apache-2.0

package image

import (
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
)

// Offsets of the image header fields.
const (
	offMagic         = 0
	offHeaderVersion = 4
	offHeaderSize    = 8
	offImageSize     = 12
	offPayloadCRC    = 16
	offFlags         = 20
	offVersion       = 24
	offHeaderCRC     = layout.ImageHeaderCRCLen
)

// CRC32 is the CRC-32 the bootloader uses (IEEE, zlib).
func CRC32(b []byte) uint32 { return crc32.ChecksumIEEE(b) }

// Pack returns merged with the image header written into its first layout.ImageHeaderSize bytes, which the loader
// build leaves zero. version is the core version, at most layout.ImageVersionLen-1 bytes. The result is checked
// with CheckImage. merged is not modified.
func Pack(merged []byte, version string) ([]byte, error) {
	if len(merged) < layout.ImageMinSize {
		return nil, fmt.Errorf("pack: input of %d bytes has no vector table after the 0x%X-byte header",
			len(merged), layout.ImageHeaderSize)
	}
	for _, b := range merged[:layout.ImageHeaderSize] {
		if b != 0 {
			return nil, errors.New("pack: the header area of the input is not empty (already packed, or not a merged image)")
		}
	}
	if version == "" || len(version) >= layout.ImageVersionLen {
		return nil, fmt.Errorf("pack: version %q must be 1 to %d bytes", version, layout.ImageVersionLen-1)
	}
	img := make([]byte, len(merged))
	copy(img, merged)
	h := img[:layout.ImageHeaderSize]
	binary.LittleEndian.PutUint32(h[offMagic:], layout.ImageMagic)
	binary.LittleEndian.PutUint32(h[offHeaderVersion:], layout.ImageHeaderVersion)
	binary.LittleEndian.PutUint32(h[offHeaderSize:], layout.ImageHeaderSize)
	binary.LittleEndian.PutUint32(h[offImageSize:], uint32(len(img)))
	binary.LittleEndian.PutUint32(h[offPayloadCRC:], CRC32(img[layout.ImageHeaderSize:]))
	binary.LittleEndian.PutUint32(h[offFlags:], 0)
	copy(h[offVersion:offVersion+layout.ImageVersionLen], version)
	binary.LittleEndian.PutUint32(h[offHeaderCRC:], CRC32(h[:layout.ImageHeaderCRCLen]))
	if err := CheckImage(img); err != nil {
		return nil, fmt.Errorf("pack: %w", err)
	}
	return img, nil
}

// Version returns the version string of a packed image header.
func Version(img []byte) string {
	if len(img) < offVersion+layout.ImageVersionLen {
		return ""
	}
	v := img[offVersion : offVersion+layout.ImageVersionLen]
	for i, b := range v {
		if b == 0 {
			return string(v[:i])
		}
	}
	return string(v)
}

// CheckImage returns why img is not a program image the bootloader would start, or nil. It makes the checks of
// docs/network-bootloader.md: header fields, both CRCs, the slot size and a plausible vector table.
func CheckImage(img []byte) error {
	if len(img) < layout.ImageMinSize {
		return fmt.Errorf("the file is %d bytes, too small to be a program image", len(img))
	}
	if len(img) > layout.SlotSize {
		return fmt.Errorf("the image is %d bytes, larger than the %d-byte program slot", len(img), layout.SlotSize)
	}
	h := img[:layout.ImageHeaderSize]
	word := func(off int) uint32 { return binary.LittleEndian.Uint32(h[off:]) }
	if m := word(offMagic); m != layout.ImageMagic {
		return fmt.Errorf("not a program image of this core: magic 0x%08X, want 0x%08X (was it built with "+
			"stm32h7rs-tool pack?)", m, uint32(layout.ImageMagic))
	}
	if v := word(offHeaderVersion); v != layout.ImageHeaderVersion {
		return fmt.Errorf("image header version %d is not supported (want %d)", v, layout.ImageHeaderVersion)
	}
	if hc, want := word(offHeaderCRC), CRC32(h[:layout.ImageHeaderCRCLen]); hc != want {
		return fmt.Errorf("image header CRC-32 0x%08X does not match its contents (0x%08X)", hc, want)
	}
	if s := word(offHeaderSize); s != layout.ImageHeaderSize {
		return fmt.Errorf("image header size 0x%X, want 0x%X", s, layout.ImageHeaderSize)
	}
	if s := word(offImageSize); s != uint32(len(img)) {
		return fmt.Errorf("the header gives an image of %d bytes, the file has %d (truncated or padded?)", s, len(img))
	}
	if pc, want := word(offPayloadCRC), CRC32(img[layout.ImageHeaderSize:]); pc != want {
		return fmt.Errorf("image CRC-32 0x%08X does not match its contents (0x%08X): the file is damaged", pc, want)
	}
	return checkVectors(img)
}

func inRange(v, base, end uint32) bool { return v >= base && v <= end }

// checkVectors checks the initial stack pointer and the reset handler of the vector table after the header.
func checkVectors(img []byte) error {
	sp := binary.LittleEndian.Uint32(img[layout.ImageHeaderSize:])
	reset := binary.LittleEndian.Uint32(img[layout.ImageHeaderSize+4:])
	inRAM := inRange(sp, layout.DTCMBase, layout.DTCMEnd) || inRange(sp, layout.AXISRAMBase, layout.AXISRAMEnd)
	if !inRAM || sp&7 != 0 {
		return fmt.Errorf("implausible vector table: initial stack pointer 0x%08X is not 8-byte aligned in DTCM "+
			"or AXI SRAM", sp)
	}
	entry := reset &^ 1
	first := uint32(layout.SlotBase + layout.ImageMinSize)
	if reset&1 == 0 || entry < first || entry >= layout.SlotBase+uint32(len(img)) {
		return fmt.Errorf("implausible vector table: reset handler 0x%08X is not a Thumb address inside the image "+
			"at 0x%08X (was it linked for another address?)", reset, uint32(layout.SlotBase))
	}
	return nil
}
