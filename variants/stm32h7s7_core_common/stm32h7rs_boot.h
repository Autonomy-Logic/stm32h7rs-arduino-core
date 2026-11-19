/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Contract between the STM32H7S7 network bootloader (bootloader/) and the program images of this core: flash
 * map, program image header, RAM mailbox and network settings record. Little-endian. The upload tool checks its
 * own constants against this file (tools/stm32h7rs-tool/internal/layout).
 */

#ifndef STM32H7RS_BOOT_H
#define STM32H7RS_BOOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Flash map ---- */

/** Internal flash: the bootloader, then the network settings sector (the last 8 KB sector). */
#define STM32H7RS_BOOT_BASE           0x08000000u
#define STM32H7RS_BOOT_SIZE           0x0000E000u
#define STM32H7RS_NETCFG_ADDR         0x0800E000u
#define STM32H7RS_NETCFG_SECTOR_SIZE  0x00002000u
/** Program write granularity of the internal flash, in bytes. */
#define STM32H7RS_NETCFG_WRITE_BLOCK  32u

/** External flash (XIP): program slot. The image header occupies the first IMAGE_HEADER_SIZE bytes. */
#define STM32H7RS_SLOT_BASE           0x70000000u
#define STM32H7RS_SLOT_SIZE           0x00800000u
#define STM32H7RS_EXT_ERASE_SIZE      0x00001000u

/** Initial stack pointer ranges a program vector table may use: DTCM and AXI SRAM. */
#define STM32H7RS_DTCM_BASE           0x20000000u
#define STM32H7RS_DTCM_END            0x20030000u
#define STM32H7RS_AXI_SRAM_BASE       0x24000000u
#define STM32H7RS_AXI_SRAM_END        0x24072000u

/* ---- Program image header (written last by the bootloader) ---- */

#define STM32H7RS_IMAGE_HEADER_SIZE   0x400u
#define STM32H7RS_IMAGE_MAGIC         0x494C504Fu /* "OPLI" */
#define STM32H7RS_IMAGE_HEADER_VER    1u
#define STM32H7RS_IMAGE_VERSION_LEN   32u
/** Smallest image: the header plus a vector table's initial SP and reset handler. */
#define STM32H7RS_IMAGE_MIN_SIZE      (STM32H7RS_IMAGE_HEADER_SIZE + 8u)

/** Image header at STM32H7RS_SLOT_BASE; the program vector table follows at offset IMAGE_HEADER_SIZE. */
typedef struct {
	uint32_t magic;          /**< STM32H7RS_IMAGE_MAGIC */
	uint32_t header_version; /**< STM32H7RS_IMAGE_HEADER_VER */
	uint32_t header_size;    /**< STM32H7RS_IMAGE_HEADER_SIZE */
	uint32_t image_size;     /**< whole image in bytes, header included */
	uint32_t payload_crc32;  /**< CRC-32 of bytes [header_size, image_size) */
	uint32_t flags;          /**< 0 */
	char version[STM32H7RS_IMAGE_VERSION_LEN]; /**< core version, NUL padded */
	uint32_t header_crc32;   /**< CRC-32 of the bytes above */
} stm32h7rs_image_header_t;

#define STM32H7RS_IMAGE_HEADER_CRC_LEN 56u

/* ---- RAM mailbox (AXI SRAM, kept across warm resets, outside the program's RAM) ---- */

#define STM32H7RS_MAILBOX_ADDR        0x24071F00u
#define STM32H7RS_MAILBOX_SIZE        0x100u
#define STM32H7RS_MAILBOX_MAGIC       0x4D424C4Fu /* "OLBM" */

/** request: what the program asks the bootloader to do at the next reset. */
#define STM32H7RS_REQUEST_NONE        0u
#define STM32H7RS_REQUEST_UPLOAD      1u

/** Consecutive program starts without a clear before the bootloader stays in recovery. */
#define STM32H7RS_MAX_STRIKES         3u
/** Time a program must run before clearing the strike count, in seconds. */
#define STM32H7RS_STRIKE_CLEAR_S      30u

typedef struct {
	uint32_t magic;   /**< STM32H7RS_MAILBOX_MAGIC */
	uint32_t request; /**< STM32H7RS_REQUEST_* */
	uint32_t strikes; /**< program starts not yet cleared by the program */
	uint32_t check;   /**< ~(magic ^ request ^ strikes) */
} stm32h7rs_mailbox_t;

static inline uint32_t stm32h7rs_mailbox_check(const stm32h7rs_mailbox_t *m)
{
	return ~(m->magic ^ m->request ^ m->strikes);
}

static inline int stm32h7rs_mailbox_valid(const stm32h7rs_mailbox_t *m)
{
	return m->magic == STM32H7RS_MAILBOX_MAGIC && m->check == stm32h7rs_mailbox_check(m);
}

static inline void stm32h7rs_mailbox_seal(stm32h7rs_mailbox_t *m)
{
	m->magic = STM32H7RS_MAILBOX_MAGIC;
	m->check = stm32h7rs_mailbox_check(m);
}

/* ---- Watchdog armed by the bootloader before it starts a program ---- */

/** IWDG prescaler divider and reload: 32 kHz LSI / 256 x 2000 = 16 s. */
#define STM32H7RS_IWDG_PRESCALER_DIV  256u
#define STM32H7RS_IWDG_RELOAD         2000u

/* ---- Network settings record (start of the network settings sector) ---- */

#define STM32H7RS_NETCFG_MAGIC        0x4746434Eu /* "NCFG" */
#define STM32H7RS_NETCFG_VERSION      1u
#define STM32H7RS_NETCFG_MODE_DHCP    0u
#define STM32H7RS_NETCFG_MODE_STATIC  1u

/** Addresses are in network byte order (first octet in the lowest byte). */
typedef struct {
	uint32_t magic;   /**< STM32H7RS_NETCFG_MAGIC */
	uint16_t version; /**< STM32H7RS_NETCFG_VERSION */
	uint8_t mode;     /**< STM32H7RS_NETCFG_MODE_* */
	uint8_t reserved0;
	uint32_t ip;
	uint32_t netmask;
	uint32_t gateway;
	uint32_t dns;
	uint32_t reserved1;
	uint32_t crc32;   /**< CRC-32 of the bytes above */
} stm32h7rs_netcfg_t;

#define STM32H7RS_NETCFG_CRC_LEN      28u

/** Address the bootloader uses when DHCP gets no answer: 192.168.10.100/24, gateway 192.168.10.1. */
#define STM32H7RS_FALLBACK_IP         0x640AA8C0u
#define STM32H7RS_FALLBACK_NETMASK    0x00FFFFFFu
#define STM32H7RS_FALLBACK_GATEWAY    0x010AA8C0u

/* ---- Network services of the bootloader ---- */

#define STM32H7RS_TFTP_PORT           69u
#define STM32H7RS_TFTP_FILE_FIRMWARE  "firmware.bin"
#define STM32H7RS_TFTP_FILE_INFO      "info"
#define STM32H7RS_TFTP_OPT_CRC32      "crc32"
#define STM32H7RS_TFTP_MIN_BLKSIZE    8u
#define STM32H7RS_TFTP_MAX_BLKSIZE    1468u
#define STM32H7RS_DISCOVERY_PORT      33333u
#define STM32H7RS_DISCOVERY_PROBE     "OPENPLC_DISCOVER_V1"

/** Modbus TCP request a running program answers by restarting into the bootloader (function code and magic). */
#define STM32H7RS_MODBUS_PORT         502u
#define STM32H7RS_MODBUS_FC_BOOTLOADER 0x4Cu

#if defined(__cplusplus)
static_assert(sizeof(stm32h7rs_image_header_t) == STM32H7RS_IMAGE_HEADER_CRC_LEN + 4u, "image header layout");
static_assert(sizeof(stm32h7rs_mailbox_t) == 16u, "mailbox layout");
static_assert(sizeof(stm32h7rs_netcfg_t) == STM32H7RS_NETCFG_CRC_LEN + 4u, "netcfg layout");
#else
_Static_assert(sizeof(stm32h7rs_image_header_t) == STM32H7RS_IMAGE_HEADER_CRC_LEN + 4u, "image header layout");
_Static_assert(sizeof(stm32h7rs_mailbox_t) == 16u, "mailbox layout");
_Static_assert(sizeof(stm32h7rs_netcfg_t) == STM32H7RS_NETCFG_CRC_LEN + 4u, "netcfg layout");
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32H7RS_BOOT_H */
