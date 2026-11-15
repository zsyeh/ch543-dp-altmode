#ifdef BOARD2_DIAGNOSTIC
#include "board2_diag.h"
#else
#include "pd.h"
#include "ch543_isr.h"
#endif
#include "ch543_regs.h"
#if defined(__SDCC)
/* Called by SDCC before C RAM initialization. The reset clock is 12 MHz and
 * on-chip xRAM is already accessible. Normalize DPTR selection/autoincrement
 * before the runtime's MOVX clear loop; no RAM globals may be used here.
 * XBUS_AUX=DPS[0]/DPTR_AUTO_INC[2], CH543DS1 chapter 9 and official CH543.H.
 * Return zero to retain normal C data initialization. */
/* SDCC 4.3 renamed the C hook: https://sdcc.sourceforge.net/previous.php
 * The installed 4.5 crtstart.asm calls ___sdcc_external_startup (asm symbol). */
#if __SDCC_VERSION_MAJOR > 4 || (__SDCC_VERSION_MAJOR == 4 && __SDCC_VERSION_MINOR >= 3)
unsigned char __sdcc_external_startup(void)
#else
unsigned char _sdcc_external_startup(void)
#endif
{
    IE = 0; /* includes EA and every legacy interrupt enable */
    IE_EX = 0;
    XBUS_AUX = 0;
    return 0;
}
#endif
/* Both images use PD Sink power. Video Source is independent of power role.
 * The initial USB UFP responds to the partner's VDMs. A partner-initiated
 * DR_SWAP can make this port DFP; an integrating application may explicitly
 * call pd_request_data_swap(). The reference main does not force a swap. */
void main(void)
{
#ifdef BOARD2_DIAGNOSTIC
    board2_diag_init();
    for (;;) board2_diag_poll();
#else
    hal_init();
    pd_init(DEFAULT_DP_ROLE, 0, PREFER_USB3);
    for (;;) pd_poll();
#endif
}
