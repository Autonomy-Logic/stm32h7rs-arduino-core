// SPDX-License-Identifier: Apache-2.0

// Command stm32h7rs-tool builds and uploads program images for the STM32H7S7 Arduino core.
package main

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"time"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/layout"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/port"
	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/smp"
)

var version = "dev"

const responseTimeout = 15 * time.Second

func usage() {
	fmt.Fprintln(os.Stderr, "usage: stm32h7rs-tool <upload|list|reset|merge|sign|version> [flags]")
	os.Exit(2)
}

func runMerge(args []string) error {
	fs := flag.NewFlagSet("merge", flag.ContinueOnError)
	loaderPath := fs.String("loader", "", "loader image (zephyr-<variant>.bin)")
	sketchPath := fs.String("sketch", "", "prelinked sketch binary")
	offsetStr := fs.String("offset", "", "sketch offset from the start of the image, e.g. 0x00080000")
	outPath := fs.String("output", "", "merged image to write")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *loaderPath == "" || *sketchPath == "" || *offsetStr == "" || *outPath == "" {
		return fmt.Errorf("merge: -loader, -sketch, -offset and -output are required")
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
	fmt.Printf("merged image: loader %d bytes, sketch %d bytes at 0x%X, total %d bytes\n",
		len(loader), len(sketch), offset, len(merged))
	return nil
}

func runSign(args []string) error {
	fs := flag.NewFlagSet("sign", flag.ContinueOnError)
	keyPath := fs.String("key", "", "ECDSA P-256 private key (PEM, PKCS#8)")
	headerStr := fs.String("header", "0x400", "image header size reserved at the start of the input")
	versionStr := fs.String("version", "0.0.0", "image version major.minor.revision[+build]")
	inPath := fs.String("input", "", "merged image to sign")
	outPath := fs.String("output", "", "signed image to write")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *keyPath == "" || *inPath == "" || *outPath == "" {
		return fmt.Errorf("sign: -key, -input and -output are required")
	}
	header, err := strconv.ParseInt(*headerStr, 0, 32)
	if err != nil {
		return fmt.Errorf("sign: invalid -header %q", *headerStr)
	}
	version, err := image.ParseVersion(*versionStr)
	if err != nil {
		return err
	}
	keyPEM, err := os.ReadFile(*keyPath)
	if err != nil {
		return err
	}
	payload, err := os.ReadFile(*inPath)
	if err != nil {
		return err
	}
	signed, err := image.Sign(payload, keyPEM, int(header), version)
	if err != nil {
		return err
	}
	if err := os.WriteFile(*outPath, signed, 0o644); err != nil {
		return err
	}
	fmt.Printf("signed image: %d bytes\n", len(signed))
	return nil
}

func portFlag(fs *flag.FlagSet) *string {
	return fs.String("port", "", "USB serial port of the device (MCUboot recovery)")
}

func withClient(name string, run func(*smp.Client) error) error {
	if name == "" {
		return fmt.Errorf("-port is required")
	}
	p, err := port.Open(name, responseTimeout)
	if err != nil {
		return fmt.Errorf("open %s: %w", name, err)
	}
	defer p.Close()
	return run(smp.NewClient(p))
}

func runUpload(args []string) error {
	fs := flag.NewFlagSet("upload", flag.ContinueOnError)
	portName := portFlag(fs)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 1 {
		return fmt.Errorf("upload: one signed image file is required")
	}
	img, err := os.ReadFile(fs.Arg(0))
	if err != nil {
		return err
	}
	if err := image.CheckUpload(img, layout.SlotSize); err != nil {
		return fmt.Errorf("upload refused: %w", err)
	}
	return withClient(*portName, func(c *smp.Client) error {
		last := -1
		err := c.Upload(img, 0, func(done, total int) {
			pct := done * 100 / total
			if pct/10 != last/10 {
				fmt.Printf("upload: %d%% (%d/%d bytes)\n", pct, done, total)
				last = pct
			}
		})
		if err != nil {
			return fmt.Errorf("upload: %w", err)
		}
		if err := c.Reset(); err != nil {
			return fmt.Errorf("reset after upload: %w", err)
		}
		fmt.Println("upload complete, device reset")
		return nil
	})
}

func runList(args []string) error {
	fs := flag.NewFlagSet("list", flag.ContinueOnError)
	portName := portFlag(fs)
	if err := fs.Parse(args); err != nil {
		return err
	}
	return withClient(*portName, func(c *smp.Client) error {
		images, err := c.State()
		if err != nil {
			return fmt.Errorf("list: %w", err)
		}
		if len(images) == 0 {
			fmt.Println("no image")
		}
		for _, im := range images {
			fmt.Printf("image %d slot %d version %s hash %x\n", im.Image, im.Slot, im.Version, im.Hash)
		}
		return nil
	})
}

func runReset(args []string) error {
	fs := flag.NewFlagSet("reset", flag.ContinueOnError)
	portName := portFlag(fs)
	if err := fs.Parse(args); err != nil {
		return err
	}
	return withClient(*portName, func(c *smp.Client) error {
		return c.Reset()
	})
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	var err error
	switch os.Args[1] {
	case "merge":
		err = runMerge(os.Args[2:])
	case "sign":
		err = runSign(os.Args[2:])
	case "upload":
		err = runUpload(os.Args[2:])
	case "list":
		err = runList(os.Args[2:])
	case "reset":
		err = runReset(os.Args[2:])
	case "version":
		fmt.Printf("stm32h7rs-tool %s\n", version)
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
