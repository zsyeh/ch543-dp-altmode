#ifndef MOCK_H
#define MOCK_H
#include "pd.h"
#define MOCK_FRAMES 512u
struct mock_frame { uint8_t len; uint8_t bytes[PD_MAX_FRAME]; uint16_t tick; };
struct mock_state {
 uint16_t tick;
 uint8_t orientation, vbus_valid, source_allowed, vbus_output;
 uint8_t phy_enabled, phy_orientation, events, rx_owned, send_blocked, suppress_tx_end;
 uint8_t mux_pin, mux_orientation, mux_role, aux_enabled, aux_orientation;
 uint8_t mux_pending, mux_fault;
 uint8_t hpd_input, hpd_irq_input, hpd_output, hpd_irq_output;
 unsigned int sends, rx_releases, hard_resets, mux_commits, mux_safe_calls;
 unsigned int hpd_irqs, vbus_on_calls;
 unsigned int hw_order, mux_order, aux_enable_order;
 struct mock_frame frames[MOCK_FRAMES];
};
extern struct mock_state mock;
void mock_reset(void);
void mock_advance(uint16_t ms);
void mock_inject(const uint8_t *data, uint8_t len);
void mock_ack(uint8_t id);
void mock_event(uint8_t event);
#endif
