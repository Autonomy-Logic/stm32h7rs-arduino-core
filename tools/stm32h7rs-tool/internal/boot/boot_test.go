// SPDX-License-Identifier: Apache-2.0

package boot

import (
	"context"
	"errors"
	"net"
	"testing"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/fakeboard"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/tftp"
)

func TestParseInfo(t *testing.T) {
	info := ParseInfo([]byte("device=SL-VISU-5R\ncore=stm32h7rs\r\nmode=bootloader\nnot a pair\n\nip=1.2.3.4"))
	want := map[string]string{"device": "SL-VISU-5R", "core": "stm32h7rs", "mode": "bootloader", "ip": "1.2.3.4"}
	if len(info) != len(want) {
		t.Fatalf("parsed %v", info)
	}
	for k, v := range want {
		if info[k] != v {
			t.Errorf("%s = %q, want %q", k, info[k], v)
		}
	}
}

func TestFromBootloader(t *testing.T) {
	cases := []struct {
		device, core, mode string
		ok                 bool
	}{
		{"SL-VISU-5R", "stm32h7rs", "bootloader", true},
		{"STM32H7S7", "stm32h7rs", "bootloader", true},
		{"", "stm32h7rs", "bootloader", true},
		{"SL-VISU-5R", "stm32h7rs", "runtime", false},
		{"SL-VISU-5R", "stm32h7rs", "", false},
		{"SL-VISU-5R", "stm32f4", "bootloader", false},
		{"SL-VISU-5R", "STM32H7RS", "bootloader", false},
		{"SL-VISU-5R", "", "bootloader", false},
	}
	for _, c := range cases {
		info := Info{"device": c.device, "core": c.core, "mode": c.mode}
		if got := info.FromBootloader(); got != c.ok {
			t.Errorf("%v: %v, want %v", info, got, c.ok)
		}
	}
}

func TestReadInfo(t *testing.T) {
	for _, c := range []struct {
		core string
		ok   bool
	}{{"stm32h7rs", true}, {"other", false}} {
		t.Run(c.core, func(t *testing.T) {
			b := fakeboard.Start(t, fakeboard.Options{Core: c.core, StartInBootloader: true})
			target := Target{Addr: &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: b.TFTPPort}}
			info, err := target.ReadInfo(context.Background(), time.Second)
			if (err == nil) != c.ok {
				t.Fatalf("ReadInfo = %v, %v", info, err)
			}
			if c.ok && (info["device"] != fakeboard.DefaultDevice || info["app"] != "invalid" || info["entry"] != "no_app") {
				t.Errorf("info %v", info)
			}
		})
	}
}

func TestWriteFirmwareRefusedImageIsAServerError(t *testing.T) {
	b := fakeboard.Start(t, fakeboard.Options{StartInBootloader: true})
	target := Target{Addr: &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: b.TFTPPort}}
	err := target.WriteFirmware(context.Background(), make([]byte, 4096), nil)
	var se *tftp.ServerError
	if !errors.As(err, &se) {
		t.Fatalf("WriteFirmware of an unpacked image = %v, want a TFTP ERROR", err)
	}
	if len(b.Commits) != 0 {
		t.Error("the device committed an unpacked image")
	}
}
