/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Reset entry and vector table of the bootloader. The bootloader polls everything, so only the fault
 * handlers and SysTick are used.
 */

#include <stdint.h>

#include "stm32h7rsxx.h"
#include "stm32h7rs_boot.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);
void SysTick_Handler(void);

/** Fault and unused interrupt handler: resets the device. */
void Default_Handler(void)
{
	NVIC_SystemReset();
}

/** Copies .data, clears .bss and runs main(). */
void Reset_Handler(void)
{
	uint32_t *src = &_sidata;
	uint32_t *dst = &_sdata;

	while (dst < &_edata) {
		*dst++ = *src++;
	}
	for (dst = &_sbss; dst < &_ebss; dst++) {
		*dst = 0u;
	}
	SCB->VTOR = STM32H7RS_BOOT_BASE;
	__DSB();
	(void)main();
	NVIC_SystemReset();
}

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used)) const vector_t boot_vectors[16] = {
	(vector_t)(uintptr_t)&_estack,
	Reset_Handler,
	Default_Handler, /* NMI */
	Default_Handler, /* HardFault */
	Default_Handler, /* MemManage */
	Default_Handler, /* BusFault */
	Default_Handler, /* UsageFault */
	0,
	0,
	0,
	0,
	Default_Handler, /* SVCall */
	Default_Handler, /* DebugMon */
	0,
	Default_Handler, /* PendSV */
	SysTick_Handler,
};
