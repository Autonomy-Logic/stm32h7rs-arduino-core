/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Octal NOR flash on XSPI2 (Macronix OctaBus, octal DTR). The set-up and commands mirror the program's Zephyr
 * XSPI driver, so the program can take the flash over in memory-mapped mode without re-initialising it.
 */

#ifndef BOOT_NOR_H
#define BOOT_NOR_H

#include <stdint.h>

/** Resets the flash, switches it to octal DTR and enables memory-mapped reads at 0x70000000. */
int nor_init(void);

/** Leaves memory-mapped mode for erase and program commands. */
int nor_indirect(void);

/** Returns to memory-mapped mode; reads through the 0x70000000 window are valid again. */
int nor_memmap(void);

/** Erases the 4 KB sector at offset (from the start of the flash). */
int nor_erase_sector(uint32_t offset);

/** Programs len bytes at offset; the range must be erased. Odd lengths are padded with 0xFF. */
int nor_program(uint32_t offset, const uint8_t *data, uint32_t len);

#endif /* BOOT_NOR_H */
