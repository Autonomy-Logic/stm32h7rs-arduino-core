/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Start-up policy and network service loop of the STM32H7S7 network bootloader (docs/network-bootloader.md).
 */

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "crc32.h"
#include "dhcp.h"
#include "eth.h"
#include "image.h"
#include "net.h"
#include "netcfg.h"
#include "nor.h"
#include "stm32h7rs_boot.h"
#include "tftp.h"
#include DEVICE_HEADER

#define BOOTLOADER_VERSION "0.2.0"
#define IDLE_RETURN_MS 60000u

enum entry {
	ENTRY_REQUEST,
	ENTRY_NO_APP,
	ENTRY_CRASH_LOOP,
};

static const char *const entry_names[] = {"request", "no_app", "crash_loop"};

static struct {
	enum entry entry;
	int nor_ok;
	int app_valid;
	uint32_t app_size;
	char app_version[STM32H7RS_IMAGE_VERSION_LEN];
	int use_dhcp;
	uint8_t mac[6];
	uint32_t last_activity_ms;
	/* Upload in progress: the first IMAGE_HEADER_SIZE bytes, kept in RAM and written last. */
	union {
		stm32h7rs_image_header_t header;
		uint8_t bytes[STM32H7RS_IMAGE_HEADER_SIZE];
	} head;
	uint32_t erased_to;
	int upload_ok;
} boot;

static struct tftp tftp;

static const stm32h7rs_image_header_t *slot_header(void)
{
	return (const stm32h7rs_image_header_t *)STM32H7RS_SLOT_BASE;
}

static const uint8_t *slot_payload(void)
{
	return (const uint8_t *)(STM32H7RS_SLOT_BASE + STM32H7RS_IMAGE_HEADER_SIZE);
}

/** Checks the program in the slot (memory-mapped) and records its size and version. */
static void check_program(void)
{
	const char *why = NULL;

	boot.app_valid = 0;
	if (!boot.nor_ok || nor_memmap() != 0) {
		return;
	}
	if (image_check(slot_header(), slot_payload(), &why) != 0) {
		board_log(why);
		return;
	}
	boot.app_valid = 1;
	boot.app_size = slot_header()->image_size;
	memcpy(boot.app_version, slot_header()->version, sizeof(boot.app_version));
	boot.app_version[sizeof(boot.app_version) - 1u] = '\0';
}

/* ---- TFTP image sink ---- */

static void *const no_ctx = NULL;

static int sink_begin(void *ctx, uint32_t size, int has_size, const char **why)
{
	(void)ctx;
	if (!boot.nor_ok) {
		*why = "external flash not available";
		return -1;
	}
	if (has_size && size < STM32H7RS_IMAGE_MIN_SIZE) {
		*why = "image too small";
		return -1;
	}
	/* Invalidate the current program before anything else is written. */
	if (nor_erase_sector(0u) != 0) {
		*why = "flash erase failed";
		return -1;
	}
	boot.app_valid = 0;
	boot.erased_to = STM32H7RS_EXT_ERASE_SIZE;
	boot.upload_ok = 0;
	memset(boot.head.bytes, 0xFF, sizeof(boot.head.bytes));
	board_log("upload started");
	return 0;
}

static int sink_data(void *ctx, uint32_t offset, const uint8_t *data, uint16_t len, const char **why)
{
	(void)ctx;
	if (offset + len > STM32H7RS_SLOT_SIZE) {
		*why = "image larger than the program slot";
		return -1;
	}
	/* The header is kept in RAM and written last. */
	if (offset < sizeof(boot.head.bytes)) {
		uint32_t n = sizeof(boot.head.bytes) - offset;

		if (n > len) {
			n = len;
		}
		memcpy(boot.head.bytes + offset, data, n);
	}
	if (offset + len <= STM32H7RS_IMAGE_HEADER_SIZE) {
		return 0;
	}
	if (offset < STM32H7RS_IMAGE_HEADER_SIZE) {
		uint32_t skip = STM32H7RS_IMAGE_HEADER_SIZE - offset;

		data += skip;
		len = (uint16_t)(len - skip);
		offset = STM32H7RS_IMAGE_HEADER_SIZE;
	}
	while (boot.erased_to < offset + len) {
		if (nor_erase_sector(boot.erased_to) != 0) {
			*why = "flash erase failed";
			return -1;
		}
		boot.erased_to += STM32H7RS_EXT_ERASE_SIZE;
	}
	if (nor_program(offset, data, len) != 0) {
		*why = "flash write failed";
		return -1;
	}
	return 0;
}

static int sink_end(void *ctx, uint32_t size, uint32_t crc, int has_crc, const char **why)
{
	(void)ctx;
	if (size < STM32H7RS_IMAGE_MIN_SIZE) {
		*why = "image too small";
		return -1;
	}
	if (nor_memmap() != 0) {
		*why = "flash read failed";
		return -1;
	}
	if (boot.head.header.image_size != size) {
		*why = "image size does not match its header";
		return -1;
	}
	if (has_crc) {
		uint32_t c = crc32_update(0u, boot.head.bytes, sizeof(boot.head.bytes));

		c = crc32_update(c, slot_payload(), size - STM32H7RS_IMAGE_HEADER_SIZE);
		if (c != crc) {
			*why = "image CRC does not match the crc32 option";
			return -1;
		}
	}
	if (image_check(&boot.head.header, slot_payload(), why) != 0) {
		return -1;
	}
	if (nor_program(0u, boot.head.bytes, sizeof(boot.head.bytes)) != 0 || nor_memmap() != 0) {
		*why = "flash write failed";
		return -1;
	}
	check_program();
	if (!boot.app_valid) {
		*why = "image check failed after writing";
		return -1;
	}
	boot.upload_ok = 1;
	board_log("upload complete");
	return 0;
}

static void sink_abort(void *ctx)
{
	(void)ctx;
	(void)nor_memmap();
	board_log("upload dropped");
}

static void sink_done(void *ctx)
{
	volatile stm32h7rs_mailbox_t *mb = board_mailbox();

	(void)ctx;
	if (boot.upload_ok) {
		mb->request = STM32H7RS_REQUEST_NONE;
		mb->strikes = 0u;
		stm32h7rs_mailbox_seal((stm32h7rs_mailbox_t *)mb);
		board_log("starting the new program");
		board_delay_ms(20u);
		board_reset();
	}
}

/* ---- info file ---- */

static uint16_t append(char *buf, uint16_t at, uint16_t max, const char *s)
{
	while (*s != '\0' && at + 1u < max) {
		buf[at++] = *s++;
	}
	buf[at] = '\0';
	return at;
}

static uint16_t append_kv(char *buf, uint16_t at, uint16_t max, const char *key, const char *value)
{
	at = append(buf, at, max, key);
	at = append(buf, at, max, "=");
	at = append(buf, at, max, value);
	return append(buf, at, max, "\n");
}

static void u32_dec(uint32_t v, char *out)
{
	char tmp[11];
	int n = 0;

	do {
		tmp[n++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v != 0u);
	while (n > 0) {
		*out++ = tmp[--n];
	}
	*out = '\0';
}

static uint16_t info_file(void *ctx, char *buf, uint16_t max)
{
	char tmp[20];
	uint16_t at = 0u;
	const char *dhcp_text = "off";

	(void)ctx;
	if (boot.use_dhcp) {
		dhcp_text = dhcp_state() == DHCP_BOUND ? "bound" : (dhcp_state() == DHCP_FALLBACK ? "fallback" : "seeking");
	}
	buf[0] = '\0';
	at = append_kv(buf, at, max, "device", DEVICE_NAME);
	at = append_kv(buf, at, max, "core", "stm32h7rs");
	at = append_kv(buf, at, max, "mode", "bootloader");
	at = append_kv(buf, at, max, "bootloader", BOOTLOADER_VERSION);
	at = append_kv(buf, at, max, "entry", entry_names[boot.entry]);
	net_format_mac(boot.mac, tmp);
	at = append_kv(buf, at, max, "mac", tmp);
	net_format_ip(net_ip(), tmp);
	at = append_kv(buf, at, max, "ip", tmp);
	at = append_kv(buf, at, max, "dhcp", dhcp_text);
	at = append_kv(buf, at, max, "app", boot.app_valid ? "valid" : "invalid");
	if (boot.app_valid) {
		u32_dec(boot.app_size, tmp);
		at = append_kv(buf, at, max, "app_size", tmp);
		at = append_kv(buf, at, max, "app_version", boot.app_version);
	}
	return at;
}

/* ---- network glue ---- */

static void udp_send_tftp(void *ctx, uint32_t ip, uint16_t port, const uint8_t *data, uint16_t len)
{
	(void)ctx;
	(void)net_udp_send(ip, port, STM32H7RS_TFTP_PORT, data, len);
}

static const struct tftp_io tftp_io = {
	.ctx = no_ctx,
	.send = udp_send_tftp,
	.write_begin = sink_begin,
	.write_data = sink_data,
	.write_end = sink_end,
	.write_abort = sink_abort,
	.write_done = sink_done,
	.info = info_file,
};

static void discovery_reply(uint32_t ip, uint16_t port)
{
	char msg[200];
	char mac[18];
	uint16_t at = 0u;

	net_format_mac(boot.mac, mac);
	msg[0] = '\0';
	at = append(msg, at, sizeof(msg), "{\"service\":\"openplc-runtime\",\"hostname\":\"" DEVICE_NAME
					  "\",\"device\":\"" DEVICE_NAME " (bootloader)\",\"mac\":\"");
	at = append(msg, at, sizeof(msg), mac);
	at = append(msg, at, sizeof(msg), "\",\"runtime_version\":\"bootloader\",\"api_port\":502}");
	(void)net_udp_send(ip, port, STM32H7RS_DISCOVERY_PORT, (const uint8_t *)msg, at);
}

static void udp_input(uint32_t src_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len)
{
	uint32_t now = board_ms();

	if (dst_port == DHCP_CLIENT_PORT) {
		dhcp_input(now, data, len);
		return;
	}
	if (net_ip() == 0u) {
		return;
	}
	if (dst_port == STM32H7RS_TFTP_PORT) {
		boot.last_activity_ms = now;
		tftp_input(&tftp, now, src_ip, src_port, data, len);
	} else if (dst_port == STM32H7RS_DISCOVERY_PORT && len >= sizeof(STM32H7RS_DISCOVERY_PROBE) - 1u &&
		   memcmp(data, STM32H7RS_DISCOVERY_PROBE, sizeof(STM32H7RS_DISCOVERY_PROBE) - 1u) == 0) {
		discovery_reply(src_ip, src_port);
	}
}

static void log_address(void)
{
	char line[40] = "address ";

	net_format_ip(net_ip(), line + 8);
	board_log(line);
}

static void serve(void)
{
	const stm32h7rs_netcfg_t *record = (const stm32h7rs_netcfg_t *)STM32H7RS_NETCFG_ADDR;
	struct netcfg cfg;
	int link = 0;
	int addressed = 0;

	netcfg_parse(record, &cfg);
	boot.use_dhcp = cfg.use_dhcp;
	board_mac(boot.mac);
	net_init(boot.mac, eth_send, udp_input);
	tftp_init(&tftp, &tftp_io);
	if (eth_init(boot.mac) != 0) {
		board_log("ethernet init failed");
	}
	board_log(cfg.use_dhcp ? "network: DHCP" : "network: static");
	boot.last_activity_ms = board_ms();

	for (;;) {
		uint32_t now = board_ms();
		uint16_t len;
		const uint8_t *frame;

		if (eth_poll_link(now) && !link) {
			link = 1;
			if (cfg.use_dhcp) {
				dhcp_start(now, boot.mac, board_uid_crc() ^ now);
			} else {
				net_set_addr(cfg.ip, cfg.netmask, cfg.gateway);
			}
		}
		while ((frame = eth_receive(&len)) != NULL) {
			net_input(frame, len);
		}
		if (link && cfg.use_dhcp) {
			dhcp_tick(now);
		}
		if (!addressed && net_ip() != 0u) {
			addressed = 1;
			log_address();
		}
		tftp_tick(&tftp, now);
		if (tftp_busy(&tftp)) {
			boot.last_activity_ms = now;
		}
		if (boot.app_valid && now - boot.last_activity_ms >= IDLE_RETURN_MS) {
			board_log("no activity: returning to the program");
			board_delay_ms(20u);
			board_reset();
		}
	}
}

int main(void)
{
	volatile stm32h7rs_mailbox_t *mb = board_mailbox();
	stm32h7rs_mailbox_t m;

	board_init();
	board_log("STM32H7S7 network bootloader " BOOTLOADER_VERSION " on " DEVICE_NAME);

	m.magic = mb->magic;
	m.request = mb->request;
	m.strikes = mb->strikes;
	m.check = mb->check;
	if (board_cold_reset() || !stm32h7rs_mailbox_valid(&m)) {
		m.request = STM32H7RS_REQUEST_NONE;
		m.strikes = 0u;
	}

	int rc = nor_init();

	boot.nor_ok = rc == 0;
	if (!boot.nor_ok) {
		char line[32] = "external flash init failed: -";

		line[29] = (char)('0' - rc);
		line[30] = '\0';
		board_log(line);
	}
	check_program();

	if (m.request == STM32H7RS_REQUEST_UPLOAD) {
		boot.entry = ENTRY_REQUEST;
		m.request = STM32H7RS_REQUEST_NONE;
	} else if (!boot.app_valid) {
		boot.entry = ENTRY_NO_APP;
	} else if (m.strikes >= STM32H7RS_MAX_STRIKES) {
		boot.entry = ENTRY_CRASH_LOOP;
		m.strikes = 0u;
	} else {
		m.strikes++;
		stm32h7rs_mailbox_seal(&m);
		mb->magic = m.magic;
		mb->request = m.request;
		mb->strikes = m.strikes;
		mb->check = m.check;
		board_log("starting the program");
		board_start_program();
	}

	stm32h7rs_mailbox_seal(&m);
	mb->magic = m.magic;
	mb->request = m.request;
	mb->strikes = m.strikes;
	mb->check = m.check;
	board_log(boot.entry == ENTRY_REQUEST ? "staying: upload requested"
					      : (boot.entry == ENTRY_NO_APP ? "staying: no valid program"
									    : "staying: program keeps restarting"));
	serve();
	return 0;
}
