// SPDX-License-Identifier: Apache-2.0

// Package image builds the single program image MCUboot validates: the loader, 0xFF padding up to the sketch
// offset, then the prelinked sketch.
package image

import (
	"errors"
	"fmt"
)

// Merge returns the loader, 0xFF bytes up to offset, then the sketch. The inputs are not modified.
func Merge(loader, sketch []byte, offset int) ([]byte, error) {
	if len(sketch) == 0 {
		return nil, errors.New("merge: the sketch is empty")
	}
	if len(loader) > offset {
		return nil, fmt.Errorf("merge: loader (%d bytes) exceeds the sketch offset (0x%X)", len(loader), offset)
	}
	out := make([]byte, offset, offset+len(sketch))
	copy(out, loader)
	for i := len(loader); i < offset; i++ {
		out[i] = 0xFF
	}
	return append(out, sketch...), nil
}
