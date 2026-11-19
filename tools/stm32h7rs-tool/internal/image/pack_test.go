// SPDX-License-Identifier: Apache-2.0

package image

import (
	"bytes"
	"encoding/binary"
	"hash/crc32"
	"strings"
	"testing"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
)

const testSize = layout.ImageHeaderSize + 4096

// merged returns an unpacked image: a zero header area, then a vector table and filler.
func merged(sp, reset uint32) []byte {
	b := make([]byte, testSize)
	binary.LittleEndian.PutUint32(b[layout.ImageHeaderSize:], sp)
	binary.LittleEndian.PutUint32(b[layout.ImageHeaderSize+4:], reset)
	for i := layout.ImageHeaderSize + 8; i < len(b); i++ {
		b[i] = byte(i * 7)
	}
	return b
}

const goodReset = layout.SlotBase + layout.ImageHeaderSize + 0x201

func packed(t *testing.T) []byte {
	t.Helper()
	img, err := Pack(merged(layout.AXISRAMEnd-0x100, goodReset), "0.2.0")
	if err != nil {
		t.Fatalf("Pack: %v", err)
	}
	return img
}

// reseal recomputes both CRCs after a test changed the image.
func reseal(img []byte) {
	binary.LittleEndian.PutUint32(img[16:], crc32.ChecksumIEEE(img[layout.ImageHeaderSize:]))
	binary.LittleEndian.PutUint32(img[56:], crc32.ChecksumIEEE(img[:56]))
}

func TestPackWritesTheHeaderOfTheContract(t *testing.T) {
	in := merged(layout.DTCMEnd, goodReset)
	orig := append([]byte(nil), in...)
	img, err := Pack(in, "0.2.0")
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(in, orig) {
		t.Fatal("Pack modified its input")
	}
	le := binary.LittleEndian
	want := map[int]uint32{
		0:  0x494C504F,
		4:  1,
		8:  0x400,
		12: uint32(len(in)),
		16: crc32.ChecksumIEEE(in[0x400:]),
		20: 0,
		56: crc32.ChecksumIEEE(img[:56]),
	}
	for off, v := range want {
		if got := le.Uint32(img[off:]); got != v {
			t.Errorf("word at %d = 0x%08X, want 0x%08X", off, got, v)
		}
	}
	if string(img[24:29]) != "0.2.0" || !bytes.Equal(img[29:56], make([]byte, 27)) {
		t.Errorf("version field % X", img[24:56])
	}
	if !bytes.Equal(img[60:0x400], make([]byte, 0x400-60)) {
		t.Error("the rest of the header is not zero")
	}
	if !bytes.Equal(img[0x400:], in[0x400:]) {
		t.Error("the program bytes changed")
	}
	if Version(img) != "0.2.0" {
		t.Errorf("Version = %q", Version(img))
	}
	if err := CheckImage(img); err != nil {
		t.Errorf("CheckImage of a packed image: %v", err)
	}
}

func TestPackRefusals(t *testing.T) {
	notEmpty := merged(layout.DTCMEnd, goodReset)
	notEmpty[3] = 1
	cases := []struct {
		name    string
		in      []byte
		version string
	}{
		{"header area not empty", notEmpty, "0.2.0"},
		{"too small", make([]byte, layout.ImageHeaderSize+4), "0.2.0"},
		{"empty version", merged(layout.DTCMEnd, goodReset), ""},
		{"version too long", merged(layout.DTCMEnd, goodReset), strings.Repeat("9", 32)},
		{"implausible vector table", merged(0x08000000, goodReset), "0.2.0"},
	}
	for _, c := range cases {
		if _, err := Pack(c.in, c.version); err == nil {
			t.Errorf("%s: Pack succeeded", c.name)
		}
	}
	if _, err := Pack(merged(layout.DTCMEnd, goodReset), strings.Repeat("9", 31)); err != nil {
		t.Errorf("31-byte version refused: %v", err)
	}
}

func TestCheckImageAcceptsBothRAMs(t *testing.T) {
	for _, sp := range []uint32{layout.DTCMBase + 8, layout.DTCMEnd, layout.AXISRAMBase + 0x1000, layout.AXISRAMEnd} {
		if _, err := Pack(merged(sp, goodReset), "0.2.0"); err != nil {
			t.Errorf("SP 0x%08X: %v", sp, err)
		}
	}
}

func TestCheckImageRefusals(t *testing.T) {
	cases := []struct {
		name   string
		mutate func(img []byte) []byte
	}{
		{"bad magic", func(b []byte) []byte { b[0] ^= 1; reseal(b); return b }},
		{"header version", func(b []byte) []byte { b[4] = 2; reseal(b); return b }},
		{"header size", func(b []byte) []byte { b[8] = 0; b[9] = 2; reseal(b); return b }},
		{"image size field", func(b []byte) []byte { b[12]++; reseal(b); return b }},
		{"header CRC", func(b []byte) []byte { b[56] ^= 1; return b }},
		{"header field changed without its CRC", func(b []byte) []byte { b[24] = 'x'; return b }},
		{"payload CRC", func(b []byte) []byte { b[len(b)-1] ^= 1; return b }},
		{"truncated", func(b []byte) []byte { return b[:len(b)-1] }},
		{"too small", func(b []byte) []byte { return b[:layout.ImageMinSize-1] }},
		{"larger than the slot", func(b []byte) []byte {
			big := make([]byte, layout.SlotSize+1)
			copy(big, b)
			binary.LittleEndian.PutUint32(big[12:], uint32(len(big)))
			reseal(big)
			return big
		}},
		{"stack pointer in flash", func(b []byte) []byte { setVec(b, 0x08010000, goodReset); return b }},
		{"stack pointer between the RAMs", func(b []byte) []byte { setVec(b, 0x20030008, goodReset); return b }},
		{"stack pointer past AXI SRAM", func(b []byte) []byte { setVec(b, layout.AXISRAMEnd+8, goodReset); return b }},
		{"stack pointer not 8-byte aligned", func(b []byte) []byte { setVec(b, layout.DTCMEnd-4, goodReset); return b }},
		{"ARM reset handler", func(b []byte) []byte { setVec(b, layout.DTCMEnd, goodReset-1); return b }},
		{"reset handler linked for address 0x08000000", func(b []byte) []byte {
			setVec(b, layout.DTCMEnd, 0x08000601)
			return b
		}},
		{"reset handler in the header", func(b []byte) []byte {
			setVec(b, layout.DTCMEnd, layout.SlotBase+0x101)
			return b
		}},
		{"reset handler beyond the image", func(b []byte) []byte {
			setVec(b, layout.DTCMEnd, layout.SlotBase+uint32(len(b))+1)
			return b
		}},
	}
	for _, c := range cases {
		img := c.mutate(packed(t))
		if err := CheckImage(img); err == nil {
			t.Errorf("%s: CheckImage accepted the image", c.name)
		}
	}
}

func setVec(img []byte, sp, reset uint32) {
	binary.LittleEndian.PutUint32(img[layout.ImageHeaderSize:], sp)
	binary.LittleEndian.PutUint32(img[layout.ImageHeaderSize+4:], reset)
	reseal(img)
}
