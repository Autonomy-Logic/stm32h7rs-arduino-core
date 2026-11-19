/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#ifndef BOOT_CRC32_H
#define BOOT_CRC32_H

#include <stddef.h>
#include <stdint.h>

/** Continues an IEEE CRC-32 (zlib polynomial): start with crc = 0. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

#endif /* BOOT_CRC32_H */
