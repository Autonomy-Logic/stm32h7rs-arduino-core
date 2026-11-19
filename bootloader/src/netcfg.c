/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "netcfg.h"

#include "crc32.h"

void netcfg_parse(const stm32h7rs_netcfg_t *record, struct netcfg *out)
{
	out->use_dhcp = 1;
	out->ip = 0u;
	out->netmask = 0u;
	out->gateway = 0u;

	if (record->magic != STM32H7RS_NETCFG_MAGIC || record->version != STM32H7RS_NETCFG_VERSION ||
	    crc32_update(0u, record, STM32H7RS_NETCFG_CRC_LEN) != record->crc32) {
		return;
	}
	if (record->mode != STM32H7RS_NETCFG_MODE_STATIC || record->ip == 0u || record->ip == 0xFFFFFFFFu ||
	    record->netmask == 0u) {
		return;
	}
	out->use_dhcp = 0;
	out->ip = record->ip;
	out->netmask = record->netmask;
	out->gateway = record->gateway;
}
