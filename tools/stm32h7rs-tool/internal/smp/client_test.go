// SPDX-License-Identifier: Apache-2.0

package smp

import (
	"bytes"
	"errors"
	"io"
	"testing"

	"github.com/fxamacker/cbor/v2"
)

// fakeDevice answers SMP requests like MCUboot serial recovery, keeping the uploaded image.
type fakeDevice struct {
	in       bytes.Buffer
	out      bytes.Buffer
	image    []byte
	size     int
	resets   int
	failAt   int
	lastSeen []uploadRequest
}

func (d *fakeDevice) Write(p []byte) (int, error) {
	d.in.Write(p)
	for {
		frameEnd := bytes.IndexByte(d.in.Bytes(), '\n')
		if frameEnd < 0 {
			return len(p), nil
		}
		r := NewFrameReader(bytes.NewReader(d.in.Bytes()))
		packet, err := r.ReadPacket()
		if err != nil {
			return len(p), nil
		}
		d.in.Next(r.Consumed())
		d.serve(packet)
	}
}

func (d *fakeDevice) Read(p []byte) (int, error) {
	if d.out.Len() == 0 {
		return 0, io.EOF
	}
	return d.out.Read(p)
}

func (d *fakeDevice) reply(req Header, body []byte) {
	h := Header{Op: req.Op + 1, Length: uint16(len(body)), Group: req.Group, Seq: req.Seq, ID: req.ID}
	d.out.Write(EncodeFrame(append(h.Bytes(), body...)))
}

func (d *fakeDevice) serve(packet []byte) {
	h := ParseHeader(packet)
	body := packet[HeaderLen:]
	switch {
	case h.Group == GroupImage && h.ID == IDImageUpload:
		var req uploadRequest
		_ = cbor.Unmarshal(body, &req)
		d.lastSeen = append(d.lastSeen, req)
		if req.Off == 0 {
			d.size = int(req.Len)
			d.image = nil
		}
		if d.failAt > 0 && int(req.Off) >= d.failAt {
			out, _ := cbor.Marshal(rcResponse{Rc: 3})
			d.reply(h, out)
			return
		}
		d.image = append(d.image, req.Data...)
		out, _ := cbor.Marshal(uploadResponse{Rc: 0, Off: uint64(len(d.image))})
		d.reply(h, out)
	case h.Group == GroupImage && h.ID == IDImageState:
		out, _ := cbor.Marshal(stateResponse{Images: []ImageState{{Slot: 0, Version: "0.1.0", Hash: []byte{1, 2}}}})
		d.reply(h, out)
	case h.Group == GroupOS && h.ID == IDOSReset:
		d.resets++
		out, _ := cbor.Marshal(rcResponse{Rc: 0})
		d.reply(h, out)
	}
}

func TestUploadSendsWholeImageInOrderAndResets(t *testing.T) {
	dev := &fakeDevice{}
	img := make([]byte, 5000)
	for i := range img {
		img[i] = byte(i * 7)
	}
	c := NewClient(dev)
	if err := c.Upload(img, 0, nil); err != nil {
		t.Fatalf("Upload: %v", err)
	}
	if !bytes.Equal(dev.image, img) {
		t.Fatal("device image differs from the uploaded one")
	}
	if dev.lastSeen[0].Len != uint64(len(img)) {
		t.Fatalf("first chunk len = %d", dev.lastSeen[0].Len)
	}
	for _, r := range dev.lastSeen[1:] {
		if r.Len != 0 {
			t.Fatal("only the first chunk may carry len")
		}
	}
	if err := c.Reset(); err != nil || dev.resets != 1 {
		t.Fatalf("Reset: %v, resets %d", err, dev.resets)
	}
}

func TestUploadChunksFitTheReceiveBuffer(t *testing.T) {
	dev := &fakeDevice{}
	c := NewClient(dev)
	if err := c.Upload(make([]byte, 4096), 0, nil); err != nil {
		t.Fatalf("Upload: %v", err)
	}
	for _, r := range dev.lastSeen {
		packet, err := uploadPacket(r)
		if err != nil {
			t.Fatal(err)
		}
		if len(packet)+4 > MaxDecodedFrame {
			t.Fatalf("decoded frame %d bytes exceeds %d", len(packet)+4, MaxDecodedFrame)
		}
	}
}

func TestUploadStopsOnDeviceError(t *testing.T) {
	dev := &fakeDevice{failAt: 2000}
	err := NewClient(dev).Upload(make([]byte, 5000), 0, nil)
	var rcErr *RcError
	if !errors.As(err, &rcErr) || rcErr.Rc != 3 {
		t.Fatalf("got %v, want an rc 3 error", err)
	}
}

func TestUploadCarriesTheRequestedImageNumber(t *testing.T) {
	dev := &fakeDevice{}
	if err := NewClient(dev).Upload(make([]byte, 100), 1, nil); err != nil {
		t.Fatalf("Upload: %v", err)
	}
	if dev.lastSeen[0].Image == nil || *dev.lastSeen[0].Image != 1 {
		t.Fatal("first chunk must carry image 1")
	}
}

func TestStateLists(t *testing.T) {
	images, err := NewClient(&fakeDevice{}).State()
	if err != nil || len(images) != 1 || images[0].Version != "0.1.0" {
		t.Fatalf("got %+v, %v", images, err)
	}
}

func TestFirstChunkReportsTheDeviceResult(t *testing.T) {
	dev := &fakeDevice{}
	rc, err := NewClient(dev).FirstChunk(3, 1<<20, []byte{1, 2, 3, 4})
	if err != nil || rc != 0 {
		t.Fatalf("rc %d, err %v", rc, err)
	}
	req := dev.lastSeen[0]
	if req.Image == nil || *req.Image != 3 || req.Len != 1<<20 || req.Off != 0 {
		t.Fatalf("unexpected first chunk %+v", req)
	}
}

func TestFirstChunkReturnsNonZeroRcWithoutError(t *testing.T) {
	dev := &fakeDevice{failAt: 0}
	dev.failAt = -1
	rc, err := NewClient(&rejectingDevice{fakeDevice: dev}).FirstChunk(0, 10, []byte{1})
	if err != nil || rc != 8 {
		t.Fatalf("rc %d, err %v", rc, err)
	}
}

// rejectingDevice answers every upload with rc 8, like MCUboot refusing an oversize image.
type rejectingDevice struct {
	*fakeDevice
}

func (d *rejectingDevice) Write(p []byte) (int, error) {
	r := NewFrameReader(bytes.NewReader(p))
	packet, err := r.ReadPacket()
	if err == nil {
		out, _ := cbor.Marshal(rcResponse{Rc: 8})
		d.reply(ParseHeader(packet), out)
	}
	return len(p), nil
}
