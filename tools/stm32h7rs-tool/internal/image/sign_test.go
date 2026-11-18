// SPDX-License-Identifier: Apache-2.0

package image

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/binary"
	"encoding/pem"
	"testing"
)

func testKey(t *testing.T) (*ecdsa.PrivateKey, []byte) {
	t.Helper()
	k, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	der, err := x509.MarshalPKCS8PrivateKey(k)
	if err != nil {
		t.Fatal(err)
	}
	return k, pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: der})
}

func payload() []byte {
	p := make([]byte, 0x400+1000)
	for i := 0x400; i < len(p); i++ {
		p[i] = byte(i)
	}
	return p
}

type tlv struct {
	kind  uint16
	value []byte
}

func parseTLVs(t *testing.T, signed []byte, end int) []tlv {
	t.Helper()
	if binary.LittleEndian.Uint16(signed[end:]) != tlvInfoMagic {
		t.Fatalf("no TLV info magic at 0x%X", end)
	}
	total := int(binary.LittleEndian.Uint16(signed[end+2:]))
	var out []tlv
	for off := end + 4; off < end+total; {
		kind := binary.LittleEndian.Uint16(signed[off:])
		n := int(binary.LittleEndian.Uint16(signed[off+2:]))
		out = append(out, tlv{kind, signed[off+4 : off+4+n]})
		off += 4 + n
	}
	if end+total != len(signed) {
		t.Fatalf("TLV area ends at 0x%X, image at 0x%X", end+total, len(signed))
	}
	return out
}

func TestSignWritesTheMCUbootHeader(t *testing.T) {
	_, keyPEM := testKey(t)
	signed, err := Sign(payload(), keyPEM, 0x400, Version{Major: 0, Minor: 1, Revision: 2, Build: 3})
	if err != nil {
		t.Fatalf("Sign: %v", err)
	}
	if binary.LittleEndian.Uint32(signed) != MCUbootMagic {
		t.Fatal("bad magic")
	}
	if binary.LittleEndian.Uint16(signed[8:]) != 0x400 {
		t.Fatal("bad header size")
	}
	if binary.LittleEndian.Uint32(signed[12:]) != 1000 {
		t.Fatalf("img_size = %d, want 1000", binary.LittleEndian.Uint32(signed[12:]))
	}
	if signed[20] != 0 || signed[21] != 1 || binary.LittleEndian.Uint16(signed[22:]) != 2 ||
		binary.LittleEndian.Uint32(signed[24:]) != 3 {
		t.Fatal("bad version fields")
	}
}

func TestSignAddsHashKeyHashAndValidSignature(t *testing.T) {
	key, keyPEM := testKey(t)
	in := payload()
	signed, err := Sign(in, keyPEM, 0x400, Version{})
	if err != nil {
		t.Fatalf("Sign: %v", err)
	}
	digest := sha256.Sum256(signed[:len(in)])
	pub, _ := x509.MarshalPKIXPublicKey(&key.PublicKey)
	keyHash := sha256.Sum256(pub)
	found := map[uint16][]byte{}
	for _, e := range parseTLVs(t, signed, len(in)) {
		found[e.kind] = e.value
	}
	if string(found[tlvSHA256]) != string(digest[:]) {
		t.Fatal("SHA256 TLV does not match the header and body")
	}
	if string(found[tlvKeyHash]) != string(keyHash[:]) {
		t.Fatal("KEYHASH TLV does not match the public key")
	}
	if !ecdsa.VerifyASN1(&key.PublicKey, digest[:], found[tlvECDSASig]) {
		t.Fatal("ECDSA signature does not verify")
	}
}

func TestSignRefusesNonZeroHeaderGap(t *testing.T) {
	_, keyPEM := testKey(t)
	in := payload()
	in[10] = 1
	if _, err := Sign(in, keyPEM, 0x400, Version{}); err == nil {
		t.Fatal("expected an error when the header gap is not empty")
	}
}

func TestSignRefusesBadKey(t *testing.T) {
	if _, err := Sign(payload(), []byte("not a key"), 0x400, Version{}); err == nil {
		t.Fatal("expected an error for a bad key")
	}
}

func TestParseVersion(t *testing.T) {
	v, err := ParseVersion("1.2.3+4")
	if err != nil || v != (Version{1, 2, 3, 4}) {
		t.Fatalf("got %+v, %v", v, err)
	}
	if v, err := ParseVersion("0.1.0-wip.3"); err != nil || v != (Version{0, 1, 0, 0}) {
		t.Fatalf("got %+v, %v", v, err)
	}
	if _, err := ParseVersion("1.x"); err == nil {
		t.Fatal("expected an error")
	}
}
