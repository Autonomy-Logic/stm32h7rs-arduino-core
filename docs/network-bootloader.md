# STM32H7S7 network bootloader

The STM32H7S7 core programs a device over Ethernet. A small bootloader in the internal flash receives the program
over TFTP, writes it to the external flash and starts it. It replaces the MCUboot USB recovery of core 0.1.0 on
devices with no USB port.

The constants named here are defined in `variants/stm32h7s7_core_common/stm32h7rs_boot.h`, the file the
bootloader and the core are both built from. `stm32h7rs-tool` checks its own copies against that file in its
tests.

This is a proof-of-concept design: there is no authentication. Anyone on the network who can reach the
device can restart it into the bootloader and write a program.

## Flash and RAM map

| Address | Size | Content |
| --- | --- | --- |
| `0x08000000` | 56 KB | Bootloader (internal flash) |
| `0x0800E000` | 8 KB | Network settings record (internal flash, last sector) |
| `0x70000000` | 1 KB | Program image header |
| `0x70000400` | | Program: Zephyr loader, then the sketch at `0x70080000` |
| `0x70000000` | 8 MB | Program slot (external flash) |
| `0x70800000` | 64 KB | HMI store (not touched by the bootloader) |
| `0x24071F00` | 256 B | RAM mailbox (AXI SRAM, kept across warm resets) |

An upload writes only the program slot. The bootloader never writes the internal flash.

## Program image

A program image is the merged loader and sketch, preceded by a 1 KB header:

| Offset | Field | Value |
| --- | --- | --- |
| 0 | `magic` | `0x494C504F` ("OPLI") |
| 4 | `header_version` | 1 |
| 8 | `header_size` | `0x400` |
| 12 | `image_size` | whole image in bytes, header included |
| 16 | `payload_crc32` | CRC-32 of bytes `[header_size, image_size)` |
| 20 | `flags` | 0 |
| 24 | `version` | core version, 32 bytes, NUL padded |
| 56 | `header_crc32` | CRC-32 of bytes 0 to 55 |

The rest of the 1 KB is zero. CRC-32 is the IEEE/zlib polynomial. `stm32h7rs-tool pack` builds the header.

A program is valid when its header magic, version, size and both CRCs match, and its vector table (at offset
`0x400`) is plausible: the initial stack pointer is 8-byte aligned inside DTCM (`0x20000000`-`0x20030000`) or AXI
SRAM (`0x24000000`-`0x24072000`), and the reset handler is a Thumb address inside the image.

## Network settings record

At `0x0800E000`, written by the program, read by the bootloader:

| Offset | Field | Value |
| --- | --- | --- |
| 0 | `magic` | `0x4746434E` ("NCFG") |
| 4 | `version` | 1 (16 bits) |
| 6 | `mode` | 0 = DHCP, 1 = static (8 bits) |
| 7 | reserved | 0 |
| 8 | `ip` | IPv4 address, network byte order |
| 12 | `netmask` | |
| 16 | `gateway` | |
| 20 | `dns` | |
| 24 | reserved | 0 |
| 28 | `crc32` | CRC-32 of bytes 0 to 27 |

The program writes the record when it brings up its network with settings that differ from the stored ones. The
bootloader uses the stored settings when the record is valid and static. It uses DHCP when the record is blank,
damaged, of another version, or set to DHCP. When DHCP gets no answer, it uses `192.168.10.100/24`, gateway
`192.168.10.1`.

The MAC address is the same as the program's: `02:80:E1` followed by the first three bytes of the CRC-32 of the
chip's 96-bit unique ID.

## RAM mailbox

At `0x24071F00`, four 32-bit words: `magic` (`0x4D424C4F`), `request`, `strikes` and `check`
(`~(magic ^ request ^ strikes)`). A mailbox whose magic or check does not match, as after a power-up, counts as
empty.

- `request = 1`: the program asks the bootloader to stay for an upload. The program writes it, then resets.
- `strikes`: the bootloader adds one before each program start. The program sets it to zero after running for 30
  s.

## Start-up

On each reset the bootloader decides whether to start the program or stay:

1. `request = 1` in the mailbox: stays (entry `request`) and clears the request.
2. No valid program: stays (entry `no_app`).
3. `strikes` reached 3: stays (entry `crash_loop`) and sets `strikes` to zero.
4. Otherwise it adds a strike, arms the independent watchdog (16 s) and starts the program without touching the
   network.

When it stays with a valid program and nothing happens on the network for 60 s (no TFTP transfer in progress, no
request received), it starts the program.

The program must refresh the watchdog. The core does so from a timer every second.

## Restarting into the bootloader from a program

The OpenPLC runtime of a program answers the Modbus TCP request function code `0x4C` followed by the magic bytes
`B0 07 10 AD` (port 502, unit id 0) with `4C 7E` and then restarts into the bootloader, keeping its address
(the bootloader reads the same settings record).

## Services

### TFTP (UDP port 69)

- Octet mode only, one transfer at a time. Replies come from port 69. A different client is refused with an
  ERROR while a transfer is in progress and has been active within the last 3 s.
- The bootloader retransmits its last packet after 1 s, up to 6 times, then drops the transfer.

**`firmware.bin` (write).** Options: `blksize` (8 to 1468, default 512), `tsize` (image size) and `crc32` (8
hex digits, CRC-32 of the whole file). The bootloader answers with an OACK echoing the accepted options, or ACK
0 when no option was sent.

1. On the request: the image size from `tsize` must fit in the slot. The bootloader erases the first 4 KB sector,
   so the old program is invalid from now on.
2. Each block is written as it arrives; each 4 KB sector is erased just before the first byte written to it. The
   first 1 KB (the header) is kept in RAM.
3. After the last block (shorter than `blksize`): the size must equal `tsize` when given; the CRC-32 of the file
   (the header from RAM, the rest read back from the flash) must equal `crc32` when given; the header and
   vector table must be valid. Any failure is an ERROR with a text message, and the old program stays invalid.
4. The header is written last, then the last ACK is sent. The bootloader repeats it if the client sends the last
   block again within 1 s, then resets and starts the new program.

**`info` (read).** One DATA block of at most 511 bytes, options declined, `key=value` lines:

| Key | Value |
| --- | --- |
| `device` | device name, for example `SL-VISU-5R` |
| `core` | `stm32h7rs` |
| `mode` | `bootloader` |
| `bootloader` | bootloader version |
| `entry` | `request`, `no_app` or `crash_loop` |
| `mac` | MAC address |
| `ip` | current IPv4 address |
| `dhcp` | `seeking`, `bound`, `fallback` or `off` (static settings) |
| `app` | `valid` or `invalid` |
| `app_size`, `app_version` | present when the program is valid |

### Discovery (UDP port 33333)

A datagram starting with `OPENPLC_DISCOVER_V1` gets a JSON reply:
`{"service":"openplc-runtime","hostname":"<device>","device":"<device> (bootloader)","mac":"<mac>",
"runtime_version":"bootloader","api_port":502}`.

### Others

The bootloader answers ARP for its address and ICMP echo requests. It does not answer TCP.

## Upload sequence (`stm32h7rs-tool upload --ip`)

1. Read `info` over TFTP. If the bootloader answers, go to 3.
2. Send the Modbus request above to the program, then poll `info` until the bootloader answers (up to 20 s).
3. Write `firmware.bin` with `blksize` 1468, `tsize` and `crc32`.
4. Optionally wait for the program to answer Modbus TCP on port 502.
