// SPDX-License-Identifier: Apache-2.0

package netflash

import (
	"bytes"
	"context"
	"encoding/binary"
	"math/rand"
	"net"
	"strings"
	"testing"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/fakeboard"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
)

// makeImage returns a packed image of n bytes, header included.
func makeImage(t *testing.T, n int) []byte {
	t.Helper()
	b := make([]byte, n)
	rand.New(rand.NewSource(int64(n))).Read(b[layout.ImageHeaderSize:])
	binary.LittleEndian.PutUint32(b[layout.ImageHeaderSize:], layout.AXISRAMEnd-0x100)
	binary.LittleEndian.PutUint32(b[layout.ImageHeaderSize+4:], layout.SlotBase+layout.ImageHeaderSize+0x201)
	img, err := image.Pack(b, "0.2.0")
	if err != nil {
		t.Fatal(err)
	}
	return img
}

func freePort(t *testing.T, network string) int {
	t.Helper()
	if network == "udp4" {
		c, err := net.ListenPacket("udp4", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		return c.LocalAddr().(*net.UDPAddr).Port
	}
	l, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port
}

func upload(t *testing.T, b *fakeboard.Board, img []byte, bootWait time.Duration) (string, error) {
	t.Helper()
	var out bytes.Buffer
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	err := Run(ctx, Options{
		IP:         net.IPv4(127, 0, 0, 1),
		TFTPPort:   b.TFTPPort,
		ModbusPort: b.ModbusPort,
		Image:      img,
		Verify:     true,
		BootWait:   bootWait,
		VerifyWait: 5 * time.Second,
		Out:        &out,
		Verbose:    true,
	})
	return out.String(), err
}

func TestUpload(t *testing.T) {
	cases := []struct {
		name string
		opt  fakeboard.Options
		size int
	}{
		{"from the running program", fakeboard.Options{}, 150001},
		{"from the running program of another device", fakeboard.Options{Device: "STM32H7S7"}, 150001},
		{"device waiting in its bootloader, empty last block", fakeboard.Options{StartInBootloader: true},
			layout.MaxBlksize * 40},
		{"lossy link", fakeboard.Options{DropEvery: 9}, 40000},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			b := fakeboard.Start(t, c.opt)
			img := makeImage(t, c.size)
			out, err := upload(t, b, img, 5*time.Second)
			if err != nil {
				t.Fatalf("%v\n%s", err, out)
			}
			select {
			case got := <-b.Commits:
				if want := (fakeboard.Commit{Size: len(img), CRC32: image.CRC32(img)}); got != want {
					t.Errorf("committed %+v, want %+v", got, want)
				}
			default:
				t.Fatalf("the device committed nothing\n%s", out)
			}
			if !strings.Contains(out, "OpenPLC runtime 4.2.7 is running") {
				t.Errorf("output:\n%s", out)
			}
		})
	}
}

func TestRejectsBootloaderOfAnotherCore(t *testing.T) {
	b := fakeboard.Start(t, fakeboard.Options{Core: "stm32f4", StartInBootloader: true})
	out, err := upload(t, b, makeImage(t, 4096), 2*time.Second)
	if err == nil {
		t.Fatalf("upload to a foreign bootloader succeeded\n%s", out)
	}
	select {
	case c := <-b.Commits:
		t.Fatalf("the device committed %+v", c)
	default:
	}
}

func TestNothingAnswers(t *testing.T) {
	var out bytes.Buffer
	err := Run(context.Background(), Options{
		IP:         net.IPv4(127, 0, 0, 1),
		TFTPPort:   freePort(t, "udp4"),
		ModbusPort: freePort(t, "tcp4"),
		Image:      makeImage(t, 4096),
		Out:        &out,
	})
	if err == nil || !strings.Contains(err.Error(), "no device answered") {
		t.Fatalf("err = %v", err)
	}
}

func TestRejectsInvalidImageBeforeContactingTheDevice(t *testing.T) {
	damaged := makeImage(t, 4096)
	damaged[len(damaged)-1] ^= 1
	unpacked := make([]byte, 4096)
	for name, img := range map[string][]byte{"damaged": damaged, "not packed": unpacked} {
		var out bytes.Buffer
		if err := Run(context.Background(), Options{IP: net.IPv4(127, 0, 0, 1), Image: img, Out: &out}); err == nil {
			t.Errorf("%s: image accepted", name)
		}
		if out.Len() != 0 {
			t.Errorf("%s: contacted the device before checking the image:\n%s", name, out.String())
		}
	}
}

func TestWithoutVerify(t *testing.T) {
	b := fakeboard.Start(t, fakeboard.Options{StartInBootloader: true})
	var out bytes.Buffer
	img := makeImage(t, 10000)
	err := Run(context.Background(), Options{
		IP:         net.IPv4(127, 0, 0, 1),
		TFTPPort:   b.TFTPPort,
		ModbusPort: b.ModbusPort,
		Image:      img,
		BootWait:   time.Second,
		Out:        &out,
	})
	if err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if strings.Contains(out.String(), "Waiting for the new program") {
		t.Errorf("verified without --verify:\n%s", out.String())
	}
	if len(b.Commits) != 1 {
		t.Errorf("%d commits", len(b.Commits))
	}
}
