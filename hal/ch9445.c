/* Board2-specific routing, not a generic CH9445 preset table.
 * See docs/BOARD2.md for the pin-by-pin derivation and physical limitations.
 * SYS_CTLR1 is buffered; only SYS_CTLR2 commits the two lane registers.
 * SAFE = PDR.MUXPD, all switches off. A bus fault cannot guarantee isolation. */
#include "pd.h"
#include "ch9445.h"
static uint8_t XDATA step, route1, route2, aux, running, fault, off_tries;
static uint8_t XDATA active_request;
static const uint8_t CODE regs[17]={0x10,0x11,0x30,0x31,0x32,
 0x11,0x30,0x31,0x32,0x10,0x31,0x32,0x30,0x31,0x32,0x30,0x10};

void ch9445_init(void) {
 step=0; route1=route2=aux=active_request=fault=off_tries=0; running=1;
}
uint8_t dp_mux_status(void) {
 if(fault) return MUX_FAULT;
 return running?MUX_PENDING:MUX_READY;
}
void dp_mux_set(uint8_t pin,uint8_t orientation,uint8_t video_role) {
 /* Repeated SAFE calls must not starve an outstanding power-down. */
 if(!pin && !active_request && (running || fault)) return;
 active_request=0; route1=route2=aux=0; step=0; running=1;
 if(!pin) return;
 if(fault || pin!=PIN_D || !orientation || orientation>2u ||
    (video_role!=DP_SOURCE && video_role!=DP_SINK)) {
  fault=1; off_tries=0; return;
 }
 active_request=1;
 if(video_role==DP_SOURCE) {
  route1=orientation==1u?0xa1u:0x58u;
  route2=orientation==1u?0x40u:0x20u;
  aux=orientation==1u?2u:1u;
 } else {
  route1=orientation==1u?0x80u:0x10u;
  route2=orientation==1u?0x52u:0xa4u;
  aux=orientation==1u?1u:2u;
 }
}
void dp_aux_set(uint8_t enable,uint8_t orientation) {
 (void)orientation;
 /* Core always pairs AUX disable with MUX_SAFE; cancel the whole route. */
 if(!enable) dp_mux_set(0,0,0);
}
void ch9445_poll(void) {
 uint8_t value=0,readback;
 if(fault) {
  /* Best effort only: after three attempts the unknown physical state is
   * latched until reboot. Never report READY after any I2C/readback fault. */
  if(off_tries<3u) { off_tries++; (void)board_i2c_write(0x10,1); }
  return;
 }
 if(!running) return;
 if(!active_request) {
  if(!step) {
   if(board_i2c_write(0x10,1)) { step=1; return; }
  } else if(board_i2c_read(0x10,&readback) && readback==1u) {
   running=0; return;
  }
  fault=1; off_tries=0; return;
 }
 if(step==0u) value=1;
 else if(step==10u || step==13u) value=route1;
 else if(step==11u || step==14u) value=route2;
 else if(step==12u || step==15u) value=aux;
 /* Read back the cleared selectors while power-down still isolates them;
  * never assume writes during power-down were accepted by the silicon. */
 if(step<5u || (step>=9u && step<13u)) {
  if(!board_i2c_write(regs[step],value)) { fault=1; off_tries=0; return; }
 } else if(!board_i2c_read(regs[step],&readback) || readback!=value) {
  fault=1; off_tries=0; return;
 }
 if(++step==17u) running=0;
}
