#pragma once

#define STM32DRV_INLINE
#include "stm32f10x.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_flash.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_spi.h"
#include "misc.h"
#include "stm32f10x_usb_hid.h"

// the same priorities for synchronization purposes
#define USB_PRIORITY 2
#define SPI_PRIORITY 2

void Set_System(void);
void Set_Interrupts(void);
void GPIO_Configuration(void);

#define ON_TICK_NUM 8
typedef void(*onTick_t)();
extern onTick_t onTick[ON_TICK_NUM];

int onTickAdd(onTick_t h);
