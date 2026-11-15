/* Software master: CH543's peripheral is an I2C SLAVE, and Board2's pins
 * differ from its hardware mapping. No interrupt masking or unbounded waits.
 * Actual bus speed / rise time must be measured with the board's 4.7k pulls. */
#include "ch9445.h"
#include "i2c_pins.h"
static void pause_bus(void) {
 volatile uint8_t delay;
 for(delay=0;delay<16u;delay++) { }
}
static uint8_t clock_high(void) {
 uint8_t limit=32;
 i2c_scl_set(1);
 do { if(i2c_scl_get()) { pause_bus(); return 1; } } while(--limit);
 return 0;
}
static uint8_t stop_bus(void) {
 uint8_t ok;
 i2c_scl_set(0); i2c_sda_set(0); pause_bus();
 ok=clock_high(); i2c_sda_set(1); pause_bus(); return ok;
}
static uint8_t start_bus(void) {
 i2c_sda_set(1);
 if(!clock_high() || !i2c_sda_get()) return 0;
 i2c_sda_set(0); pause_bus(); i2c_scl_set(0); return 1;
}
static uint8_t put_byte(uint8_t value) {
 uint8_t mask=0x80u,ack;
 do {
  i2c_sda_set((value&mask)?1:0); pause_bus();
  if(!clock_high()) return 0;
  i2c_scl_set(0); mask>>=1;
 } while(mask);
 i2c_sda_set(1); pause_bus();
 if(!clock_high()) return 0;
 ack=!i2c_sda_get(); i2c_scl_set(0); return ack;
}
uint8_t board_i2c_write(uint8_t reg,uint8_t value) {
 uint8_t ok;
 ok=start_bus() && put_byte(CH9445_ADDRESS<<1) && put_byte(reg) && put_byte(value);
 return stop_bus() && ok;
}
uint8_t board_i2c_read(uint8_t reg,uint8_t IDATA *value) {
 uint8_t bits=8,result=0,ok=0;
 if(start_bus() && put_byte(CH9445_ADDRESS<<1) && put_byte(reg) &&
    start_bus() && put_byte((CH9445_ADDRESS<<1)|1u)) {
  i2c_sda_set(1);
  do {
   pause_bus();
   if(!clock_high()) goto done;
   result=(result<<1)|i2c_sda_get(); i2c_scl_set(0);
  } while(--bits);
  /* Final byte is NACKed (SDA remains released). */
  pause_bus(); if(!clock_high()) goto done;
  i2c_scl_set(0); *value=result; ok=1;
 }
done:
 return stop_bus() && ok;
}
