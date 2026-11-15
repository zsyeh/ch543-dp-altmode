#ifndef CH9445_H
#define CH9445_H
#include "hal.h"
/* CH9445DS1 v1.5 sections 5.1, 5.2, 5.4. 7-bit address, ADDR low. */
#define CH9445_ADDRESS 0x48u
void ch9445_init(void);
void ch9445_poll(void);
/* Board transport: bounded transactions, return 1 only with all ACKs. */
uint8_t board_i2c_write(uint8_t reg, uint8_t value);
uint8_t board_i2c_read(uint8_t reg, uint8_t IDATA *value);
#endif
