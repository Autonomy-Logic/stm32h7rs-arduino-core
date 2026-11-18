// SPDX-License-Identifier: Apache-2.0

package smp

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"io"
	"strings"
	"testing"
)

func TestCRC16MatchesXmodemCheckValue(t *testing.T) {
	if got := crc16([]byte("123456789")); got != 0x31C3 {
		t.Fatalf("crc16 = 0x%04X, want 0x31C3", got)
	}
}

func TestHeaderEncoding(t *testing.T) {
	h := Header{Op: OpWrite, Length: 0x0102, Group: GroupImage, Seq: 7, ID: IDImageUpload}
	want := []byte{2, 0, 1, 2, 0, 1, 7, 1}
	if got := h.Bytes(); !bytes.Equal(got, want) {
		t.Fatalf("got % x, want % x", got, want)
	}
}

func TestEncodeFrameSplitsIntoShortLinesWithMarkers(t *testing.T) {
	packet := bytes.Repeat([]byte{0xAB}, 300)
	lines := strings.Split(strings.TrimSuffix(string(EncodeFrame(packet)), "\n"), "\n")
	if len(lines) < 2 {
		t.Fatalf("expected several lines, got %d", len(lines))
	}
	var b64 strings.Builder
	for i, line := range lines {
		if len(line)+1 > MaxLineLen {
			t.Fatalf("line %d is %d bytes with newline, limit %d", i, len(line)+1, MaxLineLen)
		}
		marker := line[:2]
		if i == 0 && marker != "\x06\x09" || i > 0 && marker != "\x04\x14" {
			t.Fatalf("line %d marker % x", i, []byte(marker))
		}
		chunk := line[2:]
		if i < len(lines)-1 && len(chunk)%4 != 0 {
			t.Fatalf("line %d base64 length %d not a multiple of 4", i, len(chunk))
		}
		dec, err := base64.StdEncoding.DecodeString(chunk)
		if err != nil {
			t.Fatalf("line %d does not decode on its own: %v", i, err)
		}
		b64.Write(dec)
	}
	raw := []byte(b64.String())
	n := binary.BigEndian.Uint16(raw[:2])
	if int(n) != len(raw)-2 {
		t.Fatalf("length field %d, payload+crc %d", n, len(raw)-2)
	}
	if !bytes.Equal(raw[2:len(raw)-2], packet) {
		t.Fatal("payload changed")
	}
	if crc16(raw[2:]) != 0 {
		t.Fatal("crc over payload+crc must be zero")
	}
}

func TestDecodeFrameRoundTrip(t *testing.T) {
	packet := []byte("hello smp packet")
	r := NewFrameReader(bytes.NewReader(EncodeFrame(packet)))
	got, err := r.ReadPacket()
	if err != nil {
		t.Fatalf("ReadPacket: %v", err)
	}
	if !bytes.Equal(got, packet) {
		t.Fatalf("got %q", got)
	}
}

func TestDecodeFrameSkipsNoise(t *testing.T) {
	stream := append([]byte("boot log line\n"), EncodeFrame([]byte{1, 2, 3})...)
	got, err := NewFrameReader(bytes.NewReader(stream)).ReadPacket()
	if err != nil || !bytes.Equal(got, []byte{1, 2, 3}) {
		t.Fatalf("got % x, %v", got, err)
	}
}

func TestDecodeFrameRejectsBadCRC(t *testing.T) {
	frame := EncodeFrame([]byte{1, 2, 3, 4})
	frame[5] ^= 0x01
	if _, err := NewFrameReader(bytes.NewReader(frame)).ReadPacket(); err == nil {
		t.Fatal("expected an error for a corrupted frame")
	}
}

func TestDecodeFrameEOF(t *testing.T) {
	if _, err := NewFrameReader(bytes.NewReader(nil)).ReadPacket(); err != io.EOF {
		t.Fatalf("got %v, want io.EOF", err)
	}
}
