/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "image.h"

#include <stddef.h>

#include "crc32.h"

static uint32_t load_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int image_check_header(const stm32h7rs_image_header_t *header, const char **why)
{
	if (header->magic != STM32H7RS_IMAGE_MAGIC) {
		*why = "not a program image: bad header magic";
		return -1;
	}
	if (header->header_version != STM32H7RS_IMAGE_HEADER_VER ||
	    header->header_size != STM32H7RS_IMAGE_HEADER_SIZE) {
		*why = "unsupported image header version";
		return -1;
	}
	if (crc32_update(0u, header, STM32H7RS_IMAGE_HEADER_CRC_LEN) != header->header_crc32) {
		*why = "image header CRC does not match";
		return -1;
	}
	if (header->image_size < STM32H7RS_IMAGE_MIN_SIZE || header->image_size > STM32H7RS_SLOT_SIZE) {
		*why = "image size out of range";
		return -1;
	}
	return 0;
}

static int in_range(uint32_t v, uint32_t lo, uint32_t hi)
{
	return v >= lo && v <= hi;
}

int image_check(const stm32h7rs_image_header_t *header, const uint8_t *payload, const char **why)
{
	if (image_check_header(header, why) != 0) {
		return -1;
	}

	uint32_t payload_len = header->image_size - STM32H7RS_IMAGE_HEADER_SIZE;

	if (crc32_update(0u, payload, payload_len) != header->payload_crc32) {
		*why = "image payload CRC does not match";
		return -1;
	}

	uint32_t sp = load_le32(payload);
	uint32_t reset = load_le32(payload + 4);
	uint32_t code_lo = STM32H7RS_SLOT_BASE + STM32H7RS_IMAGE_HEADER_SIZE;
	uint32_t code_hi = STM32H7RS_SLOT_BASE + header->image_size;

	if ((sp & 7u) != 0u || !(in_range(sp, STM32H7RS_DTCM_BASE, STM32H7RS_DTCM_END) ||
				 in_range(sp, STM32H7RS_AXI_SRAM_BASE, STM32H7RS_AXI_SRAM_END))) {
		*why = "not a program for this core: bad initial stack pointer";
		return -1;
	}
	if ((reset & 1u) == 0u || (reset & ~1u) < code_lo + 8u || (reset & ~1u) >= code_hi) {
		*why = "not a program for this core: bad reset handler";
		return -1;
	}
	return 0;
}
