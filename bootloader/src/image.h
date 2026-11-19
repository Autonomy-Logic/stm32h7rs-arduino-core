/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Program image checks (docs/network-bootloader.md, "Program image").
 */

#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdint.h>

#include "stm32h7rs_boot.h"

/**
 * Checks a program image.
 *
 * @param header the image header (first STM32H7RS_IMAGE_HEADER_SIZE bytes)
 * @param payload the bytes that follow the header, as many as the header's image_size says
 * @param why set to a short reason when the image is refused
 * @return 0 when the image is valid
 */
int image_check(const stm32h7rs_image_header_t *header, const uint8_t *payload, const char **why);

/** Checks only the header fields and the header CRC, with no access to the payload. */
int image_check_header(const stm32h7rs_image_header_t *header, const char **why);

#endif /* BOOT_IMAGE_H */
