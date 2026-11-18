// SPDX-License-Identifier: Apache-2.0

package smp

import (
	"errors"
	"fmt"
	"io"

	"github.com/fxamacker/cbor/v2"
)

// uploadRequest is the body of an image upload chunk; len is sent only with the first chunk.
type uploadRequest struct {
	Image *uint32 `cbor:"image,omitempty"`
	Data  []byte  `cbor:"data"`
	Len   uint64  `cbor:"len,omitempty"`
	Off   uint64  `cbor:"off"`
}

type uploadResponse struct {
	Rc  int64  `cbor:"rc"`
	Off uint64 `cbor:"off"`
}

type rcResponse struct {
	Rc int64 `cbor:"rc"`
}

// ImageState is one image entry of an image state read.
type ImageState struct {
	Image   uint32 `cbor:"image"`
	Slot    uint32 `cbor:"slot"`
	Version string `cbor:"version"`
	Hash    []byte `cbor:"hash"`
}

type stateResponse struct {
	Rc     int64        `cbor:"rc"`
	Images []ImageState `cbor:"images"`
}

// RcError is a non-zero SMP result code returned by the device.
type RcError struct {
	Rc int64
}

func (e *RcError) Error() string {
	return fmt.Sprintf("device returned SMP error rc=%d", e.Rc)
}

// Client sends SMP requests over a serial byte stream and waits for each response.
type Client struct {
	rw     io.ReadWriter
	frames *FrameReader
	seq    uint8
}

// NewClient returns a Client on rw.
func NewClient(rw io.ReadWriter) *Client {
	return &Client{rw: rw, frames: NewFrameReader(rw)}
}

func uploadPacket(req uploadRequest) ([]byte, error) {
	body, err := cbor.Marshal(req)
	if err != nil {
		return nil, err
	}
	h := Header{Op: OpWrite, Length: uint16(len(body)), Group: GroupImage, ID: IDImageUpload}
	return append(h.Bytes(), body...), nil
}

func (c *Client) request(op uint8, group uint16, id uint8, body []byte) ([]byte, error) {
	c.seq++
	h := Header{Op: op, Length: uint16(len(body)), Group: group, Seq: c.seq, ID: id}
	if _, err := c.rw.Write(EncodeFrame(append(h.Bytes(), body...))); err != nil {
		return nil, err
	}
	for {
		packet, err := c.frames.ReadPacket()
		if err != nil {
			if errors.Is(err, io.EOF) {
				return nil, errors.New("smp: no response from the device")
			}
			return nil, err
		}
		if len(packet) < HeaderLen {
			continue
		}
		rh := ParseHeader(packet)
		if rh.Seq == c.seq && rh.Group == group && rh.ID == id {
			return packet[HeaderLen:], nil
		}
	}
}

// chunkLimit returns the largest data length whose upload packet fits MCUboot's receive buffer.
func chunkLimit(req uploadRequest) (int, error) {
	n := MaxDecodedFrame
	for n > 0 {
		req.Data = make([]byte, n)
		p, err := uploadPacket(req)
		if err != nil {
			return 0, err
		}
		if len(p)+4 <= MaxDecodedFrame {
			return n &^ 3, nil
		}
		n -= len(p) + 4 - MaxDecodedFrame
	}
	return 0, errors.New("smp: no room for upload data")
}

// Upload sends image as the given image number in chunks, following the offsets the device returns.
// progress, when not nil, is called after each acknowledged chunk.
func (c *Client) Upload(image []byte, imageNum uint32, progress func(done, total int)) error {
	var off uint64
	for off < uint64(len(image)) {
		req := uploadRequest{Off: off}
		if off == 0 {
			req.Len = uint64(len(image))
			if imageNum != 0 {
				n := imageNum
				req.Image = &n
			}
		}
		limit, err := chunkLimit(req)
		if err != nil {
			return err
		}
		end := min(off+uint64(limit), uint64(len(image)))
		req.Data = image[off:end]
		body, err := cbor.Marshal(req)
		if err != nil {
			return err
		}
		raw, err := c.request(OpWrite, GroupImage, IDImageUpload, body)
		if err != nil {
			return err
		}
		var resp uploadResponse
		if err := cbor.Unmarshal(raw, &resp); err != nil {
			return fmt.Errorf("smp: bad upload response: %w", err)
		}
		if resp.Rc != 0 {
			return &RcError{Rc: resp.Rc}
		}
		if resp.Off <= off || resp.Off > uint64(len(image)) {
			return fmt.Errorf("smp: device acknowledged offset %d after %d", resp.Off, off)
		}
		off = resp.Off
		if progress != nil {
			progress(int(off), len(image))
		}
	}
	return nil
}

// State reads the image list.
func (c *Client) State() ([]ImageState, error) {
	empty, _ := cbor.Marshal(map[string]uint8{})
	raw, err := c.request(OpRead, GroupImage, IDImageState, empty)
	if err != nil {
		return nil, err
	}
	var resp stateResponse
	if err := cbor.Unmarshal(raw, &resp); err != nil {
		return nil, fmt.Errorf("smp: bad state response: %w", err)
	}
	if resp.Rc != 0 {
		return nil, &RcError{Rc: resp.Rc}
	}
	return resp.Images, nil
}

// Reset asks the device to reset.
func (c *Client) Reset() error {
	empty, _ := cbor.Marshal(map[string]uint8{})
	raw, err := c.request(OpWrite, GroupOS, IDOSReset, empty)
	if err != nil {
		return err
	}
	var resp rcResponse
	if err := cbor.Unmarshal(raw, &resp); err != nil {
		return fmt.Errorf("smp: bad reset response: %w", err)
	}
	if resp.Rc != 0 {
		return &RcError{Rc: resp.Rc}
	}
	return nil
}

// FirstChunk sends only the first upload chunk, with the given image number and declared total length, and returns
// the device's result code. Used by the lab abuse checks; a normal upload uses Upload.
func (c *Client) FirstChunk(imageNum uint32, declaredLen uint64, data []byte) (int64, error) {
	n := imageNum
	req := uploadRequest{Image: &n, Data: data, Len: declaredLen, Off: 0}
	body, err := cbor.Marshal(req)
	if err != nil {
		return 0, err
	}
	raw, err := c.request(OpWrite, GroupImage, IDImageUpload, body)
	if err != nil {
		return 0, err
	}
	var resp rcResponse
	if err := cbor.Unmarshal(raw, &resp); err != nil {
		return 0, fmt.Errorf("smp: bad upload response: %w", err)
	}
	return resp.Rc, nil
}
