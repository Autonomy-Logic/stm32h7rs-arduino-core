// SPDX-License-Identifier: Apache-2.0

// Command stm32h7rs-abuse runs the lab abuse checks against a unit: crafted Modbus TCP requests. It is a lab tool,
// never installed with the core.
package main

import (
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"sync"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
)

const modbusPort = layout.ModbusPort

func usage() {
	fmt.Fprintln(os.Stderr, "usage: stm32h7rs-abuse <modbus-reboot|modbus-flood> [flags]")
	os.Exit(2)
}

func modbusFrame(tid uint16, pdu []byte) []byte {
	f := make([]byte, 7, 7+len(pdu))
	binary.BigEndian.PutUint16(f[0:], tid)
	binary.BigEndian.PutUint16(f[2:], 0)
	binary.BigEndian.PutUint16(f[4:], uint16(len(pdu)+1))
	f[6] = 1
	return append(f, pdu...)
}

func readModbusReply(conn net.Conn) ([]byte, error) {
	hdr := make([]byte, 7)
	if _, err := io.ReadFull(conn, hdr); err != nil {
		return nil, err
	}
	n := int(binary.BigEndian.Uint16(hdr[4:])) - 1
	if n < 1 || n > 260 {
		return nil, fmt.Errorf("bad Modbus length %d", n)
	}
	pdu := make([]byte, n)
	_, err := io.ReadFull(conn, pdu)
	return pdu, err
}

func modbusAddr(host string) string { return net.JoinHostPort(host, fmt.Sprint(modbusPort)) }

// rebootRequest sends function code 0x4C with the magic to addr and returns the reply PDU.
func rebootRequest(addr string) ([]byte, error) {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
	if _, err := conn.Write(modbusFrame(1, []byte{layout.ModbusFCBootloader, 0xB0, 0x07, 0x10, 0xAD})); err != nil {
		return nil, err
	}
	return readModbusReply(conn)
}

func runModbusReboot(args []string) error {
	fs := flag.NewFlagSet("modbus-reboot", flag.ContinueOnError)
	host := fs.String("host", "", "IP address of the unit")
	if err := fs.Parse(args); err != nil {
		return err
	}
	pdu, err := rebootRequest(modbusAddr(*host))
	if err != nil {
		fmt.Printf("no reply to function 0x4C: %v\n", err)
		return nil
	}
	fmt.Printf("reply to function 0x4C: % X\n", pdu)
	return nil
}

type floodResult struct {
	replies int
	closed  bool
	err     error
}

func floodOne(addr string, stop time.Time) floodResult {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return floodResult{err: err}
	}
	defer conn.Close()
	var r floodResult
	for tid := uint16(1); time.Now().Before(stop); tid++ {
		_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
		if _, err := conn.Write(modbusFrame(tid, []byte{0x03, 0x00, 0x00, 0x00, 0x0A})); err != nil {
			r.closed = true
			return r
		}
		if _, err := readModbusReply(conn); err != nil {
			r.closed = errors.Is(err, io.EOF) || errors.Is(err, net.ErrClosed)
			if !r.closed {
				r.err = err
			}
			return r
		}
		r.replies++
	}
	return r
}

func runModbusFlood(args []string) error {
	fs := flag.NewFlagSet("modbus-flood", flag.ContinueOnError)
	host := fs.String("host", "", "IP address of the unit")
	conns := fs.Int("connections", 4, "number of connections to open")
	duration := fs.Duration("duration", 10*time.Minute, "how long to poll")
	if err := fs.Parse(args); err != nil {
		return err
	}
	stop := time.Now().Add(*duration)
	results := make([]floodResult, *conns)
	var wg sync.WaitGroup
	for i := range results {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			results[i] = floodOne(modbusAddr(*host), stop)
		}(i)
		time.Sleep(200 * time.Millisecond)
	}
	wg.Wait()
	for i, r := range results {
		fmt.Printf("connection %d: %d replies, closed by the unit: %t", i+1, r.replies, r.closed)
		if r.err != nil {
			fmt.Printf(", error: %v", r.err)
		}
		fmt.Println()
	}
	return nil
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	var err error
	switch os.Args[1] {
	case "modbus-reboot":
		err = runModbusReboot(os.Args[2:])
	case "modbus-flood":
		err = runModbusFlood(os.Args[2:])
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
