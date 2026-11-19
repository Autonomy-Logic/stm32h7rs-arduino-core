// SPDX-License-Identifier: Apache-2.0

package layout

import (
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

// bootHeader is the header the bootloader and the core are built from.
const bootHeader = "../../../../variants/stm32h7s7_core_common/stm32h7rs_boot.h"

// defines returns the literal #define values of bootHeader.
func defines(t *testing.T) map[string]string {
	t.Helper()
	src, err := os.ReadFile(bootHeader)
	if err != nil {
		t.Fatal(err)
	}
	re := regexp.MustCompile(`(?m)^#define\s+(STM32H7RS_\w+)\s+("[^"]*"|0x[0-9A-Fa-f]+u?|\d+u?)(?:\s|$)`)
	out := map[string]string{}
	for _, m := range re.FindAllStringSubmatch(string(src), -1) {
		out[m[1]] = m[2]
	}
	return out
}

func TestConstantsMatchTheBootHeader(t *testing.T) {
	defs := defines(t)
	nums := map[string]uint64{
		"STM32H7RS_SLOT_BASE":            SlotBase,
		"STM32H7RS_SLOT_SIZE":            SlotSize,
		"STM32H7RS_DTCM_BASE":            DTCMBase,
		"STM32H7RS_DTCM_END":             DTCMEnd,
		"STM32H7RS_AXI_SRAM_BASE":        AXISRAMBase,
		"STM32H7RS_AXI_SRAM_END":         AXISRAMEnd,
		"STM32H7RS_IMAGE_HEADER_SIZE":    ImageHeaderSize,
		"STM32H7RS_IMAGE_MAGIC":          ImageMagic,
		"STM32H7RS_IMAGE_HEADER_VER":     ImageHeaderVersion,
		"STM32H7RS_IMAGE_VERSION_LEN":    ImageVersionLen,
		"STM32H7RS_IMAGE_HEADER_CRC_LEN": ImageHeaderCRCLen,
		"STM32H7RS_TFTP_PORT":            TFTPPort,
		"STM32H7RS_TFTP_MAX_BLKSIZE":     MaxBlksize,
		"STM32H7RS_DISCOVERY_PORT":       DiscoveryPort,
		"STM32H7RS_MODBUS_PORT":          ModbusPort,
		"STM32H7RS_MODBUS_FC_BOOTLOADER": ModbusFCBootloader,
	}
	for name, want := range nums {
		v, ok := defs[name]
		if !ok {
			t.Errorf("%s not found in %s", name, bootHeader)
			continue
		}
		got, err := strconv.ParseUint(strings.TrimSuffix(v, "u"), 0, 64)
		if err != nil || got != want {
			t.Errorf("%s = %s in the boot header, layout.go has 0x%X", name, v, want)
		}
	}
	strs := map[string]string{
		"STM32H7RS_TFTP_FILE_FIRMWARE": FileFirmware,
		"STM32H7RS_TFTP_FILE_INFO":     FileInfo,
		"STM32H7RS_TFTP_OPT_CRC32":     OptCRC32,
	}
	for name, want := range strs {
		v, ok := defs[name]
		if !ok {
			t.Errorf("%s not found in %s", name, bootHeader)
			continue
		}
		if got := strings.Trim(v, `"`); got != want {
			t.Errorf("%s = %q in the boot header, layout.go has %q", name, got, want)
		}
	}
}

// STM32H7RS_IMAGE_MIN_SIZE is an expression, so it is checked by its text.
func TestImageMinSizeIsTheHeaderPlusTwoVectors(t *testing.T) {
	src, err := os.ReadFile(bootHeader)
	if err != nil {
		t.Fatal(err)
	}
	re := regexp.MustCompile(`(?m)^#define\s+STM32H7RS_IMAGE_MIN_SIZE\s+\(STM32H7RS_IMAGE_HEADER_SIZE \+ 8u\)`)
	if !re.Match(src) || ImageMinSize != ImageHeaderSize+8 {
		t.Fatal("STM32H7RS_IMAGE_MIN_SIZE is no longer the header size plus 8")
	}
}
