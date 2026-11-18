// SPDX-License-Identifier: Apache-2.0

// Package layout holds the flash layout values the tools need; a test keeps them equal to tools/ci/layout.json.
package layout

const (
	// SlotSize is the size of MCUboot slot 0, the largest image an upload may send.
	SlotSize = 0x00800000
	// SketchOffset is the offset of the sketch inside the image, after the loader partition.
	SketchOffset = 0x00080000
)
