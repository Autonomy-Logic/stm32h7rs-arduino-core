/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 *
 * SL-VISU-5R: 25 MHz HSE, PLL1 600 MHz, PLL3 S 50 MHz ETH_CLK on PD6 for a KSZ8081RNB in RMII
 * 50 MHz clock mode at MDIO address 0, USART3 console on PC10/PC11. Same values as the core's device dtsi.
 */

#ifndef BOOT_DEVICE_H
#define BOOT_DEVICE_H

#define DEVICE_NAME "SL-VISU-5R"

#define DEVICE_PLL1_M 2u
#define DEVICE_PLL1_N 48u
#define DEVICE_PLL3_M 2u
#define DEVICE_PLL3_N 32u
#define DEVICE_PLL3_S 8u

#define DEVICE_PHY_ADDR 0u

/** RMII pins: { port, pin, alternate function }. */
#define DEVICE_ETH_PINS                                                                                       \
	{GPIOA, 4, 11}, /* MDIO */                                                                            \
	{GPIOC, 1, 11}, /* MDC */                                                                             \
	{GPIOA, 7, 11}, /* CRS_DV */                                                                          \
	{GPIOC, 4, 11}, /* RXD0 */                                                                            \
	{GPIOC, 5, 11}, /* RXD1 */                                                                            \
	{GPIOD, 8, 11}, /* TX_EN */                                                                           \
	{GPIOB, 0, 11}, /* TXD0 */                                                                            \
	{GPIOB, 7, 11}, /* TXD1 */                                                                            \
	{GPIOD, 7, 4},  /* REF_CLK */                                                                         \
	{GPIOD, 6, 4},  /* ETH_CLK output to the PHY */

#define DEVICE_GPIO_CLOCKS (RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIODEN)

/** Console: USART3 TX on PC10 (AF7), 115200 baud from PCLK1. */
#define DEVICE_CONSOLE_UART USART3
#define DEVICE_CONSOLE_TX_PORT GPIOC
#define DEVICE_CONSOLE_TX_PIN 10u
#define DEVICE_CONSOLE_TX_AF 7u

#endif /* BOOT_DEVICE_H */
