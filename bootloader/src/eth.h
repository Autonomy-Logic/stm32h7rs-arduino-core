/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Ethernet MAC (polled, ST HAL) and the KSZ8081 PHY in RMII 50 MHz clock mode.
 */

#ifndef BOOT_ETH_H
#define BOOT_ETH_H

#include <stdint.h>

int eth_init(const uint8_t mac[6]);

/** Follows the PHY link; starts or stops the MAC on changes. Returns 1 while the link is up. */
int eth_poll_link(uint32_t now_ms);

/** Returns the next received frame (valid until the next call), or NULL. */
const uint8_t *eth_receive(uint16_t *len);

void eth_send(const uint8_t *frame, uint16_t len);

#endif /* BOOT_ETH_H */
