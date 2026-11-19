/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "board.h"

#include <string.h>

#include "crc32.h"
#include DEVICE_HEADER
#include "stm32h7rsxx_hal.h"

struct pin {
	GPIO_TypeDef *port;
	uint8_t pin;
	uint8_t af;
};

static const struct pin eth_pins[] = {DEVICE_ETH_PINS};
static uint32_t reset_flags;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
	(void)TickPriority;
	if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
		return HAL_ERROR;
	}
	NVIC_SetPriority(SysTick_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);
	return HAL_OK;
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}

uint32_t board_ms(void)
{
	return HAL_GetTick();
}

void board_delay_ms(uint32_t ms)
{
	uint32_t start = HAL_GetTick();

	while (HAL_GetTick() - start < ms) {
	}
}

static void pin_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
	uint32_t shift2 = pin * 2u;
	uint32_t shift4 = (pin & 7u) * 4u;

	port->OSPEEDR |= 3u << shift2;
	port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << shift4)) | (af << shift4);
	port->MODER = (port->MODER & ~(3u << shift2)) | (2u << shift2);
}

static void console_init(void)
{
	RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;
	(void)RCC->APB1ENR1;
	pin_af(DEVICE_CONSOLE_TX_PORT, DEVICE_CONSOLE_TX_PIN, DEVICE_CONSOLE_TX_AF);
	DEVICE_CONSOLE_UART->CR1 = 0u;
	DEVICE_CONSOLE_UART->BRR = (HAL_RCC_GetPCLK1Freq() + 57600u) / 115200u;
	DEVICE_CONSOLE_UART->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void console_put(char c)
{
	while ((DEVICE_CONSOLE_UART->ISR & USART_ISR_TXE_TXFNF) == 0u) {
	}
	DEVICE_CONSOLE_UART->TDR = (uint8_t)c;
}

void board_log(const char *line)
{
	static const char prefix[] = "[boot] ";

	for (const char *p = prefix; *p != '\0'; p++) {
		console_put(*p);
	}
	for (; *line != '\0'; line++) {
		console_put(*line);
	}
	console_put('\r');
	console_put('\n');
}

static void clocks_init(void)
{
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clk = {0};

	(void)HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);
	(void)HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0);
	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
	}

	osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	osc.HSEState = RCC_HSE_ON;
	osc.PLL1.PLLState = RCC_PLL_ON;
	osc.PLL1.PLLSource = RCC_PLLSOURCE_HSE;
	osc.PLL1.PLLM = DEVICE_PLL1_M;
	osc.PLL1.PLLN = DEVICE_PLL1_N;
	osc.PLL1.PLLP = 1u;
	osc.PLL1.PLLQ = 2u;
	osc.PLL1.PLLR = 2u;
	osc.PLL1.PLLS = 2u;
	osc.PLL1.PLLT = 2u;
	osc.PLL2.PLLState = RCC_PLL_NONE;
	osc.PLL3.PLLState = RCC_PLL_NONE;
	if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
		NVIC_SystemReset();
	}

	clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
			RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
	clk.AHBCLKDivider = RCC_HCLK_DIV2;
	clk.APB1CLKDivider = RCC_APB1_DIV2;
	clk.APB2CLKDivider = RCC_APB2_DIV2;
	clk.APB4CLKDivider = RCC_APB4_DIV2;
	clk.APB5CLKDivider = RCC_APB5_DIV2;
	if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK) {
		NVIC_SystemReset();
	}
}

/** XSPI2 pads: high speed at low voltage when the option byte allows it, then the compensation cell. */
static void xspi2_io_init(void)
{
	RCC->APB4ENR |= RCC_APB4ENR_SBSEN;
	(void)RCC->APB4ENR;
	RCC->CR |= RCC_CR_CSION;
	while ((RCC->CR & RCC_CR_CSIRDY) == 0u) {
	}
	HAL_PWREx_EnableXSPIM2();

	if ((FLASH->OBW1SR & FLASH_OBW1SR_XSPI2_HSLV) != 0u) {
		SBS->CCCSR |= SBS_CCCSR_XSPI2_IOHSLV;
	}
	if ((SBS->CCCSR & SBS_CCCSR_XSPI2_COMP_EN) != 0u) {
		return;
	}
	SBS->CCCSR = (SBS->CCCSR & ~SBS_CCCSR_XSPI2_COMP_CODESEL) | SBS_CCCSR_XSPI2_COMP_EN;
	while ((SBS->CCCSR & SBS_CCCSR_XSPI2_COMP_RDY) == 0u) {
	}

	/* Same adjustment as the program's Zephyr I/O cell driver (ES0596 2.2.15). */
	uint32_t nmos = (SBS->CCVALR >> 16) & 0xFu;
	uint32_t pmos = (SBS->CCVALR >> 20) & 0xFu;

	nmos = (nmos < 0xDu) ? (nmos + 2u) : 0xFu;
	pmos = (pmos > 0x2u) ? (pmos - 2u) : 0x0u;
	SBS->CCSWVALR = (SBS->CCSWVALR & ~(0xFFu << 16)) | (((pmos << 4) | nmos) << 16);
	SBS->CCCSR |= SBS_CCCSR_XSPI2_COMP_CODESEL;
}

void board_init(void)
{
	reset_flags = RCC->RSR;
	RCC->RSR |= RCC_RSR_RMVF;

	SCB_EnableICache();
	(void)HAL_Init();
	clocks_init();
	xspi2_io_init();
	console_init();
}

int board_cold_reset(void)
{
	return (reset_flags & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) != 0u;
}

volatile stm32h7rs_mailbox_t *board_mailbox(void)
{
	return (volatile stm32h7rs_mailbox_t *)STM32H7RS_MAILBOX_ADDR;
}

uint32_t board_uid_crc(void)
{
	/* Byte order of the program's chip ID: words 2, 1, 0, each big-endian. */
	const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
	uint8_t id[12];

	for (int i = 0; i < 3; i++) {
		uint32_t w = uid[2 - i];

		id[i * 4 + 0] = (uint8_t)(w >> 24);
		id[i * 4 + 1] = (uint8_t)(w >> 16);
		id[i * 4 + 2] = (uint8_t)(w >> 8);
		id[i * 4 + 3] = (uint8_t)w;
	}
	return crc32_update(0u, id, sizeof(id));
}

void board_mac(uint8_t mac[6])
{
	uint32_t crc = board_uid_crc();

	mac[0] = 0x02u;
	mac[1] = 0x80u;
	mac[2] = 0xE1u;
	mac[3] = (uint8_t)crc;
	mac[4] = (uint8_t)(crc >> 8);
	mac[5] = (uint8_t)(crc >> 16);
}

void board_eth_clocks(void)
{
	RCC_OscInitTypeDef osc = {0};

	osc.OscillatorType = RCC_OSCILLATORTYPE_NONE;
	osc.PLL1.PLLState = RCC_PLL_NONE;
	osc.PLL2.PLLState = RCC_PLL_NONE;
	osc.PLL3.PLLState = RCC_PLL_ON;
	osc.PLL3.PLLSource = RCC_PLLSOURCE_HSE;
	osc.PLL3.PLLM = DEVICE_PLL3_M;
	osc.PLL3.PLLN = DEVICE_PLL3_N;
	osc.PLL3.PLLP = 2u;
	osc.PLL3.PLLQ = 20u;
	osc.PLL3.PLLR = 16u;
	osc.PLL3.PLLS = DEVICE_PLL3_S;
	osc.PLL3.PLLT = 2u;
	(void)HAL_RCC_OscConfig(&osc);

	__HAL_RCC_ETH1PHY_CONFIG(RCC_ETH1PHYCLKSOURCE_PLL3S);
	__HAL_RCC_ETH1REF_CONFIG(RCC_ETH1REFCLKSOURCE_ETH);

	RCC->AHB4ENR |= DEVICE_GPIO_CLOCKS;
	(void)RCC->AHB4ENR;
	for (unsigned i = 0; i < sizeof(eth_pins) / sizeof(eth_pins[0]); i++) {
		pin_af(eth_pins[i].port, eth_pins[i].pin, eth_pins[i].af);
	}
}

static void watchdog_start(void)
{
	IWDG->KR = 0xCCCCu;
	IWDG->KR = 0x5555u;
	IWDG->PR = 6u; /* divide by STM32H7RS_IWDG_PRESCALER_DIV */
	IWDG->RLR = STM32H7RS_IWDG_RELOAD;
	while (IWDG->SR != 0u) {
	}
	IWDG->KR = 0xAAAAu;
}

void board_start_program(void)
{
	const volatile uint32_t *vectors =
		(const volatile uint32_t *)(STM32H7RS_SLOT_BASE + STM32H7RS_IMAGE_HEADER_SIZE);
	uint32_t sp = vectors[0];
	uint32_t entry = vectors[1];

	watchdog_start();

	__disable_irq();
	SysTick->CTRL = 0u;
	SysTick->VAL = 0u;
	for (unsigned i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
		NVIC->ICER[i] = 0xFFFFFFFFu;
		NVIC->ICPR[i] = 0xFFFFFFFFu;
	}
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
	SCB_DisableICache();

	SCB->VTOR = STM32H7RS_SLOT_BASE + STM32H7RS_IMAGE_HEADER_SIZE;
	__DSB();
	__ISB();
	__set_MSP(sp);
	((void (*)(void))entry)();
	for (;;) {
	}
}

void board_reset(void)
{
	NVIC_SystemReset();
}
