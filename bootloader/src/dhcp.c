/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "dhcp.h"

#include <string.h>

#include "net.h"
#include "stm32h7rs_boot.h"

#define MSG_DISCOVER 1u
#define MSG_OFFER 2u
#define MSG_REQUEST 3u
#define MSG_ACK 5u
#define MSG_NAK 6u

#define OPT_PAD 0u
#define OPT_SUBNET 1u
#define OPT_ROUTER 3u
#define OPT_REQUESTED_IP 50u
#define OPT_MSG_TYPE 53u
#define OPT_SERVER_ID 54u
#define OPT_PARAMS 55u
#define OPT_END 255u

#define BOOTP_LEN 300u
#define MAGIC_OFFSET 236u

static struct {
	enum dhcp_state state;
	uint8_t mac[6];
	uint32_t xid;
	uint32_t started_ms;
	uint32_t sent_ms;
	uint32_t offered_ip;
	uint32_t server_id;
} dhcp;

static void put_ip(uint8_t *p, uint32_t ip)
{
	memcpy(p, &ip, 4);
}

static uint32_t get_ip(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return v;
}

static void send_msg(uint32_t now_ms, uint8_t type)
{
	uint8_t m[BOOTP_LEN];
	unsigned at = MAGIC_OFFSET;

	memset(m, 0, sizeof(m));
	m[0] = 1u; /* BOOTREQUEST */
	m[1] = 1u; /* Ethernet */
	m[2] = 6u;
	m[4] = (uint8_t)(dhcp.xid >> 24);
	m[5] = (uint8_t)(dhcp.xid >> 16);
	m[6] = (uint8_t)(dhcp.xid >> 8);
	m[7] = (uint8_t)dhcp.xid;
	m[10] = 0x80u; /* broadcast flag */
	memcpy(m + 28, dhcp.mac, 6);
	m[at++] = 99u;
	m[at++] = 130u;
	m[at++] = 83u;
	m[at++] = 99u;
	m[at++] = OPT_MSG_TYPE;
	m[at++] = 1u;
	m[at++] = type;
	if (type == MSG_REQUEST) {
		m[at++] = OPT_REQUESTED_IP;
		m[at++] = 4u;
		put_ip(m + at, dhcp.offered_ip);
		at += 4u;
		m[at++] = OPT_SERVER_ID;
		m[at++] = 4u;
		put_ip(m + at, dhcp.server_id);
		at += 4u;
	}
	m[at++] = OPT_PARAMS;
	m[at++] = 2u;
	m[at++] = OPT_SUBNET;
	m[at++] = OPT_ROUTER;
	m[at] = OPT_END;
	dhcp.sent_ms = now_ms;
	(void)net_udp_send(NET_BROADCAST, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, m, BOOTP_LEN);
}

void dhcp_start(uint32_t now_ms, const uint8_t mac[6], uint32_t xid)
{
	memset(&dhcp, 0, sizeof(dhcp));
	memcpy(dhcp.mac, mac, 6);
	dhcp.xid = xid;
	dhcp.started_ms = now_ms;
	dhcp.state = DHCP_SELECTING;
	net_set_addr(0u, 0u, 0u);
	send_msg(now_ms, MSG_DISCOVER);
}

enum dhcp_state dhcp_state(void)
{
	return dhcp.state;
}

void dhcp_tick(uint32_t now_ms)
{
	if (dhcp.state != DHCP_SELECTING && dhcp.state != DHCP_REQUESTING) {
		return;
	}
	if (now_ms - dhcp.started_ms >= DHCP_TIMEOUT_MS) {
		dhcp.state = DHCP_FALLBACK;
		net_set_addr(STM32H7RS_FALLBACK_IP, STM32H7RS_FALLBACK_NETMASK, STM32H7RS_FALLBACK_GATEWAY);
		return;
	}
	if (now_ms - dhcp.sent_ms >= DHCP_RETRY_MS) {
		send_msg(now_ms, dhcp.state == DHCP_SELECTING ? MSG_DISCOVER : MSG_REQUEST);
	}
}

void dhcp_input(uint32_t now_ms, const uint8_t *m, uint16_t len)
{
	if (dhcp.state != DHCP_SELECTING && dhcp.state != DHCP_REQUESTING) {
		return;
	}
	if (len < MAGIC_OFFSET + 4u || m[0] != 2u || m[1] != 1u || m[2] != 6u) {
		return;
	}

	uint32_t xid = ((uint32_t)m[4] << 24) | ((uint32_t)m[5] << 16) | ((uint32_t)m[6] << 8) | m[7];

	if (xid != dhcp.xid || memcmp(m + 28, dhcp.mac, 6) != 0 || m[MAGIC_OFFSET] != 99u ||
	    m[MAGIC_OFFSET + 1] != 130u || m[MAGIC_OFFSET + 2] != 83u || m[MAGIC_OFFSET + 3] != 99u) {
		return;
	}

	uint8_t type = 0u;
	uint32_t server_id = 0u;
	uint32_t netmask = STM32H7RS_FALLBACK_NETMASK;
	uint32_t router = 0u;
	unsigned at = MAGIC_OFFSET + 4u;

	while (at < len && m[at] != OPT_END) {
		uint8_t opt = m[at++];

		if (opt == OPT_PAD) {
			continue;
		}
		if (at >= len) {
			break;
		}

		uint8_t olen = m[at++];

		if (at + olen > len) {
			break;
		}
		if (opt == OPT_MSG_TYPE && olen >= 1u) {
			type = m[at];
		} else if (opt == OPT_SERVER_ID && olen >= 4u) {
			server_id = get_ip(m + at);
		} else if (opt == OPT_SUBNET && olen >= 4u) {
			netmask = get_ip(m + at);
		} else if (opt == OPT_ROUTER && olen >= 4u) {
			router = get_ip(m + at);
		}
		at += olen;
	}

	uint32_t yiaddr = get_ip(m + 16);

	if (dhcp.state == DHCP_SELECTING && type == MSG_OFFER && yiaddr != 0u && server_id != 0u) {
		dhcp.offered_ip = yiaddr;
		dhcp.server_id = server_id;
		dhcp.state = DHCP_REQUESTING;
		send_msg(now_ms, MSG_REQUEST);
	} else if (dhcp.state == DHCP_REQUESTING && type == MSG_ACK && yiaddr != 0u &&
		   (server_id == 0u || server_id == dhcp.server_id)) {
		dhcp.state = DHCP_BOUND;
		net_set_addr(yiaddr, netmask, router);
	} else if (dhcp.state == DHCP_REQUESTING && type == MSG_NAK) {
		dhcp.state = DHCP_SELECTING;
		send_msg(now_ms, MSG_DISCOVER);
	}
}
