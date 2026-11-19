/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * Bootloader in the first 56 KB of internal flash; RAM is AXI SRAM below the mailbox, so the Ethernet DMA can
 * reach its buffers. The last internal flash sector holds the network settings and is not part of the image.
 */

MEMORY
{
	FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 56K
	RAM (rwx)  : ORIGIN = 0x24000000, LENGTH = 0x71F00
}

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
	.isr_vector : {
		KEEP(*(.isr_vector))
	} > FLASH

	.text : {
		*(.text*)
		*(.rodata*)
		. = ALIGN(4);
	} > FLASH

	.ARM.exidx : {
		*(.ARM.exidx*)
	} > FLASH

	_sidata = LOADADDR(.data);

	.data : {
		. = ALIGN(4);
		_sdata = .;
		*(.data*)
		. = ALIGN(4);
		_edata = .;
	} > RAM AT > FLASH

	.bss (NOLOAD) : {
		. = ALIGN(4);
		_sbss = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
	} > RAM

	/DISCARD/ : {
		*(.ARM.attributes)
	}
}
