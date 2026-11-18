// SPDX-License-Identifier: Apache-2.0

package image

import "testing"

func TestCheckUploadAcceptsMCUbootImageWithinSlot(t *testing.T) {
	img := []byte{0x3d, 0xb8, 0xf3, 0x96, 0, 0, 0, 0}
	if err := CheckUpload(img, 16); err != nil {
		t.Fatalf("CheckUpload: %v", err)
	}
}

func TestCheckUploadRefusesOversizeImage(t *testing.T) {
	img := make([]byte, 17)
	copy(img, []byte{0x3d, 0xb8, 0xf3, 0x96})
	if err := CheckUpload(img, 16); err == nil {
		t.Fatal("expected an error for an image larger than the slot")
	}
}

func TestCheckUploadRefusesNonMCUbootImage(t *testing.T) {
	if err := CheckUpload([]byte{1, 2, 3, 4, 5}, 16); err == nil {
		t.Fatal("expected an error for a file without the MCUboot magic")
	}
}
