/* EPRO Board2 HAL. CH543DS1 v1.6 chapters 10/12, CAP1 on P1.4.
 * All timings remain UNVERIFIED on silicon. See docs/BOARD2.md. */
#include "pd.h"
#include "ch543_regs.h"
#include "board.h"
#include "ch9445.h"
#include "ch543_isr.h"

static uint16_t XDATA tick_ms;
static volatile uint8_t hpd_level, hpd_irq, hpd_timer;
void hal_poll(void) { ch9445_poll(); }
/* hpd_timer: 0 idle, 1 measuring sink low, 2 generating source IRQ,
 * 3 establishing a high level before an IRQ received with the initial high. */

/* Timer2 is a free-running 4 MHz CAP1 clock. The foreground extends it
 * into milliseconds. Call pd_poll at least every 16 ms (one counter wrap);
 * all shipped foreground operations are bounded. No delay/sleep API exists. */
static uint16_t XDATA clock_last, clock_fraction;
static volatile uint16_t IDATA hpd_fall;

void board_init(void)
{
    P1 = 0xffu; P3 = 0xffu; /* release I2C, PWREN# high before output enable */
    P1_MOD_OC = 0x20u; P1_DIR_PU = 0;
    P3_MOD_OC = 0x04u; P3_DIR_PU = 0x10u;
    P1_ANA_PIN = 0; P3_ANA_PIN = 0;
    PIN_MISC &= (uint8_t)~0xa0u; /* P1.7 weak pullup/P3.5 pulldown off */
    PIN_FUNC = 1u; /* CAP1 on P1.4; INT0 remains DISABLED on I2C SDA */
    EX0=0; IE0=0;
    tick_ms=clock_last=clock_fraction=0;
    hpd_irq=hpd_timer=0; hpd_fall=0;
    hpd_level=BOARD_HPD;
    TMOD=0x11u; TR0=TR1=0; TF0=TF1=0; ET0=ET1=1;
    T2CON=1u; T2MOD=5u; /* capture, any edge, CAP1 enabled, Fsys/12 */
    TL2=TH2=0; TF2=0; ET2=1; TR2=1;
    ch9445_init();
}

uint16_t system_get_tick(void)
{
    uint16_t current,elapsed;
    uint8_t high,low;
    do { high=TH2; low=TL2; } while(high!=TH2);
    current=((uint16_t)high<<8)|low;
    elapsed=(uint16_t)(current-clock_last); clock_last=current;
    while(elapsed>=4000u) { elapsed-=4000u; ++tick_ms; }
    clock_fraction+=elapsed;
    if(clock_fraction>=4000u) { clock_fraction-=4000u; ++tick_ms; }
    return tick_ms;
}

uint8_t board_vbus_valid(void)
{
#if BOARD2_VBUS_VERIFIED
    /* Requires an external 5V-good circuit with legal logic levels on P3.5.
     * The original R5-only circuit is NOT a voltage window detector. */
    return (P3 & 0x20u)?1u:0u;
#else
    return 0; /* no PD attach until the power sensing hardware is corrected */
#endif
}

uint8_t board_source_allowed(void) { return 0; }
void board_vbus_set(uint8_t enable) { (void)enable; BOARD_PWR_DISABLE=1; }

uint8_t dp_hpd_get(void)
{
    /* Synchronize after a video-role change made while detached. A pending
     * short-low capture must keep the previous high until its width is known. */
    if (hpd_timer != 1u) hpd_level = BOARD_HPD;
    return hpd_level;
}

uint8_t dp_hpd_irq_get(void)
{
    uint8_t result;
    uint8_t enabled = EA;
    EA = 0;
    result = hpd_irq;
    hpd_irq = 0;
    EA = enabled;
    return result;
}

/* Capture timestamps are hardware values, independent of foreground I2C.
 * Timer1 classifies a sustained low after 2ms. Rise/fall ISR delivery latency
 * and the shared HPD line's level translation still require bench validation. */
void timer2_isr(void) ISR(5,2)
{
    uint16_t capture,elapsed;
    TF2=0; /* CAP1F is the same bit in capture mode */
    capture=((uint16_t)T2CAP1H<<8)|T2CAP1L;
    if(pd.video_role!=DP_SINK) return;
    if(BOARD_HPD) {
        if(hpd_timer==1u) {
            TR1=0;
            elapsed=(uint16_t)(capture-hpd_fall);
            if(!TF1 && hpd_level && elapsed>=2000u && elapsed<=4000u)
                hpd_irq=1;
            TF1=0; hpd_timer=0;
        }
        hpd_level=1;
    } else {
        hpd_fall=capture; TR1=0;
        TH1=0xe0u; TL1=0xc0u; TF1=0; hpd_timer=1; TR1=1;
    }
}

void timer1_isr(void) ISR(3,2)
{
    TR1 = 0;
    if (hpd_timer == 3u) {
        BOARD_HPD=0;
        TH1 = 0xf6u;
        TL1 = 0xa0u;
        hpd_timer = 2;
        TR1 = 1;
        return;
    }
    if (hpd_timer == 1u) hpd_level = 0;
    else if (hpd_timer == 2u) BOARD_HPD=1;
    hpd_timer = 0;
}

void dp_hpd_set(uint8_t level, uint8_t irq)
{
    uint8_t enabled = EA;
    EA = 0;
    /* Input pulse capture must survive core calls while acting as DP Sink. */
    if (pd.video_role != DP_SOURCE) {
        P1_DIR_PU &= (uint8_t)~0x10u; /* shared HPD input when video Sink */
        BOARD_HPD=1;
        if (hpd_timer >= 2u) { TR1 = 0; TF1 = 0; hpd_timer = 0; }
        EA = enabled;
        return;
    }
    /* Drive only while video Source; clear the latch before output enable. */
    if(!(P1_DIR_PU&0x10u)) { BOARD_HPD=0; P1_DIR_PU|=0x10u; }
    if (!level) {
        TR1 = 0;
        TF1 = 0;
        hpd_timer = 0;
        BOARD_HPD=0;
    } else if (irq && hpd_timer < 2u) {
        TR1 = 0;
        if (!BOARD_HPD) {
            /* A first high+IRQ must first establish a real rising edge. */
            BOARD_HPD=1;
            TH1 = 0xe0u;
            TL1 = 0xc0u; /* nominal 2 ms high before pulsing low */
            hpd_timer = 3;
        } else {
            BOARD_HPD=0;
            TH1 = 0xf6u;
            TL1 = 0xa0u; /* 2400 counts = nominal 600 us IRQ low */
            hpd_timer = 2;
        }
        TF1 = 0;
        TR1 = 1;
    } else if (hpd_timer != 2u) BOARD_HPD=1;
    EA = enabled;
}
