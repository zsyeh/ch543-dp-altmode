/* End-to-end PD core + real CH9445 driver + modeled PD wire/I2C registers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mock.h"
#include "ch9445.h"
static uint8_t r[0x33],buffer,peer;
static unsigned changes;
uint8_t board_i2c_write(uint8_t reg,uint8_t value) {
 if(reg==0x31) buffer=value;
 else if(reg==0x32) { r[0x31]=buffer; r[reg]=value; changes++; }
 else r[reg]=value;
 return 1;
}
uint8_t board_i2c_read(uint8_t reg,uint8_t *value) { *value=r[reg]; return 1; }
static uint32_t get32(const uint8_t *b) {
 return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
static void send(uint8_t type,uint8_t count,uint32_t h,uint32_t value) {
 uint8_t b[30]={0},i;
 uint16_t header=(uint16_t)(type|0x140u|((uint16_t)!pd.data_role<<5)|
  ((uint16_t)(peer++&7u)<<9)|((uint16_t)count<<12));
 b[0]=(uint8_t)header; b[1]=(uint8_t)(header>>8);
 for(i=0;i<4;i++) { b[2+i]=(uint8_t)(h>>(i*8)); b[6+i]=(uint8_t)(value>>(i*8)); }
 mock_inject(b,(uint8_t)(2+count*4));
}
static void vdm(uint16_t svid,uint8_t cmd,uint8_t kind,uint8_t obj,uint8_t count,uint32_t v) {
 send(15,count,((uint32_t)svid<<16)|0x8000UL|((uint32_t)obj<<8)|((uint32_t)kind<<6)|cmd,v);
}
static void ack(void) { assert(pd.tx_busy); mock_ack(pd.tx_id); }
static void expect(uint16_t svid,uint8_t cmd,uint8_t kind,uint8_t obj,uint8_t count) {
 struct mock_frame *f=&mock.frames[mock.sends-1];
 uint16_t header=(uint16_t)(15u|0x40u|((uint16_t)pd.data_role<<5)|((uint16_t)pd.tx_id<<9)|((uint16_t)count<<12));
 assert(pd.tx_busy && f->len==2+count*4);
 assert((f->bytes[0]|((uint16_t)f->bytes[1]<<8))==header);
 assert(get32(f->bytes+2)==(((uint32_t)svid<<16)|0x8000UL|((uint32_t)obj<<8)|((uint32_t)kind<<6)|cmd));
}
static void enter(uint8_t video,uint8_t dfp,uint8_t orientation) {
 uint32_t mode=video==1?0x00000846UL:0x00080045UL;
 mock_reset(); ch9445_init(); memset(r,0,sizeof r); buffer=peer=0; changes=0;
 pd_init(video,0,1); mock.orientation=orientation; mock.vbus_valid=1;
 pd_poll(); mock_advance(120); assert(pd.orientation==orientation);
 send(1,1,0x04019032UL,0); ack(); send(3,0,0,0); send(6,0,0,0);
 assert(pd.contract && !pd.power_role && !pd.data_role && r[0x10]==1);
 assert(dp_local_mode()==mode);
 if(dfp) {
  assert(pd_request_data_swap()); pd_poll(); ack(); send(3,0,0,0);
  expect(0xff00,1,0,0,1); ack(); vdm(0xff00,1,1,0,4,0x14000000UL);
  expect(0xff00,2,0,0,1); ack(); vdm(0xff00,2,1,0,2,0xff010000UL);
  expect(0xff01,3,0,0,1); ack();
  vdm(0xff01,3,1,0,2,video==1?0x001c0045UL:0x00001c46UL);
  expect(0xff01,4,0,1,1); ack(); vdm(0xff01,4,1,1,1,0);
  expect(0xff01,16,0,1,2); ack(); vdm(0xff01,16,1,1,2,video==1?0x92:1);
  expect(0xff01,17,0,1,2);
  assert(get32(mock.frames[mock.sends-1].bytes+6)==(video==1?0x806u:0x805u));
  ack(); assert(!changes && r[0x10]==1); vdm(0xff01,17,1,1,1,0);
 } else {
  vdm(0xff00,1,0,0,1,0); expect(0xff00,1,1,0,4); ack();
  vdm(0xff00,2,0,0,1,0); expect(0xff00,2,1,0,2); ack();
  vdm(0xff01,3,0,0,1,0); expect(0xff01,3,1,0,2);
  assert(get32(mock.frames[mock.sends-1].bytes+6)==mode); ack();
  vdm(0xff01,4,0,1,1,0); expect(0xff01,4,1,1,1); ack();
  vdm(0xff01,16,0,1,2,video==1?0x82:1); expect(0xff01,16,1,1,2); ack();
  /* C and E are rejected even though the portable protocol supports them. */
  vdm(0xff01,17,0,1,2,0x404u|video); expect(0xff01,17,2,1,1); ack();
  vdm(0xff01,17,0,1,2,0x1004u|video); expect(0xff01,17,2,1,1); ack();
  vdm(0xff01,17,0,1,2,0x804u|video); expect(0xff01,17,1,1,1);
  assert(!changes && r[0x10]==1); ack();
 }
 assert(pd.dp==DP_MUX_WAIT && !mock.hpd_output);
}
int main(void) {
 unsigned video,dfp,o,i;
 for(video=1;video<=2;video++) for(dfp=0;dfp<=1;dfp++) for(o=1;o<=2;o++) {
  enter(video,dfp,o);
  for(i=0;i<16;i++) { assert(!mock.hpd_output); pd_poll(); }
  assert(pd.dp==DP_MUX_WAIT); pd_poll(); assert(pd.dp==DP_ACTIVE);
  assert(!r[0x10] && r[0x30]); assert(!mock.vbus_on_calls);
  if(video==1) { mock_advance(3); assert(!mock.hpd_output); mock_advance(1); assert(mock.hpd_output); }
  if(pd.tx_busy) ack();
  mock.orientation=0; pd_poll(); assert(r[0x10]==1 && !mock.hpd_output);
  pd_poll(); assert(dp_mux_status()==MUX_READY && !pd.entered);
 }
 for(i=0;i<17;i++) {
  unsigned j;
  enter(1,1,1); for(j=0;j<i;j++) pd_poll();
  mock.orientation=0; pd_poll();
  for(j=0;j<20;j++) pd_poll();
  assert(r[0x10]==1 && pd.dp==DP_IDLE && !mock.hpd_output);
 }
 puts("Board2 integration: 8 USB/video/orientation negotiations, C/E NAKs, delayed MUX/HPD, 17 detach phases passed (models)");
 return 0;
}
