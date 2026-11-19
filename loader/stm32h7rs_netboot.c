/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Program side of the STM32H7S7 network bootloader (docs/network-bootloader.md): watchdog refresh,
 * strike clear, restart into the bootloader and the network settings record.
 */

#if defined(CONFIG_ARDUINO_STM32H7RS_NETBOOT)

#include <string.h>

#include <soc.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>

#include "../variants/stm32h7s7_core_common/stm32h7rs_boot.h"

#define REBOOT_DELAY_MS 300
#define NETCFG_OFFSET   (STM32H7RS_NETCFG_ADDR - DT_REG_ADDR(DT_NODELABEL(flash0)))

static volatile stm32h7rs_mailbox_t *const mailbox =
	(volatile stm32h7rs_mailbox_t *)STM32H7RS_MAILBOX_ADDR;

/** Writes the mailbox and pushes it past the data cache, so it survives the reset that follows. */
static void mailbox_store(uint32_t request, uint32_t strikes) {
	stm32h7rs_mailbox_t m = {.request = request, .strikes = strikes};

	stm32h7rs_mailbox_seal(&m);
	mailbox->magic = m.magic;
	mailbox->request = m.request;
	mailbox->strikes = m.strikes;
	mailbox->check = m.check;
	(void)sys_cache_data_flush_range((void *)STM32H7RS_MAILBOX_ADDR, sizeof(m));
}

static void watchdog_refresh(struct k_timer *timer) {
	ARG_UNUSED(timer);
	IWDG->KR = 0xAAAAu;
}

static K_TIMER_DEFINE(watchdog_timer, watchdog_refresh, NULL);

static void strikes_clear(struct k_work *work) {
	ARG_UNUSED(work);
	mailbox_store(STM32H7RS_REQUEST_NONE, 0u);
}

static K_WORK_DELAYABLE_DEFINE(strikes_work, strikes_clear);

static void reboot_now(struct k_work *work) {
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_now);

/** Asks the bootloader to stay for an upload, then resets after a short delay so a reply can go out
 * first. */
void stm32h7rs_request_bootloader(void) {
	mailbox_store(STM32H7RS_REQUEST_UPLOAD, 0u);
	(void)k_work_schedule(&reboot_work, K_MSEC(REBOOT_DELAY_MS));
}

/**
 * Stores the network settings the bootloader uses, when they differ from the stored ones. Addresses
 * are in network byte order; with dhcp set they are ignored.
 *
 * @return 0 when stored or unchanged, a negative errno otherwise
 */
int stm32h7rs_netcfg_store(int dhcp, uint32_t ip, uint32_t netmask, uint32_t gateway,
						   uint32_t dns) {
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(flash));
	stm32h7rs_netcfg_t rec;
	stm32h7rs_netcfg_t cur;
	int ret;

	memset(&rec, 0, sizeof(rec));
	rec.magic = STM32H7RS_NETCFG_MAGIC;
	rec.version = STM32H7RS_NETCFG_VERSION;
	rec.mode = dhcp ? STM32H7RS_NETCFG_MODE_DHCP : STM32H7RS_NETCFG_MODE_STATIC;
	if (!dhcp) {
		rec.ip = ip;
		rec.netmask = netmask;
		rec.gateway = gateway;
		rec.dns = dns;
	}
	rec.crc32 = crc32_ieee((const uint8_t *)&rec, STM32H7RS_NETCFG_CRC_LEN);

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}
	ret = flash_read(dev, NETCFG_OFFSET, &cur, sizeof(cur));
	if (ret == 0 && memcmp(&cur, &rec, sizeof(rec)) == 0) {
		return 0;
	}
	ret = flash_erase(dev, NETCFG_OFFSET, STM32H7RS_NETCFG_SECTOR_SIZE);
	if (ret == 0) {
		ret = flash_write(dev, NETCFG_OFFSET, &rec, sizeof(rec));
	}
	return ret;
}

static int netboot_init(void) {
	k_timer_start(&watchdog_timer, K_NO_WAIT, K_SECONDS(1));
	(void)k_work_schedule(&strikes_work, K_SECONDS(STM32H7RS_STRIKE_CLEAR_S));
	return 0;
}

SYS_INIT(netboot_init, APPLICATION, 0);

#endif /* CONFIG_ARDUINO_STM32H7RS_NETBOOT */
