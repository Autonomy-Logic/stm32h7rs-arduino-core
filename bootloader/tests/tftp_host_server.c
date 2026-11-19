/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * The bootloader's TFTP server and image check on a host UDP socket, with the program slot in RAM, so
 * stm32h7rs-tool can upload to it. Exits 0 after a complete, valid upload, 3 after a refused one, 1 on timeout.
 * Usage: tftp_host_server <udp port>
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "crc32.h"
#include "image.h"
#include "tftp.h"

static uint8_t slot[STM32H7RS_SLOT_SIZE];
static int sock;
static int upload_done;
static int upload_refused;

static uint32_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void tx(void *ctx, uint32_t ip, uint16_t port, const uint8_t *data, uint16_t len)
{
	struct sockaddr_in to = {0};

	(void)ctx;
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = ip;
	to.sin_port = htons(port);
	(void)sendto(sock, data, len, 0, (struct sockaddr *)&to, sizeof(to));
}

static int begin(void *ctx, uint32_t size, int has_size, const char **why)
{
	(void)ctx;
	(void)size;
	(void)has_size;
	(void)why;
	memset(slot, 0xFF, sizeof(slot));
	return 0;
}

static int data(void *ctx, uint32_t offset, const uint8_t *d, uint16_t len, const char **why)
{
	(void)ctx;
	if (offset + len > sizeof(slot)) {
		*why = "image larger than the program slot";
		return -1;
	}
	memcpy(slot + offset, d, len);
	return 0;
}

static int end(void *ctx, uint32_t size, uint32_t crc, int has_crc, const char **why)
{
	const stm32h7rs_image_header_t *h = (const stm32h7rs_image_header_t *)slot;

	(void)ctx;
	upload_refused = 1;
	if (has_crc && crc32_update(0u, slot, size) != crc) {
		*why = "image CRC does not match the crc32 option";
		return -1;
	}
	if (h->image_size != size) {
		*why = "image size does not match its header";
		return -1;
	}
	if (image_check(h, slot + STM32H7RS_IMAGE_HEADER_SIZE, why) != 0) {
		return -1;
	}
	upload_refused = 0;
	return 0;
}

static void abort_write(void *ctx)
{
	(void)ctx;
}

static void done(void *ctx)
{
	(void)ctx;
	upload_done = 1;
}

static uint16_t info(void *ctx, char *buf, uint16_t max)
{
	(void)ctx;
	snprintf(buf, max,
		 "device=HOST\ncore=stm32h7rs\nmode=bootloader\nbootloader=test\nentry=no_app\nmac=02:80:E1:00:00:01\n"
		 "ip=127.0.0.1\ndhcp=off\napp=invalid\n");
	return (uint16_t)strlen(buf);
}

int main(int argc, char **argv)
{
	static const struct tftp_io io = {
		.send = tx,
		.write_begin = begin,
		.write_data = data,
		.write_end = end,
		.write_abort = abort_write,
		.write_done = done,
		.info = info,
	};
	struct tftp t;
	struct sockaddr_in addr = {0};
	struct timeval tv = {0, 10000};
	uint8_t pkt[2048];
	uint32_t start = now_ms();

	if (argc != 2) {
		return 2;
	}
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((uint16_t)atoi(argv[1]));
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("bind");
		return 2;
	}
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	tftp_init(&t, &io);

	uint32_t refused_at = 0u;

	while (!upload_done && now_ms() - start < 60000u) {
		if (upload_refused && refused_at == 0u) {
			refused_at = now_ms();
		}
		if (refused_at != 0u && now_ms() - refused_at > 500u) {
			fprintf(stderr, "upload refused by the image check\n");
			return 3;
		}
		struct sockaddr_in from;
		socklen_t fl = sizeof(from);
		ssize_t n = recvfrom(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fl);

		if (n > 0) {
			tftp_input(&t, now_ms(), from.sin_addr.s_addr, ntohs(from.sin_port), pkt, (uint16_t)n);
		}
		tftp_tick(&t, now_ms());
	}
	if (!upload_done) {
		fprintf(stderr, "no complete upload\n");
		return 1;
	}
	printf("upload received: %u bytes, valid image\n", (unsigned)((stm32h7rs_image_header_t *)slot)->image_size);
	return 0;
}
