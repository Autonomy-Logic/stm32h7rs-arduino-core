/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * DHCP client of the bootloader: DISCOVER, REQUEST, ACK; no lease renewal (bootloader sessions are short). When
 * no server answers within DHCP_TIMEOUT_MS it falls back to the core's fixed address.
 */

#ifndef BOOT_DHCP_H
#define BOOT_DHCP_H

#include <stdint.h>

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_RETRY_MS 2000u
#define DHCP_TIMEOUT_MS 10000u

enum dhcp_state {
	DHCP_OFF,
	DHCP_SELECTING,
	DHCP_REQUESTING,
	DHCP_BOUND,
	DHCP_FALLBACK,
};

/** Starts address acquisition; the interface stays unconfigured until BOUND or FALLBACK. */
void dhcp_start(uint32_t now_ms, const uint8_t mac[6], uint32_t xid);

void dhcp_tick(uint32_t now_ms);

/** Handles a datagram received on the DHCP client port. */
void dhcp_input(uint32_t now_ms, const uint8_t *data, uint16_t len);

enum dhcp_state dhcp_state(void);

#endif /* BOOT_DHCP_H */
