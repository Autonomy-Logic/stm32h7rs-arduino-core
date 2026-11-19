/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "net.h"

#include <stddef.h>
#include <string.h>

#define ETH_HDR 14u
#define ETHERTYPE_IPV4 0x0800u
#define ETHERTYPE_ARP 0x0806u
#define IP_HDR 20u
#define UDP_HDR 8u
#define PROTO_ICMP 1u
#define PROTO_UDP 17u
#define ARP_CACHE_SIZE 8u

struct arp_entry {
	uint32_t ip;
	uint8_t mac[6];
};

static struct {
	uint8_t mac[6];
	uint32_t ip;
	uint32_t netmask;
	uint32_t gateway;
	net_tx_fn tx;
	net_udp_fn udp;
	struct arp_entry arp[ARP_CACHE_SIZE];
	unsigned arp_next;
	uint16_t ip_id;
	uint8_t frame[NET_FRAME_MAX];
} net;

static const uint8_t mac_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint16_t ld16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void st16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static uint32_t ld_ip(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return v;
}

static void st_ip(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, 4);
}

static uint32_t sum16(uint32_t sum, const uint8_t *p, uint16_t len)
{
	while (len > 1u) {
		sum += ld16(p);
		p += 2;
		len -= 2u;
	}
	if (len != 0u) {
		sum += (uint32_t)p[0] << 8;
	}
	return sum;
}

static uint16_t fold(uint32_t sum)
{
	while ((sum >> 16) != 0u) {
		sum = (sum & 0xFFFFu) + (sum >> 16);
	}
	return (uint16_t)~sum;
}

void net_init(const uint8_t mac[6], net_tx_fn tx, net_udp_fn udp)
{
	memset(&net, 0, sizeof(net));
	memcpy(net.mac, mac, 6);
	net.tx = tx;
	net.udp = udp;
}

void net_set_addr(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
	net.ip = ip;
	net.netmask = netmask;
	net.gateway = gateway;
}

uint32_t net_ip(void)
{
	return net.ip;
}

static void arp_learn(uint32_t ip, const uint8_t *mac)
{
	if (ip == 0u || ip == NET_BROADCAST || (mac[0] & 1u) != 0u) {
		return;
	}
	for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
		if (net.arp[i].ip == ip) {
			memcpy(net.arp[i].mac, mac, 6);
			return;
		}
	}
	net.arp[net.arp_next].ip = ip;
	memcpy(net.arp[net.arp_next].mac, mac, 6);
	net.arp_next = (net.arp_next + 1u) % ARP_CACHE_SIZE;
}

static const uint8_t *arp_lookup(uint32_t ip)
{
	for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
		if (net.arp[i].ip == ip) {
			return net.arp[i].mac;
		}
	}
	return NULL;
}

static void eth_header(uint8_t *f, const uint8_t *dst, uint16_t type)
{
	memcpy(f, dst, 6);
	memcpy(f + 6, net.mac, 6);
	st16(f + 12, type);
}

static void arp_send(uint16_t oper, const uint8_t *dst_mac, const uint8_t *target_mac, uint32_t target_ip)
{
	uint8_t *f = net.frame;
	uint8_t *a = f + ETH_HDR;

	eth_header(f, dst_mac, ETHERTYPE_ARP);
	st16(a, 1u);
	st16(a + 2, ETHERTYPE_IPV4);
	a[4] = 6u;
	a[5] = 4u;
	st16(a + 6, oper);
	memcpy(a + 8, net.mac, 6);
	st_ip(a + 14, net.ip);
	memcpy(a + 18, target_mac, 6);
	st_ip(a + 24, target_ip);
	net.tx(f, (uint16_t)(ETH_HDR + 28u));
}

static void arp_input(const uint8_t *a, uint16_t len)
{
	if (len < 28u || ld16(a) != 1u || ld16(a + 2) != ETHERTYPE_IPV4 || a[4] != 6u || a[5] != 4u) {
		return;
	}

	uint16_t oper = ld16(a + 6);
	uint32_t sender_ip = ld_ip(a + 14);
	uint32_t target_ip = ld_ip(a + 24);

	if (net.ip == 0u || target_ip != net.ip) {
		return;
	}
	arp_learn(sender_ip, a + 8);
	if (oper == 1u) {
		uint8_t sender_mac[6];

		memcpy(sender_mac, a + 8, 6);
		arp_send(2u, sender_mac, sender_mac, sender_ip);
	}
}

/** Fills the IPv4 header at f + ETH_HDR for a payload of len bytes. */
static void ip_header(uint8_t *f, uint8_t proto, uint32_t dst, uint16_t len)
{
	uint8_t *ip = f + ETH_HDR;

	ip[0] = 0x45u;
	ip[1] = 0u;
	st16(ip + 2, (uint16_t)(IP_HDR + len));
	st16(ip + 4, net.ip_id++);
	st16(ip + 6, 0x4000u); /* don't fragment */
	ip[8] = 64u;
	ip[9] = proto;
	st16(ip + 10, 0u);
	st_ip(ip + 12, net.ip);
	st_ip(ip + 16, dst);
	st16(ip + 10, fold(sum16(0u, ip, IP_HDR)));
}

static void icmp_input(const uint8_t *src_mac, uint32_t src, const uint8_t *icmp, uint16_t len)
{
	if (len < 8u || icmp[0] != 8u || len > NET_MTU - IP_HDR) {
		return;
	}

	uint8_t *f = net.frame;
	uint8_t *out = f + ETH_HDR + IP_HDR;

	eth_header(f, src_mac, ETHERTYPE_IPV4);
	ip_header(f, PROTO_ICMP, src, len);
	memcpy(out, icmp, len);
	out[0] = 0u; /* echo reply */
	st16(out + 2, 0u);
	st16(out + 2, fold(sum16(0u, out, len)));
	net.tx(f, (uint16_t)(ETH_HDR + IP_HDR + len));
}

static uint16_t udp_checksum(uint32_t src, uint32_t dst, const uint8_t *udp, uint16_t len)
{
	uint8_t pseudo[12];

	st_ip(pseudo, src);
	st_ip(pseudo + 4, dst);
	pseudo[8] = 0u;
	pseudo[9] = PROTO_UDP;
	st16(pseudo + 10, len);
	uint16_t c = fold(sum16(sum16(0u, pseudo, 12u), udp, len));

	return c == 0u ? 0xFFFFu : c;
}

static void ipv4_input(const uint8_t *src_mac, const uint8_t *ip, uint16_t len)
{
	if (len < IP_HDR || (ip[0] >> 4) != 4u) {
		return;
	}

	uint16_t ihl = (uint16_t)((ip[0] & 0x0Fu) * 4u);
	uint16_t total = ld16(ip + 2);

	if (ihl < IP_HDR || total < ihl || total > len || fold(sum16(0u, ip, ihl)) != 0u) {
		return;
	}
	if ((ld16(ip + 6) & 0x3FFFu) != 0u) {
		return; /* fragment */
	}

	uint32_t src = ld_ip(ip + 12);
	uint32_t dst = ld_ip(ip + 16);
	uint32_t subnet_bcast = net.ip | ~net.netmask;
	int for_us = dst == net.ip || dst == NET_BROADCAST || (net.ip != 0u && dst == subnet_bcast) || net.ip == 0u;

	if (!for_us || (net.ip != 0u && dst == net.ip && src == 0u)) {
		return;
	}
	arp_learn(src, src_mac);

	const uint8_t *payload = ip + ihl;
	uint16_t plen = (uint16_t)(total - ihl);

	if (ip[9] == PROTO_ICMP && net.ip != 0u && dst == net.ip) {
		icmp_input(src_mac, src, payload, plen);
		return;
	}
	if (ip[9] != PROTO_UDP || plen < UDP_HDR) {
		return;
	}

	uint16_t ulen = ld16(payload + 4);

	if (ulen < UDP_HDR || ulen > plen) {
		return;
	}
	if (ld16(payload + 6) != 0u) {
		uint8_t pseudo[12];

		st_ip(pseudo, src);
		st_ip(pseudo + 4, dst);
		pseudo[8] = 0u;
		pseudo[9] = PROTO_UDP;
		st16(pseudo + 10, ulen);
		if (fold(sum16(sum16(0u, pseudo, 12u), payload, ulen)) != 0u) {
			return;
		}
	}
	net.udp(src, ld16(payload), ld16(payload + 2), payload + UDP_HDR, (uint16_t)(ulen - UDP_HDR));
}

void net_input(const uint8_t *frame, uint16_t len)
{
	if (len < ETH_HDR) {
		return;
	}
	if (memcmp(frame, net.mac, 6) != 0 && memcmp(frame, mac_broadcast, 6) != 0) {
		return;
	}

	uint16_t type = ld16(frame + 12);

	if (type == ETHERTYPE_ARP) {
		arp_input(frame + ETH_HDR, (uint16_t)(len - ETH_HDR));
	} else if (type == ETHERTYPE_IPV4) {
		ipv4_input(frame + 6, frame + ETH_HDR, (uint16_t)(len - ETH_HDR));
	}
}

int net_udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const uint8_t *data, uint16_t len)
{
	const uint8_t *dst_mac;

	if (len > NET_UDP_PAYLOAD_MAX) {
		return -1;
	}
	if (dst_ip == NET_BROADCAST || (net.ip != 0u && dst_ip == (net.ip | ~net.netmask))) {
		dst_mac = mac_broadcast;
	} else {
		uint32_t hop = ((dst_ip ^ net.ip) & net.netmask) == 0u ? dst_ip : net.gateway;

		dst_mac = arp_lookup(dst_ip);
		if (dst_mac == NULL) {
			dst_mac = arp_lookup(hop);
		}
		if (dst_mac == NULL) {
			if (net.ip != 0u && hop != 0u) {
				arp_send(1u, mac_broadcast, (const uint8_t[6]){0}, hop);
			}
			return -1;
		}
	}

	uint8_t *f = net.frame;
	uint8_t *udp = f + ETH_HDR + IP_HDR;
	uint16_t ulen = (uint16_t)(UDP_HDR + len);

	eth_header(f, dst_mac, ETHERTYPE_IPV4);
	ip_header(f, PROTO_UDP, dst_ip, ulen);
	st16(udp, src_port);
	st16(udp + 2, dst_port);
	st16(udp + 4, ulen);
	st16(udp + 6, 0u);
	memcpy(udp + UDP_HDR, data, len);
	st16(udp + 6, udp_checksum(net.ip, dst_ip, udp, ulen));
	net.tx(f, (uint16_t)(ETH_HDR + IP_HDR + ulen));
	return 0;
}

int net_format_ip(uint32_t ip, char *out)
{
	const uint8_t *b = (const uint8_t *)&ip;
	int n = 0;

	for (int i = 0; i < 4; i++) {
		unsigned v = b[i];

		if (v >= 100u) {
			out[n++] = (char)('0' + v / 100u);
		}
		if (v >= 10u) {
			out[n++] = (char)('0' + (v / 10u) % 10u);
		}
		out[n++] = (char)('0' + v % 10u);
		if (i < 3) {
			out[n++] = '.';
		}
	}
	out[n] = '\0';
	return n;
}

int net_format_mac(const uint8_t mac[6], char *out)
{
	static const char hex[] = "0123456789ABCDEF";
	int n = 0;

	for (int i = 0; i < 6; i++) {
		out[n++] = hex[mac[i] >> 4];
		out[n++] = hex[mac[i] & 0xFu];
		if (i < 5) {
			out[n++] = ':';
		}
	}
	out[n] = '\0';
	return n;
}
