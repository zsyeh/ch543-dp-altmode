/* Actual driver + functional register model. No physical MUX or timing proof. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pd.h"
#include "ch9445.h"
static uint8_t r[0x33],buffer;
static unsigned calls,fail_at,bad_at;
uint8_t board_i2c_write(uint8_t reg,uint8_t value) {
 assert(reg<sizeof r); calls++;
 if(calls==fail_at) return 0;
 if(reg==0x31) buffer=value;
 else if(reg==0x32) { r[0x31]=buffer; r[reg]=value; }
 else r[reg]=value;
 return 1;
}
uint8_t board_i2c_read(uint8_t reg,uint8_t *value) {
 assert(reg<sizeof r); calls++;
 if(calls==fail_at) return 0;
 *value=r[reg]^(calls==bad_at?1u:0u); return 1;
}
static void reset(void) {
 memset(r,0,sizeof r); calls=fail_at=bad_at=0; buffer=0;
 ch9445_init(); ch9445_poll(); assert(r[0x10]==1);
 ch9445_poll(); assert(dp_mux_status()==MUX_READY); calls=0;
}
/* Index = CH9445 inputs CRX2, CTX2, CTX1, CRX1, all P pins.
 * Board2 PORT1 physical pad oracle, independent of driver's route bytes. */
static const uint8_t typec_pads[4]={11,22,2,31}; /* A11, B2, A2, B11 */
static uint8_t selected(uint8_t mask) {
 unsigned i; uint8_t result=0;
 for(i=0;i<4;i++) if(mask&(1u<<i)) { assert(!result); result=typec_pads[i]; }
 return result;
}
static void check_path(uint8_t role,uint8_t orientation) {
 uint8_t a=r[0x31]&9u,b=(r[0x31]>>4)&6u;
 uint8_t c=r[0x32]&6u,d=(r[0x32]>>4)&9u;
 uint8_t rx=(r[0x31]>>4)&9u,tx=(r[0x32]>>4)&6u;
 assert(r[0x10]==0 && r[0x11]==0);
 if(role==1u) {
  /* Native DP sink ML0+ pin12 -> A+, ML1+ pin9 -> B+. */
  assert(!c && !d);
  assert(selected(a)==(orientation==1?11:31));
  assert(selected(b)==(orientation==1?22:2));
  assert(r[0x30]==(orientation==1?2:1));
 } else {
  /* Native DP source ML0+ pin1 -> C+, ML1+ pin4 -> D+. */
  assert(!a && !b);
  assert(selected(c)==(orientation==1?22:2));
  assert(selected(d)==(orientation==1?11:31));
  assert(r[0x30]==(orientation==1?1:2));
 }
 assert(selected(rx)==(orientation==1?31:11));
 assert(selected(tx)==(orientation==1?2:22));
}
int main(void) {
 unsigned role,o,i,j;
 for(role=1;role<=2;role++) for(o=1;o<=2;o++) {
  reset(); dp_mux_set(8,o,role); dp_aux_set(1,o);
  for(i=0;i<17;i++) {
   assert(dp_mux_status()==MUX_PENDING); ch9445_poll();
   assert(calls==i+1); /* at most one transaction per poll */
   if(i<9) assert(r[0x10]==1); /* initial off, including stale map */
   if(i>=9 && i<11) assert(r[0x31]==0 && r[0x32]==0);
   if(i<12) assert(r[0x30]==0);
  }
  assert(dp_mux_status()==MUX_READY); check_path(role,o);
  dp_aux_set(0,o); dp_mux_set(0,o,role); ch9445_poll();
  assert(r[0x10]==1); ch9445_poll(); assert(dp_mux_status()==MUX_READY);
 }
 /* Every write/read fault, including final readback: latched + best-effort OFF. */
 for(i=1;i<=17;i++) {
  reset(); fail_at=i; dp_mux_set(8,1,1);
  for(j=0;j<20;j++) ch9445_poll();
  assert(dp_mux_status()==MUX_FAULT && r[0x10]==1 && calls==i+3);
  dp_mux_set(8,2,2); ch9445_poll(); assert(dp_mux_status()==MUX_FAULT);
 }
 for(i=6;i<=17;i++) {
  if(i>=10 && i<=13) continue;
  reset(); bad_at=i; dp_mux_set(8,1,2);
  for(j=0;j<20;j++) ch9445_poll();
  assert(dp_mux_status()==MUX_FAULT && r[0x10]==1);
 }
 /* A cancellation at every stage must never finish the obsolete route. */
 for(i=0;i<=17;i++) {
  reset(); dp_mux_set(8,2,1);
  for(j=0;j<i;j++) ch9445_poll();
  dp_aux_set(0,2); dp_mux_set(0,2,1);
  for(j=0;j<20;j++) ch9445_poll();
  assert(dp_mux_status()==MUX_READY && r[0x10]==1);
 }
 reset(); dp_mux_set(4,1,1); ch9445_poll(); assert(dp_mux_status()==MUX_FAULT);
 reset(); dp_mux_set(16,2,2); ch9445_poll(); assert(dp_mux_status()==MUX_FAULT);
 /* Power-on from a stale active map still isolates first. */
 reset(); r[0x10]=0; r[0x30]=2; r[0x31]=0x21; r[0x32]=0;
 ch9445_init(); ch9445_poll(); assert(r[0x10]==1);
 puts("CH9445: 4 physical routes, 17 NACK stages, 8 readback faults, 18 cancellations, invalid pins and stale boot passed (model)");
 return 0;
}
