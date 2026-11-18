// SPDX-License-Identifier: Apache-2.0

package image

import (
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/binary"
	"encoding/pem"
	"errors"
	"fmt"
	"strconv"
	"strings"
)

// MCUboot TLV values written by Sign: the image hash, the public key hash and an ECDSA-P256 signature.
const (
	tlvInfoMagic = 0x6907
	tlvKeyHash   = 0x01
	tlvSHA256    = 0x10
	tlvECDSASig  = 0x22
	headerUsed   = 32
)

// Version is the MCUboot image version: major.minor.revision+build.
type Version struct {
	Major    uint8
	Minor    uint8
	Revision uint16
	Build    uint32
}

// ParseVersion reads "major.minor.revision" with an optional "+build"; a "-prerelease" part is ignored.
func ParseVersion(s string) (Version, error) {
	var v Version
	main, build, hasBuild := strings.Cut(s, "+")
	main, _, _ = strings.Cut(main, "-")
	parts := strings.Split(main, ".")
	if len(parts) != 3 {
		return v, fmt.Errorf("version %q is not major.minor.revision[+build]", s)
	}
	nums := make([]uint64, 3)
	limits := []int{8, 8, 16}
	for i, p := range parts {
		n, err := strconv.ParseUint(p, 10, limits[i])
		if err != nil {
			return v, fmt.Errorf("version %q: %w", s, err)
		}
		nums[i] = n
	}
	v.Major, v.Minor, v.Revision = uint8(nums[0]), uint8(nums[1]), uint16(nums[2])
	if hasBuild {
		n, err := strconv.ParseUint(build, 10, 32)
		if err != nil {
			return v, fmt.Errorf("version %q: %w", s, err)
		}
		v.Build = uint32(n)
	}
	return v, nil
}

func loadKey(keyPEM []byte) (*ecdsa.PrivateKey, error) {
	block, _ := pem.Decode(keyPEM)
	if block == nil {
		return nil, errors.New("sign: the key file is not PEM")
	}
	parsed, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err != nil {
		return nil, fmt.Errorf("sign: %w", err)
	}
	key, ok := parsed.(*ecdsa.PrivateKey)
	if !ok || key.Curve.Params().Name != "P-256" {
		return nil, errors.New("sign: the key is not an ECDSA P-256 private key")
	}
	return key, nil
}

func appendTLV(b []byte, kind uint16, value []byte) []byte {
	b = binary.LittleEndian.AppendUint16(b, kind)
	b = binary.LittleEndian.AppendUint16(b, uint16(len(value)))
	return append(b, value...)
}

// Sign returns payload as a signed MCUboot image. payload starts with headerSize zero bytes reserved for the header,
// as Zephyr builds it with CONFIG_ROM_START_OFFSET; the header is written there and the TLV area appended.
func Sign(payload, keyPEM []byte, headerSize int, version Version) ([]byte, error) {
	key, err := loadKey(keyPEM)
	if err != nil {
		return nil, err
	}
	if headerSize < headerUsed || len(payload) <= headerSize {
		return nil, fmt.Errorf("sign: payload of %d bytes has no room after a 0x%X-byte header", len(payload), headerSize)
	}
	for _, b := range payload[:headerSize] {
		if b != 0 {
			return nil, errors.New("sign: the header area of the payload is not empty")
		}
	}
	img := make([]byte, len(payload))
	copy(img, payload)
	h := img[:headerUsed]
	binary.LittleEndian.PutUint32(h[0:], MCUbootMagic)
	binary.LittleEndian.PutUint32(h[4:], 0)
	binary.LittleEndian.PutUint16(h[8:], uint16(headerSize))
	binary.LittleEndian.PutUint16(h[10:], 0)
	binary.LittleEndian.PutUint32(h[12:], uint32(len(payload)-headerSize))
	binary.LittleEndian.PutUint32(h[16:], 0)
	h[20], h[21] = version.Major, version.Minor
	binary.LittleEndian.PutUint16(h[22:], version.Revision)
	binary.LittleEndian.PutUint32(h[24:], version.Build)
	binary.LittleEndian.PutUint32(h[28:], 0)

	digest := sha256.Sum256(img)
	pub, err := x509.MarshalPKIXPublicKey(&key.PublicKey)
	if err != nil {
		return nil, fmt.Errorf("sign: %w", err)
	}
	keyHash := sha256.Sum256(pub)
	sig, err := ecdsa.SignASN1(rand.Reader, key, digest[:])
	if err != nil {
		return nil, fmt.Errorf("sign: %w", err)
	}
	var tlvs []byte
	tlvs = appendTLV(tlvs, tlvSHA256, digest[:])
	tlvs = appendTLV(tlvs, tlvKeyHash, keyHash[:])
	tlvs = appendTLV(tlvs, tlvECDSASig, sig)
	img = binary.LittleEndian.AppendUint16(img, tlvInfoMagic)
	img = binary.LittleEndian.AppendUint16(img, uint16(4+len(tlvs)))
	return append(img, tlvs...), nil
}
