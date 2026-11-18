// SPDX-License-Identifier: Apache-2.0

package layout

import (
	"encoding/json"
	"os"
	"strconv"
	"testing"
)

type partition struct {
	Size   string `json:"size"`
	Offset string `json:"offset"`
}

type layoutFile struct {
	Partitions map[string]partition `json:"partitions"`
}

func TestConstantsMatchTheCoreLayout(t *testing.T) {
	raw, err := os.ReadFile("../../../ci/layout.json")
	if err != nil {
		t.Fatalf("read layout.json: %v", err)
	}
	var l layoutFile
	if err := json.Unmarshal(raw, &l); err != nil {
		t.Fatal(err)
	}
	size, _ := strconv.ParseInt(l.Partitions["slot0_partition"].Size, 0, 64)
	if size != SlotSize {
		t.Fatalf("SlotSize = 0x%X, layout.json slot0_partition size = 0x%X", SlotSize, size)
	}
	off, _ := strconv.ParseInt(l.Partitions["user_sketch"].Offset, 0, 64)
	if off != SketchOffset {
		t.Fatalf("SketchOffset = 0x%X, layout.json user_sketch offset = 0x%X", SketchOffset, off)
	}
}
