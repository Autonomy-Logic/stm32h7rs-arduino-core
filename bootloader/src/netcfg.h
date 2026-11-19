/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Network settings record written by the program (docs/network-bootloader.md, "Network settings record").
 */

#ifndef BOOT_NETCFG_H
#define BOOT_NETCFG_H

#include <stdint.h>

#include "stm32h7rs_boot.h"

/** Addresses in network byte order, as in the record. */
struct netcfg {
	int use_dhcp;
	uint32_t ip;
	uint32_t netmask;
	uint32_t gateway;
};

/** Reads a record: a valid static record gives its addresses; anything else gives DHCP. */
void netcfg_parse(const stm32h7rs_netcfg_t *record, struct netcfg *out);

#endif /* BOOT_NETCFG_H */
