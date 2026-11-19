// SPDX-License-Identifier: Apache-2.0
// Copyright (c) Autonomy Logic

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/fakeboard"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
)

func runCLI(t *testing.T, args ...string) (int, string, string) {
	t.Helper()
	var out, errOut bytes.Buffer
	code := run(context.Background(), args, &out, &errOut)
	return code, out.String(), errOut.String()
}

// mergeAndPack builds an image the way boards.txt does: merge, then pack.
func mergeAndPack(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	loader := make([]byte, 0x2000)
	binary.LittleEndian.PutUint32(loader[layout.ImageHeaderSize:], layout.DTCMEnd)
	binary.LittleEndian.PutUint32(loader[layout.ImageHeaderSize+4:], layout.SlotBase+layout.ImageHeaderSize+0x201)
	write := func(name string, b []byte) string {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, b, 0o644); err != nil {
			t.Fatal(err)
		}
		return p
	}
	lp, sp := write("loader.bin", loader), write("sketch.bin", []byte("sketch"))
	mp, ip := filepath.Join(dir, "merged.bin"), filepath.Join(dir, "image.bin")
	if code, _, e := runCLI(t, "merge", "-loader", lp, "-sketch", sp, "-offset", "0x00080000", "-output", mp); code != 0 {
		t.Fatalf("merge: %d %s", code, e)
	}
	if code, _, e := runCLI(t, "pack", "-version", "0.2.0", "-input", mp, "-output", ip); code != 0 {
		t.Fatalf("pack: %d %s", code, e)
	}
	return ip
}

func TestMergeAndPackProduceACheckedImage(t *testing.T) {
	img, err := os.ReadFile(mergeAndPack(t))
	if err != nil {
		t.Fatal(err)
	}
	if err := image.CheckImage(img); err != nil {
		t.Fatal(err)
	}
	if len(img) != 0x00080000+len("sketch") || image.Version(img) != "0.2.0" {
		t.Errorf("image of %d bytes, version %q", len(img), image.Version(img))
	}
}

func TestUploadAndInfoAgainstTheFakeDevice(t *testing.T) {
	path := mergeAndPack(t)
	b := fakeboard.Start(t, fakeboard.Options{})
	ports := []string{"--tftp-port", strconv.Itoa(b.TFTPPort), "--modbus-port", strconv.Itoa(b.ModbusPort)}

	args := append(append([]string{"upload", "--ip", "127.0.0.1", "--verify"}, ports...), path)
	code, out, e := runCLI(t, args...)
	if code != 0 {
		t.Fatalf("upload: %d\n%s%s", code, out, e)
	}
	if len(b.Commits) != 1 || !strings.Contains(out, "OpenPLC runtime 4.2.7 is running") {
		t.Errorf("upload output:\n%s", out)
	}

	code, out, e = runCLI(t, append([]string{"info", "--ip", "127.0.0.1"}, ports...)...)
	if code != 0 || out != "mode=runtime\nruntime=4.2.7\n" {
		t.Errorf("info of the running program: %d %q %s", code, out, e)
	}
}

func TestInfoOfTheBootloader(t *testing.T) {
	b := fakeboard.Start(t, fakeboard.Options{StartInBootloader: true})
	code, out, e := runCLI(t, "info", "--ip", "127.0.0.1", "--tftp-port", strconv.Itoa(b.TFTPPort))
	if code != 0 || !strings.Contains(out, "core=stm32h7rs\n") || !strings.Contains(out, "mode=bootloader\n") {
		t.Errorf("info: %d %q %s", code, out, e)
	}
}

func TestUsageErrors(t *testing.T) {
	for _, args := range [][]string{
		nil,
		{"sign"},
		{"list"},
		{"reset"},
		{"upload", "image.bin"},
		{"upload", "--ip", "127.0.0.1"},
		{"upload", "--port", "/dev/ttyACM0", "image.bin"},
		{"pack", "-input", "a", "-output", "b"},
		{"info"},
	} {
		if code, _, _ := runCLI(t, args...); code != 2 {
			t.Errorf("%q: exit %d, want 2", args, code)
		}
	}
}

func TestVersion(t *testing.T) {
	if code, out, _ := runCLI(t, "version"); code != 0 || out != "stm32h7rs-tool dev\n" {
		t.Errorf("version: %d %q", code, out)
	}
}
