#ifndef CONFIG_H
#define CONFIG_H
/* Product owners must replace zero laboratory identity with their assigned IDs. */
#define PRODUCT_VID 0u
#define PRODUCT_PID 0u
#define PRODUCT_BCD 0x0100u
#ifndef DEFAULT_DP_ROLE
#define DEFAULT_DP_ROLE 1 /* 1 = DFP_D video source, 2 = UFP_D video sink */
#endif
#ifndef PREFER_USB3
#define PREFER_USB3 0
#endif
#define LOCAL_PINS 0x1cu /* C, D, E: routed by the board */
#define PD_CURRENT_10MA 50u /* conservative 5 V / 500 mA */
#define PD_MAX_FRAME 30u
#define PD_DMA_SIZE 128u
#define PD_ACK_TIMEOUT_MS 2u
#define PD_TX_RETRIES 3u /* PD2: three retries after initial send */
#define VDM_TIMEOUT_MS 30u
#define VDM_MODE_TIMEOUT_MS 45u /* Enter/Exit have a separate mode timer */
#define VDM_BUSY_DELAY_MS 50u
#define VDM_BUSY_RETRIES 3u
#define TYPEC_DEBOUNCE_MS 120u
/* Reference board intentionally requires a real VBUS power-good input. */
#endif
