# Network bootloader install requirement (STM32H7S7 core, SL-VISU-5R)

This is the one-time, factory-side step that prepares a unit for OpenPLC: it replaces the internal-flash bootloader
with the core's network bootloader. Every program after that is uploaded over Ethernet from the OpenPLC Editor.

## What to install

| Item | Value |
| --- | --- |
| Binary | `bootloader-sl_visu_5r-<version>.hex` (or `.bin`) from the core release |
| Checksum | the SHA-256 listed for that file in the release's `SHA256SUMS.txt` |
| Address | `0x08000000` (internal flash) |
| Erasable range | `0x08000000`-`0x0800FFFF`, the whole 64 KB internal flash |
| Option bytes | **leave unchanged**; `XSPI1_HSLV` and `XSPI2_HSLV` must stay enabled (as the factory bootloader sets them) |

The previous contents of the internal flash are not kept: the factory bootloader and its records (including the
assigned MAC address and serial number) are replaced. The unit then uses a MAC address derived from the chip's
unique ID (`02:80:E1:xx:xx:xx`). The last 8 KB sector (`0x0800E000`) is left blank: the program writes its network
settings there.

## How to install

With an SWD probe (ST-LINK and STM32CubeProgrammer, or any equivalent):

1. Connect to the STM32H7S7 under reset.
2. Erase the internal flash sectors (`0x08000000`-`0x0800FFFF`). Do not change option bytes.
3. Program the binary at `0x08000000` (the `.hex` carries the address) and verify.
4. Disconnect the probe and power-cycle the unit.

## Acceptance check

Connect the unit's Ethernet port to a switch with a PC on it, and a serial adapter to USART3 (PC10 TX, 115200 8N1) if
one is available.

1. After the power cycle, with no program installed yet, the unit stays in the bootloader. The console prints
   `[boot] staying: no valid program`, then the link and `address <ip>`.
2. The address comes from DHCP; with no DHCP server it is `192.168.10.100` after about 10 s. The bootloader also
   answers the OpenPLC Editor's network search as "SL-VISU-5R (bootloader)".
3. `stm32h7rs-tool info --ip <ip>` prints `mode=bootloader`, `entry=no_app` and `app=invalid`.
4. In the OpenPLC Editor, select the SL-VISU-5R, set its address, build and upload a project: the upload completes
   and the unit starts the program.

## Security

This is a proof of concept: there is no authentication. Anyone on the network can restart a running unit into the
bootloader and upload a program. Images carry CRC-32 checks only, which catch damaged transfers, not deliberate
changes.
