/* Functional SFR model: manually inject captured edges/timer expiry.
 * No analogue pin, bit alias, timing latency or instruction simulation. */
#include <assert.h>
#include <stdio.h>
#define CH543_PHY_TEST 1
#define ISR(number,bank)
#include "../hal/ch543_board2.c"
pd_context pd;
void ch9445_init(void) { }
void ch9445_poll(void) { }
static void edge(uint8_t level,uint16_t at) {
 BOARD_HPD=level; T2CAP1L=(uint8_t)at; T2CAP1H=(uint8_t)(at>>8);
 TF2=1; timer2_isr(); assert(!TF2);
}
static void pulse(uint16_t width,uint8_t expect) {
 pd.video_role=2; hpd_timer=0; hpd_level=1; hpd_irq=0; TF1=0;
 edge(0,65000u); assert(hpd_timer==1 && TR1); assert(dp_hpd_get()==1);
 edge(1,(uint16_t)(65000u+width)); assert(!TR1);
 assert(dp_hpd_get()==1); assert(dp_hpd_irq_get()==expect);
 assert(dp_hpd_irq_get()==0);
}
int main(void) {
 unsigned long i,total=0; uint16_t count=0;
 board_init(); assert(P3_DIR_PU==0x10 && (P3&0x10));
 assert(P1_MOD_OC==0x20 && P3_MOD_OC==0x04);
 assert(!EX0 && PIN_FUNC==1 && T2MOD==5 && T2CON==1);
 assert(!board_source_allowed()); P3|=0x20; assert(!board_vbus_valid());
 board_vbus_set(1); assert(BOARD_PWR_DISABLE);
 for(i=0;i<140000;i++) {
  count=(uint16_t)(count+2000u); total+=2000u;
  TH2=(uint8_t)(count>>8); TL2=(uint8_t)count;
  assert(system_get_tick()==(uint16_t)(total/4000u));
 }
 pulse(1999,0); pulse(2000,1); pulse(2400,1); pulse(4000,1); pulse(4001,0);
 pd.video_role=2; hpd_level=1; edge(0,1000); TF1=1; timer1_isr();
 assert(!dp_hpd_get()); edge(1,10000); assert(!dp_hpd_irq_get());
 pd.video_role=1; dp_hpd_set(0,0); assert(P1_DIR_PU&0x10); assert(!BOARD_HPD);
 dp_hpd_set(1,0); assert(BOARD_HPD);
 dp_hpd_set(1,1); assert(!BOARD_HPD && TR1 && TH1==0xf6 && TL1==0xa0);
 timer1_isr(); assert(BOARD_HPD && !TR1);
 dp_hpd_set(0,0); dp_hpd_set(1,1); assert(BOARD_HPD && hpd_timer==3);
 timer1_isr(); assert(!BOARD_HPD && hpd_timer==2);
 pd.video_role=2; dp_hpd_set(0,0); assert(!(P1_DIR_PU&0x10) && !TR1);
 puts("Board2 HAL: GPIO safety, disabled VBUS guard, 140000 clock samples/wrap, CAP1 HPD boundaries, IRQ output and role direction passed (model)");
 return 0;
}
