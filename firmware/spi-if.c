#include "spi-if.h"

#include "engine.h"

#include <string.h>

#define SPIIF_PORT GPIOA
#define SPIIF_PIN_CS GPIO_Pin_4
#define SPIIF_PIN_SCK GPIO_Pin_5
#define SPIIF_PIN_MISO GPIO_Pin_6
#define SPIIF_PIN_MOSI GPIO_Pin_7

#define SPI_BLK_SZ 256

#define SPI_BUF_CNT (SPI_BLK_SZ * 2)

static const uint32_t MAGIC = 0xa5a5a5a5;

static uint8_t SPIif_rxBuf[SPI_BUF_CNT] __attribute__((aligned(4)));
static uint8_t SPIif_txBuf[SPI_BUF_CNT] __attribute__((aligned(4)));

void SPIif_init() {
	memset(SPIif_txBuf, 0, SPI_BUF_CNT);
	memcpy(SPIif_txBuf, &MAGIC, 4);
	memcpy(SPIif_txBuf + SPI_BLK_SZ, &MAGIC, 4);

	RCC_APB2PeriphResetCmd(RCC_APB2Periph_SPI1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
	RCC_APB2PeriphResetCmd(RCC_APB2Periph_SPI1, DISABLE);

//	RCC_AHBPeriphResetCmd(RCC_AHBPeriph_DMA1, ENABLE);
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
//	RCC_AHBPeriphResetCmd(RCC_AHBPeriph_DMA1, DISABLE);

	NVIC_InitTypeDef NVIC_Cfg;
	NVIC_Cfg.NVIC_IRQChannel = DMA1_Channel2_IRQn;
	NVIC_Cfg.NVIC_IRQChannelPreemptionPriority = SPI_PRIORITY;
	NVIC_Cfg.NVIC_IRQChannelSubPriority = 0;
	NVIC_Cfg.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_Cfg);
	NVIC_Cfg.NVIC_IRQChannel = DMA1_Channel3_IRQn;
	NVIC_Init(&NVIC_Cfg);

	DMA_InitTypeDef dmaCfg;
	dmaCfg.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
	dmaCfg.DMA_MemoryBaseAddr = (uint32_t)SPIif_rxBuf;
	dmaCfg.DMA_DIR = DMA_DIR_PeripheralSRC;
	dmaCfg.DMA_BufferSize = SPI_BUF_CNT;
	dmaCfg.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	dmaCfg.DMA_MemoryInc = DMA_MemoryInc_Enable;
	dmaCfg.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	dmaCfg.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	dmaCfg.DMA_Mode = DMA_Mode_Circular;
	dmaCfg.DMA_Priority = DMA_Priority_High;
	dmaCfg.DMA_M2M = DMA_M2M_Disable;
	DMA_Init(DMA1_Channel2, &dmaCfg);
	DMA_Cmd(DMA1_Channel2, ENABLE);
	dmaCfg.DMA_MemoryBaseAddr = (uint32_t)SPIif_txBuf;
	dmaCfg.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_Init(DMA1_Channel3, &dmaCfg);
	DMA_Cmd(DMA1_Channel3, ENABLE);

	DMA_ITConfig(DMA1_Channel2, DMA_IT_HT | DMA_IT_TC, ENABLE);

	SPI_InitTypeDef spiCfg;
	spiCfg.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	spiCfg.SPI_Mode = SPI_Mode_Slave;
	spiCfg.SPI_DataSize = SPI_DataSize_8b;
	spiCfg.SPI_CPOL = SPI_CPOL_Low;
	spiCfg.SPI_CPHA = SPI_CPHA_1Edge;
	spiCfg.SPI_NSS = SPI_NSS_Hard;
	spiCfg.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
	spiCfg.SPI_FirstBit = SPI_FirstBit_MSB;
	spiCfg.SPI_CRCPolynomial = 7;
	SPI_Init(SPI1, &spiCfg);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, ENABLE);
	SPI_Cmd(SPI1, ENABLE);

	GPIO_InitTypeDef gpioCfg;
	gpioCfg.GPIO_Speed = GPIO_Speed_50MHz;
	gpioCfg.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	gpioCfg.GPIO_Pin = SPIIF_PIN_SCK | SPIIF_PIN_MOSI | SPIIF_PIN_CS;
	GPIO_Init(SPIIF_PORT, &gpioCfg);
	gpioCfg.GPIO_Mode = GPIO_Mode_AF_PP;
	gpioCfg.GPIO_Pin = SPIIF_PIN_MISO;
	GPIO_Init(SPIIF_PORT, &gpioCfg);
}

static void SPIif_reset() {
}

#define DO_RESET do{SPIif_reset();return;}while(0)

void DMA1_Channel2_IRQHandler() { // SPI RX
	int offset = (DMA_GetITStatus(DMA1_IT_HT2) == SET) ? 0 : SPI_BLK_SZ;
	DMA_ClearITPendingBit(DMA1_IT_GL2);
	memset(SPIif_txBuf + offset + 4, 0, SPI_BLK_SZ - 4);

	uint8_t *p = SPIif_rxBuf + offset;
	uint8_t *e = p + SPI_BLK_SZ;
	uint32_t magic = *(uint32_t*)p;
	if(magic != MAGIC) DO_RESET;
	p += 4;
	while(p < e) {
		uint16_t len = *(uint16_t*)p;
		if(len == 0) break;
		p += 2;
		if((e - p) < len) DO_RESET;
		if(CMD_parseAndExecute(p, len) != CMD_RET_OK) DO_RESET;
		p += len;
	}

	fifo_read(&CMD_response, SPIif_txBuf + offset + 4, SPI_BLK_SZ - 4);
}

/*
void DMA1_Channel3_IRQHandler() { // SPI TX
	DMA_ClearITPendingBit(DMA1_IT_GL3);
}
*/
