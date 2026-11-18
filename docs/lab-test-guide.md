# SL-VISU-5R lab test guide (proof of concept)

## Hand-off contents

- The signed SL-VISU-5R package (`.vpp`) for the OpenPLC Editor; the editor build carrying the HMI feature is supplied
  separately.
- From the core release: `mcuboot-stm32h7s7-<version>.bin`/`.hex`, `mcuboot-install-requirement.md`, this guide,
  `stm32h7rs-invalid-test-image.bin`, and `stm32h7rs-abuse-<version>-<host>.tar.gz` (the abuse-check tool).
- `stm32h7rs-tool` is installed with the core; it is also in the release for manual use.

## Prerequisites

- MCUboot installed per `mcuboot-install-requirement.md`, acceptance check passed.
- The OpenPLC Editor build with the HMI feature, the SL-VISU-5R package installed, a demo project with screens.
- A PC on the same Ethernet switch as the unit for the network checks.

## Scenarios to run

| ID | Check | How |
| --- | --- | --- |
| TC14 | MCUboot recovery | a) unit with no program: enumerates as PID `0x0101`; b) unit running a program: an upload from the editor switches it to recovery by itself (1200 bps request) |
| TC15 | Build and Upload over USB | upload from the editor in three states: empty unit, unit running a program, unit running a different program; each time the new program runs |
| TC16 | Stable MAC address | read the MAC from the switch or with `arp`; power-cycle twice; it does not change and starts with `02:80:E1` |
| TC17 | Landscape screens, PLC running | the demo screens fill the 800x480 display in landscape; a PLC value changes on screen |
| TC18 | Touch after calibration | calibrate when asked; taps on 5 controls in the corners and centre select the tapped control |
| TC19 | Calibration kept | with calibration mode "once": power-cycle, and upload the same project again; no calibration is asked |
| TC20 | Invalid program refused | `stm32h7rs-tool upload --port <port> stm32h7rs-invalid-test-image.bin` (after putting the unit in recovery); the unit does not start it and stays in recovery; a valid upload afterwards works |
| TC21 | Interrupted upload | unplug USB halfway through an upload; plug back: the unit is in recovery; the next upload succeeds |
| TC22 | Network address | enable the network on the Network screen: DHCP gets an address; a static address is used as set |
| TC23 | Modbus TCP | a Modbus TCP client reads and writes the project's registers |
| TC24 | Debugger | the editor's debugger connects over USB and over Ethernet |
| TC25 | 10-minute soak | the demo runs 10 minutes with no Modbus client, then 10 minutes with a client polling; the heartbeat keeps increasing and taps select controls |

## Abuse checks

The tool is `stm32h7rs-abuse`. Ports and addresses are those of the unit under test.

| ID | Command | Expected |
| --- | --- | --- |
| ABUSE-01 | `stm32h7rs-abuse smp-image-number --port <recovery port> --image 1`, then `--image 4294967294` | the unit keeps answering the image list after each; a valid upload afterwards starts without asking for calibration |
| ABUSE-02 | `stm32h7rs-abuse smp-oversize --port <recovery port>` | the upload is refused with an error code |
| ABUSE-03 | capture on the switch from power-up until 2 minutes after the start screen, with the Network screen left at its defaults | no frame from the unit |
| ABUSE-04 | `stm32h7rs-abuse modbus-reboot --host <unit IP>` | the unit does not reboot into recovery; the PLC keeps running |
| ABUSE-05 | `stm32h7rs-abuse modbus-flood --host <unit IP> --connections 4 --duration 10m` during TC25 | the 4th connection is closed by the unit, the other 3 keep getting replies; the heartbeat keeps increasing and taps work |

## Lab report

For each TC and ABUSE row: pass or fail, notes, and the serial console log or photos where useful. Also report the
frame rate seen on the demo, the time a Build and Upload takes, and the package and core versions tested.

## Open items to confirm

- Whether holding PE1 at a fixed level (full brightness) is safe for the AP3032 backlight driver.
- USB vendor and product IDs for the product (the PoC uses the Zephyr test vendor ID `0x2FE3`).
- The unit's power source (USB or its own supply) and the Ethernet PHY part fitted (the firmware drives a KSZ8081).
