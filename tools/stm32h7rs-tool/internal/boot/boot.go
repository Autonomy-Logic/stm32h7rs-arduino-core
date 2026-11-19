// SPDX-License-Identifier: Apache-2.0

// Package boot talks to the STM32H7S7 network bootloader over TFTP, as described in docs/network-bootloader.md.
package boot

import (
	"context"
	"fmt"
	"net"
	"strings"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/tftp"
)

// Core is the "core" value the bootloader of this core reports in its info file.
const Core = "stm32h7rs"

// infoMax bounds the info file; the bootloader sends one block of at most 511 bytes.
const infoMax = 4096

// Target is a bootloader's TFTP request port.
type Target struct {
	Addr *net.UDPAddr
	// Logf, when set, receives protocol events.
	Logf func(format string, args ...any)
}

// Info is the bootloader's "info" file: device, core, mode, bootloader version, entry reason, mac, ip, app state.
type Info map[string]string

// ParseInfo reads key=value lines.
func ParseInfo(b []byte) Info {
	info := Info{}
	for _, line := range strings.Split(string(b), "\n") {
		if k, v, ok := strings.Cut(strings.TrimSpace(line), "="); ok {
			info[k] = v
		}
	}
	return info
}

// FromBootloader reports whether info was written by a bootloader of this core, whatever the device.
func (info Info) FromBootloader() bool {
	return info["mode"] == "bootloader" && info["core"] == Core
}

// ReadInfo asks the bootloader for its info file, waiting at most wait.
func (t Target) ReadInfo(ctx context.Context, wait time.Duration) (Info, error) {
	c := &tftp.Client{Server: t.Addr, Retry: min(wait/2, 500*time.Millisecond), Timeout: wait, Logf: t.Logf}
	b, err := c.Get(ctx, layout.FileInfo, infoMax)
	if err != nil {
		return nil, err
	}
	info := ParseInfo(b)
	if !info.FromBootloader() {
		return nil, fmt.Errorf("the info file is not from an STM32H7S7 network bootloader (core=%q, mode=%q)",
			info["core"], info["mode"])
	}
	return info, nil
}

// WriteFirmware uploads a program image with the blksize, tsize and crc32 options. The bootloader checks its
// size, CRC-32, header and vector table, writes the header last and then starts it.
func (t Target) WriteFirmware(ctx context.Context, img []byte, progress func(done, total int)) error {
	c := &tftp.Client{Server: t.Addr, Retry: 500 * time.Millisecond, Timeout: 10 * time.Second, Logf: t.Logf}
	return c.Put(ctx, layout.FileFirmware, img, tftp.PutOptions{
		Blksize:  layout.MaxBlksize,
		Tsize:    true,
		Extra:    []tftp.Option{{Name: layout.OptCRC32, Value: fmt.Sprintf("%08x", image.CRC32(img))}},
		Progress: progress,
	})
}
