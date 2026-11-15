/* Protocol oracles use explicit numeric wire values, independent of wire.c. */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "mock.h"

static jmp_buf abort_test;
static const char *test_name;
static unsigned int tests_run, tests_failed, assertions;
static uint8_t peer_id, local_id;
#define CHECK(x) do { assertions++; if(!(x)) { \
 fprintf(stderr,"FAIL %s:%d [%s]: %s\n",__FILE__,__LINE__,test_name,#x); \
 longjmp(abort_test,1); } } while(0)
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t u32(const uint8_t *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v) {
 unsigned int i; for(i=0;i<4;i++) { p[i]=(uint8_t)v; v>>=8; }
}
static const struct mock_frame *last(void) { CHECK(mock.sends); return &mock.frames[mock.sends-1]; }
static void safe(void) { CHECK(mock.mux_pin==0); CHECK(mock.aux_enabled==0); CHECK(mock.hpd_output==0); }
static void tx(uint8_t type,uint8_t count) {
 const struct mock_frame *f=last();
 uint16_t expected=(uint16_t)(type|0x40u|((uint16_t)pd.power_role<<8)|
  ((uint16_t)pd.data_role<<5)|((uint16_t)local_id<<9)|((uint16_t)count<<12));
 CHECK(f->len==2u+4u*count); CHECK(u16(f->bytes)==expected);
 CHECK(pd.tx_busy); CHECK(pd.tx_id==local_id);
}
static void vdm_tx(uint16_t svid,uint8_t cmd,uint8_t type,uint8_t obj,uint8_t count) {
 const struct mock_frame *f;
 tx(15,count); f=last();
 CHECK(u32(f->bytes+2)==((uint32_t)svid<<16|0x8000UL|((uint32_t)obj<<8)|((uint32_t)type<<6)|cmd));
}
static void ack(void) {
 uint8_t id=local_id;
 CHECK(pd.tx_busy); CHECK(((u16(last()->bytes)>>9)&7)==id);
 local_id=(uint8_t)((local_id+1)&7); mock_ack(id);
 CHECK(pd.tx_id==local_id);
}
static void inject(uint8_t type,uint8_t count,const uint32_t *objects) {
 uint8_t frame[30],i;
 uint16_t h=(uint16_t)(type|0x40u|((uint16_t)(!pd.power_role)<<8)|
  ((uint16_t)(!pd.data_role)<<5)|((uint16_t)peer_id<<9)|((uint16_t)count<<12));
 frame[0]=(uint8_t)h; frame[1]=(uint8_t)(h>>8);
 for(i=0;i<count;i++) put32(frame+2u+4u*i,objects[i]);
 peer_id=(uint8_t)((peer_id+1)&7); mock_inject(frame,(uint8_t)(2+4*count));
 CHECK(!mock.rx_owned);
}
static void vdm(uint16_t svid,uint8_t cmd,uint8_t type,uint8_t obj,uint8_t n,uint32_t data) {
 uint32_t objects[7]={0,0,0,0,0,0,0};
 objects[0]=((uint32_t)svid<<16)|0x8000UL|((uint32_t)obj<<8)|((uint32_t)type<<6)|cmd;
 objects[1]=data; inject(15,n,objects);
}
static void boot(uint8_t video,uint8_t power,uint8_t preference) {
 mock_reset(); peer_id=local_id=0; pd_init(video,power,preference);
 CHECK(pd.video_role==video); CHECK(pd.power_role==power);
 safe(); CHECK(!mock.vbus_output); CHECK(!mock.phy_enabled);
 mock.orientation=2; mock.vbus_valid=1; mock.source_allowed=1;
 pd_poll(); mock_advance(119); CHECK(!pd.orientation); mock_advance(1);
 CHECK(pd.orientation==2); CHECK(mock.phy_enabled); CHECK(mock.phy_orientation==2);
}
static void sink_contract(uint8_t video,uint8_t preference) {
 uint32_t cap=0x04019032UL;
 boot(video,0,preference); CHECK(pd.pe==PE_WAIT_CAP); CHECK(!pd.contract);
 inject(1,1,&cap); tx(2,1); CHECK(u32(last()->bytes+2)==0x1300c832UL);
 CHECK(pd.pe==PE_WAIT_ACCEPT); CHECK(!pd.contract); safe(); ack();
 inject(3,0,0); CHECK(pd.pe==PE_WAIT_READY); CHECK(!pd.contract);
 inject(6,0,0); CHECK(pd.pe==PE_READY); CHECK(pd.contract); CHECK(pd.dp==DP_IDLE);
 CHECK(!pd.data_role); CHECK(!mock.vbus_output); CHECK(!mock.vbus_on_calls);
}
static void become_dfp(void) {
 CHECK(pd_request_data_swap()); pd_poll(); tx(9,0); ack(); CHECK(pd.dr_swap==2);
 inject(3,0,0); CHECK(pd.data_role==1); CHECK(pd.power_role==0);
 CHECK(pd.dp==DP_DISC_ID); vdm_tx(0xff00,1,0,0,1); safe(); ack();
}
static void to_modes(uint8_t video,uint8_t preference) {
 sink_contract(video,preference); become_dfp();
 vdm(0xff00,1,1,0,4,0x14000000UL);
 CHECK(pd.dp==DP_DISC_SVID); vdm_tx(0xff00,2,0,0,1); ack();
 vdm(0xff00,2,1,0,2,0xff010000UL);
 CHECK(pd.dp==DP_DISC_MODE); vdm_tx(0xff01,3,0,0,1); safe(); ack();
}
static void to_enter(uint8_t video,uint8_t preference,uint32_t mode) {
 to_modes(video,preference); vdm(0xff01,3,1,0,2,mode);
 CHECK(pd.dp==DP_ENTERING); CHECK(pd.mode_obj==1);
 vdm_tx(0xff01,4,0,1,1); safe(); ack();
}
static void to_config(uint8_t video,uint8_t preference,uint16_t status) {
 to_enter(video,preference,video==1?0x001c0045UL:0x00001c46UL);
 vdm(0xff01,4,1,1,1,0); CHECK(pd.entered); CHECK(pd.dp==DP_STATUS_WAIT);
 vdm_tx(0xff01,16,0,1,2); CHECK((u32(last()->bytes+6)&3)==video); safe(); ack();
 vdm(0xff01,16,1,1,2,status); CHECK(pd.dp==DP_CONFIG_WAIT);
 vdm_tx(0xff01,17,0,1,2); safe(); ack();
}
static void dfp_active(uint8_t video,uint8_t preference,uint16_t status,uint8_t pin) {
 to_config(video,preference,status);
 CHECK(u32(last()->bytes+6)==((uint32_t)pin<<8|4u|(video==1?2u:1u)));
 CHECK(mock.mux_commits==0); vdm(0xff01,17,1,1,1,0);
 CHECK(pd.dp==DP_ACTIVE); CHECK(mock.mux_pin==pin); CHECK(mock.mux_commits==1);
 CHECK(mock.mux_orientation==2); CHECK(mock.mux_role==video); CHECK(mock.aux_enabled);
 CHECK(pd.power_role==0); CHECK(pd.data_role==1);
 CHECK(mock.mux_order<mock.aux_enable_order);
 if(video==1) {
  CHECK(!mock.hpd_output); CHECK(pd.hpd_pending); mock_advance(2); CHECK(!mock.hpd_output);
  mock_advance(1); CHECK(!mock.hpd_output); CHECK(pd.hpd_pending);
  mock_advance(1); CHECK(!pd.hpd_pending);
 }
 if(video==2) {
  vdm_tx(0xff01,16,0,1,2); CHECK(u32(last()->bytes+6)==2u); ack();
  vdm(0xff01,16,1,1,2,9u); CHECK(pd.dp==DP_ACTIVE); CHECK(!pd.tx_busy);
  CHECK(mock.mux_commits==1);
 }
}
static void ufp_enter(uint8_t video) {
 sink_contract(video,0);
 vdm(0xff00,1,0,0,1,0); vdm_tx(0xff00,1,1,0,4);
 CHECK(u32(last()->bytes+6)==0x14000000UL); CHECK(u32(last()->bytes+10)==0);
 CHECK(u32(last()->bytes+14)==0x00000100UL); ack();
 vdm(0xff00,2,0,0,1,0); vdm_tx(0xff00,2,1,0,2);
 CHECK(u32(last()->bytes+6)==0xff010000UL); ack();
 vdm(0xff01,3,0,0,1,0); vdm_tx(0xff01,3,1,0,2);
 CHECK(u32(last()->bytes+6)==(video==1?0x00001c46UL:0x001c0045UL)); ack();
 vdm(0xff01,4,0,1,1,0); vdm_tx(0xff01,4,1,1,1);
 CHECK(pd.entered); safe(); ack();
}
static void ufp_active(uint8_t video) {
 ufp_enter(video); vdm(0xff01,16,0,1,2,video==1?0x82u:1u);
 vdm_tx(0xff01,16,1,1,2); CHECK((u32(last()->bytes+6)&3)==video); ack();
 vdm(0xff01,17,0,1,2,0x404u|video); vdm_tx(0xff01,17,1,1,1);
 safe(); CHECK(mock.mux_commits==0); ack();
 CHECK(pd.dp==DP_ACTIVE); CHECK(mock.mux_pin==4); CHECK(mock.aux_enabled);
 CHECK(pd.data_role==0); CHECK(pd.power_role==0);
 CHECK(mock.mux_order<mock.aux_enable_order);
 if(video==1) {
  CHECK(!mock.hpd_output); CHECK(pd.hpd_pending); mock_advance(2); CHECK(!mock.hpd_output);
  mock_advance(1); CHECK(!mock.hpd_output); CHECK(pd.hpd_pending);
  mock_advance(1); CHECK(!pd.hpd_pending);
 }
 if(pd.tx_busy) { vdm_tx(0xff01,6,0,1,2); ack(); }
}
static void test_dfp_source(void) { dfp_active(1,0,0x82,4); CHECK(mock.hpd_output); }
static void test_dfp_sink(void) { dfp_active(2,0,1,4); CHECK(!mock.hpd_output); }
static void test_ufp_source(void) { ufp_active(1); CHECK(mock.hpd_output); }
static void test_ufp_sink(void) { ufp_active(2); CHECK(!mock.hpd_output); }
static void test_multifunction(void) { dfp_active(1,1,0x92,8); }
static void test_pin_e(void) {
 to_enter(1,0,0x00100045UL); vdm(0xff01,4,1,1,1,0); ack();
 vdm(0xff01,16,1,1,2,2); vdm_tx(0xff01,17,0,1,2);
 CHECK(u32(last()->bytes+6)==0x1006UL); safe(); ack();
 vdm(0xff01,17,1,1,1,0); CHECK(mock.mux_pin==0x10);
}
static void test_no_svid(void) {
 sink_contract(1,0); become_dfp(); vdm(0xff00,1,1,0,4,0x14000000UL); ack();
 vdm(0xff00,2,1,0,2,0x12340000UL); CHECK(pd.dp==DP_FAILED);
 CHECK(pd.error==ERR_NO_SVID); CHECK(!pd.entered); safe();
}
static void test_enter_nak(void) {
 to_enter(1,0,0x001c0045UL); vdm(0xff01,4,2,1,1,0);
 CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_NAK); CHECK(!pd.entered); safe();
}
static void test_busy_retry(void) {
 unsigned int sends;
 to_modes(1,0); sends=mock.sends; vdm(0xff01,3,3,0,1,0);
 CHECK(pd.vdm_retry); CHECK(pd.busy_count==1); CHECK(pd.dp==DP_DISC_MODE); safe();
 mock_advance(49); CHECK(mock.sends==sends); mock_advance(1);
 CHECK(mock.sends==sends+1); vdm_tx(0xff01,3,0,0,1); ack();
 vdm(0xff01,3,1,0,2,0x001c0045UL); CHECK(pd.dp==DP_ENTERING); CHECK(!pd.busy_count);
}
static void test_busy_exhaustion(void) {
 unsigned int i;
 to_modes(1,0);
 for(i=0;i<3;i++) { vdm(0xff01,3,3,0,1,0); mock_advance(50); vdm_tx(0xff01,3,0,0,1); ack(); }
 vdm(0xff01,3,3,0,1,0); CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_BUSY); safe();
}
static void test_timeout(void) {
 to_modes(1,0); mock_advance(29); CHECK(pd.dp==DP_DISC_MODE);
 mock_advance(1); CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_TIMEOUT); safe();
}
static void test_pin_mismatch(void) {
 to_modes(1,0); vdm(0xff01,3,1,0,2,0x00010045UL);
 CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_PINS); safe();
}
static void test_reject_configure(void) {
 ufp_enter(2); vdm(0xff01,16,0,1,2,1); ack();
 vdm(0xff01,17,0,1,2,0x0106UL); vdm_tx(0xff01,17,2,1,1);
 CHECK(pd.error==ERR_PINS); safe(); ack(); CHECK(mock.mux_commits==0);
}
static void test_hpd_source(void) {
 unsigned int sends;
 dfp_active(1,0,2,4); CHECK(!mock.hpd_output); sends=mock.sends;
 vdm(0xff01,6,0,1,2,0x82); CHECK(mock.hpd_output); CHECK(!mock.hpd_irqs);
 CHECK(mock.sends==sends);
 vdm(0xff01,6,0,1,2,0x182); CHECK(mock.hpd_output); CHECK(mock.hpd_irqs==1);
 vdm(0xff01,6,0,1,2,2); CHECK(!mock.hpd_output); CHECK(mock.sends==sends);
}
static void test_hpd_sink(void) {
 ufp_active(2); mock.hpd_input=1; pd_poll();
 vdm_tx(0xff01,6,0,1,2); CHECK(u32(last()->bytes+6)==0x8aUL); ack();
 mock.hpd_irq_input=1; pd_poll(); vdm_tx(0xff01,6,0,1,2);
 CHECK(u32(last()->bytes+6)==0x18aUL); ack(); CHECK(!pd.irq);
 mock.hpd_input=0; pd_poll(); vdm_tx(0xff01,6,0,1,2);
 CHECK(u32(last()->bytes+6)==0x0aUL); ack();
}
static void test_detach(void) {
 to_config(1,0,2); mock.orientation=0; pd_poll();
 CHECK(!pd.orientation); CHECK(!pd.contract); CHECK(!pd.entered); CHECK(pd.pe==PE_OFF);
 CHECK(!mock.phy_enabled); CHECK(!mock.vbus_output); safe();
}
static void test_detach_active(void) {
 ufp_active(2); mock.vbus_valid=0; pd_poll();
 CHECK(!pd.contract); CHECK(!pd.entered); CHECK(!mock.phy_enabled); safe();
}
static void test_soft_reset(void) {
 ufp_active(2); peer_id=0; inject(13,0,0); local_id=0; tx(3,0);
 CHECK(mock.mux_pin==4); CHECK(mock.aux_enabled); CHECK(!pd.contract);
 CHECK(pd.entered); /* USB PD Soft_Reset preserves modal operation and MUX. */
 ack(); CHECK(pd.pe==PE_WAIT_CAP); CHECK(pd.tx_id==1); CHECK(mock.hard_resets==0);
 { uint32_t cap=0x06019032UL; unsigned int commits=mock.mux_commits;
 inject(1,1,&cap); tx(2,1); CHECK(mock.mux_pin==4); ack();
 inject(3,0,0); inject(6,0,0); CHECK(pd.contract); CHECK(pd.dp==DP_ACTIVE);
 CHECK(pd.entered); CHECK(mock.mux_commits==commits); CHECK(!pd.tx_busy); }
}
static void test_hard_reset(void) {
 dfp_active(1,0,0x82,4); mock_event(PHY_RESET); local_id=0;
 CHECK(pd.tx_id==0); CHECK(!pd.rx_valid); CHECK(!pd.contract); CHECK(!pd.entered);
 CHECK(pd.dp==DP_IDLE); CHECK(pd.pe==PE_WAIT_CAP); safe(); CHECK(mock.hard_resets==0);
}
static void test_duplicate(void) {
 uint8_t frame[10]; unsigned int sends;
 dfp_active(1,0,0x82,4); vdm(0xff01,6,0,1,2,0x182); CHECK(mock.hpd_irqs==1);
 memcpy(frame,pd_rx,10); sends=mock.sends; mock_inject(frame,10);
 CHECK(mock.hpd_irqs==1); CHECK(mock.sends==sends); CHECK(pd.dp==DP_ACTIVE);
}
static void test_illegal_status(void) {
 dfp_active(1,0,2,4); vdm(0xff01,6,0,1,2,0x102);
 CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_FORMAT); CHECK(pd.entered); safe();
}
static void test_illegal_header(void) {
 uint8_t f[6]={0x6f,0x91,1,0x80,0,0xff}; unsigned int sends;
 sink_contract(2,0); sends=mock.sends; mock_inject(f,6);
 CHECK(pd.pe==PE_READY); CHECK(mock.sends==sends); CHECK(mock.hard_resets==0); safe();
}
static void test_illegal_roles(void) {
 uint8_t f[2]={0x43,0}; sink_contract(2,0); mock_inject(f,2);
 CHECK(mock.hard_resets==1); CHECK(pd.error==ERR_FORMAT); CHECK(!pd.contract); safe();
}
static void test_bad_length(void) {
 uint8_t f[6]={0x6f,0x21,1,0x80,0,0xff}; unsigned int sends;
 sink_contract(2,0); sends=mock.sends; mock_inject(f,6);
 CHECK(mock.sends==sends); CHECK(pd.contract); safe();
}
static void test_retry(void) {
 struct mock_frame first; unsigned int n;
 to_modes(1,0); vdm(0xff01,3,1,0,2,0x001c0045UL);
 vdm_tx(0xff01,4,0,1,1); first=*last(); n=mock.sends;
 pd_poll(); mock_advance(2); CHECK(mock.sends==n+1); CHECK(pd.tx_tries==1);
 CHECK(last()->len==first.len); CHECK(!memcmp(last()->bytes,first.bytes,first.len));
 mock_ack((uint8_t)((local_id+1)&7)); CHECK(pd.tx_busy); CHECK(pd.tx_id==local_id);
 ack(); CHECK(pd.vdm_wait==1); CHECK(pd.dp==DP_ENTERING); safe();
}
static void test_retry_exhaustion(void) {
 unsigned int i,n; struct mock_frame first;
 to_modes(1,0); vdm(0xff01,3,1,0,2,0x001c0045UL); n=mock.sends; first=*last();
 for(i=0;i<3;i++) { pd_poll(); mock_advance(2); CHECK(mock.sends==n+i+1);
 CHECK(!memcmp(last()->bytes,first.bytes,first.len)); }
 pd_poll(); mock_advance(2); CHECK(mock.hard_resets==0); CHECK(pd.error==ERR_LINK);
 CHECK(pd.pe==PE_SOFT_WAIT); CHECK(pd.tx_id==0); local_id=0;
 pd_poll(); tx(13,0); ack(); mock_advance(30);
 CHECK(mock.hard_resets==1); CHECK(pd.error==ERR_TIMEOUT);
 CHECK(!pd.contract); CHECK(!pd.entered); CHECK(pd.tx_id==0); safe();
}
static void test_exit_dfp(void) {
 dfp_active(1,0,0x82,4); pd_request_exit(); pd_poll();
 CHECK(pd.dp==DP_CONFIG_WAIT); vdm_tx(0xff01,17,0,1,2);
 CHECK(u32(last()->bytes+6)==0); safe(); CHECK(pd.entered); ack();
 vdm(0xff01,17,1,1,1,0);
 CHECK(pd.dp==DP_EXITING); vdm_tx(0xff01,5,0,1,1); safe(); ack();
 vdm(0xff01,5,1,1,1,0); CHECK(!pd.entered); CHECK(pd.dp==DP_IDLE); safe();
}
static void test_exit_ufp(void) {
 ufp_active(2); vdm(0xff01,5,0,7,1,0); vdm_tx(0xff01,5,1,7,1); safe();
 CHECK(pd.entered); ack(); CHECK(!pd.entered); CHECK(pd.dp==DP_IDLE); safe();
}
static void test_active_config_nak(void) {
 ufp_active(2); vdm(0xff01,17,0,1,2,0x0306); vdm_tx(0xff01,17,2,1,1);
 safe(); ack(); CHECK(mock.mux_commits==1);
 /* Even though mode remains entered, a safe MUX cannot advertise enabled. */
 CHECK(!(dp_local_status()&8u));
}
static void test_power_source(void) {
 uint32_t rdo=0x1300c832UL;
 boot(2,1,0); CHECK(mock.vbus_output); CHECK(mock.vbus_on_calls==1);
 pd_poll(); tx(1,1); CHECK(u32(last()->bytes+2)==0x06019032UL); ack();
 inject(2,1,&rdo); tx(3,0); CHECK(pd.pe==PE_SEND_ACCEPT); CHECK(!pd.contract); ack();
 CHECK(pd.pe==PE_SEND_READY); mock_advance(24); CHECK(!pd.tx_busy);
 mock_advance(1); tx(6,0); CHECK(!pd.contract); ack();
 CHECK(pd.contract); CHECK(pd.power_role==1); CHECK(pd.video_role==2); CHECK(pd.data_role==1);
 vdm_tx(0xff00,1,0,0,1); safe();
}
static void test_no_power(void) {
 mock_reset(); pd_init(1,0,0); mock.orientation=1; pd_poll(); mock_advance(120);
 CHECK(!pd.orientation); CHECK(!mock.phy_enabled); CHECK(!mock.vbus_output); CHECK(!mock.sends); safe();
}
static void test_source_gate(void) {
 mock_reset(); pd_init(1,1,0); mock.orientation=1; mock.vbus_valid=1;
 pd_poll(); mock_advance(120); CHECK(!pd.orientation); CHECK(!mock.vbus_output);
 CHECK(!mock.vbus_on_calls); CHECK(pd.error==ERR_POWER); safe();
}
static void test_cap_current_limit(void) {
 uint32_t cap=0x0401900aUL;
 boot(2,0,0); inject(1,1,&cap); tx(2,1); CHECK(u32(last()->bytes+2)==0x1300280aUL); safe();
}
static void test_bad_cap(void) {
 uint32_t cap=0x0402d032UL; boot(2,0,0); inject(1,1,&cap);
 CHECK(mock.hard_resets==1); CHECK(pd.error==ERR_POWER); CHECK(!pd.contract); safe();
}
static void test_role_config(void) {
 mock_reset(); pd_init(1,0,0); CHECK(pd_set_video_role(2)); CHECK(pd.video_role==2);
 CHECK(!pd_set_video_role(0)); sink_contract(2,0); CHECK(!pd_set_video_role(1));
}
static void test_tick_wrap(void) {
 to_modes(1,0); mock.tick=65520u; /* Restart a BUSY timer across wraparound. */
 vdm(0xff01,3,3,0,1,0); CHECK(pd.vdm_deadline==34u);
 mock_advance(49); CHECK(!pd.tx_busy); mock_advance(1); vdm_tx(0xff01,3,0,0,1); ack();
}
static void test_wrong_response(void) {
 unsigned int sends; to_modes(1,0); sends=mock.sends;
 vdm(0x1234,3,1,0,2,0x001c0045UL); CHECK(pd.dp==DP_DISC_MODE); CHECK(mock.sends==sends);
 vdm(0xff01,3,1,1,2,0x001c0045UL); CHECK(pd.dp==DP_DISC_MODE); CHECK(mock.sends==sends);
 mock_advance(30); CHECK(pd.error==ERR_TIMEOUT); safe();
}
static void test_attention_usb(void) {
 dfp_active(1,0,0x82,4); vdm(0xff01,6,0,1,2,0xa2);
 vdm_tx(0xff01,17,0,1,2); CHECK(u32(last()->bytes+6)==0); safe();
 CHECK(pd.dp==DP_CONFIG_WAIT); CHECK(pd.entered); ack();
 vdm(0xff01,17,1,1,1,0); CHECK(pd.dp==DP_IDLE); CHECK(pd.entered); safe();
 vdm(0xff01,6,0,1,2,0x82); CHECK(pd.dp==DP_IDLE); safe();
}
static void test_attention_exit(void) {
 dfp_active(1,0,0x82,4); vdm(0xff01,6,0,1,2,0xc2);
 CHECK(pd.dp==DP_CONFIG_WAIT); vdm_tx(0xff01,17,0,1,2);
 CHECK(u32(last()->bytes+6)==0); safe(); CHECK(pd.entered); ack();
 vdm(0xff01,17,1,1,1,0);
 CHECK(pd.dp==DP_EXITING); vdm_tx(0xff01,5,0,1,1); safe(); ack();
 vdm(0xff01,5,1,1,1,0); CHECK(pd.dp==DP_IDLE); CHECK(!pd.entered); safe();
 vdm(0xff01,6,0,1,2,0x82); safe();
}
static void test_dr_swap_active(void) {
 dfp_active(1,0,0x82,4); CHECK(!pd_request_data_swap()); inject(9,0,0);
 CHECK(mock.hard_resets==1); CHECK(!pd.entered); CHECK(!pd.contract);
 CHECK(pd.data_role==0); CHECK(pd.video_role==1); safe();
}
static void test_hard_reset_vbus(void) {
 ufp_active(2); mock.vbus_valid=0; mock_event(PHY_RESET);
 CHECK(pd.reset_wait); CHECK(pd.orientation==2); CHECK(mock.phy_enabled);
 CHECK(!pd.entered); safe(); mock_advance(100); CHECK(pd.orientation==2);
 mock.vbus_valid=1; pd_poll(); CHECK(!pd.reset_wait); CHECK(pd.orientation==2);
 { uint32_t cap=0x06019032UL; local_id=0; peer_id=0;
 inject(1,1,&cap); tx(2,1); ack(); inject(3,0,0); inject(6,0,0);
 CHECK(pd.contract); CHECK(pd.dp==DP_IDLE); safe(); }
}
static void test_soft_reset_dfp(void) {
 uint32_t cap=0x06019032UL; unsigned int sends;
 dfp_active(1,0,0x82,4); peer_id=0; inject(13,0,0); local_id=0;
 tx(3,0); CHECK(pd.entered); CHECK(pd.dp==DP_ACTIVE); CHECK(mock.mux_pin==4);
 ack(); inject(1,1,&cap); tx(2,1); ack(); inject(3,0,0); sends=mock.sends;
 inject(6,0,0); CHECK(pd.contract); CHECK(pd.dp==DP_ACTIVE); CHECK(pd.entered);
 CHECK(pd.data_role==1); CHECK(mock.sends==sends); CHECK(mock.mux_commits==1);
}
static void test_ufp_usb_config(void) {
 ufp_active(2); vdm(0xff01,17,0,1,2,0); vdm_tx(0xff01,17,1,1,1);
 safe(); ack(); CHECK(pd.dp==DP_IDLE); CHECK(pd.entered); CHECK(!(dp_local_status()&8u));
}
static void test_disconnect_attention(void) {
 dfp_active(1,0,0x82,4); vdm(0xff01,6,0,1,2,0x80);
 safe(); CHECK(pd.dp!=DP_ACTIVE); CHECK(pd.entered);
}
static void test_config_timeout(void) {
 to_config(1,0,0x82); CHECK(pd.entered); mock_advance(30);
 CHECK(pd.error==ERR_TIMEOUT); CHECK(pd.dp==DP_FAILED); CHECK(pd.entered); safe();
 CHECK(!pd_request_data_swap()); pd_request_exit(); pd_poll();
 vdm_tx(0xff01,5,0,1,1); ack(); vdm(0xff01,5,1,1,1,0); CHECK(!pd.entered); safe();
}
static void test_malformed_busy(void) {
 to_modes(1,0); vdm(0xff01,3,3,0,2,0); CHECK(pd.error==ERR_FORMAT);
 CHECK(pd.dp==DP_FAILED); safe();
}
static void test_svid_low_half(void) {
 sink_contract(1,0); become_dfp(); vdm(0xff00,1,1,0,4,0x14000000UL); ack();
 vdm(0xff00,2,1,0,2,0x1234ff01UL); CHECK(pd.dp==DP_DISC_MODE); vdm_tx(0xff01,3,0,0,1);
}
static void test_mode_object_two(void) {
 uint32_t v[3]={0xff018043UL,0x00010045UL,0x001c0045UL};
 to_modes(1,0); inject(15,3,v); CHECK(pd.mode_obj==2); CHECK(pd.dp==DP_ENTERING);
 vdm_tx(0xff01,4,0,2,1); safe();
}
static void test_plug_mode(void) {
 to_enter(1,0,0x00001c05UL); CHECK(pd.partner_mode==0x00001c05UL);
}
static void test_no_vdm_before_contract(void) {
 unsigned int sends;
 boot(2,0,0); sends=mock.sends; vdm(0xff00,1,0,0,1,0);
 CHECK(mock.sends==sends); CHECK(!pd.contract); safe();
}
static void test_generated_malformed_frames(void) {
 uint32_t random=0x543dfeedUL; unsigned int i,j;
 /* Each case starts without a contract, so even valid random VDMs cannot map DP. */
 for(i=0;i<1024;i++) {
  uint8_t f[30],len;
  boot(2,0,0);
  random=random*1664525UL+1013904223UL; len=(uint8_t)(2+random%29);
  for(j=0;j<len;j++) { random=random*1664525UL+1013904223UL; f[j]=(uint8_t)(random>>24); }
  mock_inject(f,len); CHECK(mock.mux_commits==0); safe();
  CHECK(!mock.vbus_output); CHECK(!mock.rx_owned);
 }
}
static void test_dfp_sink_status_hpd(void) {
 unsigned int commits;
 dfp_active(2,0,1,4); commits=mock.mux_commits;
 mock.hpd_input=1; pd_poll(); vdm_tx(0xff01,16,0,1,2);
 CHECK(u32(last()->bytes+6)==0x82UL); ack(); vdm(0xff01,16,1,1,2,9);
 CHECK(pd.dp==DP_ACTIVE); CHECK(!pd.tx_busy); CHECK(mock.mux_commits==commits);
 mock.hpd_irq_input=1; pd_poll(); vdm_tx(0xff01,16,0,1,2);
 CHECK(u32(last()->bytes+6)==0x182UL); ack(); vdm(0xff01,16,1,1,2,9);
 CHECK(pd.dp==DP_ACTIVE); CHECK(!pd.tx_busy); CHECK(mock.mux_commits==commits);
 mock.hpd_input=0; pd_poll(); vdm_tx(0xff01,16,0,1,2);
 CHECK(u32(last()->bytes+6)==2u); ack(); vdm(0xff01,16,1,1,2,9);
 CHECK(!pd.tx_busy); CHECK(mock.mux_commits==commits);
}
static void test_ufp_source_status_hpd(void) {
 unsigned int commits;
 ufp_active(1); commits=mock.mux_commits;
 vdm(0xff01,16,0,1,2,0x182); vdm_tx(0xff01,16,1,1,2);
 CHECK(u32(last()->bytes+6)==9u); CHECK(mock.hpd_output); CHECK(mock.hpd_irqs==1);
 CHECK(!(pd.partner_status&0x100u)); ack();
 vdm(0xff01,16,0,1,2,2); vdm_tx(0xff01,16,1,1,2);
 CHECK(!mock.hpd_output); CHECK(mock.hpd_irqs==1); ack();
 CHECK(mock.mux_commits==commits); CHECK(pd.dp==DP_ACTIVE);
}
static void test_ufp_source_exit_request(void) {
 ufp_active(1); pd_request_exit(); pd_poll();
 vdm_tx(0xff01,6,0,1,2); CHECK(u32(last()->bytes+6)==0x49u); ack();
 CHECK(pd.entered); CHECK(pd.dp==DP_ACTIVE);
 vdm(0xff01,5,0,1,1,0); vdm_tx(0xff01,5,1,1,1); safe(); ack();
 CHECK(!pd.entered); CHECK(pd.dp==DP_IDLE);
}
static void test_ufp_ignores_dfp_attention(void) {
 unsigned int sends;
 ufp_active(1); sends=mock.sends;
 vdm(0xff01,6,0,1,2,0x182); CHECK(mock.hpd_irqs==0); CHECK(mock.sends==sends);
 CHECK(pd.dp==DP_ACTIVE);
}
static void test_dfp_status_reserved_zero(void) {
 to_config(1,1,0x92); /* DFP preference must not leak into bits 2..6 on Status. */
 { unsigned int i; unsigned int found=0;
 for(i=0;i<mock.sends;i++) {
  const struct mock_frame *f=&mock.frames[i];
  if(f->len==10 && (u32(f->bytes+2)&0x1fu)==16u) {
   CHECK(u32(f->bytes+6)==1u); found++;
  }
 }
 CHECK(found==1); }
}
static void test_hpd_guard_irq_once(void) {
 to_config(1,0,0x82); vdm(0xff01,17,1,1,1,0);
 CHECK(pd.dp==DP_ACTIVE); CHECK(mock.mux_pin==4); CHECK(mock.aux_enabled);
 CHECK(mock.mux_order<mock.aux_enable_order); CHECK(!mock.hpd_output);
 CHECK(pd.hpd_pending); mock_advance(2); CHECK(!mock.hpd_output);
 vdm(0xff01,6,0,1,2,0x182); CHECK(!mock.hpd_output); CHECK(!mock.hpd_irqs);
 mock_advance(1); CHECK(!mock.hpd_output); CHECK(!mock.hpd_irqs);
 mock_advance(1); CHECK(mock.hpd_output); CHECK(mock.hpd_irqs==1);
 CHECK(!pd.hpd_pending); CHECK(!(pd.partner_status&0x100u));
 mock_advance(10); CHECK(mock.hpd_irqs==1);
}
static void test_hpd_guard_detach(void) {
 to_config(1,0,0x182); vdm(0xff01,17,1,1,1,0); CHECK(pd.hpd_pending);
 mock.orientation=0; pd_poll(); CHECK(!pd.hpd_pending); safe();
 mock_advance(4); safe(); CHECK(!mock.hpd_irqs);
}
static void test_hpd_guard_latest_level(void) {
 to_config(1,0,0x82); vdm(0xff01,17,1,1,1,0); CHECK(pd.hpd_pending);
 vdm(0xff01,6,0,1,2,2); mock_advance(4);
 CHECK(!mock.hpd_output); CHECK(!pd.hpd_pending); CHECK(mock.mux_pin==4);
}
static void test_hard_reset_delayed_vbus_cycle(void) {
 ufp_active(2); mock_event(PHY_RESET); CHECK(pd.reset_wait==1);
 mock_advance(20); CHECK(pd.reset_wait==1); CHECK(pd.orientation==2);
 mock.vbus_valid=0; mock.orientation=0; pd_poll(); CHECK(pd.reset_wait==2);
 CHECK(pd.orientation==2); CHECK(mock.phy_enabled); safe();
 mock_advance(100); CHECK(pd.reset_wait==2); CHECK(pd.orientation==2);
 mock.vbus_valid=1; mock.orientation=2; pd_poll();
 CHECK(!pd.reset_wait); CHECK(pd.orientation==2); CHECK(mock.phy_enabled); safe();
}
static void test_hard_reset_hold_timeout(void) {
 ufp_active(2); mock_event(PHY_RESET); mock_advance(10);
 mock.vbus_valid=0; mock.orientation=0; pd_poll(); CHECK(pd.reset_wait==2);
 mock_advance(990); CHECK(!pd.reset_wait); pd_poll();
 CHECK(!pd.orientation); CHECK(!mock.phy_enabled); safe();
}
static void test_ufp_status_disconnected(void) {
 ufp_active(1); vdm(0xff01,16,0,1,2,0x80);
 vdm_tx(0xff01,16,1,1,2); CHECK(u32(last()->bytes+6)==1u);
 safe(); CHECK(pd.entered); CHECK(pd.dp!=DP_ACTIVE); ack();
 vdm(0xff01,16,0,1,2,0x82); safe();
}
static void test_pending_irq_preserved(void) {
 to_config(1,0,0x82); vdm(0xff01,17,1,1,1,0); CHECK(pd.hpd_pending);
 vdm(0xff01,6,0,1,2,0x182); CHECK(pd.partner_status&0x100u);
 vdm(0xff01,6,0,1,2,0x82); CHECK(pd.partner_status&0x100u);
 mock_advance(4); CHECK(mock.hpd_output); CHECK(mock.hpd_irqs==1);
 CHECK(!(pd.partner_status&0x100u)); mock_advance(10); CHECK(mock.hpd_irqs==1);
}
static void test_pending_irq_cleared_by_low(void) {
 to_config(1,0,0x82); vdm(0xff01,17,1,1,1,0); CHECK(pd.hpd_pending);
 vdm(0xff01,6,0,1,2,0x182); CHECK(pd.partner_status&0x100u);
 vdm(0xff01,6,0,1,2,2); CHECK(!(pd.partner_status&0x100u));
 vdm(0xff01,6,0,1,2,0x82); mock_advance(4);
 CHECK(mock.hpd_output); CHECK(!mock.hpd_irqs);
}
static void test_phy_tx_end_missing(void) {
 uint32_t cap=0x06019032UL;
 boot(2,0,0); mock.suppress_tx_end=1; inject(1,1,&cap); tx(2,1);
 mock_advance(4); CHECK(pd.tx_busy); CHECK(!mock.hard_resets);
 mock_advance(1); CHECK(mock.hard_resets==1); CHECK(pd.error==ERR_LINK);
 CHECK(!pd.contract); CHECK(!pd.tx_busy); safe();
}
static void test_status_busy_preserves_irq(void) {
 dfp_active(2,0,1,4); mock.hpd_input=1; mock.hpd_irq_input=1; pd_poll();
 vdm_tx(0xff01,16,0,1,2); CHECK(u32(last()->bytes+6)==0x182UL); ack();
 vdm(0xff01,16,3,1,1,0); CHECK(pd.vdm_retry); CHECK(pd.dp==DP_ACTIVE);
 mock_advance(50); vdm_tx(0xff01,16,0,1,2);
 CHECK(u32(last()->bytes+6)==0x182UL); ack();
 vdm(0xff01,16,1,1,2,9); CHECK(!pd.pending_cmd); CHECK(pd.dp==DP_ACTIVE);
 CHECK(mock.mux_commits==1);
}
static void test_enter_timeout_boundary(void) {
 to_enter(1,0,0x001c0045UL); CHECK(pd.dp==DP_ENTERING); CHECK(pd.vdm_wait==1);
 mock_advance(30); CHECK(pd.dp==DP_ENTERING); CHECK(pd.pending_cmd==4);
 CHECK(pd.vdm_wait==1); CHECK(pd.error==ERR_NONE); safe();
 mock_advance(14); CHECK(pd.dp==DP_ENTERING); CHECK(pd.pending_cmd==4);
 CHECK(pd.vdm_wait==1); CHECK(pd.error==ERR_NONE); safe();
 mock_advance(1); CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_TIMEOUT);
 CHECK(!pd.entered); CHECK(!pd.pending_cmd); safe();
}
static void test_exit_timeout_boundary(void) {
 dfp_active(1,0,0x82,4); pd_request_exit(); pd_poll();
 vdm_tx(0xff01,17,0,1,2); CHECK(u32(last()->bytes+6)==0); safe(); ack();
 vdm(0xff01,17,1,1,1,0); vdm_tx(0xff01,5,0,1,1); ack();
 CHECK(pd.dp==DP_EXITING); CHECK(pd.entered); CHECK(pd.vdm_wait==1);
 mock_advance(30); CHECK(pd.dp==DP_EXITING); CHECK(pd.pending_cmd==5);
 CHECK(pd.entered); CHECK(pd.error==ERR_NONE); safe();
 mock_advance(14); CHECK(pd.dp==DP_EXITING); CHECK(pd.pending_cmd==5);
 CHECK(pd.entered); CHECK(pd.error==ERR_NONE); safe();
 mock_advance(1); CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_TIMEOUT);
 CHECK(pd.entered); CHECK(!pd.pending_cmd); CHECK(!pd_request_data_swap()); safe();
 mock_event(PHY_RESET); CHECK(!pd.entered); CHECK(pd.dp==DP_IDLE); safe();
}
static void test_mux_delayed(void) {
 to_config(1,0,0x82); mock.mux_pending=1;
 vdm(0xff01,17,1,1,1,0); CHECK(pd.dp==DP_MUX_WAIT);
 CHECK(!mock.hpd_output); CHECK(pd.hpd_pending);
 mock_advance(20); CHECK(pd.dp==DP_MUX_WAIT); CHECK(!mock.hpd_output);
 vdm(0xff01,6,0,1,2,0x182); CHECK(pd.partner_status&0x100u);
 vdm(0xff01,6,0,1,2,0x82); CHECK(pd.partner_status&0x100u);
 mock.mux_pending=0; pd_poll(); CHECK(pd.dp==DP_ACTIVE);
 CHECK(pd.hpd_pending); mock_advance(3); CHECK(!mock.hpd_output);
 mock_advance(1); CHECK(mock.hpd_output); CHECK(mock.hpd_irqs==1);
}
static void test_mux_fault(void) {
 to_config(1,0,0x82); mock.mux_pending=1;
 vdm(0xff01,17,1,1,1,0); CHECK(pd.dp==DP_MUX_WAIT);
 mock.mux_fault=1; pd_poll(); CHECK(pd.dp==DP_FAILED);
 CHECK(pd.error==ERR_HARDWARE); safe();
}
static void test_mux_timeout(void) {
 to_config(1,0,0x82); mock.mux_pending=1;
 vdm(0xff01,17,1,1,1,0); CHECK(pd.dp==DP_MUX_WAIT);
 mock_advance(99); CHECK(pd.dp==DP_MUX_WAIT); CHECK(!mock.hpd_output);
 mock_advance(1); CHECK(pd.dp==DP_FAILED); CHECK(pd.error==ERR_HARDWARE); safe();
}
static void test_mux_cancel(void) {
 to_config(1,0,0x82); mock.mux_pending=1;
 vdm(0xff01,17,1,1,1,0); CHECK(pd.dp==DP_MUX_WAIT);
 mock.orientation=0; pd_poll(); CHECK(!pd.entered); safe();
 mock.mux_pending=0; mock_advance(30); CHECK(pd.dp==DP_IDLE); safe();
}
static void run(const char *name,void (*test)(void)) {
 test_name=name; tests_run++;
 if(!setjmp(abort_test)) { test(); printf("PASS %s\n",name); }
 else tests_failed++;
}
int main(void) {
#define RUN(name) run(#name,test_##name)
 RUN(dfp_source); RUN(dfp_sink); RUN(ufp_source); RUN(ufp_sink); RUN(multifunction);
 RUN(pin_e); RUN(no_svid); RUN(enter_nak); RUN(busy_retry); RUN(busy_exhaustion);
 RUN(timeout); RUN(pin_mismatch); RUN(reject_configure); RUN(hpd_source); RUN(hpd_sink);
 RUN(detach); RUN(detach_active); RUN(soft_reset); RUN(hard_reset); RUN(duplicate);
 RUN(illegal_status); RUN(illegal_header); RUN(illegal_roles); RUN(bad_length);
 RUN(retry); RUN(retry_exhaustion); RUN(exit_dfp); RUN(exit_ufp); RUN(active_config_nak);
 RUN(power_source); RUN(no_power); RUN(source_gate); RUN(cap_current_limit); RUN(bad_cap);
 RUN(role_config); RUN(tick_wrap); RUN(wrong_response);
 RUN(attention_usb); RUN(attention_exit); RUN(dr_swap_active); RUN(hard_reset_vbus);
 RUN(soft_reset_dfp); RUN(ufp_usb_config); RUN(disconnect_attention); RUN(config_timeout);
 RUN(malformed_busy); RUN(svid_low_half); RUN(mode_object_two); RUN(plug_mode);
 RUN(no_vdm_before_contract); RUN(generated_malformed_frames);
 RUN(dfp_sink_status_hpd); RUN(ufp_source_status_hpd); RUN(ufp_source_exit_request);
 RUN(ufp_ignores_dfp_attention); RUN(dfp_status_reserved_zero);
 RUN(hpd_guard_irq_once); RUN(hpd_guard_detach); RUN(hpd_guard_latest_level);
 RUN(hard_reset_delayed_vbus_cycle); RUN(hard_reset_hold_timeout);
 RUN(ufp_status_disconnected); RUN(pending_irq_preserved); RUN(pending_irq_cleared_by_low);
 RUN(phy_tx_end_missing); RUN(status_busy_preserves_irq);
 RUN(enter_timeout_boundary); RUN(exit_timeout_boundary);
 RUN(mux_delayed); RUN(mux_fault); RUN(mux_timeout); RUN(mux_cancel);
 printf("%u tests, %u failures, %u assertions (PC Mock HAL; no hardware validation)\n",tests_run,tests_failed,assertions);
 return tests_failed?1:0;
}
