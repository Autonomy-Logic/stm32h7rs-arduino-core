// SPDX-License-Identifier: Apache-2.0
// Copyright (c) Autonomy Logic

package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"net"
	"testing"
	"time"
)

// serveModbus answers up to limit requests per connection with answer, then closes the connection.
func serveModbus(t *testing.T, limit int, answer func(pdu []byte) []byte) (string, <-chan []byte) {
	t.Helper()
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	seen := make(chan []byte, 64)
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			go func(c net.Conn) {
				defer c.Close()
				for i := 0; i < limit; i++ {
					var hdr [7]byte
					if _, err := io.ReadFull(c, hdr[:]); err != nil {
						return
					}
					pdu := make([]byte, int(binary.BigEndian.Uint16(hdr[4:]))-1)
					if _, err := io.ReadFull(c, pdu); err != nil {
						return
					}
					select {
					case seen <- append(hdr[:], pdu...):
					default:
					}
					reply := answer(pdu)
					binary.BigEndian.PutUint16(hdr[4:], uint16(1+len(reply)))
					c.Write(append(hdr[:], reply...))
				}
			}(c)
		}
	}()
	return ln.Addr().String(), seen
}

func TestRebootRequestFrameAndReply(t *testing.T) {
	addr, seen := serveModbus(t, 1, func(pdu []byte) []byte { return []byte{pdu[0], 0x7E} })
	pdu, err := rebootRequest(addr)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(pdu, []byte{0x4C, 0x7E}) {
		t.Errorf("reply % X", pdu)
	}
	want := []byte{0, 1, 0, 0, 0, 6, 1, 0x4C, 0xB0, 0x07, 0x10, 0xAD}
	if got := <-seen; !bytes.Equal(got, want) {
		t.Errorf("request % X, want % X", got, want)
	}
}

func TestFloodOneCountsRepliesUntilTheUnitCloses(t *testing.T) {
	addr, _ := serveModbus(t, 5, func(pdu []byte) []byte { return []byte{pdu[0], 20, 0, 0} })
	r := floodOne(addr, time.Now().Add(10*time.Second))
	if r.replies != 5 || !r.closed || r.err != nil {
		t.Errorf("result %+v, want 5 replies and closed", r)
	}
}

func TestFloodOneStopsAtTheDeadline(t *testing.T) {
	addr, _ := serveModbus(t, 1<<30, func(pdu []byte) []byte { return []byte{pdu[0], 20, 0, 0} })
	r := floodOne(addr, time.Now().Add(100*time.Millisecond))
	if r.replies == 0 || r.closed || r.err != nil {
		t.Errorf("result %+v", r)
	}
}

func TestModbusAddrUsesPort502(t *testing.T) {
	if got := modbusAddr("192.168.10.100"); got != "192.168.10.100:502" {
		t.Errorf("modbusAddr = %q", got)
	}
}
