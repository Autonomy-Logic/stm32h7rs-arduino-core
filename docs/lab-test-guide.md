# SL-VISU-5R lab test guide (proof of concept)

## Hand-off contents

- The signed SL-VISU-5R package (`.vpp`) for the OpenPLC Editor; the editor build carrying the HMI feature is supplied
  separately.
- From the core release: `bootloader-sl_visu_5r-<version>.hex`/`.bin`, `bootloader-install-requirement.md`,
  `network-bootloader.md`, this guide, `stm32h7rs-invalid-test-image.bin`, and
  `stm32h7rs-abuse-<version>-<host>.tar.gz` (the abuse-check tool).
- `stm32h7rs-tool` is installed with the core; it is also in the release for manual use.

## Prerequisites

- The network bootloader installed per `bootloader-install-requirement.md`, acceptance check passed.
- The OpenPLC Editor build with the HMI feature, the SL-VISU-5R package installed, a demo project with screens.
- A PC on the same Ethernet switch as the unit, with a DHCP server on that network for TC22 and TC26 (a router is
  enough). A USB-to-serial adapter on USART3 (PC10/PC11, 115200) helps: the bootloader reports what it does there.

## Scenarios to run

| ID | Check | How |
| --- | --- | --- |
| TC14 | Bootloader entry | a) unit with no program: stays in the bootloader and answers `stm32h7rs-tool info --ip <ip>` with `entry=no_app`; b) unit running a program: an upload from the editor restarts it into the bootloader by itself (Modbus request) |
| TC15 | Build and Upload over Ethernet | upload from the editor in three states: empty unit, unit running a program, unit running a different program; each time the new program runs |
| TC16 | Stable MAC address | read the MAC from the switch or with `arp`; power-cycle twice; it does not change, starts with `02:80:E1` and is the same in the bootloader (`info`) and in the program |
| TC17 | Landscape screens, PLC running | the demo screens fill the 800x480 display in landscape; a PLC value changes on screen |
| TC18 | Touch after calibration | calibrate when asked; taps on 5 controls in the corners and centre select the tapped control |
| TC19 | Calibration kept | with calibration mode "once": power-cycle, and upload the same project again; no calibration is asked |
| TC20 | Invalid program refused | `stm32h7rs-tool upload --skip-local-check --ip <ip> stm32h7rs-invalid-test-image.bin`: the bootloader refuses it ("image payload CRC does not match"), the unit stays in the bootloader (`app=invalid`); a valid upload afterwards works |
| TC21 | Interrupted upload | unplug the Ethernet cable halfway through an upload; plug back: the unit is in the bootloader (`app=invalid`); the next upload succeeds |
| TC22 | Network address | a) project set to DHCP: the program and, after an upload request, the bootloader get the same DHCP address; b) project set to a static address: both use it |
| TC23 | Modbus TCP | a Modbus TCP client reads and writes the project's registers |
| TC24 | Debugger | the editor's debugger connects over Ethernet |
| TC25 | 10-minute soak | the demo runs 10 minutes with no Modbus client, then 10 minutes with a client polling; the heartbeat keeps increasing and taps select controls |
| TC26 | Bootloader fallback address | erase the internal flash sector `0x0800E000` with the SWD probe (or start a unit that never ran a program) on a network with no DHCP server: the bootloader takes `192.168.10.100` after about 10 s |
| TC27 | Crash recovery | a program that hangs at start (for example an endless loop in setup with interrupts disabled) is restarted by the watchdog; after 3 restarts the unit stays in the bootloader (`entry=crash_loop`) and accepts an upload |
| TC28 | Return to the program | restart a unit with a valid program into the bootloader (`stm32h7rs-abuse modbus-reboot`); with no network activity for 60 s it starts the program again |

## Abuse checks

The tool is `stm32h7rs-abuse`. Addresses are those of the unit under test.

| ID | Command | Expected |
| --- | --- | --- |
| ABUSE-03 | capture on the switch from power-up until 2 minutes after the start screen, with the project's network left at its defaults | only the traffic the project's network settings call for (DHCP when set to DHCP); nothing from the bootloader on a normal start |
| ABUSE-04 | `stm32h7rs-abuse modbus-reboot --host <unit IP>` | known limitation of this proof of concept: the unit restarts into the bootloader (the restart request has no authentication); it returns to the program after 60 s |
| ABUSE-05 | `stm32h7rs-abuse modbus-flood --host <unit IP> --connections 4 --duration 10m` during TC25 | the 4th connection is closed by the unit, the other 3 keep getting replies; the heartbeat keeps increasing and taps work |

ABUSE-01 and ABUSE-02 checked the USB recovery protocol, which is no longer used.

## Lab report

For each TC and ABUSE row: pass or fail, notes, and the serial console log or photos where useful. Also report the
frame rate seen on the demo, the time a Build and Upload takes, and the package and core versions tested.

## Open items to confirm

- The backlight lights at full brightness.
- The board revision: Ethernet TXD1 is on PB7 on the revision the firmware targets; a revision that moves it to PB1
  needs a core change.
- The unit's power source and that the Ethernet PHY fitted is a KSZ8081RNB (the firmware drives it in RMII 50 MHz
  clock mode at MDIO address 0).
