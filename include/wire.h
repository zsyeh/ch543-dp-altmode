#ifndef WIRE_H
#define WIRE_H
#include "platform.h"
/* USB PD2 header: NDO[14:12], ID[11:9], PR[8], rev[7:6], DR[5]. */
#define PD_GOODCRC 1u
#define PD_ACCEPT 3u
#define PD_REJECT 4u
#define PD_PS_RDY 6u
#define PD_GET_SOURCE_CAP 7u
#define PD_GET_SINK_CAP 8u
#define PD_DR_SWAP 9u
#define PD_PR_SWAP 10u
#define PD_VCONN_SWAP 11u
#define PD_WAIT 12u
#define PD_SOFT_RESET 13u
#define PD_SOURCE_CAP 1u
#define PD_REQUEST 2u
#define PD_SINK_CAP 4u
#define PD_VENDOR 15u
#define USB_SID 0xff00u
#define DP_SVID 0xff01u
#define VDM_IDENTITY 1u
#define VDM_SVIDS 2u
#define VDM_MODES 3u
#define VDM_ENTER 4u
#define VDM_EXIT 5u
#define VDM_ATTENTION 6u
#define VDM_STATUS 16u
#define VDM_CONFIGURE 17u
#define VDM_INIT 0u
#define VDM_ACK 1u
#define VDM_NAK 2u
#define VDM_BUSY 3u
#define PIN_C 0x04u
#define PIN_D 0x08u
#define PIN_E 0x10u
#define DP_SOURCE 1u /* Status connection value, NOT Mode capability value */
#define DP_SINK 2u
#define ST_ENABLED 0x08u
#define ST_MULTI 0x10u
#define ST_USB 0x20u
#define ST_EXIT 0x40u
#define ST_HPD 0x80u
#define ST_IRQ 0x100u
uint16_t wire_get16(const uint8_t XDATA *p);
uint32_t wire_get32(const uint8_t XDATA *p);
void wire_put16(uint8_t XDATA *p, uint16_t v);
void wire_put32(uint8_t XDATA *p, uint32_t v);
#endif
