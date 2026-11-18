// SPDX-License-Identifier: Apache-2.0

package smp

import "encoding/binary"

// HeaderLen is the size of an SMP header.
const HeaderLen = 8

// SMP operations, groups and command IDs used by this tool.
const (
	OpRead        uint8  = 0
	OpWrite       uint8  = 2
	GroupOS       uint16 = 0
	GroupImage    uint16 = 1
	IDImageState  uint8  = 0
	IDImageUpload uint8  = 1
	IDOSReset     uint8  = 5
)

// Header is an SMP packet header.
type Header struct {
	Op     uint8
	Flags  uint8
	Length uint16
	Group  uint16
	Seq    uint8
	ID     uint8
}

// Bytes returns the header in wire order (big endian).
func (h Header) Bytes() []byte {
	b := make([]byte, HeaderLen)
	b[0], b[1] = h.Op, h.Flags
	binary.BigEndian.PutUint16(b[2:], h.Length)
	binary.BigEndian.PutUint16(b[4:], h.Group)
	b[6], b[7] = h.Seq, h.ID
	return b
}

// ParseHeader reads the header at the start of an SMP packet; the packet must hold at least HeaderLen bytes.
func ParseHeader(p []byte) Header {
	return Header{
		Op: p[0], Flags: p[1], Length: binary.BigEndian.Uint16(p[2:]),
		Group: binary.BigEndian.Uint16(p[4:]), Seq: p[6], ID: p[7],
	}
}
