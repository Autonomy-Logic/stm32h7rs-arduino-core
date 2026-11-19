/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Host tests of the bootloader's hardware-independent parts: CRC-32, image checks, network settings record,
 * TFTP server, IPv4 stack and DHCP client.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "dhcp.h"
#include "image.h"
#include "net.h"
#include "netcfg.h"
#include "stm32h7rs_boot.h"
#include "tftp.h"

static int failures;
static int checks;

#define CHECK(cond)                                                                                           \
	do {                                                                                                  \
		checks++;                                                                                     \
		if (!(cond)) {                                                                                \
			failures++;                                                                           \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);              \
		}                                                                                             \
	} while (0)

static uint32_t ip4(unsigned a, unsigned b, unsigned c, unsigned d)
{
	uint8_t o[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
	uint32_t v;

	memcpy(&v, o, 4);
	return v;
}

/* ---- CRC-32 ---- */

static void test_crc32(void)
{
	CHECK(crc32_update(0u, "123456789", 9) == 0xCBF43926u);
	uint32_t c = crc32_update(0u, "1234", 4);

	CHECK(crc32_update(c, "56789", 5) == 0xCBF43926u);
	CHECK(crc32_update(0u, "", 0) == 0u);
}

/* ---- Image ---- */

#define TEST_IMAGE_SIZE 0x1000u

static uint8_t image_buf[TEST_IMAGE_SIZE];

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void seal_image(void)
{
	stm32h7rs_image_header_t *h = (stm32h7rs_image_header_t *)image_buf;

	h->payload_crc32 = crc32_update(0u, image_buf + STM32H7RS_IMAGE_HEADER_SIZE,
					h->image_size - STM32H7RS_IMAGE_HEADER_SIZE);
	h->header_crc32 = crc32_update(0u, h, STM32H7RS_IMAGE_HEADER_CRC_LEN);
}

static void make_image(void)
{
	stm32h7rs_image_header_t *h = (stm32h7rs_image_header_t *)image_buf;

	memset(image_buf, 0, sizeof(image_buf));
	for (unsigned i = STM32H7RS_IMAGE_HEADER_SIZE + 8u; i < sizeof(image_buf); i++) {
		image_buf[i] = (uint8_t)(i * 7u);
	}
	put32(image_buf + STM32H7RS_IMAGE_HEADER_SIZE, STM32H7RS_AXI_SRAM_END);
	put32(image_buf + STM32H7RS_IMAGE_HEADER_SIZE + 4u, STM32H7RS_SLOT_BASE + 0x800u + 1u);
	h->magic = STM32H7RS_IMAGE_MAGIC;
	h->header_version = STM32H7RS_IMAGE_HEADER_VER;
	h->header_size = STM32H7RS_IMAGE_HEADER_SIZE;
	h->image_size = TEST_IMAGE_SIZE;
	strcpy(h->version, "0.2.0");
	seal_image();
}

static int check_image(void)
{
	const char *why = NULL;

	return image_check((const stm32h7rs_image_header_t *)image_buf, image_buf + STM32H7RS_IMAGE_HEADER_SIZE,
			   &why);
}

static void test_image(void)
{
	stm32h7rs_image_header_t *h = (stm32h7rs_image_header_t *)image_buf;
	uint8_t *vec = image_buf + STM32H7RS_IMAGE_HEADER_SIZE;

	make_image();
	CHECK(check_image() == 0);

	make_image();
	h->magic ^= 1u;
	h->header_crc32 = crc32_update(0u, h, STM32H7RS_IMAGE_HEADER_CRC_LEN);
	CHECK(check_image() != 0);

	make_image();
	h->header_version = 2u;
	h->header_crc32 = crc32_update(0u, h, STM32H7RS_IMAGE_HEADER_CRC_LEN);
	CHECK(check_image() != 0);

	make_image();
	h->flags = 1u; /* header CRC no longer matches */
	CHECK(check_image() != 0);

	make_image();
	h->image_size = STM32H7RS_SLOT_SIZE + 4u;
	h->header_crc32 = crc32_update(0u, h, STM32H7RS_IMAGE_HEADER_CRC_LEN);
	CHECK(check_image() != 0);

	make_image();
	image_buf[TEST_IMAGE_SIZE - 1u] ^= 0xFFu; /* payload CRC no longer matches */
	CHECK(check_image() != 0);

	make_image();
	put32(vec, 0x30000000u);
	seal_image();
	CHECK(check_image() != 0);

	make_image();
	put32(vec, STM32H7RS_DTCM_END - 4u); /* not 8-byte aligned */
	seal_image();
	CHECK(check_image() != 0);

	make_image();
	put32(vec, STM32H7RS_DTCM_END);
	seal_image();
	CHECK(check_image() == 0);

	make_image();
	put32(vec + 4, STM32H7RS_SLOT_BASE + 0x800u); /* not Thumb */
	seal_image();
	CHECK(check_image() != 0);

	make_image();
	put32(vec + 4, STM32H7RS_SLOT_BASE + TEST_IMAGE_SIZE + 1u); /* past the image */
	seal_image();
	CHECK(check_image() != 0);

	make_image();
	put32(vec + 4, STM32H7RS_SLOT_BASE + 0x101u); /* in the header */
	seal_image();
	CHECK(check_image() != 0);
}

/* ---- Network settings record ---- */

static void test_netcfg(void)
{
	stm32h7rs_netcfg_t r;
	struct netcfg c;

	memset(&r, 0, sizeof(r));
	r.magic = STM32H7RS_NETCFG_MAGIC;
	r.version = STM32H7RS_NETCFG_VERSION;
	r.mode = STM32H7RS_NETCFG_MODE_STATIC;
	r.ip = ip4(10, 0, 0, 5);
	r.netmask = ip4(255, 255, 255, 0);
	r.gateway = ip4(10, 0, 0, 1);
	r.crc32 = crc32_update(0u, &r, STM32H7RS_NETCFG_CRC_LEN);
	netcfg_parse(&r, &c);
	CHECK(!c.use_dhcp && c.ip == ip4(10, 0, 0, 5) && c.netmask == ip4(255, 255, 255, 0) &&
	      c.gateway == ip4(10, 0, 0, 1));

	r.mode = STM32H7RS_NETCFG_MODE_DHCP;
	r.crc32 = crc32_update(0u, &r, STM32H7RS_NETCFG_CRC_LEN);
	netcfg_parse(&r, &c);
	CHECK(c.use_dhcp);

	r.mode = STM32H7RS_NETCFG_MODE_STATIC;
	r.crc32 = crc32_update(0u, &r, STM32H7RS_NETCFG_CRC_LEN) ^ 1u;
	netcfg_parse(&r, &c);
	CHECK(c.use_dhcp);

	r.version = 2u;
	r.crc32 = crc32_update(0u, &r, STM32H7RS_NETCFG_CRC_LEN);
	netcfg_parse(&r, &c);
	CHECK(c.use_dhcp);

	memset(&r, 0xFF, sizeof(r));
	netcfg_parse(&r, &c);
	CHECK(c.use_dhcp);

	memset(&r, 0, sizeof(r));
	r.magic = STM32H7RS_NETCFG_MAGIC;
	r.version = STM32H7RS_NETCFG_VERSION;
	r.mode = STM32H7RS_NETCFG_MODE_STATIC; /* static but no address */
	r.crc32 = crc32_update(0u, &r, STM32H7RS_NETCFG_CRC_LEN);
	netcfg_parse(&r, &c);
	CHECK(c.use_dhcp);
}

/* ---- TFTP ---- */

struct sent {
	uint32_t ip;
	uint16_t port;
	uint16_t len;
	uint8_t data[600];
};

static struct {
	struct sent sent[64];
	int nsent;
	uint8_t written[8192];
	uint32_t written_len;
	int began, ended, aborted, done;
	uint32_t end_size, end_crc;
	int end_has_crc;
	int fail_end;
} fx;

static void fx_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *data, uint16_t len)
{
	(void)ctx;
	if (fx.nsent < 64) {
		fx.sent[fx.nsent].ip = ip;
		fx.sent[fx.nsent].port = port;
		fx.sent[fx.nsent].len = len;
		memcpy(fx.sent[fx.nsent].data, data, len < 600u ? len : 600u);
		fx.nsent++;
	}
}

static int fx_begin(void *ctx, uint32_t size, int has_size, const char **why)
{
	(void)ctx;
	(void)size;
	(void)has_size;
	(void)why;
	fx.began++;
	fx.written_len = 0u;
	return 0;
}

static int fx_data(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len, const char **why)
{
	(void)ctx;
	if (offset + len > sizeof(fx.written)) {
		*why = "too large";
		return -1;
	}
	memcpy(fx.written + offset, data, len);
	if (offset + len > fx.written_len) {
		fx.written_len = offset + len;
	}
	return 0;
}

static int fx_end(void *ctx, uint32_t size, uint32_t crc, int has_crc, const char **why)
{
	(void)ctx;
	fx.ended++;
	fx.end_size = size;
	fx.end_crc = crc;
	fx.end_has_crc = has_crc;
	if (fx.fail_end) {
		*why = "image refused";
		return -1;
	}
	return 0;
}

static void fx_abort(void *ctx)
{
	(void)ctx;
	fx.aborted++;
}

static void fx_done(void *ctx)
{
	(void)ctx;
	fx.done++;
}

static uint16_t fx_info(void *ctx, char *buf, uint16_t max)
{
	(void)ctx;
	(void)max;
	strcpy(buf, "device=TEST\nmode=bootloader\n");
	return (uint16_t)strlen(buf);
}

static const struct tftp_io fx_io = {
	.send = fx_send,
	.write_begin = fx_begin,
	.write_data = fx_data,
	.write_end = fx_end,
	.write_abort = fx_abort,
	.write_done = fx_done,
	.info = fx_info,
};

static uint16_t build_req(uint8_t *p, uint16_t op, const char *const *fields, int n)
{
	uint16_t at = 2u;

	p[0] = 0u;
	p[1] = (uint8_t)op;
	for (int i = 0; i < n; i++) {
		size_t l = strlen(fields[i]) + 1u;

		memcpy(p + at, fields[i], l);
		at = (uint16_t)(at + l);
	}
	return at;
}

static uint16_t build_data(uint8_t *p, uint16_t block, const uint8_t *data, uint16_t n)
{
	p[0] = 0u;
	p[1] = 3u;
	p[2] = (uint8_t)(block >> 8);
	p[3] = (uint8_t)block;
	memcpy(p + 4, data, n);
	return (uint16_t)(4u + n);
}

static uint16_t op_of(const struct sent *s)
{
	return (uint16_t)((s->data[0] << 8) | s->data[1]);
}

static uint16_t block_of(const struct sent *s)
{
	return (uint16_t)((s->data[2] << 8) | s->data[3]);
}

/** True when the OACK s contains the option name=value. */
static int oack_has(const struct sent *s, const char *name, const char *value)
{
	const char *p = (const char *)s->data + 2;
	const char *end = (const char *)s->data + s->len;

	while (p < end) {
		const char *v = p + strlen(p) + 1;

		if (strcmp(p, name) == 0) {
			return strcmp(v, value) == 0;
		}
		p = v + strlen(v) + 1;
	}
	return 0;
}

static void test_tftp_write(void)
{
	struct tftp t;
	uint8_t pkt[1600];
	uint8_t payload[1000];
	const uint32_t client = ip4(192, 168, 1, 10);
	char crc_hex[9];
	char tsize[12];

	for (unsigned i = 0; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)(i * 13u + 1u);
	}
	snprintf(crc_hex, sizeof(crc_hex), "%08x", crc32_update(0u, payload, sizeof(payload)));
	snprintf(tsize, sizeof(tsize), "%u", (unsigned)sizeof(payload));

	memset(&fx, 0, sizeof(fx));
	tftp_init(&t, &fx_io);
	const char *wrq[] = {"FIRMWARE.BIN", "octet", "blksize", "512", "tsize", tsize, "crc32", crc_hex, "foo", "1"};
	uint16_t n = build_req(pkt, 2u, wrq, 10);

	tftp_input(&t, 0u, client, 5000u, pkt, n);
	CHECK(fx.began == 1 && fx.nsent == 1 && op_of(&fx.sent[0]) == 6u);
	CHECK(oack_has(&fx.sent[0], "blksize", "512") && oack_has(&fx.sent[0], "tsize", tsize) &&
	      oack_has(&fx.sent[0], "crc32", crc_hex) && !oack_has(&fx.sent[0], "foo", "1"));
	CHECK(fx.sent[0].ip == client && fx.sent[0].port == 5000u);

	n = build_data(pkt, 1u, payload, 512u);
	tftp_input(&t, 10u, client, 5000u, pkt, n);
	CHECK(fx.nsent == 2 && op_of(&fx.sent[1]) == 4u && block_of(&fx.sent[1]) == 1u);

	/* A repeated block is acknowledged again and not written twice. */
	tftp_input(&t, 20u, client, 5000u, pkt, n);
	CHECK(fx.nsent == 3 && block_of(&fx.sent[2]) == 1u && fx.written_len == 512u);

	/* Another client is refused while the transfer is active. */
	const char *rrq[] = {"info", "octet"};
	uint16_t rn = build_req(pkt, 1u, rrq, 2);

	tftp_input(&t, 30u, ip4(192, 168, 1, 11), 6000u, pkt, rn);
	CHECK(fx.nsent == 4 && op_of(&fx.sent[3]) == 5u && fx.sent[3].port == 6000u);

	n = build_data(pkt, 2u, payload + 512, (uint16_t)(sizeof(payload) - 512u));
	tftp_input(&t, 40u, client, 5000u, pkt, n);
	CHECK(fx.ended == 1 && fx.end_size == sizeof(payload) && fx.end_has_crc &&
	      fx.end_crc == crc32_update(0u, payload, sizeof(payload)));
	CHECK(fx.nsent == 5 && op_of(&fx.sent[4]) == 4u && block_of(&fx.sent[4]) == 2u);
	CHECK(memcmp(fx.written, payload, sizeof(payload)) == 0);

	/* The last block repeated within the linger time gets the last ACK again. */
	tftp_input(&t, 500u, client, 5000u, pkt, n);
	CHECK(fx.nsent == 6 && block_of(&fx.sent[5]) == 2u && fx.done == 0);
	tftp_tick(&t, 900u);
	CHECK(fx.done == 0);
	tftp_tick(&t, 1100u);
	CHECK(fx.done == 1 && !tftp_busy(&t));
}

static void test_tftp_refusals(void)
{
	struct tftp t;
	uint8_t pkt[1600];
	uint8_t payload[100] = {0};
	const uint32_t client = ip4(192, 168, 1, 10);

	/* tsize mismatch at the end */
	memset(&fx, 0, sizeof(fx));
	tftp_init(&t, &fx_io);
	const char *wrq[] = {"firmware.bin", "octet", "tsize", "200"};
	uint16_t n = build_req(pkt, 2u, wrq, 4);

	tftp_input(&t, 0u, client, 5000u, pkt, n);
	n = build_data(pkt, 1u, payload, sizeof(payload));
	tftp_input(&t, 10u, client, 5000u, pkt, n);
	CHECK(op_of(&fx.sent[fx.nsent - 1]) == 5u && fx.aborted == 1 && !tftp_busy(&t));

	/* image refused by write_end */
	memset(&fx, 0, sizeof(fx));
	fx.fail_end = 1;
	tftp_init(&t, &fx_io);
	const char *plain[] = {"firmware.bin", "octet"};

	n = build_req(pkt, 2u, plain, 2);
	tftp_input(&t, 0u, client, 5000u, pkt, n);
	CHECK(op_of(&fx.sent[0]) == 4u && block_of(&fx.sent[0]) == 0u); /* no options: ACK 0 */
	n = build_data(pkt, 1u, payload, sizeof(payload));
	tftp_input(&t, 10u, client, 5000u, pkt, n);
	CHECK(op_of(&fx.sent[fx.nsent - 1]) == 5u && strcmp((const char *)fx.sent[fx.nsent - 1].data + 4,
							    "image refused") == 0);
	CHECK(fx.done == 0 && !tftp_busy(&t));

	/* image larger than the slot */
	memset(&fx, 0, sizeof(fx));
	tftp_init(&t, &fx_io);
	const char *big[] = {"firmware.bin", "octet", "tsize", "8388609"};

	n = build_req(pkt, 2u, big, 4);
	tftp_input(&t, 0u, client, 5000u, pkt, n);
	CHECK(fx.began == 0 && op_of(&fx.sent[0]) == 5u && !tftp_busy(&t));

	/* unknown file and mode */
	memset(&fx, 0, sizeof(fx));
	tftp_init(&t, &fx_io);
	const char *other[] = {"other.bin", "octet"};

	n = build_req(pkt, 1u, other, 2);
	tftp_input(&t, 0u, client, 5000u, pkt, n);
	CHECK(op_of(&fx.sent[0]) == 5u && block_of(&fx.sent[0]) == 1u);
	const char *netascii[] = {"info", "netascii"};

	n = build_req(pkt, 1u, netascii, 2);
	tftp_input(&t, 0u, client, 5000u, pkt, n);
	CHECK(op_of(&fx.sent[1]) == 5u && block_of(&fx.sent[1]) == 4u);

	/* blksize is clamped */
	memset(&fx, 0, sizeof(fx));
	tftp_init(&t, &fx_io);
	const char *huge[] = {"firmware.bin", "octet", "blksize", "65464"};

	n = build_req(pkt, 2u, huge, 4);
	tftp_input(&t, 0u, client, 5000u, pkt, n);
	CHECK(oack_has(&fx.sent[0], "blksize", "1468"));
}

static void test_tftp_info_and_retries(void)
{
	struct tftp t;
	uint8_t pkt[64];
	const uint32_t client = ip4(192, 168, 1, 10);

	memset(&fx, 0, sizeof(fx));
	tftp_init(&t, &fx_io);
	const char *rrq[] = {"INFO", "octet", "blksize", "1468"};
	uint16_t n = build_req(pkt, 1u, rrq, 4);

	tftp_input(&t, 0u, client, 7000u, pkt, n);
	CHECK(fx.nsent == 1 && op_of(&fx.sent[0]) == 3u && block_of(&fx.sent[0]) == 1u);
	CHECK(memcmp(fx.sent[0].data + 4, "device=TEST\n", 12) == 0);

	/* Retransmitted after 1 s, up to 6 times, then dropped. */
	for (uint32_t ms = 1000u; ms <= 6000u; ms += 1000u) {
		tftp_tick(&t, ms);
	}
	CHECK(fx.nsent == 7 && tftp_busy(&t));
	tftp_tick(&t, 7000u);
	CHECK(fx.nsent == 7 && !tftp_busy(&t));

	/* ACK 1 completes the read. */
	tftp_input(&t, 9000u, client, 7000u, pkt, n);
	uint8_t ack[4] = {0, 4, 0, 1};

	tftp_input(&t, 9010u, client, 7000u, ack, 4);
	CHECK(!tftp_busy(&t));

	/* A stale transfer (idle over 3 s) gives way to a new client. */
	const char *wrq[] = {"firmware.bin", "octet"};

	n = build_req(pkt, 2u, wrq, 2);
	tftp_input(&t, 10000u, client, 7001u, pkt, n);
	CHECK(tftp_busy(&t));
	tftp_input(&t, 14000u, ip4(192, 168, 1, 12), 7002u, pkt, n);
	CHECK(fx.aborted == 1 && fx.began == 2 && fx.sent[fx.nsent - 1].port == 7002u);
}

/* ---- IPv4 stack and DHCP ---- */

static struct {
	uint8_t frames[16][1600];
	uint16_t lens[16];
	int n;
	uint32_t udp_src;
	uint16_t udp_dport;
	uint16_t udp_len;
	int udp_calls;
} nx;

static void nx_tx(const uint8_t *frame, uint16_t len)
{
	if (nx.n < 16) {
		memcpy(nx.frames[nx.n], frame, len);
		nx.lens[nx.n] = len;
		nx.n++;
	}
}

static void nx_udp(uint32_t src_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len)
{
	(void)src_port;
	(void)data;
	nx.udp_calls++;
	nx.udp_src = src_ip;
	nx.udp_dport = dst_port;
	nx.udp_len = len;
	if (dst_port == DHCP_CLIENT_PORT) {
		dhcp_input(0u, data, len);
	}
}

static const uint8_t my_mac[6] = {0x02, 0x80, 0xE1, 0x11, 0x22, 0x33};
static const uint8_t peer_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

static uint16_t csum(const uint8_t *p, unsigned len, uint32_t sum)
{
	for (unsigned i = 0; i + 1u < len; i += 2u) {
		sum += (uint32_t)((p[i] << 8) | p[i + 1]);
	}
	if ((len & 1u) != 0u) {
		sum += (uint32_t)p[len - 1u] << 8;
	}
	while ((sum >> 16) != 0u) {
		sum = (sum & 0xFFFFu) + (sum >> 16);
	}
	return (uint16_t)~sum;
}

/** Builds an Ethernet+IPv4+UDP frame from peer to dst. */
static uint16_t udp_frame(uint8_t *f, const uint8_t *dst_mac, uint32_t src, uint32_t dst, uint16_t sport,
			  uint16_t dport, const uint8_t *data, uint16_t len)
{
	uint8_t *ip = f + 14;
	uint8_t *udp = ip + 20;
	uint16_t ulen = (uint16_t)(8u + len);
	uint8_t pseudo[12];

	memcpy(f, dst_mac, 6);
	memcpy(f + 6, peer_mac, 6);
	f[12] = 0x08;
	f[13] = 0x00;
	memset(ip, 0, 20);
	ip[0] = 0x45;
	ip[2] = (uint8_t)((20u + ulen) >> 8);
	ip[3] = (uint8_t)(20u + ulen);
	ip[8] = 64;
	ip[9] = 17;
	memcpy(ip + 12, &src, 4);
	memcpy(ip + 16, &dst, 4);
	uint16_t c = csum(ip, 20, 0);

	ip[10] = (uint8_t)(c >> 8);
	ip[11] = (uint8_t)c;
	udp[0] = (uint8_t)(sport >> 8);
	udp[1] = (uint8_t)sport;
	udp[2] = (uint8_t)(dport >> 8);
	udp[3] = (uint8_t)dport;
	udp[4] = (uint8_t)(ulen >> 8);
	udp[5] = (uint8_t)ulen;
	udp[6] = 0;
	udp[7] = 0;
	memcpy(udp + 8, data, len);
	memcpy(pseudo, &src, 4);
	memcpy(pseudo + 4, &dst, 4);
	pseudo[8] = 0;
	pseudo[9] = 17;
	pseudo[10] = (uint8_t)(ulen >> 8);
	pseudo[11] = (uint8_t)ulen;
	uint32_t s = 0u;

	for (unsigned i = 0; i < 12u; i += 2u) {
		s += (uint32_t)((pseudo[i] << 8) | pseudo[i + 1]);
	}
	c = csum(udp, ulen, s);
	udp[6] = (uint8_t)(c >> 8);
	udp[7] = (uint8_t)c;
	return (uint16_t)(14u + 20u + ulen);
}

static uint16_t bootp_reply(uint8_t *m, uint32_t xid, uint8_t type, uint32_t yiaddr, uint32_t server)
{
	uint16_t at = 240u;

	memset(m, 0, 300);
	m[0] = 2;
	m[1] = 1;
	m[2] = 6;
	m[4] = (uint8_t)(xid >> 24);
	m[5] = (uint8_t)(xid >> 16);
	m[6] = (uint8_t)(xid >> 8);
	m[7] = (uint8_t)xid;
	memcpy(m + 16, &yiaddr, 4);
	memcpy(m + 28, my_mac, 6);
	m[236] = 99;
	m[237] = 130;
	m[238] = 83;
	m[239] = 99;
	m[at++] = 53;
	m[at++] = 1;
	m[at++] = type;
	m[at++] = 54;
	m[at++] = 4;
	memcpy(m + at, &server, 4);
	at += 4;
	m[at++] = 1;
	m[at++] = 4;
	uint32_t mask = ip4(255, 255, 255, 0);

	memcpy(m + at, &mask, 4);
	at += 4;
	m[at++] = 3;
	m[at++] = 4;
	memcpy(m + at, &server, 4);
	at += 4;
	m[at++] = 255;
	return 300u;
}

static void test_dhcp_and_net(void)
{
	static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t f[1600];
	uint8_t m[300];
	const uint32_t server = ip4(192, 168, 1, 1);
	const uint32_t xid = 0x12345678u;

	memset(&nx, 0, sizeof(nx));
	net_init(my_mac, nx_tx, nx_udp);
	dhcp_start(0u, my_mac, xid);
	CHECK(dhcp_state() == DHCP_SELECTING && nx.n == 1);
	CHECK(memcmp(nx.frames[0], bcast, 6) == 0 && nx.frames[0][14 + 9] == 17);
	CHECK(nx.frames[0][14 + 20 + 2] == 0 && nx.frames[0][14 + 20 + 3] == 67);
	CHECK(nx.frames[0][14 + 28 + 242] == 1); /* DHCPDISCOVER */
	CHECK(csum(nx.frames[0] + 14, 20, 0) == 0u);

	uint16_t n = udp_frame(f, bcast, server, NET_BROADCAST, 67, 68, m,
			       bootp_reply(m, xid, 2, ip4(192, 168, 1, 50), server));

	net_input(f, n);
	CHECK(dhcp_state() == DHCP_REQUESTING && nx.n == 2 && nx.frames[1][14 + 28 + 242] == 3);

	n = udp_frame(f, bcast, server, NET_BROADCAST, 67, 68, m,
		      bootp_reply(m, xid, 5, ip4(192, 168, 1, 50), server));
	net_input(f, n);
	CHECK(dhcp_state() == DHCP_BOUND && net_ip() == ip4(192, 168, 1, 50));

	/* ARP request for our address: reply to the requester. */
	uint8_t arp[42] = {0};

	memcpy(arp, bcast, 6);
	memcpy(arp + 6, peer_mac, 6);
	arp[12] = 0x08;
	arp[13] = 0x06;
	arp[15] = 1;
	arp[16] = 0x08;
	arp[18] = 6;
	arp[19] = 4;
	arp[21] = 1;
	memcpy(arp + 22, peer_mac, 6);
	uint32_t peer = ip4(192, 168, 1, 10);
	uint32_t me = net_ip();

	memcpy(arp + 28, &peer, 4);
	memcpy(arp + 38, &me, 4);
	nx.n = 0;
	net_input(arp, sizeof(arp));
	CHECK(nx.n == 1 && memcmp(nx.frames[0], peer_mac, 6) == 0 && nx.frames[0][21] == 2);

	/* UDP to us is delivered; a reply reaches the learned MAC. */
	n = udp_frame(f, my_mac, peer, me, 5000, STM32H7RS_TFTP_PORT, (const uint8_t *)"hello", 5);
	nx.udp_calls = 0;
	net_input(f, n);
	CHECK(nx.udp_calls == 1 && nx.udp_dport == STM32H7RS_TFTP_PORT && nx.udp_len == 5 && nx.udp_src == peer);
	nx.n = 0;
	CHECK(net_udp_send(peer, 5000, STM32H7RS_TFTP_PORT, (const uint8_t *)"ok", 2) == 0);
	CHECK(nx.n == 1 && memcmp(nx.frames[0], peer_mac, 6) == 0);

	/* A corrupted UDP checksum is dropped. */
	n = udp_frame(f, my_mac, peer, me, 5000, STM32H7RS_TFTP_PORT, (const uint8_t *)"hello", 5);
	f[n - 1] ^= 0xFFu;
	nx.udp_calls = 0;
	net_input(f, n);
	CHECK(nx.udp_calls == 0);

	/* Unknown off-link host with no gateway entry: ARP request, datagram dropped. */
	nx.n = 0;
	CHECK(net_udp_send(ip4(192, 168, 1, 99), 1, 2, (const uint8_t *)"x", 1) == -1);
	CHECK(nx.n == 1 && nx.frames[0][12] == 0x08 && nx.frames[0][13] == 0x06);

	/* ICMP echo request: reply with a valid checksum. */
	uint8_t ping[14 + 20 + 12];

	memcpy(ping, my_mac, 6);
	memcpy(ping + 6, peer_mac, 6);
	ping[12] = 0x08;
	ping[13] = 0x00;
	memset(ping + 14, 0, 32);
	ping[14] = 0x45;
	ping[17] = 32;
	ping[22] = 64;
	ping[23] = 1;
	memcpy(ping + 26, &peer, 4);
	memcpy(ping + 30, &me, 4);
	uint16_t c = csum(ping + 14, 20, 0);

	ping[24] = (uint8_t)(c >> 8);
	ping[25] = (uint8_t)c;
	ping[34] = 8;
	memcpy(ping + 38, "\x00\x01\x00\x01pong", 8);
	c = csum(ping + 34, 12, 0);
	ping[36] = (uint8_t)(c >> 8);
	ping[37] = (uint8_t)c;
	nx.n = 0;
	net_input(ping, sizeof(ping));
	CHECK(nx.n == 1 && nx.frames[0][34] == 0 && csum(nx.frames[0] + 34, 12, 0) == 0u);

	/* No DHCP server: fallback address after the timeout. */
	net_init(my_mac, nx_tx, nx_udp);
	dhcp_start(0u, my_mac, xid);
	dhcp_tick(DHCP_TIMEOUT_MS - 1u);
	CHECK(dhcp_state() == DHCP_SELECTING);
	dhcp_tick(DHCP_TIMEOUT_MS);
	CHECK(dhcp_state() == DHCP_FALLBACK && net_ip() == STM32H7RS_FALLBACK_IP);

	char text[18];

	net_format_ip(STM32H7RS_FALLBACK_IP, text);
	CHECK(strcmp(text, "192.168.10.100") == 0);
	net_format_mac(my_mac, text);
	CHECK(strcmp(text, "02:80:E1:11:22:33") == 0);
}

/* ---- An image packed by stm32h7rs-tool, when given ---- */

static void test_packed_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	static uint8_t buf[1u << 20];
	size_t n;
	const char *why = "";

	int before = failures;

	CHECK(f != NULL);
	if (f == NULL) {
		return;
	}
	n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	CHECK(n >= STM32H7RS_IMAGE_MIN_SIZE);
	CHECK(image_check((const stm32h7rs_image_header_t *)buf, buf + STM32H7RS_IMAGE_HEADER_SIZE, &why) == 0);
	CHECK(((const stm32h7rs_image_header_t *)buf)->image_size == n);
	if (failures != before) {
		fprintf(stderr, "packed image refused: %s\n", why);
	}
}

int main(int argc, char **argv)
{
	test_crc32();
	test_image();
	test_netcfg();
	test_tftp_write();
	test_tftp_refusals();
	test_tftp_info_and_retries();
	test_dhcp_and_net();
	if (argc > 1) {
		test_packed_file(argv[1]);
	}
	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
