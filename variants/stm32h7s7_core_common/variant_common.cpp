/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>

/** 1200 bps request on the USB serial port: reboot into MCUboot serial recovery through the retained boot mode. */
void _on_1200_bps() {
	bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
	sys_reboot(SYS_REBOOT_WARM);
}

/** Runs after each loop(): yields one tick so the lower-priority network and USB threads get CPU time. */
void __loopHook(void) {
	k_sleep(K_TICKS(1));
}
