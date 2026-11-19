# SPDX-License-Identifier: Apache-2.0
# Copyright (c) Autonomy Logic
#
# Host tests of the bootloader's protocol code. "check" also packs an image with stm32h7rs-tool (needs Go) and
# checks it with the bootloader's image check, so the tool and the bootloader agree on the format.

HOSTCC ?= cc
OUT ?= $(CURDIR)/../../build/bootloader-tests
SRC := ../src
TOOL := ../../tools/stm32h7rs-tool

CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I$(SRC) \
	-I../../variants/stm32h7s7_core_common
SRCS := test_main.c $(SRC)/crc32.c $(SRC)/image.c $(SRC)/netcfg.c $(SRC)/tftp.c $(SRC)/net.c $(SRC)/dhcp.c

PORT ?= 16969

check: $(OUT)/bootloader_tests $(OUT)/packed.bin $(OUT)/tftp_host_server
	$(OUT)/bootloader_tests $(OUT)/packed.bin
	$(OUT)/tftp_host_server $(PORT) & server=$$!; sleep 0.3; \
	(cd $(TOOL) && go run ./cmd/stm32h7rs-tool upload --ip 127.0.0.1 --tftp-port $(PORT) $(OUT)/packed.bin); \
	status=$$?; wait $$server; server_status=$$?; test $$status -eq 0 && test $$server_status -eq 0
	python3 -I tamper.py $(OUT)/packed.bin $(OUT)/tampered.bin
	$(OUT)/tftp_host_server $(PORT) & server=$$!; sleep 0.3; \
	(cd $(TOOL) && go run ./cmd/stm32h7rs-tool upload --skip-local-check --ip 127.0.0.1 --tftp-port $(PORT) \
		$(OUT)/tampered.bin); status=$$?; wait $$server; server_status=$$?; \
	test $$status -ne 0 && test $$server_status -eq 3

check-c: $(OUT)/bootloader_tests
	$(OUT)/bootloader_tests

$(OUT)/bootloader_tests: $(SRCS) $(wildcard $(SRC)/*.h) ../../variants/stm32h7s7_core_common/stm32h7rs_boot.h
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) $(SRCS) -o $@

$(OUT)/tftp_host_server: tftp_host_server.c $(SRC)/tftp.c $(SRC)/image.c $(SRC)/crc32.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) tftp_host_server.c $(SRC)/tftp.c $(SRC)/image.c $(SRC)/crc32.c -o $@

$(OUT)/merged.bin: make_merged.py
	@mkdir -p $(OUT)
	python3 -I make_merged.py $@

$(OUT)/packed.bin: $(OUT)/merged.bin
	cd $(TOOL) && go run ./cmd/stm32h7rs-tool pack -version 0.2.0-test -input $(OUT)/merged.bin -output $(OUT)/packed.bin

clean:
	rm -rf $(OUT)

.PHONY: check check-c clean
