// SPDX-License-Identifier: Apache-2.0

// Package port opens the device's USB serial port for SMP with a bounded wait for each response.
package port

import (
	"errors"
	"time"

	"go.bug.st/serial"
)

// pollInterval is how long one read waits before the deadline is checked again.
const pollInterval = 100 * time.Millisecond

// Port is a serial port whose reads fail once no byte arrives within the response timeout.
type Port struct {
	sp      serial.Port
	timeout time.Duration
}

// Open opens name at 115200 baud (ignored by USB CDC ACM) with the given response timeout.
func Open(name string, timeout time.Duration) (*Port, error) {
	sp, err := serial.Open(name, &serial.Mode{BaudRate: 115200})
	if err != nil {
		return nil, err
	}
	if err := sp.SetReadTimeout(pollInterval); err != nil {
		_ = sp.Close()
		return nil, err
	}
	return &Port{sp: sp, timeout: timeout}, nil
}

// Read returns the next bytes, or an error when nothing arrives before the response timeout.
func (p *Port) Read(b []byte) (int, error) {
	deadline := time.Now().Add(p.timeout)
	for {
		n, err := p.sp.Read(b)
		if n > 0 || err != nil {
			return n, err
		}
		if time.Now().After(deadline) {
			return 0, errors.New("timed out waiting for the device")
		}
	}
}

// Write sends b to the device.
func (p *Port) Write(b []byte) (int, error) {
	return p.sp.Write(b)
}

// Close closes the port.
func (p *Port) Close() error {
	return p.sp.Close()
}
