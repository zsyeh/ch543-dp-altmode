#ifndef PD_H
#define PD_H
#include "hal.h"
#include "wire.h"
#define PE_OFF 0u
#define PE_WAIT_CAP 1u
#define PE_WAIT_ACCEPT 2u
#define PE_WAIT_READY 3u
#define PE_READY 4u
#define PE_SEND_ACCEPT 5u
#define PE_SEND_READY 6u
#define PE_SOURCE_WAIT 7u
#define PE_SOFT_WAIT 8u
#define DP_IDLE 0u
#define DP_DISC_ID 1u
#define DP_DISC_SVID 2u
#define DP_DISC_MODE 3u
#define DP_ENTERING 4u
#define DP_STATUS_WAIT 5u
#define DP_CONFIG_WAIT 6u
#define DP_ACTIVE 7u
#define DP_EXITING 8u
#define DP_FAILED 9u
#define DP_MUX_WAIT 10u
#define ERR_NONE 0u
#define ERR_LINK 1u
#define ERR_TIMEOUT 2u
#define ERR_NAK 3u
#define ERR_PINS 4u
#define ERR_FORMAT 5u
#define ERR_NO_SVID 6u
#define ERR_BUSY 7u
#define ERR_POWER 8u
#define ERR_HARDWARE 9u
typedef struct {
 uint16_t link_deadline, pe_deadline, vdm_deadline, cc_since;
 uint32_t partner_mode;
 uint16_t partner_status, hpd_deadline, status_irq_sent;
 uint8_t power_role, data_role, video_role, orientation, contract;
 uint8_t pe, dp, error, tx_id, rx_id, rx_valid;
 uint8_t tx_busy, tx_len, tx_tries, tx_wait;
 uint8_t job, job_type, job_obj, job_count, job_cmd, job_action;
 uint16_t job_svid;
 uint32_t job_vdo;
 uint8_t pending_cmd, mode_obj, pin, entered;
 uint8_t busy_count, vdm_wait, vdm_retry, svid_pages;
 uint8_t hpd, irq, attention, prefer_usb3, cc_candidate;
 uint8_t after_tx, dr_swap, exit_request, rx_soft, reset_wait, usb_request, hpd_pending;
} pd_context;
extern pd_context IDATA pd;
void pd_init(uint8_t video_role, uint8_t power_role, uint8_t prefer_usb3);
void pd_poll(void);
uint8_t pd_set_video_role(uint8_t role); /* only detached; attached returns 0 */
void pd_request_exit(void);
uint8_t pd_request_data_swap(void); /* no swap while a mode is entered */
uint32_t dp_local_mode(void);
uint16_t dp_local_status(void);
#endif
