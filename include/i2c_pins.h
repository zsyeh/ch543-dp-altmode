#ifndef I2C_PINS_H
#define I2C_PINS_H
#ifdef I2C_TEST
void i2c_scl_set(uint8_t release);
void i2c_sda_set(uint8_t release);
uint8_t i2c_scl_get(void);
uint8_t i2c_sda_get(void);
#else
#include "ch543_regs.h"
/* Bit writes preserve HPD and PWREN# while interrupts touch the same ports. */
#define i2c_scl_set(v) (BOARD_SCL = (v))
#define i2c_sda_set(v) (BOARD_SDA = (v))
#define i2c_scl_get() BOARD_SCL
#define i2c_sda_get() BOARD_SDA
#endif
#endif
