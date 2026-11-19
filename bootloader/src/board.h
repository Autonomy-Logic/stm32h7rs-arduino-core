/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * MCU setup of the bootloader: power, clocks, console, chip ID, watchdog and the hand-over to the program.
 */

#ifndef BOOT_BOARD_H
#define BOOT_BOARD_H

#include <stdint.h>

#include "stm32h7rs_boot.h"

/** Power, caches, 600 MHz clock tree (the program's), 1 ms tick, XSPI2 I/O cells and the console. */
void board_init(void);

/** Milliseconds since board_init(). */
uint32_t board_ms(void);

/** Busy-waits for ms milliseconds. */
void board_delay_ms(uint32_t ms);

/** Writes a line to the console. */
void board_log(const char *line);

/** The program's MAC address: 02:80:E1 and the first three bytes of the CRC-32 of the chip ID. */
void board_mac(uint8_t mac[6]);

/** CRC-32 of the chip ID, used to seed DHCP transaction IDs. */
uint32_t board_uid_crc(void);

/** True when the last reset came from power-up or brown-out (the mailbox RAM is then undefined). */
int board_cold_reset(void);

/** The RAM mailbox shared with the program. */
volatile stm32h7rs_mailbox_t *board_mailbox(void);

/** Arms the watchdog, resets the bootloader's peripherals and jumps to the program. Does not return. */
void board_start_program(void) __attribute__((noreturn));

/** Resets the device. */
void board_reset(void) __attribute__((noreturn));

/** Starts PLL3 and the Ethernet clocks and pins (ETH_CLK output to the PHY). */
void board_eth_clocks(void);

#endif /* BOOT_BOARD_H */
