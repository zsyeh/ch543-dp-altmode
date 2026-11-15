/* PC test double: no electrical CC/PD timing or silicon emulation. */
#include <assert.h>
#include <string.h>
#include "mock.h"
struct mock_state mock;
volatile uint8_t phy_rx_len, phy_ack_id;
void mock_reset(void) { memset(&mock,0,sizeof(mock)); phy_rx_len=0; phy_ack_id=0; }
void mock_advance(uint16_t ms) { mock.tick=(uint16_t)(mock.tick+ms); pd_poll(); }
void mock_inject(const uint8_t *data, uint8_t len) {
 assert(len<=PD_MAX_FRAME); assert(!mock.rx_owned);
 memcpy(pd_rx,data,len); phy_rx_len=len; mock.rx_owned=1; mock.events|=PHY_RX;
 pd_poll();
}
void mock_ack(uint8_t id) { phy_ack_id=id; mock.events|=PHY_ACK; pd_poll(); }
void mock_event(uint8_t event) { mock.events|=event; pd_poll(); }
void hal_init(void) { mock_reset(); }
#ifndef MOCK_CH9445
void hal_poll(void) { }
uint8_t dp_mux_status(void) { return mock.mux_fault?MUX_FAULT:(mock.mux_pending?MUX_PENDING:MUX_READY); }
#else
#include "ch9445.h"
void hal_poll(void) { ch9445_poll(); }
#endif
void pd_phy_enable(uint8_t orientation) {
 mock.phy_enabled=1; mock.phy_orientation=orientation;
}
void pd_phy_disable(void) { mock.phy_enabled=0; mock.events=0; mock.rx_owned=0; }
uint8_t pd_phy_send(uint8_t len) {
 struct mock_frame *f;
 if(mock.send_blocked || !mock.phy_enabled) return 0;
 assert(len<=PD_MAX_FRAME); assert(mock.sends<MOCK_FRAMES);
 f=&mock.frames[mock.sends++]; f->len=len; f->tick=mock.tick;
 memcpy(f->bytes,pd_tx,len);
 if(!mock.suppress_tx_end) mock.events|=PHY_TX_END;
 return 1;
}
void pd_phy_hard_reset(void) { mock.hard_resets++; mock.events=0; mock.rx_owned=0; }
uint8_t pd_phy_events(void) { uint8_t e=mock.events; mock.events=0; return e; }
void pd_phy_rx_release(void) { mock.rx_owned=0; mock.rx_releases++; }
uint8_t typec_get_orientation(void) { return mock.orientation; }
uint8_t board_vbus_valid(void) { return mock.vbus_valid; }
uint16_t system_get_tick(void) { return mock.tick; }
#ifndef MOCK_CH9445
void dp_mux_set(uint8_t pin,uint8_t orientation,uint8_t role) {
 mock.mux_pin=pin; mock.mux_orientation=orientation; mock.mux_role=role;
 mock.mux_order=++mock.hw_order;
 if(pin) mock.mux_commits++; else mock.mux_safe_calls++;
}
void dp_aux_set(uint8_t enable,uint8_t orientation) {
 mock.aux_enabled=enable; mock.aux_orientation=orientation;
 if(enable) mock.aux_enable_order=++mock.hw_order;
}
#endif
uint8_t dp_hpd_get(void) { return mock.hpd_input; }
uint8_t dp_hpd_irq_get(void) { uint8_t irq=mock.hpd_irq_input; mock.hpd_irq_input=0; return irq; }
void dp_hpd_set(uint8_t level,uint8_t irq) {
 mock.hpd_output=level; mock.hpd_irq_output=irq;
 if(irq) mock.hpd_irqs++;
}
uint8_t board_source_allowed(void) { return mock.source_allowed; }
void board_vbus_set(uint8_t enable) {
 mock.vbus_output=enable; if(enable) mock.vbus_on_calls++;
}
