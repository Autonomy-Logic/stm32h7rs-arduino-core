/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Autonomy Logic
 */

#include "eth.h"

#include <string.h>

#include "board.h"
#include DEVICE_HEADER
#include "stm32h7rsxx_hal.h"

#define RX_BUF_SIZE 1536u
#define RX_POOL (ETH_RX_DESC_CNT * 2u)

/* PHY registers. */
#define PHY_BMCR 0x00u
#define PHY_BMSR 0x01u
#define PHY_CTRL1 0x1Eu
#define PHY_CTRL2 0x1Fu
#define BMCR_RESET 0x8000u
#define BMCR_ANEN 0x1000u
#define BMCR_ANRESTART 0x0200u
#define BMSR_LINK 0x0004u
#define CTRL1_MODE_MASK 0x0007u
#define CTRL1_MODE_10HD 1u
#define CTRL1_MODE_100HD 2u
#define CTRL1_MODE_10FD 5u
#define CTRL1_MODE_100FD 6u
#define CTRL2_RMII_50MHZ 0x0080u

#define LINK_POLL_MS 250u

static ETH_HandleTypeDef heth;
static ETH_DMADescTypeDef rx_desc[ETH_RX_DESC_CNT] __attribute__((aligned(32)));
static ETH_DMADescTypeDef tx_desc[ETH_TX_DESC_CNT] __attribute__((aligned(32)));
static uint8_t rx_pool[RX_POOL][RX_BUF_SIZE] __attribute__((aligned(32)));
static uint8_t tx_buf[1536] __attribute__((aligned(32)));
static unsigned rx_next;
static const uint8_t *rx_frame;
static uint16_t rx_len;
static int link_up;
static uint32_t link_checked_ms;

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
	*buff = rx_pool[rx_next];
	rx_next = (rx_next + 1u) % RX_POOL;
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
	(void)pEnd;
	*pStart = buff;
	rx_frame = buff;
	rx_len = Length;
}

void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
	(void)buff;
}

static uint16_t phy_read(uint32_t reg)
{
	uint32_t v = 0u;

	(void)HAL_ETH_ReadPHYRegister(&heth, DEVICE_PHY_ADDR, reg, &v);
	return (uint16_t)v;
}

static void phy_write(uint32_t reg, uint16_t v)
{
	(void)HAL_ETH_WritePHYRegister(&heth, DEVICE_PHY_ADDR, reg, v);
}

int eth_init(const uint8_t mac[6])
{
	static uint8_t mac_addr[6];

	board_eth_clocks();
	RCC->AHB1ENR |= RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN;
	(void)RCC->AHB1ENR;
	RCC->AHB1RSTR |= RCC_AHB1RSTR_ETH1RST;
	RCC->AHB1RSTR &= ~RCC_AHB1RSTR_ETH1RST;

	memcpy(mac_addr, mac, 6);
	heth.Instance = ETH;
	heth.Init.MACAddr = mac_addr;
	heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
	heth.Init.RxDesc = rx_desc;
	heth.Init.TxDesc = tx_desc;
	heth.Init.RxBuffLen = RX_BUF_SIZE;
	if (HAL_ETH_Init(&heth) != HAL_OK) {
		return -1;
	}

	/* The PHY takes its 50 MHz reference from the MCU (ETH_CLK); select that clock mode, then autonegotiate. */
	phy_write(PHY_BMCR, BMCR_RESET);
	board_delay_ms(10u);
	phy_write(PHY_CTRL2, (uint16_t)(phy_read(PHY_CTRL2) | CTRL2_RMII_50MHZ));
	phy_write(PHY_BMCR, BMCR_ANEN | BMCR_ANRESTART);
	link_up = 0;
	return 0;
}

static void mac_start(uint16_t mode)
{
	ETH_MACConfigTypeDef cfg;

	(void)HAL_ETH_GetMACConfig(&heth, &cfg);
	cfg.Speed = (mode == CTRL1_MODE_100HD || mode == CTRL1_MODE_100FD) ? ETH_SPEED_100M : ETH_SPEED_10M;
	cfg.DuplexMode = (mode == CTRL1_MODE_10FD || mode == CTRL1_MODE_100FD) ? ETH_FULLDUPLEX_MODE
									      : ETH_HALFDUPLEX_MODE;
	(void)HAL_ETH_SetMACConfig(&heth, &cfg);
	(void)HAL_ETH_Start(&heth);
}

int eth_poll_link(uint32_t now_ms)
{
	if (now_ms - link_checked_ms < LINK_POLL_MS) {
		return link_up;
	}
	link_checked_ms = now_ms;

	/* BMSR latches link-down: read twice for the current state. */
	(void)phy_read(PHY_BMSR);
	int up = (phy_read(PHY_BMSR) & BMSR_LINK) != 0u;

	if (up && !link_up) {
		uint16_t mode = phy_read(PHY_CTRL1) & CTRL1_MODE_MASK;

		if (mode == CTRL1_MODE_10HD || mode == CTRL1_MODE_100HD || mode == CTRL1_MODE_10FD ||
		    mode == CTRL1_MODE_100FD) {
			mac_start(mode);
			link_up = 1;
			board_log(mode == CTRL1_MODE_100FD ? "link up 100 Mb/s full duplex" : "link up");
		}
	} else if (!up && link_up) {
		(void)HAL_ETH_Stop(&heth);
		link_up = 0;
		board_log("link down");
	}
	return link_up;
}

const uint8_t *eth_receive(uint16_t *len)
{
	void *buf = NULL;

	if (!link_up) {
		return NULL;
	}
	rx_frame = NULL;
	if (HAL_ETH_ReadData(&heth, &buf) != HAL_OK || rx_frame == NULL) {
		return NULL;
	}
	*len = rx_len;
	return rx_frame;
}

void eth_send(const uint8_t *frame, uint16_t len)
{
	ETH_BufferTypeDef buf = {0};
	ETH_TxPacketConfigTypeDef tx = {0};

	if (!link_up || len > sizeof(tx_buf)) {
		return;
	}
	memcpy(tx_buf, frame, len);
	buf.buffer = tx_buf;
	buf.len = len;
	tx.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
	tx.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
	tx.CRCPadCtrl = ETH_CRC_PAD_INSERT;
	tx.Length = len;
	tx.TxBuffer = &buf;
	(void)HAL_ETH_Transmit(&heth, &tx, 20u);
	(void)HAL_ETH_ReleaseTxPacket(&heth);
}
