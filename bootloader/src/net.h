/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Minimal IPv4 stack of the bootloader: Ethernet II, ARP, ICMP echo and UDP. Single-threaded and polled; no
 * fragmentation, no TCP. Addresses are uint32_t holding the four octets in memory (network) order.
 */

#ifndef BOOT_NET_H
#define BOOT_NET_H

#include <stdint.h>

#define NET_MTU 1500u
#define NET_FRAME_MAX 1514u
#define NET_UDP_PAYLOAD_MAX (NET_MTU - 20u - 8u)
#define NET_BROADCAST 0xFFFFFFFFu

/** Sends one Ethernet frame (without FCS). */
typedef void (*net_tx_fn)(const uint8_t *frame, uint16_t len);

/** Receives a UDP datagram addressed to this device (or broadcast). */
typedef void (*net_udp_fn)(uint32_t src_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *data,
			   uint16_t len);

void net_init(const uint8_t mac[6], net_tx_fn tx, net_udp_fn udp);

/** Sets the interface address; ip 0 means unconfigured (only DHCP traffic flows). */
void net_set_addr(uint32_t ip, uint32_t netmask, uint32_t gateway);

uint32_t net_ip(void);

/** Handles one received Ethernet frame. */
void net_input(const uint8_t *frame, uint16_t len);

/**
 * Sends a UDP datagram. The next hop MAC comes from the ARP cache, which learns from every received frame; a miss
 * sends an ARP request and drops the datagram (callers retransmit).
 *
 * @return 0 when sent, -1 when dropped
 */
int net_udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const uint8_t *data, uint16_t len);

/** Formats ip as dotted decimal into out (at least 16 bytes); returns the length. */
int net_format_ip(uint32_t ip, char *out);

/** Formats mac as aa:bb:cc:dd:ee:ff into out (at least 18 bytes); returns the length. */
int net_format_mac(const uint8_t mac[6], char *out);

#endif /* BOOT_NET_H */
