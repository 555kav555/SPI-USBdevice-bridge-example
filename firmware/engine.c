#include "engine.h"

#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define DESC_POOL_LEN 512

#define CIRCBUF_LEN 64
#define CIRCBUF_NUM 4

#define CTLHOOKSETUPLIST_NUM 16

typedef enum {
	CMD_EP_BULK = 1,
	CMD_EP_INT,
	CMD_EP_ISO
} CMD_ep_type_t;

typedef enum {
	CMD_GETSTATE = 1,
	CMD_RESETCFG = 0x10,
	CMD_DEVUP,
	CMD_DEVDOWN,
	CMD_SETEPCFG = 0x20,
	CMD_SETEPCFG_BIDIR,
	CMD_SETDEVDESC = 0x30,
	CMD_SETCFGDESC,
	CMD_SETSTRDESC,
	CMD_SETHIDDESC,
	CMD_EPDATA = 0x80,
	CMD_EPDATA_CIRC,
	CMD_CTLHOOKLIST = 0xC0,
	CMD_CTLHOOKDATA,
	CMD_CTLHOOKSTALL,
	CMD_CTLMONUP = 0xF0,
	CMD_CTLMONDOWN
} CMD_cmd_t;

typedef enum {
	CMD_STATE = 1,
	CMD_ISENUMERATED,
	CMD_HASEPDATA = 0x80,
	CMD_CTLHOOKSETUP = 0xC0,
	CMD_CTLMON = 0xF0
} CMD_retCode_t;

typedef struct __attribute__((packed)) {
	uint16_t size_tx;
	uint16_t size_rx;
	uint16_t flags;
} CMD_epbd_t;

/*
typedef struct __attribute__((packed)) {
	uint16_t size;
	uint16_t flags;
} CMD_ep_t;
*/

typedef struct __attribute__((packed)) { // subject to growth
	union __attribute__((packed)) {
		uint8_t flags;
		struct __attribute__((packed)) {
			uint8_t isUp : 1;
		};
	};
	uint8_t devAddr;
} CMD_state_t;

typedef struct __attribute__((packed)) {
	uint8_t devAddr;
	uint8_t cfgId;
} CMD_enumSt_t;

typedef struct {
	uint8_t ep_num;
	uint16_t flags;
	uint16_t size_tx;
	uint16_t size_rx;
} CMD_ep_st_t;

typedef struct {
	uint8_t ep_slot;
	uint8_t len;
} circBufMeta_t;

FIFO_DECL(CMD_response, 252);

static uint16_t controlTransferSetupList[CTLHOOKSETUPLIST_NUM];
static unsigned controlTransferSetupListLen = 0;

static void respond(CMD_retCode_t retCode, void *body, unsigned bodyLen) {
	uint16_t len = bodyLen + 2;
	if(fifo_toWrite(&CMD_response) < (len + 2))
		return;
	uint16_t rc = retCode;
	fifo_write(&CMD_response, &len, 2);
	fifo_write(&CMD_response, &rc, 2);
	fifo_write(&CMD_response, body, bodyLen);
}

static void respondVec(CMD_retCode_t retCode, ...) {
	uint16_t len = 2;
	va_list args;
	va_start(args, retCode);
	while(va_arg(args, void*) != NULL) {
		len += va_arg(args, unsigned);
	}
	va_end(args);
	if(fifo_toWrite(&CMD_response) < (len + 2))
		return;
	uint16_t rc = retCode;
	fifo_write(&CMD_response, &len, 2);
	fifo_write(&CMD_response, &rc, 2);
	va_start(args, retCode);
	void *arg;
	unsigned argLen;
	while((arg = va_arg(args, void*)) != NULL) {
		argLen = va_arg(args, unsigned);
		fifo_write(&CMD_response, arg, argLen);
	}
	va_end(args);
}

static void respondEPData(CMD_retCode_t retCode, unsigned ep_slot) {
	unsigned bodyLen = USB_EP_toRead(ep_slot);
	uint16_t len = bodyLen + 3;
	if(fifo_toWrite(&CMD_response) < (len + 2))
		return;
	uint16_t rc = retCode;
	uint8_t ep_num = USB_getEPNumBySlot(ep_slot);
	fifo_write(&CMD_response, &len, 2);
	fifo_write(&CMD_response, &rc, 2);
	fifo_write(&CMD_response, &ep_num, 1);
	fifo_fromEP(&CMD_response, ep_slot);
}

static int is_up = 0;
static CMD_ep_st_t ep_st[USB_EP_SLOT_NUM] = { [0 ... USB_EP_SLOT_NUM - 1] = {0, 0, 0, 0} };
static uint8_t descPool[DESC_POOL_LEN] __attribute__((aligned(4)));
static uint8_t *descPtr = descPool;

static uint8_t circBuf[CIRCBUF_NUM][CIRCBUF_LEN] __attribute__((aligned(4)));
static circBufMeta_t circBufMeta[CIRCBUF_NUM] = { [0 ... CIRCBUF_NUM - 1] = {0, 0} };
static uint8_t circBufPtr[USB_EP_SLOT_NUM] = { [0 ... USB_EP_SLOT_NUM - 1] = 0 };

static uint8_t controlTransferData[64];
static unsigned controlTransferDataLen = 0;
static bool controlTransferDataIsNeeded = 0;

static int descPoolFree() {
	return descPool + sizeof(descPool) - descPtr;
}

static void CMD_usb_clearCfg() {
	unsigned i;
	for(i = 0; i < CIRCBUF_NUM; ++i) {
		circBufMeta[i].len = 0;
		circBufMeta[i].ep_slot = 0;
	}
	controlTransferSetupListLen = 0;
	USB_clearDescriptors();
	descPtr = descPool;
	for(i = 0; i < USB_EP_SLOT_NUM; ++i) {
		circBufPtr[i] = 0;
		ep_st[i].size_tx = 0;
		ep_st[i].size_rx = 0;
	}
}

static uint8_t CMD_usb_ep_slotByNum(uint8_t ep_num) {
	unsigned i;
	ep_num &= 0x0F;
	for(i = 1; i < USB_EP_SLOT_NUM; ++i) {
		if(ep_st[i].ep_num == ep_num) {
			return i;
		}
	}
	return 0;
}

static void CMD_usb_ep_writeNext_circBufPtr(uint8_t ep_slot) {
	uint8_t steps = CIRCBUF_NUM;
	uint8_t ptr;
	do {
		if(!steps--) return;
		ptr = circBufPtr[ep_slot];
		++circBufPtr[ep_slot];
		if(circBufPtr[ep_slot] >= CIRCBUF_NUM) circBufPtr[ep_slot] = 0;
	} while((circBufMeta[ptr].ep_slot != ep_slot) || (circBufMeta[ptr].len == 0));
	USB_EP_write(ep_slot, circBuf[ptr], circBufMeta[ptr].len, 0);
	USB_EP_setTXState(ep_slot, USB_EPR_STAT_TX_VALID);
}

static void CMD_usb_ep_handler(uint8_t ep_slot, USB_event_t evt) {
	switch(evt) {
		case USB_EVT_IN:
			CMD_usb_ep_writeNext_circBufPtr(ep_slot);
			break;
		case USB_EVT_OUT:
			respondEPData(CMD_HASEPDATA, ep_slot);
			// fall through
		default: // control
			USB_EP_setRXState(ep_slot, USB_EPR_STAT_RX_VALID); // soak the input
	}
}

void CMD_usb_config_handler(uint8_t cfg_num) {
	uint8_t i;
	for(i = 1; i < USB_EP_SLOT_NUM; ++i) {
		if((ep_st[i].size_tx == 0) && (ep_st[i].size_rx == 0)) continue;
		USB_EP_setup(i, ep_st[i].ep_num, ep_st[i].flags, ep_st[i].size_tx, ep_st[i].size_rx, CMD_usb_ep_handler);
		CMD_usb_ep_writeNext_circBufPtr(i);
	}
	CMD_enumSt_t resp = {
		.devAddr = USB_getDevAddr(),
		.cfgId = cfg_num
	};
	respond(CMD_ISENUMERATED, &resp, sizeof(resp));
}

static void CMD_usb_ctl_custom_data() {
	USB_ctl_outStatus(0, &USB_control, USB_EPR_STAT_TX_VALID);
	uint8_t zero = 0;
	respondVec(CMD_CTLHOOKSETUP, &zero, 1, &USB_control.request, sizeof(USB_control.request), controlTransferData, controlTransferDataLen, NULL);
}

void CMD_usb_ctl_custom() {
	controlTransferDataIsNeeded = 0;
	unsigned i = 0;
	while(1) {
		if(i == controlTransferSetupListLen) return;
		if(controlTransferSetupList[i] == *(uint16_t*)&USB_control.request) break;
		++i;
	}
	if(USB_control.request.bmRequestType.Dir) {
		USB_ctl_inDataWait(0, &USB_control);
		controlTransferDataIsNeeded = 1;
		uint8_t zero = 0;
		respondVec(CMD_CTLHOOKSETUP, &zero, 1, &USB_control.request, sizeof(USB_control.request), NULL);
	} else {
		if(USB_control.request.wLength > 0) {
			controlTransferDataLen = USB_control.request.wLength;
			if(controlTransferDataLen > sizeof(controlTransferData))
				controlTransferDataLen = sizeof(controlTransferData);
			USB_ctl_outDataH(0, &USB_control, controlTransferData, controlTransferDataLen, CMD_usb_ctl_custom_data);
		} else {
			uint8_t zero = 0;
			respondVec(CMD_CTLHOOKSETUP, &zero, 1, &USB_control.request, sizeof(USB_control.request), NULL);
			USB_ctl_outStatus(0, &USB_control, USB_EPR_STAT_TX_VALID);
		}
	}
}

void CMD_usb_ctl_monitor() {
	respond(CMD_CTLMON, &USB_control.request, sizeof(USB_control.request));
}

int CMD_parseAndExecute(void *buf, unsigned len) {
	uint8_t *p = (uint8_t*)buf;
	if(len < 2) return CMD_RET_INVAL; // command only
	uint16_t cmd = *(uint16_t*)p;
	p += 2;
	len -= 2;
	switch(cmd) {
		case CMD_GETSTATE: {
			CMD_state_t resp = {
				.isUp = is_up,
				.devAddr = USB_getDevAddr()
			};
			respond(CMD_STATE, &resp, sizeof(resp));
		} break;
		case CMD_DEVUP:
			if(is_up) break;
			USB_connect(1);
			is_up = 1;
			break;
		case CMD_DEVDOWN:
			if(!is_up) break;
			USB_connect(0);
			is_up = 0;
			break;
		case CMD_RESETCFG:
			if(is_up) {
				USB_connect(0);
				is_up = 0;
			}
			CMD_usb_clearCfg();
			break;
		case CMD_SETEPCFG_BIDIR: {
			if(is_up) return CMD_RET_INVAL;
			if(len != (2 + sizeof(CMD_epbd_t))) return CMD_RET_INVAL;
//			uint8_t cfg_id = ((uint8_t*)p)[0];
			uint8_t ep_num = ((uint8_t*)p)[1];
			if(ep_num > 15) return CMD_RET_INVAL;
			uint8_t i;
			for(i = 1; ; ++i) {
				if(i >= USB_EP_SLOT_NUM) return CMD_RET_INVAL;
				if((ep_st[i].size_tx == 0) && (ep_st[i].size_rx == 0)) break;
				if(ep_st[i].ep_num == ep_num) break;
			}
			CMD_epbd_t *ep = (CMD_epbd_t*)(p + 2);
			uint16_t flags;
			switch(ep->flags) {
				case CMD_EP_BULK:
					flags = USB_EPR_EP_TYPE_BULK;
					break;
				case CMD_EP_INT:
					flags = USB_EPR_EP_TYPE_INTERRUPT;
					break;
				case CMD_EP_ISO:
					flags = USB_EPR_EP_TYPE_ISO;
					break;
				default:
					return CMD_RET_INVAL;
			}
			if(ep->size_tx != 0) flags |= USB_EPR_STAT_TX_NAK;
			if(ep->size_rx != 0) flags |= USB_EPR_STAT_RX_VALID;
			ep_st[i].ep_num = ep_num;
			ep_st[i].flags = flags;
			ep_st[i].size_tx = ep->size_tx;
			ep_st[i].size_rx = ep->size_rx;
		} break;
		case CMD_SETDEVDESC:
			if(is_up) return CMD_RET_INVAL;
			if(len != sizeof(USB_deviceDescriptor_t)) return CMD_RET_INVAL;
			if(len > descPoolFree()) return CMD_RET_INVAL;
			memcpy(descPtr, p, len);
			USB_setDescriptor(USB_DEVICE_DESCRIPTOR_TABLE, 0, descPtr, len);
			descPtr += len;
			break;
		case CMD_SETCFGDESC: {
			if(is_up) return CMD_RET_INVAL;
			if(len < (1 + sizeof(USB_configurationDescriptor_t))) return CMD_RET_INVAL;
			if(len > (1 + descPoolFree())) return CMD_RET_INVAL;
//			uint8_t cfg_id = *(uint8_t*)p;
			++p;
			--len;
			memcpy(descPtr, p, len);
			USB_setDescriptor(USB_CONFIGURATION_DESCRIPTOR_TABLE, 0, descPtr, len);
			descPtr += len;
		} break;
		case CMD_SETSTRDESC: {
			if(is_up) return CMD_RET_INVAL;
			if(len < (1 + 4)) return CMD_RET_INVAL;
			if(len > (1 + descPoolFree())) return CMD_RET_INVAL;
			uint8_t str_id = *(uint8_t*)p;
			if(str_id >= USB_DESCRIPTOR_NUM) return CMD_RET_INVAL;
			++p;
			--len;
			memcpy(descPtr, p, len);
			USB_setDescriptor(USB_STRING_DESCRIPTOR_TABLE, str_id, descPtr, len);
			descPtr += len;
		} break;
		case CMD_SETHIDDESC: {
			if(is_up) return CMD_RET_INVAL;
			if(len < 2) return CMD_RET_INVAL;
			if(len > (2 + descPoolFree())) return CMD_RET_INVAL;
//			uint8_t cfg_id = ((uint8_t*)p)[0];
			uint8_t hid_id = ((uint8_t*)p)[1];
			if(hid_id >= USB_DESCRIPTOR_NUM) return CMD_RET_INVAL;
			p += 2;
			len -= 2;
			memcpy(descPtr, p, len);
			USB_setDescriptor(USB_HID_REPORT_DESCRIPTOR_TABLE, hid_id, descPtr, len);
			descPtr += len;
		} break;
		case CMD_EPDATA_CIRC: {
			if(len < 3) return CMD_RET_INVAL;
			if(len > (3 + CIRCBUF_LEN)) return CMD_RET_INVAL;
//			uint8_t cfg_id = ((uint8_t*)p)[0];
			uint8_t ep_num = ((uint8_t*)p)[1];
			uint8_t circBuf_slot = ((uint8_t*)p)[2];
			uint8_t ep_slot = CMD_usb_ep_slotByNum(ep_num & 0xFF);
			if(ep_slot == USB_EP_FREESLOT) return CMD_RET_INVAL;
			if(circBuf_slot >= CIRCBUF_NUM) return CMD_RET_INVAL;
			p += 3;
			len -= 3;
			circBufMeta[circBuf_slot].len = 0; // atomic
			memcpy(circBuf[circBuf_slot], p, len);
			circBufMeta[circBuf_slot].ep_slot = ep_slot;
			circBufMeta[circBuf_slot].len = len;
		} break;
		case CMD_CTLHOOKLIST:
			if(len < 1) return CMD_RET_INVAL;
			if(len > (sizeof(controlTransferSetupList) + 1)) return CMD_RET_INVAL;
			if((len & 1) == 0) return CMD_RET_INVAL;
//			uint8_t ep_num = ((uint8_t*)p)[0];
			++p;
			--len;
			memcpy(controlTransferSetupList, p, len);
			controlTransferSetupListLen = len >> 1;
			break;
		case CMD_CTLHOOKDATA:
			if(len < 1) return CMD_RET_INVAL;
			if(len > (sizeof(controlTransferData) + 1)) return CMD_RET_INVAL;
			if(!controlTransferDataIsNeeded) break;
			controlTransferDataIsNeeded = 0;
//			uint8_t ep_num = ((uint8_t*)p)[0];
			++p;
			--len;
			memcpy(controlTransferData, p, len);
			controlTransferDataLen = len;
			USB_ctl_inData(0, &USB_control, controlTransferData, controlTransferDataLen);
			break;
		case CMD_CTLHOOKSTALL:
			if(len < 1) return CMD_RET_INVAL;
			if(!controlTransferDataIsNeeded) break;
			controlTransferDataIsNeeded = 0;
//			uint8_t ep_num = ((uint8_t*)p)[0];
			USB_ctl_inStatus(0, &USB_control, USB_EPR_STAT_RX_STALL);
			break;
		case CMD_CTLMONUP:
			USB_onControlRequest[0] = CMD_usb_ctl_monitor;
			break;
		case CMD_CTLMONDOWN:
			USB_onControlRequest[0] = NULL;
			break;
		default:
			return CMD_RET_INVAL;
	}
	return CMD_RET_OK;
}
