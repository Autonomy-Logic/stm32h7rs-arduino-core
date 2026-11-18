// SPDX-License-Identifier: Apache-2.0

// Package smp implements the subset of the MCUmgr SMP protocol over the serial line framing that MCUboot serial
// recovery accepts: image upload, image state read and OS reset.
package smp

import (
	"bufio"
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
)

const (
	// MaxLineLen is the longest line, newline included, MCUboot accepts (BOOT_MAX_LINE_INPUT_LEN).
	MaxLineLen = 128
	// MaxDecodedFrame is the largest decoded frame (length, packet, CRC) MCUboot accepts (BOOT_SERIAL_MAX_RECEIVE_SIZE).
	MaxDecodedFrame = 1024
	// maxChunkChars is the base64 characters per line: a multiple of 4 that fits the marker and newline.
	maxChunkChars = 124
)

var (
	markerStart    = []byte{0x06, 0x09}
	markerContinue = []byte{0x04, 0x14}
)

// crc16 is CRC-16/XMODEM (CCITT polynomial 0x1021, initial value 0), the CRC MCUboot checks.
func crc16(data []byte) uint16 {
	var crc uint16
	for _, b := range data {
		crc ^= uint16(b) << 8
		for i := 0; i < 8; i++ {
			if crc&0x8000 != 0 {
				crc = crc<<1 ^ 0x1021
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

// EncodeFrame wraps an SMP packet as MCUboot's serial framing: length, packet and CRC, base64 encoded in lines.
func EncodeFrame(packet []byte) []byte {
	raw := make([]byte, 2, len(packet)+4)
	binary.BigEndian.PutUint16(raw, uint16(len(packet)+2))
	raw = append(raw, packet...)
	raw = binary.BigEndian.AppendUint16(raw, crc16(packet))
	text := base64.StdEncoding.EncodeToString(raw)
	var out bytes.Buffer
	for i := 0; i < len(text); i += maxChunkChars {
		end := min(i+maxChunkChars, len(text))
		if i == 0 {
			out.Write(markerStart)
		} else {
			out.Write(markerContinue)
		}
		out.WriteString(text[i:end])
		out.WriteByte('\n')
	}
	return out.Bytes()
}

// FrameReader decodes SMP packets from a serial byte stream, skipping lines that are not frames.
type FrameReader struct {
	r        *bufio.Reader
	consumed int
}

// NewFrameReader returns a FrameReader on r.
func NewFrameReader(r io.Reader) *FrameReader {
	return &FrameReader{r: bufio.NewReader(r)}
}

// Consumed returns the number of bytes of complete lines read so far.
func (f *FrameReader) Consumed() int {
	return f.consumed
}

// ReadPacket returns the next SMP packet whose frame decodes with a valid length and CRC.
func (f *FrameReader) ReadPacket() ([]byte, error) {
	var raw []byte
	inFrame := false
	for {
		line, err := f.r.ReadBytes('\n')
		if err != nil {
			if err == io.EOF && len(line) == 0 && !inFrame {
				return nil, io.EOF
			}
			if err == io.EOF {
				return nil, io.ErrUnexpectedEOF
			}
			return nil, err
		}
		f.consumed += len(line)
		line = bytes.TrimRight(line, "\r\n")
		switch {
		case bytes.HasPrefix(line, markerStart):
			raw, inFrame = nil, true
		case bytes.HasPrefix(line, markerContinue) && inFrame:
		default:
			continue
		}
		dec, err := base64.StdEncoding.DecodeString(string(line[2:]))
		if err != nil {
			return nil, fmt.Errorf("smp: bad base64 in frame: %w", err)
		}
		raw = append(raw, dec...)
		if len(raw) < 2 {
			continue
		}
		want := int(binary.BigEndian.Uint16(raw[:2]))
		if len(raw)-2 < want {
			continue
		}
		body := raw[2 : 2+want]
		if want < 2 || crc16(body) != 0 {
			return nil, errors.New("smp: frame CRC mismatch")
		}
		return body[:want-2], nil
	}
}
