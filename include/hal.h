#ifndef HAL_H
#define HAL_H
#include "config.h"
#include "platform.h"
#define PHY_TX_END 1u
#define PHY_RX 2u
#define PHY_RESET 4u
#define PHY_ACK 8u
#define PHY_FAULT 16u
#define MUX_SAFE 0u
/* All functions are nonblocking. Target ISR handles only PHY/CRC/GoodCRC. */
extern uint8_t XDATA pd_rx[PD_MAX_FRAME];
extern uint8_t XDATA pd_tx[PD_MAX_FRAME];
extern volatile uint8_t phy_rx_len, phy_ack_id;
void hal_init(void);
void pd_phy_enable(uint8_t orientation);
void pd_phy_disable(void);
uint8_t pd_phy_send(uint8_t len);
void pd_phy_hard_reset(void);
uint8_t pd_phy_events(void);
void pd_phy_rx_release(void);
uint8_t typec_get_orientation(void); /* 0 absent, 1 CC1, 2 CC2 */
uint8_t board_vbus_valid(void); /* externally measured 5V, never inferred from CC */
uint16_t system_get_tick(void); /* atomic, 1ms wrapping */
void dp_mux_set(uint8_t pin, uint8_t orientation, uint8_t video_role);
void dp_aux_set(uint8_t enable, uint8_t orientation);
uint8_t dp_hpd_get(void); /* local sink HPD level */
uint8_t dp_hpd_irq_get(void); /* consume a latched sink IRQ event */
void dp_hpd_set(uint8_t level, uint8_t irq); /* output to local DP source */
/* Power Source is optional: the shipped board never sources VBUS. */
uint8_t board_source_allowed(void);
void board_vbus_set(uint8_t enable);
#endif
