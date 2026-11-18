// SPDX-License-Identifier: Apache-2.0

package image

import (
	"bytes"
	"testing"
)

func TestMergePadsLoaderWithFFAndAppendsSketch(t *testing.T) {
	loader := []byte{1, 2, 3}
	sketch := []byte{9, 8}
	got, err := Merge(loader, sketch, 6)
	if err != nil {
		t.Fatalf("Merge: %v", err)
	}
	want := []byte{1, 2, 3, 0xFF, 0xFF, 0xFF, 9, 8}
	if !bytes.Equal(got, want) {
		t.Fatalf("got % x, want % x", got, want)
	}
}

func TestMergeAcceptsLoaderFillingTheOffsetExactly(t *testing.T) {
	got, err := Merge([]byte{1, 2}, []byte{3}, 2)
	if err != nil {
		t.Fatalf("Merge: %v", err)
	}
	if !bytes.Equal(got, []byte{1, 2, 3}) {
		t.Fatalf("got % x", got)
	}
}

func TestMergeRefusesLoaderLargerThanOffset(t *testing.T) {
	if _, err := Merge([]byte{1, 2, 3}, []byte{4}, 2); err == nil {
		t.Fatal("expected an error for a loader larger than the offset")
	}
}

func TestMergeRefusesEmptySketch(t *testing.T) {
	if _, err := Merge([]byte{1}, nil, 4); err == nil {
		t.Fatal("expected an error for an empty sketch")
	}
}

func TestMergeDoesNotModifyItsInputs(t *testing.T) {
	loader := make([]byte, 2, 16)
	loader[0], loader[1] = 1, 2
	_, err := Merge(loader, []byte{7}, 4)
	if err != nil {
		t.Fatalf("Merge: %v", err)
	}
	if loader[:4][2] != 0 {
		t.Fatal("Merge wrote into the loader's backing array")
	}
}
