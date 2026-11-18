// SPDX-License-Identifier: Apache-2.0

// Command stm32h7rs-tool builds and uploads program images for the STM32H7S7 Arduino core.
package main

import (
	"flag"
	"fmt"
	"os"
	"strconv"

	"github.com/Autonomy-Logic/stm32h7rs-arduino-core/tools/stm32h7rs-tool/internal/image"
)

var version = "dev"

func usage() {
	fmt.Fprintln(os.Stderr, "usage: stm32h7rs-tool <merge|version> [flags]")
	os.Exit(2)
}

func runMerge(args []string) error {
	fs := flag.NewFlagSet("merge", flag.ContinueOnError)
	loaderPath := fs.String("loader", "", "loader image (zephyr-<variant>.bin)")
	sketchPath := fs.String("sketch", "", "prelinked sketch binary")
	offsetStr := fs.String("offset", "", "sketch offset from the start of the image, e.g. 0x00180000")
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

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	var err error
	switch os.Args[1] {
	case "merge":
		err = runMerge(os.Args[2:])
	case "version":
		fmt.Println(version)
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
