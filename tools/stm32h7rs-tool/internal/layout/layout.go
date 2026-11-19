// SPDX-License-Identifier: Apache-2.0

// Package layout holds the values of variants/stm32h7s7_core_common/stm32h7rs_boot.h that the tools need; a test
// keeps them equal to that file.
package layout

const (
	// SlotBase is the XIP address of the program slot; the image header sits there.
	SlotBase = 0x70000000
	// SlotSize is the size of the program slot, the largest image an upload may send.
	SlotSize = 0x00800000

	// DTCMBase and DTCMEnd bound the initial stack pointer of a program in DTCM.
	DTCMBase = 0x20000000
	DTCMEnd  = 0x20030000
	// AXISRAMBase and AXISRAMEnd bound the initial stack pointer of a program in AXI SRAM.
	AXISRAMBase = 0x24000000
	AXISRAMEnd  = 0x24072000

	// ImageHeaderSize is the size of the image header; the vector table follows it.
	ImageHeaderSize = 0x400
	// ImageMagic is the first word of the image header ("OPLI").
	ImageMagic = 0x494C504F
	// ImageHeaderVersion is the header_version field Pack writes and CheckImage accepts.
	ImageHeaderVersion = 1
	// ImageVersionLen is the size of the NUL-padded version field.
	ImageVersionLen = 32
	// ImageHeaderCRCLen is the number of header bytes covered by header_crc32.
	ImageHeaderCRCLen = 56
	// ImageMinSize is the header plus a vector table's initial SP and reset handler.
	ImageMinSize = ImageHeaderSize + 8

	// TFTPPort is the bootloader's TFTP request port.
	TFTPPort = 69
	// FileFirmware is the TFTP file an upload writes.
	FileFirmware = "firmware.bin"
	// FileInfo is the TFTP file the bootloader describes itself in.
	FileInfo = "info"
	// OptCRC32 is the TFTP option carrying the CRC-32 of the whole file.
	OptCRC32 = "crc32"
	// MaxBlksize is the largest TFTP block size the bootloader accepts.
	MaxBlksize = 1468

	// DiscoveryPort is the UDP port of the discovery service.
	DiscoveryPort = 33333

	// ModbusPort is the Modbus TCP port of the OpenPLC runtime.
	ModbusPort = 502
	// ModbusFCBootloader is the function code that restarts a program into the bootloader.
	ModbusFCBootloader = 0x4C
)
