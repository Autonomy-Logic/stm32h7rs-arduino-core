/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * TFTP server of the bootloader (docs/network-bootloader.md, "TFTP"): writes of firmware.bin, reads of info.
 * Hardware independent; the board supplies the I/O callbacks.
 */

#ifndef BOOT_TFTP_H
#define BOOT_TFTP_H

#include <stdint.h>

/** Retransmission interval, retries, busy window and final-ACK linger, in milliseconds or counts. */
#define TFTP_RETRY_MS 1000u
#define TFTP_MAX_RETRIES 6u
#define TFTP_BUSY_MS 3000u
#define TFTP_LINGER_MS 1000u
#define TFTP_INFO_MAX 511u

struct tftp_io {
	void *ctx;
	/** Sends one UDP datagram from the TFTP port to ip:port. */
	void (*send)(void *ctx, uint32_t ip, uint16_t port, const uint8_t *data, uint16_t len);
	/** Starts an image write; size is valid when has_size. Returns 0 or sets why. */
	int (*write_begin)(void *ctx, uint32_t size, int has_size, const char **why);
	/** Stores len bytes at offset. Returns 0 or sets why. */
	int (*write_data)(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len, const char **why);
	/** Completes a write of size bytes; crc is valid when has_crc. Returns 0 or sets why. */
	int (*write_end)(void *ctx, uint32_t size, uint32_t crc, int has_crc, const char **why);
	/** A write was dropped before completion. */
	void (*write_abort)(void *ctx);
	/** A write completed and its last ACK was sent; called once the linger time is over. */
	void (*write_done)(void *ctx);
	/** Writes the info file into buf (at most max bytes) and returns its length. */
	uint16_t (*info)(void *ctx, char *buf, uint16_t max);
};

enum tftp_state {
	TFTP_IDLE,
	TFTP_WRITING,
	TFTP_READING,
	TFTP_LINGER,
};

struct tftp {
	const struct tftp_io *io;
	enum tftp_state state;
	uint32_t peer_ip;
	uint16_t peer_port;
	uint16_t blksize;
	uint16_t block;      /**< last block acknowledged (write) or sent (read) */
	uint32_t offset;     /**< bytes received */
	uint32_t tsize;
	int has_tsize;
	uint32_t crc;
	int has_crc;
	uint32_t last_rx_ms;
	uint32_t last_tx_ms;
	uint32_t retries;
	uint16_t last_len;
	uint8_t last[4u + TFTP_INFO_MAX + 1u];
};

void tftp_init(struct tftp *t, const struct tftp_io *io);

/** Handles one datagram received on the TFTP port. */
void tftp_input(struct tftp *t, uint32_t now_ms, uint32_t ip, uint16_t port, const uint8_t *pkt, uint16_t len);

/** Retransmits, drops stalled transfers and ends the linger; call often. */
void tftp_tick(struct tftp *t, uint32_t now_ms);

/** True while a transfer is in progress. */
int tftp_busy(const struct tftp *t);

#endif /* BOOT_TFTP_H */
