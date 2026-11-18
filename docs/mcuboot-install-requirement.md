# MCUboot install requirement (STM32H7S7 core, SL-VISU-5R)

This is the one-time, factory-side step that prepares a unit for OpenPLC: it replaces the internal-flash bootloader
with the core's MCUboot. Every program after that is uploaded over USB from the OpenPLC Editor.

## What to install

| Item | Value |
| --- | --- |
| Binary | `mcuboot-stm32h7s7-<version>.bin` (or `.hex`) from the core release |
| Checksum | the SHA-256 listed for that file in the release's `SHA256SUMS.txt` |
| Address | `0x08000000` (internal flash) |
| Erasable range | `0x08000000`-`0x0800FFFF`, the whole 64 KB internal flash |
| Option bytes | **leave unchanged**; `XSPI1_HSLV` and `XSPI2_HSLV` must stay enabled (as the factory bootloader sets them) |

The previous contents of the internal flash are not kept: the factory bootloader and its records (including the
assigned MAC address and serial number) are replaced. The unit then uses a MAC address derived from the chip's
unique ID.

## How to install

With an SWD probe (ST-LINK and STM32CubeProgrammer, or any equivalent):

1. Connect to the STM32H7S7 under reset.
2. Erase the internal flash sectors (`0x08000000`-`0x0800FFFF`). Do not change option bytes.
3. Program the binary at `0x08000000` (the `.hex` carries the address) and verify.
4. Disconnect the probe and power-cycle the unit.

## Acceptance check

1. After the power cycle, with no program installed yet, the unit stays in MCUboot recovery and its USB port enumerates
   as a serial port with VID `0x2FE3`, PID `0x0101` ("STM32H7RS recovery").
2. `stm32h7rs-tool list --port <port>` answers (no image yet).
3. In the OpenPLC Editor, select the SL-VISU-5R, build and upload a project: the upload completes and the unit starts
   the program (USB re-enumerates with PID `0x0100`).

## Signing key

MCUboot accepts only images signed with the core's published **development key**. It proves integrity, not
authenticity, and is for the proof of concept only, never for production units.
