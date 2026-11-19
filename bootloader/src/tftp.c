/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "tftp.h"

#include <stddef.h>
#include <string.h>

#include "stm32h7rs_boot.h"

#define OP_RRQ 1u
#define OP_WRQ 2u
#define OP_DATA 3u
#define OP_ACK 4u
#define OP_ERROR 5u
#define OP_OACK 6u

#define ERR_UNDEFINED 0u
#define ERR_NOT_FOUND 1u
#define ERR_DISK_FULL 3u
#define ERR_ILLEGAL 4u

#define DEFAULT_BLKSIZE 512u

static uint16_t ld16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void st16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static int lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int equals_nocase(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (lower((unsigned char)*a) != lower((unsigned char)*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

/** Parses a decimal number of at most 10 digits; returns -1 when malformed. */
static int parse_dec(const char *s, uint32_t *out)
{
	uint64_t v = 0u;
	int n = 0;

	for (; *s != '\0'; s++, n++) {
		if (*s < '0' || *s > '9' || n >= 10) {
			return -1;
		}
		v = v * 10u + (uint64_t)(*s - '0');
	}
	if (n == 0 || v > 0xFFFFFFFFu) {
		return -1;
	}
	*out = (uint32_t)v;
	return 0;
}

/** Parses exactly 8 hex digits; returns -1 when malformed. */
static int parse_hex32(const char *s, uint32_t *out)
{
	uint32_t v = 0u;
	int n;

	for (n = 0; s[n] != '\0'; n++) {
		int c = lower((unsigned char)s[n]);
		uint32_t d;

		if (c >= '0' && c <= '9') {
			d = (uint32_t)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			d = (uint32_t)(c - 'a' + 10);
		} else {
			return -1;
		}
		if (n >= 8) {
			return -1;
		}
		v = (v << 4) | d;
	}
	if (n != 8) {
		return -1;
	}
	*out = v;
	return 0;
}

static uint16_t put_str(uint8_t *buf, uint16_t at, uint16_t max, const char *s)
{
	size_t n = strlen(s) + 1u;

	if ((size_t)at + n > max) {
		return at;
	}
	memcpy(buf + at, s, n);
	return (uint16_t)(at + n);
}

static void u32_to_dec(uint32_t v, char *out)
{
	char tmp[11];
	int n = 0;

	do {
		tmp[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while (v != 0u);
	while (n > 0) {
		*out++ = tmp[--n];
	}
	*out = '\0';
}

static void u32_to_hex(uint32_t v, char *out)
{
	static const char digits[] = "0123456789abcdef";

	for (int i = 7; i >= 0; i--) {
		out[7 - i] = digits[(v >> (4 * i)) & 0xFu];
	}
	out[8] = '\0';
}

/** Sends a packet and keeps it for retransmission. */
static void send_kept(struct tftp *t, uint32_t now_ms, uint16_t len)
{
	t->last_len = len;
	t->last_tx_ms = now_ms;
	t->retries = 0u;
	t->io->send(t->io->ctx, t->peer_ip, t->peer_port, t->last, len);
}

static void send_error(struct tftp *t, uint32_t ip, uint16_t port, uint16_t code, const char *msg)
{
	uint8_t pkt[4u + 64u];
	size_t n = strlen(msg);

	if (n > 63u) {
		n = 63u;
	}
	st16(pkt, OP_ERROR);
	st16(pkt + 2, code);
	memcpy(pkt + 4, msg, n);
	pkt[4u + n] = 0u;
	t->io->send(t->io->ctx, ip, port, pkt, (uint16_t)(5u + n));
}

static void send_ack(struct tftp *t, uint32_t now_ms, uint16_t block)
{
	st16(t->last, OP_ACK);
	st16(t->last + 2, block);
	send_kept(t, now_ms, 4u);
}

static void drop(struct tftp *t)
{
	if (t->state == TFTP_WRITING && t->io->write_abort != NULL) {
		t->io->write_abort(t->io->ctx);
	}
	t->state = TFTP_IDLE;
}

static void fail(struct tftp *t, uint16_t code, const char *msg)
{
	send_error(t, t->peer_ip, t->peer_port, code, msg);
	drop(t);
}

void tftp_init(struct tftp *t, const struct tftp_io *io)
{
	memset(t, 0, sizeof(*t));
	t->io = io;
	t->state = TFTP_IDLE;
}

int tftp_busy(const struct tftp *t)
{
	return t->state != TFTP_IDLE;
}

/** Splits a request into NUL-terminated strings; returns the count, or -1 when the last one is unterminated. */
static int split_request(const uint8_t *p, uint16_t len, const char **fields, int max)
{
	int n = 0;
	uint16_t start = 0u;

	for (uint16_t i = 0u; i < len; i++) {
		if (p[i] == 0u) {
			if (n < max) {
				fields[n++] = (const char *)p + start;
			}
			start = (uint16_t)(i + 1u);
		}
	}
	return start == len ? n : -1;
}

static void start_read(struct tftp *t, uint32_t now_ms)
{
	uint16_t n = t->io->info(t->io->ctx, (char *)t->last + 4, TFTP_INFO_MAX);

	if (n > TFTP_INFO_MAX) {
		n = TFTP_INFO_MAX;
	}
	st16(t->last, OP_DATA);
	st16(t->last + 2, 1u);
	t->block = 1u;
	t->state = TFTP_READING;
	send_kept(t, now_ms, (uint16_t)(4u + n));
}

static void start_write(struct tftp *t, uint32_t now_ms, const char **opts, int nopts)
{
	int any_option = 0;
	uint16_t blksize = DEFAULT_BLKSIZE;
	uint32_t value;
	const char *why = "write refused";

	t->has_tsize = 0;
	t->has_crc = 0;
	for (int i = 0; i + 1 < nopts; i += 2) {
		if (equals_nocase(opts[i], "blksize") && parse_dec(opts[i + 1], &value) == 0) {
			if (value < STM32H7RS_TFTP_MIN_BLKSIZE) {
				value = STM32H7RS_TFTP_MIN_BLKSIZE;
			}
			if (value > STM32H7RS_TFTP_MAX_BLKSIZE) {
				value = STM32H7RS_TFTP_MAX_BLKSIZE;
			}
			blksize = (uint16_t)value;
			any_option |= 1;
		} else if (equals_nocase(opts[i], "tsize") && parse_dec(opts[i + 1], &value) == 0) {
			t->tsize = value;
			t->has_tsize = 1;
			any_option |= 2;
		} else if (equals_nocase(opts[i], STM32H7RS_TFTP_OPT_CRC32) && parse_hex32(opts[i + 1], &value) == 0) {
			t->crc = value;
			t->has_crc = 1;
			any_option |= 4;
		}
	}

	if (t->has_tsize && t->tsize > STM32H7RS_SLOT_SIZE) {
		send_error(t, t->peer_ip, t->peer_port, ERR_DISK_FULL, "image larger than the program slot");
		return;
	}
	if (t->io->write_begin(t->io->ctx, t->tsize, t->has_tsize, &why) != 0) {
		send_error(t, t->peer_ip, t->peer_port, ERR_UNDEFINED, why);
		return;
	}

	t->state = TFTP_WRITING;
	t->blksize = blksize;
	t->block = 0u;
	t->offset = 0u;

	if (any_option == 0) {
		send_ack(t, now_ms, 0u);
		return;
	}

	uint16_t at = 2u;
	char num[11];

	st16(t->last, OP_OACK);
	if ((any_option & 1) != 0) {
		at = put_str(t->last, at, sizeof(t->last), "blksize");
		u32_to_dec(blksize, num);
		at = put_str(t->last, at, sizeof(t->last), num);
	}
	if ((any_option & 2) != 0) {
		at = put_str(t->last, at, sizeof(t->last), "tsize");
		u32_to_dec(t->tsize, num);
		at = put_str(t->last, at, sizeof(t->last), num);
	}
	if ((any_option & 4) != 0) {
		at = put_str(t->last, at, sizeof(t->last), STM32H7RS_TFTP_OPT_CRC32);
		u32_to_hex(t->crc, num);
		at = put_str(t->last, at, sizeof(t->last), num);
	}
	send_kept(t, now_ms, at);
}

static void handle_request(struct tftp *t, uint32_t now_ms, uint16_t op, const uint8_t *pkt, uint16_t len)
{
	const char *fields[16];
	int n = split_request(pkt + 2, (uint16_t)(len - 2u), fields, 16);

	if (n < 2) {
		send_error(t, t->peer_ip, t->peer_port, ERR_ILLEGAL, "malformed request");
		return;
	}
	if (!equals_nocase(fields[1], "octet")) {
		send_error(t, t->peer_ip, t->peer_port, ERR_ILLEGAL, "only octet mode is supported");
		return;
	}
	if (op == OP_RRQ && equals_nocase(fields[0], STM32H7RS_TFTP_FILE_INFO)) {
		start_read(t, now_ms);
	} else if (op == OP_WRQ && equals_nocase(fields[0], STM32H7RS_TFTP_FILE_FIRMWARE)) {
		start_write(t, now_ms, fields + 2, n - 2);
	} else {
		send_error(t, t->peer_ip, t->peer_port, ERR_NOT_FOUND, "file not found");
	}
}

static void handle_data(struct tftp *t, uint32_t now_ms, uint16_t block, const uint8_t *data, uint16_t n)
{
	const char *why = "write failed";
	uint16_t expected = (uint16_t)(t->block + 1u);

	if (t->state == TFTP_LINGER) {
		if (block == t->block) {
			t->io->send(t->io->ctx, t->peer_ip, t->peer_port, t->last, t->last_len);
		}
		return;
	}
	if (block == t->block) {
		/* The client did not get our last ACK or OACK. */
		t->io->send(t->io->ctx, t->peer_ip, t->peer_port, t->last, t->last_len);
		return;
	}
	if (block != expected) {
		return;
	}
	if (n > t->blksize) {
		fail(t, ERR_ILLEGAL, "block larger than blksize");
		return;
	}
	if (n > 0u && t->io->write_data(t->io->ctx, t->offset, data, n, &why) != 0) {
		fail(t, ERR_DISK_FULL, why);
		return;
	}
	t->offset += n;
	t->block = block;

	if (n == t->blksize) {
		send_ack(t, now_ms, block);
		return;
	}
	if (t->has_tsize && t->offset != t->tsize) {
		fail(t, ERR_UNDEFINED, "image size does not match tsize");
		return;
	}
	if (t->io->write_end(t->io->ctx, t->offset, t->crc, t->has_crc, &why) != 0) {
		fail(t, ERR_UNDEFINED, why);
		return;
	}
	send_ack(t, now_ms, block);
	t->state = TFTP_LINGER;
}

void tftp_input(struct tftp *t, uint32_t now_ms, uint32_t ip, uint16_t port, const uint8_t *pkt, uint16_t len)
{
	if (len < 4u) {
		return;
	}

	uint16_t op = ld16(pkt);
	int same_peer = t->state != TFTP_IDLE && ip == t->peer_ip && port == t->peer_port;

	if (t->state != TFTP_IDLE && !same_peer) {
		if (op != OP_RRQ && op != OP_WRQ) {
			send_error(t, ip, port, 5u, "unknown transfer");
			return;
		}
		if (t->state != TFTP_LINGER && now_ms - t->last_rx_ms < TFTP_BUSY_MS) {
			send_error(t, ip, port, ERR_UNDEFINED, "busy with another transfer");
			return;
		}
		if (t->state == TFTP_LINGER) {
			t->io->write_done(t->io->ctx);
		}
		drop(t);
	}

	if (op == OP_RRQ || op == OP_WRQ) {
		if (same_peer) {
			if (t->state == TFTP_LINGER) {
				return;
			}
			drop(t);
		}
		t->peer_ip = ip;
		t->peer_port = port;
		t->last_rx_ms = now_ms;
		handle_request(t, now_ms, op, pkt, len);
		return;
	}
	if (!same_peer) {
		return;
	}
	t->last_rx_ms = now_ms;

	if (op == OP_DATA && (t->state == TFTP_WRITING || t->state == TFTP_LINGER)) {
		handle_data(t, now_ms, ld16(pkt + 2), pkt + 4, (uint16_t)(len - 4u));
	} else if (op == OP_ACK && t->state == TFTP_READING) {
		if (ld16(pkt + 2) == t->block) {
			t->state = TFTP_IDLE;
		}
	} else if (op == OP_ERROR) {
		drop(t);
	}
}

void tftp_tick(struct tftp *t, uint32_t now_ms)
{
	if (t->state == TFTP_IDLE) {
		return;
	}
	if (t->state == TFTP_LINGER) {
		if (now_ms - t->last_tx_ms >= TFTP_LINGER_MS) {
			t->state = TFTP_IDLE;
			t->io->write_done(t->io->ctx);
		}
		return;
	}
	if (now_ms - t->last_tx_ms < TFTP_RETRY_MS) {
		return;
	}
	if (t->retries >= TFTP_MAX_RETRIES) {
		drop(t);
		return;
	}
	t->retries++;
	t->last_tx_ms = now_ms;
	t->io->send(t->io->ctx, t->peer_ip, t->peer_port, t->last, t->last_len);
}
