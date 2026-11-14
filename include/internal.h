#ifndef INTERNAL_H
#define INTERNAL_H
#include "pd.h"
#define AFTER_NONE 0u
#define AFTER_VDM 1u
#define AFTER_CONFIG 2u
#define AFTER_EXIT 3u
#define AFTER_SOFT 4u
#define AFTER_DR 5u
#define AFTER_ACCEPT 6u
#define AFTER_READY 7u
#define AFTER_DR_REQUEST 8u
#define AFTER_SOFT_REQUEST 9u
uint8_t pd_due(uint16_t deadline);
void pd_safe(void);
void pd_recover(uint8_t error);
void pd_queue_control(uint8_t type, uint8_t action);
void pd_queue_data(uint8_t type, uint32_t value);
void pd_queue_vdm(uint16_t svid, uint8_t cmd, uint8_t type,
                  uint8_t obj, uint8_t count, uint32_t vdo);
void dp_reset(void);
void dp_poll(void);
void dp_receive(uint8_t count);
void dp_tx_complete(uint8_t action);
void dp_begin(void);
#endif
