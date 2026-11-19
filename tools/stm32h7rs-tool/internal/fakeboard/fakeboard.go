// SPDX-License-Identifier: Apache-2.0
// Copyright (c) Autonomy Logic

// Package fakeboard is a test double for an STM32H7S7 device on local sockets: the running OpenPLC program on
// Modbus TCP and the network bootloader on TFTP, following docs/network-bootloader.md.
package fakeboard

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
)

// Defaults of Options.
const (
	DefaultDevice  = "SL-VISU-5R"
	DefaultCore    = "stm32h7rs"
	DefaultRuntime = "4.2.7"
)

// busyWindow is how long a transfer without packets still refuses other clients.
const busyWindow = 3 * time.Second

// Options shape the device.
type Options struct {
	// Device is the info file's device value; empty means DefaultDevice.
	Device string
	// Core is the info file's core value; empty means DefaultCore.
	Core string
	// StartInBootloader starts with no program running.
	StartInBootloader bool
	// DropEvery drops every n-th TFTP packet the device receives; 0 drops none.
	DropEvery int
	// Runtime is the version the program reports; empty means DefaultRuntime.
	Runtime string
}

// Commit is an image the bootloader accepted.
type Commit struct {
	Size  int
	CRC32 uint32
}

// Board is a running fake.
type Board struct {
	TFTPPort, ModbusPort int
	// Commits receives every accepted image.
	Commits chan Commit

	opt      Options
	inBoot   atomic.Bool
	udp      *net.UDPConn
	tcp      net.Listener
	mu       sync.Mutex
	received int
	entry    string
	app      []byte

	peer       *net.UDPAddr
	lastSeen   time.Time
	blksize    int
	block      uint16
	want       int
	wantCRC    uint32
	haveCRC    bool
	data       []byte
	lastAnswer []byte
}

// Start runs a device until the test ends.
func Start(t *testing.T, opt Options) *Board {
	t.Helper()
	if opt.Device == "" {
		opt.Device = DefaultDevice
	}
	if opt.Core == "" {
		opt.Core = DefaultCore
	}
	if opt.Runtime == "" {
		opt.Runtime = DefaultRuntime
	}
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	tcp, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	b := &Board{
		TFTPPort:   udp.LocalAddr().(*net.UDPAddr).Port,
		ModbusPort: tcp.Addr().(*net.TCPAddr).Port,
		Commits:    make(chan Commit, 4),
		opt:        opt,
		udp:        udp,
		tcp:        tcp,
		entry:      "no_app",
	}
	b.inBoot.Store(opt.StartInBootloader)
	t.Cleanup(func() {
		udp.Close()
		tcp.Close()
	})
	go b.serveTFTP()
	go b.serveModbus()
	return b
}

// ---- Bootloader (TFTP) ------------------------------------------------------

const (
	opRRQ, opWRQ, opDATA, opACK, opERROR, opOACK = 1, 2, 3, 4, 5, 6
)

func (b *Board) serveTFTP() {
	buf := make([]byte, 65536)
	for {
		n, from, err := b.udp.ReadFromUDP(buf)
		if err != nil {
			return
		}
		if !b.inBoot.Load() || n < 4 {
			continue
		}
		b.mu.Lock()
		b.received++
		drop := b.opt.DropEvery > 0 && b.received%b.opt.DropEvery == 0
		if !drop {
			b.handle(from, append([]byte(nil), buf[:n]...))
		}
		b.mu.Unlock()
	}
}

func (b *Board) send(to *net.UDPAddr, p []byte) {
	b.lastAnswer = p
	b.udp.WriteToUDP(p, to)
}

func errorPacket(code uint16, msg string) []byte {
	p := binary.BigEndian.AppendUint16(nil, opERROR)
	p = binary.BigEndian.AppendUint16(p, code)
	return append(append(p, msg...), 0)
}

// fail ends the transfer with an ERROR; the old program stays invalid, as on the device.
func (b *Board) fail(to *net.UDPAddr, code uint16, msg string) {
	b.udp.WriteToUDP(errorPacket(code, msg), to)
	b.peer = nil
}

func samePeer(a, c *net.UDPAddr) bool { return a != nil && a.IP.Equal(c.IP) && a.Port == c.Port }

func (b *Board) busyFor(from *net.UDPAddr) bool {
	return b.peer != nil && !samePeer(b.peer, from) && time.Since(b.lastSeen) < busyWindow
}

func (b *Board) info() string {
	app := "invalid"
	if b.app != nil {
		app = "valid"
	}
	s := fmt.Sprintf("device=%s\ncore=%s\nmode=bootloader\nbootloader=test\nentry=%s\nmac=02:80:E1:00:00:01\n"+
		"ip=127.0.0.1\ndhcp=off\napp=%s\n", b.opt.Device, b.opt.Core, b.entry, app)
	if b.app != nil {
		s += fmt.Sprintf("app_size=%d\napp_version=%s\n", len(b.app), image.Version(b.app))
	}
	return s
}

func (b *Board) handle(from *net.UDPAddr, p []byte) {
	op := binary.BigEndian.Uint16(p)
	switch op {
	case opRRQ, opWRQ:
		if b.busyFor(from) {
			b.udp.WriteToUDP(errorPacket(0, "busy: another transfer is in progress"), from)
			return
		}
		fields := strings.Split(string(p[2:]), "\x00")
		if len(fields) < 2 || !strings.EqualFold(fields[1], "octet") {
			b.fail(from, 4, "only octet mode is supported")
			return
		}
		opts := map[string]string{}
		for i := 2; i+1 < len(fields); i += 2 {
			opts[strings.ToLower(fields[i])] = fields[i+1]
		}
		file := fields[0]
		switch {
		case op == opRRQ && file == layout.FileInfo:
			b.send(from, append([]byte{0, opDATA, 0, 1}, b.info()...))
		case op == opWRQ && file == layout.FileFirmware:
			b.acceptWrite(from, opts)
		case op == opRRQ:
			b.fail(from, 1, "no such file")
		default:
			b.fail(from, 2, "only firmware.bin can be written")
		}
	case opDATA:
		if !samePeer(b.peer, from) {
			return
		}
		b.lastSeen = time.Now()
		b.dataBlock(from, binary.BigEndian.Uint16(p[2:]), p[4:])
	}
}

func (b *Board) acceptWrite(from *net.UDPAddr, opts map[string]string) {
	b.peer, b.lastSeen, b.block, b.data, b.blksize = from, time.Now(), 0, nil, 512
	b.want, b.haveCRC = -1, false
	oack := []byte{0, opOACK}
	put := func(k, v string) { oack = append(append(append(append(oack, k...), 0), v...), 0) }
	if v, ok := opts["blksize"]; ok {
		n, err := strconv.Atoi(v)
		if err != nil || n < 8 {
			b.fail(from, 8, "invalid blksize")
			return
		}
		b.blksize = min(n, layout.MaxBlksize)
		put("blksize", strconv.Itoa(b.blksize))
	}
	if v, ok := opts["tsize"]; ok {
		n, err := strconv.Atoi(v)
		if err != nil || n < layout.ImageMinSize || n > layout.SlotSize {
			b.fail(from, 3, "image does not fit the program slot")
			return
		}
		b.want = n
		put("tsize", v)
	}
	if v, ok := opts[layout.OptCRC32]; ok {
		c, err := strconv.ParseUint(v, 16, 32)
		if err != nil || len(v) != 8 {
			b.fail(from, 8, "invalid crc32")
			return
		}
		b.wantCRC, b.haveCRC = uint32(c), true
		put(layout.OptCRC32, v)
	}
	// Erasing the first sector invalidates the old program.
	b.app = nil
	if len(oack) == 2 {
		b.send(from, []byte{0, opACK, 0, 0})
		return
	}
	b.send(from, oack)
}

func (b *Board) dataBlock(from *net.UDPAddr, block uint16, d []byte) {
	if block == b.block {
		b.udp.WriteToUDP(b.lastAnswer, from) // the acknowledgement was lost
		return
	}
	if block != b.block+1 {
		return
	}
	if len(d) > b.blksize {
		b.fail(from, 4, "block larger than negotiated blksize")
		return
	}
	if len(b.data)+len(d) > layout.SlotSize {
		b.fail(from, 3, "image does not fit the program slot")
		return
	}
	b.data = append(b.data, d...)
	b.block = block
	ack := binary.BigEndian.AppendUint16([]byte{0, opACK}, block)
	if len(d) == b.blksize {
		b.send(from, ack)
		return
	}
	switch {
	case b.want >= 0 && len(b.data) != b.want:
		b.fail(from, 0, "image size does not match tsize")
	case b.haveCRC && image.CRC32(b.data) != b.wantCRC:
		b.fail(from, 0, "image CRC-32 does not match the crc32 option")
	case image.CheckImage(b.data) != nil:
		b.fail(from, 0, "invalid image: "+image.CheckImage(b.data).Error())
	default:
		b.send(from, ack)
		b.app = b.data
		b.Commits <- Commit{Size: len(b.data), CRC32: image.CRC32(b.data)}
		go func() {
			time.Sleep(time.Second) // repeat window for the last ACK, then reset into the new program
			b.mu.Lock()
			b.peer = nil
			b.mu.Unlock()
			b.inBoot.Store(false)
		}()
	}
}

// ---- Program (Modbus TCP) ---------------------------------------------------

var rebootMagic = []byte{0xB0, 0x07, 0x10, 0xAD}

func (b *Board) serveModbus() {
	for {
		c, err := b.tcp.Accept()
		if err != nil {
			return
		}
		go b.modbusConn(c)
	}
}

func (b *Board) modbusConn(c net.Conn) {
	defer c.Close()
	for {
		// The bootloader does not answer TCP.
		if b.inBoot.Load() {
			return
		}
		var hdr [7]byte
		if _, err := io.ReadFull(c, hdr[:]); err != nil {
			return
		}
		n := int(binary.BigEndian.Uint16(hdr[4:])) - 1
		if n < 1 {
			return
		}
		pdu := make([]byte, n)
		if _, err := io.ReadFull(c, pdu); err != nil {
			return
		}
		reply, reboot := b.answer(pdu)
		binary.BigEndian.PutUint16(hdr[4:], uint16(1+len(reply)))
		c.Write(append(hdr[:], reply...))
		if reboot {
			time.Sleep(100 * time.Millisecond)
			b.mu.Lock()
			b.entry = "request"
			b.mu.Unlock()
			b.inBoot.Store(true)
			return
		}
	}
}

func (b *Board) answer(pdu []byte) (reply []byte, reboot bool) {
	fc := pdu[0]
	switch fc {
	case layout.ModbusFCBootloader:
		if !bytes.Equal(pdu[1:], rebootMagic) {
			return []byte{fc, 0x81}, false
		}
		return []byte{fc, 0x7E}, true
	case 0x47:
		return append([]byte{fc, 0x7E}, b.opt.Runtime...), false
	}
	return []byte{fc | 0x80, 0x01}, false
}
