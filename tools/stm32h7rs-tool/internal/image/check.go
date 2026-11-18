// SPDX-License-Identifier: Apache-2.0

package image

import (
	"encoding/binary"
	"fmt"
)

// MCUbootMagic is the little-endian magic at the start of every MCUboot image header.
const MCUbootMagic = 0x96f3b83d

// CheckUpload refuses a file that is not an MCUboot image or does not fit the program slot.
func CheckUpload(img []byte, slotSize int) error {
	if len(img) < 4 || binary.LittleEndian.Uint32(img) != MCUbootMagic {
		return fmt.Errorf("not a signed MCUboot image (no 0x%08x magic at offset 0)", uint32(MCUbootMagic))
	}
	if len(img) > slotSize {
		return fmt.Errorf("image is %d bytes, larger than the %d-byte program slot", len(img), slotSize)
	}
	return nil
}
