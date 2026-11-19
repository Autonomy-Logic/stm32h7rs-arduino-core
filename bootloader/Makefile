# SPDX-License-Identifier: Apache-2.0
# Copyright (c) Autonomy Logic
#
# STM32H7S7 network bootloader. Builds one device (DEVICE, a file pair in devices/) into $(OUT):
#   make DEVICE=sl_visu_5r CROSS=<path>/arm-zephyr-eabi- HAL_DIR=<hal_stm32>/stm32cube/stm32h7rsxx CMSIS_DIR=<cmsis>
# Host tests: make -C tests check

DEVICE ?= sl_visu_5r
CROSS ?= arm-zephyr-eabi-
WS_DIR ?= $(abspath $(CURDIR)/../..)
HAL_DIR ?= $(WS_DIR)/modules/hal/stm32/stm32cube/stm32h7rsxx
CMSIS_DIR ?= $(WS_DIR)/modules/hal/cmsis/CMSIS/Core
OUT ?= $(CURDIR)/../build/bootloader/$(DEVICE)

include devices/$(DEVICE).mk

CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size

ARCH := -mcpu=cortex-m7 -mthumb -mfloat-abi=soft
DEFS := -DSTM32H7S7xx -DUSE_HAL_DRIVER -DHSE_VALUE=$(DEVICE_HSE_HZ)UL -DDEVICE_HEADER='"$(DEVICE).h"'
INCS := -Isrc -Idevices -I../variants/stm32h7s7_core_common -I$(HAL_DIR)/drivers/include -I$(HAL_DIR)/soc \
	-I$(CMSIS_DIR)/Include
COMMON := $(ARCH) -Os -g -ffunction-sections -fdata-sections -fno-common -ffreestanding -std=gnu11 $(DEFS) $(INCS)
CFLAGS := $(COMMON) -Wall -Wextra -Werror -Wno-unused-parameter
HAL_CFLAGS := $(COMMON) -w
LDFLAGS := $(ARCH) -nostartfiles -Wl,--gc-sections -Tsrc/bootloader.ld -Wl,-Map,$(OUT)/bootloader.map

SRCS := src/startup.c src/main.c src/board.c src/nor.c src/eth.c src/net.c src/dhcp.c src/tftp.c src/image.c \
	src/netcfg.c src/crc32.c
HAL_SRCS := $(addprefix $(HAL_DIR)/drivers/src/, stm32h7rsxx_hal.c stm32h7rsxx_hal_cortex.c stm32h7rsxx_hal_rcc.c \
	stm32h7rsxx_hal_rcc_ex.c stm32h7rsxx_hal_gpio.c stm32h7rsxx_hal_pwr.c stm32h7rsxx_hal_pwr_ex.c \
	stm32h7rsxx_hal_xspi.c stm32h7rsxx_hal_eth.c stm32h7rsxx_hal_dma.c) $(HAL_DIR)/soc/system_stm32h7rsxx.c

OBJS := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRCS))
HAL_OBJS := $(patsubst %.c,$(OUT)/hal/%.o,$(notdir $(HAL_SRCS)))

vpath %.c $(sort $(dir $(HAL_SRCS)))

all: $(OUT)/bootloader.bin $(OUT)/bootloader.hex
	$(SIZE) $(OUT)/bootloader.elf

$(OUT)/obj/%.o: src/%.c $(wildcard src/*.h) devices/$(DEVICE).h ../variants/stm32h7s7_core_common/stm32h7rs_boot.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/hal/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(HAL_CFLAGS) -c $< -o $@

$(OUT)/bootloader.elf: $(OBJS) $(HAL_OBJS) src/bootloader.ld
	$(CC) $(LDFLAGS) $(OBJS) $(HAL_OBJS) -lc -lgcc -o $@

$(OUT)/bootloader.bin: $(OUT)/bootloader.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/bootloader.hex: $(OUT)/bootloader.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
