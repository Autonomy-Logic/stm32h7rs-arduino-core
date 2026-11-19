// SPDX-License-Identifier: Apache-2.0

// Package handover gets a device into its network bootloader: when the bootloader does not answer on TFTP, it asks
// the running OpenPLC runtime (Modbus TCP function code 0x4C) to restart into it and waits for it.
package handover

import (
	"context"
	"errors"
	"fmt"
	"net"
	"strconv"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/boot"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/modbus"
)

// Options for Enter.
type Options struct {
	Target     boot.Target
	IP         net.IP
	ModbusPort int
	// BootWait bounds the wait for the bootloader after the hand-over.
	BootWait time.Duration
	// Say reports progress to the user, Debug protocol details. Both may be nil.
	Say, Debug func(format string, args ...any)
}

func (o Options) say(format string, args ...any) {
	if o.Say != nil {
		o.Say(format, args...)
	}
}

func (o Options) debug(format string, args ...any) {
	if o.Debug != nil {
		o.Debug(format, args...)
	}
}

// Enter returns the bootloader's info file, handing the device over from its program first when the bootloader
// is not already running.
func Enter(ctx context.Context, o Options) (boot.Info, error) {
	info, err := o.Target.ReadInfo(ctx, 1200*time.Millisecond)
	if err == nil {
		return info, nil
	}
	o.debug("no bootloader at %s: %v", o.Target.Addr, err)
	if err := handOver(ctx, o); err != nil {
		return nil, err
	}
	return awaitBootloader(ctx, o)
}

// handOver asks the running OpenPLC runtime to restart into the bootloader.
func handOver(ctx context.Context, o Options) error {
	o.say("Asking the OpenPLC runtime at %s to start the bootloader...", o.IP)
	addr := net.JoinHostPort(o.IP.String(), strconv.Itoa(o.ModbusPort))
	c, err := modbus.Dial(ctx, addr, 3*time.Second)
	if err != nil {
		o.debug("Modbus TCP %s: %v", addr, err)
		return fmt.Errorf("no device answered at %s: neither its network bootloader (TFTP) nor an OpenPLC runtime "+
			"(Modbus TCP port %d) responded. Check the IP address and the network connection. A device "+
			"without a working program waits in its bootloader at its last address or at 192.168.10.100.",
			o.IP, o.ModbusPort)
	}
	defer c.Close()
	err = c.RebootToBootloader()
	var ex *modbus.ExceptionError
	switch {
	case errors.As(err, &ex) && ex.Code == modbus.ExIllegalFunction:
		return fmt.Errorf("the device at %s does not support uploads over the network "+
			"(function code 0x%02X refused); is it an STM32H7S7 device running an OpenPLC program?",
			o.IP, modbus.FCRebootBootloader)
	case err != nil:
		return fmt.Errorf("the OpenPLC runtime at %s did not start the bootloader: %w", o.IP, err)
	}
	o.debug("the runtime accepted function code 0x%02X and is restarting", modbus.FCRebootBootloader)
	return nil
}

func awaitBootloader(ctx context.Context, o Options) (boot.Info, error) {
	deadline := time.Now().Add(o.BootWait)
	for {
		attempt := time.Now()
		info, err := o.Target.ReadInfo(ctx, time.Second)
		if err == nil {
			return info, nil
		}
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		if time.Now().After(deadline) {
			return nil, fmt.Errorf("the device accepted the request but its bootloader did not answer within %s",
				o.BootWait)
		}
		// A refused request returns at once; this keeps the polling rate bounded.
		if pause := 300*time.Millisecond - time.Since(attempt); pause > 0 {
			Sleep(ctx, pause)
		}
	}
}

// Sleep waits for d or until ctx is done.
func Sleep(ctx context.Context, d time.Duration) {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
	case <-t.C:
	}
}
