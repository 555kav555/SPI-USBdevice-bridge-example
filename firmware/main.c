#include "hw_config.h"

#include "engine.h"

#include "spi-if.h"

//static void onTickH() {
//}

void USB_LP_CAN1_RX0_IRQHandler() {
	USB_LP_Int();
}

int main(void) {
	Set_System();
	Set_Interrupts();

//	onTickAdd(onTickH);

	USB_CAN_intInit(USB_PRIORITY, 0);
	USB_init();
	USB_onConfig = CMD_usb_config_handler;
//	USB_onControlRequest[0] = CMD_usb_ctl_monitor;
	USB_onControlRequest[1] = CMD_usb_ctl_custom;
	USB_onControlRequest[2] = USB_HID_controlRequestHandler;

	SPIif_init();

//	DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP; // keep CPU clock for debug (Olimex ARM-USB-TINY goes bad)
	while(1) {
//		NVIC_SystemLPConfig(NVIC_LP_SLEEPONEXIT, ENABLE); // pure event driven application
//		__WFI(); // debugger wakes it up also
	}
}
