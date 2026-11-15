/* Standalone Board2 diagnostic, deliberately not linked with the PD stack.
 * UART0 uses Timer1, which the full firmware needs for HPD. Do not call this
 * module from that firmware. All interrupts and CC terminations stay off.
 * CH543DS1 v1.6 sections 8,10,12,13; WCH EVT DEBUG.C mInitSTDIO;
 * CH9445DS1 v1.5 section 5.4. See docs/DIAGNOSTIC.md for evidence/limits. */
#include "board2_diag.h"
#include "ch543_regs.h"
#include "ch9445.h"

static const char CODE line[] =
    "B2DIAG1 PD=OFF EN#=1 A7=48 W10=! R10=?? CVR=?? R11=?? R30=?? R31=?? R32=?? P1=?? P3=??\r\n";
static const char CODE digits[] = "0123456789ABCDEF";
static const uint8_t CODE registers[] = {0x10,0x08,0x11,0x30,0x31,0x32};
static uint8_t XDATA values[8];
static uint16_t XDATA ticks, last_count, fraction, last_report;
static uint8_t phase, valid, write_ok, position, nibble;
static uint8_t IDATA received;

/* Poll faster than the 16.384 ms wrap. UART never waits for a character;
 * each call performs at most one bounded I2C transaction or one UART byte. */
static void update_time(void)
{
    uint8_t high, low;
    uint16_t count, elapsed;
    do { high=TH2; low=TL2; } while (high!=TH2);
    count=((uint16_t)high<<8)|low;
    elapsed=(uint16_t)(count-last_count); last_count=count;
    while(elapsed>=4000u) { elapsed-=4000u; ++ticks; }
    fraction+=elapsed;
    if(fraction>=4000u) { fraction-=4000u; ++ticks; }
}

void board2_diag_init(void)
{
    IE=0; IE_EX=0;
    /* Release first, then enable only TXD and the active-low power DISABLE. */
    P1=0xffu; P3=0xffu;
    P1_MOD_OC=0x20u; P1_DIR_PU=0;
    P3_MOD_OC=0x04u; P3_DIR_PU=0x12u;
    P1_ANA_PIN=0; P3_ANA_PIN=0; PIN_FUNC=0;
    PIN_MISC&=(uint8_t)~0xa0u;
    CC1_CTRL=CC2_CTRL=0; PD_START=0; UPD_CTRL=0; UPD_INT_EN=0;
    SAFE_MOD=0x55u; SAFE_MOD=0xaau;
    POWER_CFG|=bLDO_CORE_VOL;
    CLOCK_CFG=(CLOCK_CFG&(uint8_t)~0x17u)|7u; /* 48 MHz, UART_2X=0 */
    SAFE_MOD=0;
    /* UART mode 1, TX-only, 115384.6 baud: 48 MHz / 16 / (256-230).
     * Nominal error +0.1603% vs 115200, before internal oscillator error. */
    TCON=0; TMOD=0x20u; T2CON=0;
    T2MOD=0xa0u; /* fastest T1; T2 remains Fsys/12 = 4 MHz */
    PCON|=0x80u; TH1=TL1=230u;
    SCON=0x42u; /* SM1=1, TI=1 primes the first polled transmission */
    TR1=1;
    TL2=TH2=RCAP2L=RCAP2H=0; TR2=1;
    ticks=last_count=fraction=last_report=0;
    phase=1; valid=write_ok=position=nibble=0;
}

void board2_diag_poll(void)
{
    uint8_t index, value, c;
    update_time();
    if(!phase) {
        if((uint16_t)(ticks-last_report)>=1000u) { valid=0; phase=1; }
        return;
    }
    if(phase==1u) {
        /* Only write in this image: request ALL MUX switches OFF. */
        write_ok=board_i2c_write(0x10u,1u); ++phase; return;
    }
    if(phase<=7u) {
        index=phase-2u;
        if(board_i2c_read(registers[index],&received)) {
            values[index]=received; valid|=(uint8_t)(1u<<index);
        }
        ++phase; return;
    }
    if(phase==8u) {
        values[6]=P1; values[7]=P3; valid|=0xc0u;
        position=nibble=0; ++phase; return;
    }
    if(!(SCON&2u)) return; /* UART busy: never block */
    c=(uint8_t)line[position++];
    if(!c) { last_report=ticks; phase=0; return; }
    if(c=='!') c=write_ok?'1':'0';
    else if(c=='?') {
        index=nibble>>1;
        if(valid&(uint8_t)(1u<<index)) {
            value=values[index];
            if(!(nibble&1u)) value>>=4;
            c=(uint8_t)digits[value&0x0fu];
        } else c='-'; /* Never print a fabricated value for a failed read. */
        ++nibble;
    }
    SCON&=(uint8_t)~2u; SBUF=c;
}
