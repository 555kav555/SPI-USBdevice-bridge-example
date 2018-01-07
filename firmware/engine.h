#pragma once

#include "hw_config.h"

#include "fifo.h"

typedef enum {
	CMD_RET_OK,
	CMD_RET_INVAL
} CMD_ret_t;

void CMD_usb_config_handler(uint8_t cfg_num);

int CMD_parseAndExecute(void *buf, unsigned len);

extern fifo_t CMD_response;

void CMD_usb_ctl_custom();

void CMD_usb_ctl_monitor();
