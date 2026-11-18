// SPDX-License-Identifier: Apache-2.0
// Compile-and-link probe of every core API the SL-VISU-5R package HAL uses; it is never run on a unit.

#include <Wire.h>
#include <Ethernet.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <soc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_gpio.h>

static LTDC_HandleTypeDef ltdc;
static DMA2D_HandleTypeDef dma2d;
static EthernetServer server(502);
static uint8_t band[800 * 4];

static void probe_display()
{
	LL_APB5_GRP1_EnableClock(LL_APB5_GRP1_PERIPH_LTDC);
	LL_AHB5_GRP1_EnableClock(LL_AHB5_GRP1_PERIPH_DMA2D);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_5, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_5, LL_GPIO_AF_14);

	ltdc.Instance = LTDC;
	(void)HAL_LTDC_Init(&ltdc);
	LTDC_LayerCfgTypeDef layer = {};
	layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
	layer.FBStartAdress = DT_REG_ADDR(DT_NODELABEL(psram));
	(void)HAL_LTDC_ConfigLayer(&ltdc, &layer, 0);
	(void)HAL_LTDC_SetAddress(&ltdc, DT_REG_ADDR(DT_NODELABEL(psram)), 0);

	dma2d.Instance = DMA2D;
	dma2d.Init.Mode = DMA2D_M2M_PFC;
	dma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
	(void)HAL_DMA2D_Init(&dma2d);
	(void)HAL_DMA2D_ConfigLayer(&dma2d, 1);
	(void)HAL_DMA2D_Start(&dma2d, (uint32_t)band, DT_REG_ADDR(DT_NODELABEL(psram)), 800, 1);
	(void)HAL_DMA2D_PollForTransfer(&dma2d, 10);
}

static void probe_store()
{
	const struct flash_area *fa = NULL;
	uint8_t buf[16] = {};
	if (flash_area_open(FIXED_PARTITION_ID(hmi_store_partition), &fa) == 0) {
		(void)flash_area_read(fa, 0, buf, sizeof(buf));
		(void)flash_area_erase(fa, 0, fa->fa_size);
		(void)flash_area_write(fa, 0, buf, sizeof(buf));
		flash_area_close(fa);
	}
}

void setup()
{
	pinMode(66, INPUT);
	pinMode(67, OUTPUT);
	digitalWrite(67, LOW);
	delayMicroseconds(100);
	digitalWrite(67, HIGH);
	Wire.begin();
	Wire.setClock(400000);
	Wire.beginTransmission(0x48);
	Wire.write((uint8_t)0x80);
	(void)Wire.endTransmission();
	(void)Wire.requestFrom((uint8_t)0x48, (size_t)8);
	(void)Wire.read();
	probe_display();
	probe_store();
	uint8_t mac[1] = {0};
	Ethernet.begin(mac, IPAddress(192, 168, 0, 10));
	server.begin();
}

void loop()
{
	EthernetClient client = server.available();
	if (client && client.connected() && client.available()) {
		uint8_t req[12];
		int n = client.read(req, sizeof(req));
		if (n > 0) {
			client.write(req, n);
		}
	}
	(void)digitalRead(66);
}
