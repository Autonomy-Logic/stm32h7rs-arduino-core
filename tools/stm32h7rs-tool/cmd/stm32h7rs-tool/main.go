// SPDX-License-Identifier: Apache-2.0

// Command stm32h7rs-tool builds program images for the STM32H7S7 Arduino core and uploads them over Ethernet to
// the network bootloader (docs/network-bootloader.md).
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"sort"
	"strconv"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/boot"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/modbus"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/netflash"
)

const name = "stm32h7rs-tool"

// version is set at build time with -ldflags "-X main.version=...".
var version = "dev"

const usage = `usage:
  stm32h7rs-tool merge -loader <loader.bin> -sketch <sketch.bin> -offset <offset> -output <merged.bin>
      Join the loader and the prelinked sketch into one unpacked image.
  stm32h7rs-tool pack -version <version> -input <merged.bin> -output <image.bin>
      Write the image header into a merged image.
  stm32h7rs-tool upload --ip <address> [--verify] [-v] <image.bin>
      Start the device's network bootloader and write the image.
  stm32h7rs-tool info --ip <address>
      Show the bootloader's info file, or the runtime version of a running program.
  stm32h7rs-tool version
`

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	os.Exit(run(ctx, os.Args[1:], os.Stdout, os.Stderr))
}

func run(ctx context.Context, args []string, stdout, stderr io.Writer) int {
	if len(args) == 0 {
		fmt.Fprint(stderr, usage)
		return 2
	}
	var err error
	switch args[0] {
	case "merge":
		err = runMerge(args[1:], stdout, stderr)
	case "pack":
		err = runPack(args[1:], stdout, stderr)
	case "upload":
		err = runUpload(ctx, args[1:], stdout, stderr)
	case "info":
		err = runInfo(ctx, args[1:], stdout, stderr)
	case "version":
		fmt.Fprintf(stdout, "%s %s\n", name, version)
	default:
		fmt.Fprint(stderr, usage)
		return 2
	}
	var uerr *usageError
	switch {
	case errors.As(err, &uerr):
		fmt.Fprintf(stderr, "error: %v\n%s", err, usage)
		return 2
	case err != nil:
		fmt.Fprintln(stderr, "error:", err)
		return 1
	}
	return 0
}

type usageError struct{ msg string }

func (e *usageError) Error() string { return e.msg }

func newFlagSet(cmd string, stderr io.Writer) *flag.FlagSet {
	fs := flag.NewFlagSet(cmd, flag.ContinueOnError)
	fs.SetOutput(stderr)
	return fs
}

func runMerge(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("merge", stderr)
	loaderPath := fs.String("loader", "", "loader image (zephyr-<variant>.bin)")
	sketchPath := fs.String("sketch", "", "prelinked sketch binary")
	offsetStr := fs.String("offset", "", "sketch offset from the start of the image, e.g. 0x00080000")
	outPath := fs.String("output", "", "merged image to write")
	if err := fs.Parse(args); err != nil {
		return &usageError{err.Error()}
	}
	if *loaderPath == "" || *sketchPath == "" || *offsetStr == "" || *outPath == "" {
		return &usageError{"merge: -loader, -sketch, -offset and -output are required"}
	}
	offset, err := strconv.ParseInt(*offsetStr, 0, 64)
	if err != nil || offset < 0 {
		return fmt.Errorf("merge: invalid -offset %q", *offsetStr)
	}
	loader, err := os.ReadFile(*loaderPath)
	if err != nil {
		return err
	}
	sketch, err := os.ReadFile(*sketchPath)
	if err != nil {
		return err
	}
	merged, err := image.Merge(loader, sketch, int(offset))
	if err != nil {
		return err
	}
	if err := os.WriteFile(*outPath, merged, 0o644); err != nil {
		return err
	}
	fmt.Fprintf(stdout, "merged image: loader %d bytes, sketch %d bytes at 0x%X, total %d bytes\n",
		len(loader), len(sketch), offset, len(merged))
	return nil
}

func runPack(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("pack", stderr)
	versionStr := fs.String("version", "", "core version written into the image header")
	inPath := fs.String("input", "", "merged image whose first 0x400 bytes are empty")
	outPath := fs.String("output", "", "program image to write")
	if err := fs.Parse(args); err != nil {
		return &usageError{err.Error()}
	}
	if *versionStr == "" || *inPath == "" || *outPath == "" {
		return &usageError{"pack: -version, -input and -output are required"}
	}
	merged, err := os.ReadFile(*inPath)
	if err != nil {
		return err
	}
	img, err := image.Pack(merged, *versionStr)
	if err != nil {
		return err
	}
	if err := os.WriteFile(*outPath, img, 0o644); err != nil {
		return err
	}
	fmt.Fprintf(stdout, "program image: version %s, %d bytes, CRC-32 %08x\n", *versionStr, len(img), image.CRC32(img))
	return nil
}

type netFlags struct {
	ip         *string
	tftpPort   *int
	modbusPort *int
}

func addNetFlags(fs *flag.FlagSet) netFlags {
	return netFlags{
		ip:         fs.String("ip", "", "device IPv4 address"),
		tftpPort:   fs.Int("tftp-port", layout.TFTPPort, "bootloader TFTP port"),
		modbusPort: fs.Int("modbus-port", layout.ModbusPort, "OpenPLC runtime Modbus TCP port"),
	}
}

func (n netFlags) resolve() (net.IP, error) {
	if *n.ip == "" {
		return nil, &usageError{"--ip is required"}
	}
	a, err := net.ResolveIPAddr("ip4", *n.ip)
	if err != nil {
		return nil, fmt.Errorf("%s is not an IPv4 address or known host name", *n.ip)
	}
	return a.IP, nil
}

func runUpload(ctx context.Context, args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("upload", stderr)
	nf := addNetFlags(fs)
	verify := fs.Bool("verify", false, "wait for the new program to answer Modbus TCP")
	verbose := fs.Bool("v", false, "show protocol details")
	bootWait := fs.Duration("boot-wait", 20*time.Second, "how long to wait for the bootloader after the hand-over")
	verifyWait := fs.Duration("verify-wait", 30*time.Second, "how long --verify waits for the new program")
	skipCheck := fs.Bool("skip-local-check", false, "send the image even if it fails the local check (device tests)")
	if err := fs.Parse(args); err != nil {
		return &usageError{err.Error()}
	}
	if fs.NArg() != 1 {
		return &usageError{"upload: one program image file is required, after the flags"}
	}
	ip, err := nf.resolve()
	if err != nil {
		return err
	}
	img, err := os.ReadFile(fs.Arg(0))
	if err != nil {
		return err
	}
	fmt.Fprintf(stdout, "%s %s: uploading to %s\n", name, version, ip)
	return netflash.Run(ctx, netflash.Options{
		IP:         ip,
		TFTPPort:   *nf.tftpPort,
		ModbusPort: *nf.modbusPort,
		Image:      img,
		Verify:     *verify,
		BootWait:   *bootWait,
		VerifyWait: *verifyWait,
		Out:        stdout,
		Verbose:    *verbose,
		SkipCheck:  *skipCheck,
	})
}

// runInfo only reads: a TFTP read of the bootloader's info file, else a Modbus read of the runtime version.
func runInfo(ctx context.Context, args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("info", stderr)
	nf := addNetFlags(fs)
	if err := fs.Parse(args); err != nil {
		return &usageError{err.Error()}
	}
	ip, err := nf.resolve()
	if err != nil {
		return err
	}
	target := boot.Target{Addr: &net.UDPAddr{IP: ip, Port: *nf.tftpPort}}
	if bi, err := target.ReadInfo(ctx, 1500*time.Millisecond); err == nil {
		keys := make([]string, 0, len(bi))
		for k := range bi {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			fmt.Fprintf(stdout, "%s=%s\n", k, bi[k])
		}
		return nil
	}
	mb, err := modbus.Dial(ctx, net.JoinHostPort(ip.String(), strconv.Itoa(*nf.modbusPort)), 3*time.Second)
	if err != nil {
		return fmt.Errorf("nothing answered at %s: no network bootloader (TFTP) and no OpenPLC runtime (Modbus TCP)", ip)
	}
	defer mb.Close()
	v, err := mb.RuntimeVersion()
	if err != nil {
		return fmt.Errorf("the device at %s did not report an OpenPLC runtime version: %w", ip, err)
	}
	fmt.Fprintf(stdout, "mode=runtime\nruntime=%s\n", v)
	return nil
}
