/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

/* Restarts into the network bootloader for an upload (loader/stm32h7rs_netboot.c). */
extern "C" void stm32h7rs_request_bootloader(void);

/** 1200 bps request on the USB serial port: restart into the network bootloader for an upload. */
void _on_1200_bps() {
	stm32h7rs_request_bootloader();
}

/** Runs after each loop(): yields one tick so the lower-priority network and USB threads get CPU
 * time. */
void __loopHook(void) {
	k_sleep(K_TICKS(1));
}
