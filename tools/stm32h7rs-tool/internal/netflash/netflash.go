// SPDX-License-Identifier: Apache-2.0
// Copyright (c) Autonomy Logic

// Package netflash is the network upload: check the image, hand the device over to its bootloader, write the
// image over TFTP and optionally check that the new program answers.
package netflash

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/boot"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/handover"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/modbus"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/tftp"
)

// Options for one upload.
type Options struct {
	IP         net.IP
	TFTPPort   int
	ModbusPort int
	Image      []byte
	// Verify waits for the new program to answer Modbus TCP.
	Verify bool
	// BootWait bounds the wait for the bootloader after the hand-over.
	BootWait time.Duration
	// VerifyWait bounds the wait for the new program.
	VerifyWait time.Duration
	Out        io.Writer
	Verbose    bool
	// SkipCheck sends the image without the local check, so the device's own check can be tested.
	SkipCheck bool
}

type run struct {
	Options
	target boot.Target
	modbus string
}

func (r *run) say(format string, args ...any) { fmt.Fprintf(r.Out, format+"\n", args...) }

func (r *run) debug(format string, args ...any) {
	if r.Verbose {
		fmt.Fprintf(r.Out, "  "+format+"\n", args...)
	}
}

// Run uploads o.Image to the device at o.IP. The image is checked before the device is contacted.
func Run(ctx context.Context, o Options) error {
	if err := image.CheckImage(o.Image); err != nil {
		if !o.SkipCheck {
			return fmt.Errorf("upload refused: %w", err)
		}
		fmt.Fprintf(o.Out, "Sending an image that fails the local check (%v)\n", err)
	}
	r := &run{Options: o, modbus: net.JoinHostPort(o.IP.String(), strconv.Itoa(o.ModbusPort))}
	r.target = boot.Target{Addr: &net.UDPAddr{IP: o.IP, Port: o.TFTPPort}, Logf: r.debug}
	r.say("Image: version %s, %d bytes (%d%% of the program slot), CRC-32 %08x",
		image.Version(o.Image), len(o.Image), len(o.Image)*100/layout.SlotSize, image.CRC32(o.Image))

	info, err := handover.Enter(ctx, handover.Options{
		Target:     r.target,
		IP:         o.IP,
		ModbusPort: o.ModbusPort,
		BootWait:   o.BootWait,
		Say:        r.say,
		Debug:      r.debug,
	})
	if err != nil {
		return err
	}
	r.say("Bootloader %s of %s ready (MAC %s, entered: %s).", info["bootloader"], info["device"], info["mac"],
		info["entry"])

	r.say("Writing %d bytes...", len(o.Image))
	start := time.Now()
	next := 20
	err = r.target.WriteFirmware(ctx, o.Image, func(done, total int) {
		for pct := done * 100 / total; next <= 100 && pct >= next; next += 20 {
			r.say("  %d%%", next)
		}
	})
	unconfirmed := errors.Is(err, tftp.ErrNoFinalAck)
	switch {
	case unconfirmed:
		r.say("The bootloader did not confirm the last block; checking whether the new program started.")
	case err != nil:
		return writeError(err)
	default:
		r.say("Written and checked by the bootloader in %.1f s.", time.Since(start).Seconds())
	}
	if !o.Verify {
		if unconfirmed {
			return errors.New("the bootloader did not confirm the upload")
		}
		return nil
	}
	return r.verify(ctx, unconfirmed)
}

// answers reports whether a Modbus TCP server answers at r.modbus, and the runtime version when it reports one.
func (r *run) answers(ctx context.Context) (bool, string) {
	c, err := modbus.Dial(ctx, r.modbus, time.Second)
	if err != nil {
		return false, ""
	}
	defer c.Close()
	version, err := c.RuntimeVersion()
	var ex *modbus.ExceptionError
	switch {
	case err == nil:
		return true, version
	case errors.As(err, &ex), errors.Is(err, modbus.ErrLocked):
		return true, ""
	default:
		r.debug("Modbus TCP not ready: %v", err)
		return false, ""
	}
}

// verify waits for the new program to answer Modbus TCP.
func (r *run) verify(ctx context.Context, unconfirmed bool) error {
	r.say("Waiting for the new program to start...")
	// The bootloader waits a second for a repeated last block, then resets.
	handover.Sleep(ctx, 1500*time.Millisecond)
	deadline := time.Now().Add(r.VerifyWait)
	var probed time.Time
	for time.Now().Before(deadline) && ctx.Err() == nil {
		if ok, version := r.answers(ctx); ok {
			if version != "" {
				r.say("OpenPLC runtime %s is running at %s.", version, r.IP)
			} else {
				r.say("The new program answers Modbus TCP at %s.", r.IP)
			}
			return nil
		}
		if time.Since(probed) >= 3*time.Second {
			probed = time.Now()
			if info, err := r.target.ReadInfo(ctx, 700*time.Millisecond); err == nil {
				return fmt.Errorf("the new program did not start: the device is still in its bootloader "+
					"(entered: %s, app: %s)", info["entry"], info["app"])
			}
		}
		handover.Sleep(ctx, 500*time.Millisecond)
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	if unconfirmed {
		return fmt.Errorf("could not confirm the upload: %s answered neither as the bootloader nor as the new program",
			r.IP)
	}
	r.say("Warning: the image was written, but nothing answered Modbus TCP at %s within %s. "+
		"If the new program uses a different IP address, it is running there.", r.IP, r.VerifyWait)
	return nil
}

func writeError(err error) error {
	var se *tftp.ServerError
	switch {
	case errors.As(err, &se):
		return fmt.Errorf("the bootloader refused the image: %s", se.Msg)
	case errors.Is(err, tftp.ErrTimeout):
		return errors.New("the bootloader stopped answering during the upload; the device keeps waiting " +
			"in its bootloader, so run the upload again")
	default:
		return fmt.Errorf("upload failed: %w", err)
	}
}
