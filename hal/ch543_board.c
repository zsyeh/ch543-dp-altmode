/* Reference board HAL. Register evidence: CH543DS1 v1.6 chapters 4, 9, 10;
 * PIN_MISC bit 4 explicitly changes INT0 between falling and rising edges.
 * All signal pulse widths remain UNVERIFIED on silicon. */
#include "pd.h"
#include "ch543_regs.h"
#include "board.h"
#include "ch543_isr.h"

static volatile uint16_t tick_ms;
static volatile uint8_t hpd_level, hpd_irq, hpd_timer;
void hal_poll(void) { }
uint8_t dp_mux_status(void) { return MUX_READY; }
/* hpd_timer: 0 idle, 1 measuring sink low, 2 generating source IRQ,
 * 3 establishing a high level before an IRQ received with the initial high. */

void board_init(void)
{
    /* Set safe output latches before enabling push-pull drivers. */
    P1 &= 0x07u;
    P3 &= (uint8_t)~(BOARD_MODE1 | BOARD_VIDEO_SOURCE);
    P1_MOD_OC = 0;
    P1_DIR_PU = 0xf8u;
    P3_MOD_OC &= (uint8_t)~0x07u;
    P3_DIR_PU = (P3_DIR_PU & (uint8_t)~0x07u) | 0x03u;
    P1_ANA_PIN = 0;
    P3_ANA_PIN &= (uint8_t)~BOARD_HPD_IN;
    PIN_FUNC &= (uint8_t)~0x04u; /* INT0 stays on P3.2 */
    PIN_MISC &= (uint8_t)~0x90u; /* HPD_OUT weak pullup off; falling INT0 */
    tick_ms = 0;
    hpd_irq = hpd_timer = 0;
    hpd_level = (P3 & BOARD_HPD_IN) ? 1u : 0u;
    if (!hpd_level) PIN_MISC |= bINT0_POLAR;
    IT0 = 1;
    IE0 = 0;
    EX0 = 1;
    /* 48 MHz / 12 = 4 MHz: 4000 timer ticks per ms. */
    T2MOD = 0;
    TMOD = 0x11u; /* Timer0/1 16 bit; both stopped initially */
    TR0 = 0;
    TR1 = 0;
    TF0 = TF1 = 0;
    ET0 = ET1 = 1;
    T2CON = 0;
    RCAP2L = TL2 = 0x60u;
    RCAP2H = TH2 = 0xf0u;
    ET2 = 1;
    TR2 = 1;
}

void timer2_isr(void) ISR(5,2)
{
    TF2 = 0;
    ++tick_ms;
}

uint16_t system_get_tick(void)
{
    uint16_t result;
    uint8_t enabled = EA;
    EA = 0;
    result = tick_ms;
    EA = enabled;
    return result;
}

uint8_t board_vbus_valid(void)
{
    return (P1 & BOARD_VBUS_OK) ? 1u : 0u;
}

uint8_t board_source_allowed(void) { return 0; }
void board_vbus_set(uint8_t enable) { (void)enable; }

void dp_mux_set(uint8_t pin, uint8_t orientation, uint8_t video_role)
{
    uint8_t mode;
    P1 &= (uint8_t)~BOARD_MUX_EN;
    if (!orientation || !pin) return;
    if (pin == PIN_C) mode = 1u;
    else if (pin == PIN_D) mode = 2u;
    else if (pin == PIN_E) mode = 3u;
    else return;
    if (mode & 1u) P1 |= BOARD_MODE0;
    else P1 &= (uint8_t)~BOARD_MODE0;
    if (mode & 2u) P3 |= BOARD_MODE1;
    else P3 &= (uint8_t)~BOARD_MODE1;
    if (orientation == 2u) P1 |= BOARD_FLIP;
    else P1 &= (uint8_t)~BOARD_FLIP;
    if (video_role == DP_SOURCE) P3 |= BOARD_VIDEO_SOURCE;
    else P3 &= (uint8_t)~BOARD_VIDEO_SOURCE;
    P1 |= BOARD_MUX_EN;
}

void dp_aux_set(uint8_t enable, uint8_t orientation)
{
    P1 &= (uint8_t)~BOARD_AUX_EN;
    if (!enable || !orientation) return;
    if (orientation == 2u) P1 |= BOARD_FLIP;
    else P1 &= (uint8_t)~BOARD_FLIP;
    P1 |= BOARD_AUX_EN;
}

uint8_t dp_hpd_get(void)
{
    /* Synchronize after a video-role change made while detached. A pending
     * short-low capture must keep the previous high until its width is known. */
    if (hpd_timer != 1u) hpd_level = (P3 & BOARD_HPD_IN) ? 1u : 0u;
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

/* HPD input low pulses are timed at 0.25 us/count, not sampled at 1 ms.
 * A low lasting 2 ms changes HPD level; a 0.5..1 ms pulse latches IRQ.
 * INT0 entry latency and PD interrupt preemption require bench validation. */
void hpd_edge_isr(void) ISR(0,2)
{
    uint16_t elapsed;
    if (P3 & BOARD_HPD_IN) {
        PIN_MISC &= (uint8_t)~bINT0_POLAR;
        if (pd.video_role != DP_SINK) return;
        if (hpd_timer == 1u) {
            TR1 = 0;
            elapsed = (((uint16_t)TH1 << 8) | TL1) - 0xe0c0u;
            if (!TF1 && hpd_level && elapsed >= 2000u && elapsed <= 4000u)
                hpd_irq = 1;
            TF1 = 0;
            hpd_timer = 0;
        }
        hpd_level = 1;
    } else {
        PIN_MISC |= bINT0_POLAR;
        if (pd.video_role != DP_SINK) return;
        TR1 = 0;
        TH1 = 0xe0u;
        TL1 = 0xc0u; /* expire after 8000 counts / 4 MHz = 2 ms */
        TF1 = 0;
        hpd_timer = 1;
        TR1 = 1;
    }
}

void timer1_isr(void) ISR(3,2)
{
    TR1 = 0;
    if (hpd_timer == 3u) {
        P1 &= (uint8_t)~BOARD_HPD_OUT;
        TH1 = 0xf6u;
        TL1 = 0xa0u;
        hpd_timer = 2;
        TR1 = 1;
        return;
    }
    if (hpd_timer == 1u) hpd_level = 0;
    else if (hpd_timer == 2u) P1 |= BOARD_HPD_OUT;
    hpd_timer = 0;
}

void dp_hpd_set(uint8_t level, uint8_t irq)
{
    uint8_t enabled = EA;
    EA = 0;
    /* Input pulse capture must survive core calls while acting as DP Sink. */
    if (pd.video_role != DP_SOURCE) {
        P1 &= (uint8_t)~BOARD_HPD_OUT;
        if (hpd_timer >= 2u) { TR1 = 0; TF1 = 0; hpd_timer = 0; }
        EA = enabled;
        return;
    }
    if (!level) {
        TR1 = 0;
        TF1 = 0;
        hpd_timer = 0;
        P1 &= (uint8_t)~BOARD_HPD_OUT;
    } else if (irq && hpd_timer < 2u) {
        TR1 = 0;
        if (!(P1 & BOARD_HPD_OUT)) {
            /* A first high+IRQ must first establish a real rising edge. */
            P1 |= BOARD_HPD_OUT;
            TH1 = 0xe0u;
            TL1 = 0xc0u; /* nominal 2 ms high before pulsing low */
            hpd_timer = 3;
        } else {
            P1 &= (uint8_t)~BOARD_HPD_OUT;
            TH1 = 0xf6u;
            TL1 = 0xa0u; /* 2400 counts = nominal 600 us IRQ low */
            hpd_timer = 2;
        }
        TF1 = 0;
        TR1 = 1;
    } else if (hpd_timer != 2u) P1 |= BOARD_HPD_OUT;
    EA = enabled;
}
