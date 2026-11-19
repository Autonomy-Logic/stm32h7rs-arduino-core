/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "nor.h"

#include <string.h>

#include "board.h"
#include "stm32h7rsxx_hal.h"

/* Macronix OctaBus commands (single SPI and octal forms). */
#define CMD_WREN 0x06u
#define CMD_RDSR 0x05u
#define CMD_WRCR2 0x72u
#define CMD_RESET_EN 0x66u
#define CMD_RESET_MEM 0x99u
#define OCMD_WREN 0x06F9u
#define OCMD_RDSR 0x05FAu
#define OCMD_RDCR2 0x718Eu
#define OCMD_RESET_EN 0x6699u
#define OCMD_RESET_MEM 0x9966u
#define OCMD_SE 0x21DEu
#define OCMD_PP 0x12EDu
#define OCMD_DTR_RD 0xEE11u

/* CR2 addresses and values: 6 dummy cycles (66 MHz class), DTR OPI enable. */
#define CR2_ADDR_MODE 0x00000000u
#define CR2_ADDR_DUMMY 0x00000300u
#define CR2_DTR_OPI 0x02u
#define CR2_DUMMY_66MHZ 0x07u

#define DUMMY_REG_OCTAL_DTR 5u
#define DUMMY_RD_OCTAL_DTR 6u

#define SR_WIP 0x01u
#define SR_WEL 0x02u

#define PAGE_SIZE 256u
#define MAX_FREQ_HZ 50000000u
#define POLL_INTERVAL 0x10u
#define TIMEOUT_MS HAL_XSPI_TIMEOUT_DEFAULT_VALUE
#define ERASE_TIMEOUT_MS 1000u

static XSPI_HandleTypeDef hxspi;

static void pins_init(void)
{
	/* PN0 DQS, PN1 NCS1, PN2..PN5 IO0..IO3, PN6 CLK, PN8..PN11 IO4..IO7, all AF9. */
	static const uint8_t pins[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11};

	RCC->AHB4ENR |= RCC_AHB4ENR_GPIONEN;
	(void)RCC->AHB4ENR;
	for (unsigned i = 0; i < sizeof(pins); i++) {
		uint32_t p = pins[i];
		uint32_t shift4 = (p & 7u) * 4u;

		GPION->OSPEEDR |= 3u << (p * 2u);
		GPION->AFR[p >> 3] = (GPION->AFR[p >> 3] & ~(0xFu << shift4)) | (9u << shift4);
		GPION->MODER = (GPION->MODER & ~(3u << (p * 2u))) | (2u << (p * 2u));
	}
}

/** Command template: single SPI/STR (octal = 0) or octal DTR (octal = 1). */
static void cmd_init(XSPI_RegularCmdTypeDef *c, int octal)
{
	memset(c, 0, sizeof(*c));
	c->OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
	c->InstructionWidth = octal ? HAL_XSPI_INSTRUCTION_16_BITS : HAL_XSPI_INSTRUCTION_8_BITS;
	c->InstructionDTRMode = octal ? HAL_XSPI_INSTRUCTION_DTR_ENABLE : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
	c->AddressDTRMode = octal ? HAL_XSPI_ADDRESS_DTR_ENABLE : HAL_XSPI_ADDRESS_DTR_DISABLE;
	c->AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
	c->AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
	c->DataDTRMode = octal ? HAL_XSPI_DATA_DTR_ENABLE : HAL_XSPI_DATA_DTR_DISABLE;
	c->DQSMode = octal ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
	c->InstructionMode = octal ? HAL_XSPI_INSTRUCTION_8_LINES : HAL_XSPI_INSTRUCTION_1_LINE;
	c->AddressMode = octal ? HAL_XSPI_ADDRESS_8_LINES : HAL_XSPI_ADDRESS_1_LINE;
	c->DataMode = octal ? HAL_XSPI_DATA_8_LINES : HAL_XSPI_DATA_1_LINE;
}

static int send_cmd(XSPI_RegularCmdTypeDef *c)
{
	return HAL_XSPI_Command(&hxspi, c, TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

/** Polls the status register until (SR & mask) == match. */
static int poll_status(int octal, uint8_t mask, uint8_t match, uint32_t timeout_ms)
{
	XSPI_RegularCmdTypeDef c;
	XSPI_AutoPollingTypeDef p = {0};

	cmd_init(&c, octal);
	if (octal) {
		c.Instruction = OCMD_RDSR;
		c.DummyCycles = DUMMY_REG_OCTAL_DTR;
		c.DataLength = 2u;
	} else {
		c.Instruction = CMD_RDSR;
		c.AddressMode = HAL_XSPI_ADDRESS_NONE;
		c.DataLength = 1u;
	}
	c.Address = 0u;
	if (send_cmd(&c) != 0) {
		return -1;
	}
	p.MatchValue = match;
	p.MatchMask = mask;
	p.MatchMode = HAL_XSPI_MATCH_MODE_AND;
	p.IntervalTime = POLL_INTERVAL;
	p.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
	return HAL_XSPI_AutoPolling(&hxspi, &p, timeout_ms) == HAL_OK ? 0 : -1;
}

static int write_enable(int octal)
{
	XSPI_RegularCmdTypeDef c;

	cmd_init(&c, octal);
	c.Instruction = octal ? OCMD_WREN : CMD_WREN;
	c.AddressMode = HAL_XSPI_ADDRESS_NONE;
	c.DataMode = HAL_XSPI_DATA_NONE;
	if (send_cmd(&c) != 0) {
		return -1;
	}
	return poll_status(octal, SR_WEL, SR_WEL, TIMEOUT_MS);
}

/** Writes one byte to configuration register 2 in single SPI mode. */
static int write_cr2_spi(uint32_t addr, uint8_t value)
{
	XSPI_RegularCmdTypeDef c;

	cmd_init(&c, 0);
	c.Instruction = CMD_WRCR2;
	c.Address = addr;
	c.DataLength = 1u;
	if (send_cmd(&c) != 0) {
		return -1;
	}
	return HAL_XSPI_Transmit(&hxspi, &value, TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

/** Software reset in single SPI, octal STR and octal DTR, so the flash leaves any mode a program left it in. */
static void flash_reset(void)
{
	XSPI_RegularCmdTypeDef c;

	memset(&c, 0, sizeof(c));
	c.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
	c.AddressMode = HAL_XSPI_ADDRESS_NONE;
	c.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
	c.DataMode = HAL_XSPI_DATA_NONE;
	c.DQSMode = HAL_XSPI_DQS_DISABLE;

	c.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
	c.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
	c.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
	c.Instruction = CMD_RESET_EN;
	(void)send_cmd(&c);
	c.Instruction = CMD_RESET_MEM;
	(void)send_cmd(&c);

	c.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
	c.InstructionWidth = HAL_XSPI_INSTRUCTION_16_BITS;
	c.Instruction = OCMD_RESET_EN;
	(void)send_cmd(&c);
	c.Instruction = OCMD_RESET_MEM;
	(void)send_cmd(&c);

	c.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
	c.Instruction = OCMD_RESET_EN;
	(void)send_cmd(&c);
	c.Instruction = OCMD_RESET_MEM;
	(void)send_cmd(&c);

	board_delay_ms(100u);
}

static uint32_t prescaler_for(uint32_t kernel_hz)
{
	uint32_t p = 0u;

	while (p < 255u && kernel_hz / (p + 1u) > MAX_FREQ_HZ) {
		p++;
	}
	return p;
}

static int controller_init(uint32_t memory_type)
{
	hxspi.Instance = XSPI2;
	hxspi.Init.FifoThresholdByte = 4u;
	hxspi.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
	hxspi.Init.MemoryType = memory_type;
	hxspi.Init.MemorySize = HAL_XSPI_SIZE_256MB;
	hxspi.Init.ChipSelectHighTimeCycle = 2u;
	hxspi.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
	hxspi.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
	hxspi.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
	hxspi.Init.ClockPrescaler = prescaler_for(HAL_RCC_GetHCLKFreq());
	hxspi.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
	hxspi.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_ENABLE;
	hxspi.Init.ChipSelectBoundary = 0u;
	hxspi.Init.MaxTran = 0u;
	hxspi.Init.Refresh = 0u;
	hxspi.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
	return HAL_XSPI_Init(&hxspi) == HAL_OK ? 0 : -1;
}

int nor_init(void)
{
	uint8_t cr2[2];

	pins_init();
	RCC->AHB5RSTR |= RCC_AHB5RSTR_XSPI2RST;
	RCC->AHB5RSTR &= ~RCC_AHB5RSTR_XSPI2RST;
	RCC->AHB5ENR |= RCC_AHB5ENR_XSPI2EN;
	(void)RCC->AHB5ENR;

	if (controller_init(HAL_XSPI_MEMTYPE_MACRONIX) != 0) {
		return -1;
	}
	flash_reset();
	if (poll_status(0, SR_WIP, 0u, TIMEOUT_MS) != 0) {
		return -2;
	}

	/* Dummy cycles for the clock used, then octal DTR. */
	if (write_enable(0) != 0 || write_cr2_spi(CR2_ADDR_DUMMY, CR2_DUMMY_66MHZ) != 0 ||
	    poll_status(0, SR_WIP, 0u, TIMEOUT_MS) != 0 || write_enable(0) != 0 ||
	    write_cr2_spi(CR2_ADDR_MODE, CR2_DTR_OPI) != 0) {
		return -3;
	}
	board_delay_ms(40u);
	if (controller_init(HAL_XSPI_MEMTYPE_MACRONIX) != 0 || poll_status(1, SR_WIP, 0u, TIMEOUT_MS) != 0) {
		return -4;
	}

	XSPI_RegularCmdTypeDef c;

	cmd_init(&c, 1);
	c.Instruction = OCMD_RDCR2;
	c.Address = CR2_ADDR_MODE;
	c.DummyCycles = DUMMY_REG_OCTAL_DTR;
	c.DataLength = 2u;
	if (send_cmd(&c) != 0 || HAL_XSPI_Receive(&hxspi, cr2, TIMEOUT_MS) != HAL_OK ||
	    (cr2[0] & CR2_DTR_OPI) == 0u) {
		return -5;
	}
	return nor_memmap();
}

int nor_indirect(void)
{
	if (HAL_XSPI_GetState(&hxspi) != HAL_XSPI_STATE_BUSY_MEM_MAPPED) {
		return 0;
	}
	return HAL_XSPI_Abort(&hxspi) == HAL_OK ? 0 : -1;
}

int nor_memmap(void)
{
	XSPI_RegularCmdTypeDef c;
	XSPI_MemoryMappedTypeDef m = {0};

	if (HAL_XSPI_GetState(&hxspi) == HAL_XSPI_STATE_BUSY_MEM_MAPPED) {
		return 0;
	}
	cmd_init(&c, 1);
	c.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
	c.Instruction = OCMD_DTR_RD;
	c.DummyCycles = DUMMY_RD_OCTAL_DTR;
	if (send_cmd(&c) != 0) {
		return -1;
	}
	c.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
	c.Instruction = OCMD_PP;
	c.DummyCycles = DUMMY_RD_OCTAL_DTR;
	c.DQSMode = HAL_XSPI_DQS_DISABLE;
	if (send_cmd(&c) != 0) {
		return -1;
	}
	m.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
	return HAL_XSPI_MemoryMapped(&hxspi, &m) == HAL_OK ? 0 : -1;
}

int nor_erase_sector(uint32_t offset)
{
	XSPI_RegularCmdTypeDef c;

	if (nor_indirect() != 0 || write_enable(1) != 0) {
		return -1;
	}
	cmd_init(&c, 1);
	c.Instruction = OCMD_SE;
	c.Address = offset;
	c.DataMode = HAL_XSPI_DATA_NONE;
	if (send_cmd(&c) != 0) {
		return -1;
	}
	return poll_status(1, SR_WIP, 0u, ERASE_TIMEOUT_MS);
}

static int program_page(uint32_t offset, const uint8_t *data, uint32_t len)
{
	XSPI_RegularCmdTypeDef c;

	if (write_enable(1) != 0) {
		return -1;
	}
	cmd_init(&c, 1);
	c.Instruction = OCMD_PP;
	c.Address = offset;
	c.DataLength = len;
	c.DQSMode = HAL_XSPI_DQS_DISABLE;
	if (send_cmd(&c) != 0 || HAL_XSPI_Transmit(&hxspi, data, TIMEOUT_MS) != HAL_OK) {
		return -1;
	}
	return poll_status(1, SR_WIP, 0u, TIMEOUT_MS);
}

int nor_program(uint32_t offset, const uint8_t *data, uint32_t len)
{
	uint8_t page[PAGE_SIZE];

	if ((offset & 1u) != 0u || nor_indirect() != 0) {
		return -1;
	}
	while (len > 0u) {
		uint32_t room = PAGE_SIZE - (offset % PAGE_SIZE);
		uint32_t n = len < room ? len : room;
		uint32_t padded = (n + 1u) & ~1u;

		memset(page, 0xFF, sizeof(page));
		memcpy(page, data, n);
		if (program_page(offset, page, padded) != 0) {
			return -1;
		}
		offset += n;
		data += n;
		len -= n;
	}
	return 0;
}
