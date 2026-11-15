/* Actual diagnostic code with byte SFR variables and scripted I2C results.
 * UART timing, pin aliases and analogue behaviour are NOT simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CH543_PHY_TEST 1
#include "../src/board2_diag.c"

static unsigned transactions, bad_transaction;
static uint8_t reply[6];
static uint16_t timer_count;
static unsigned long total_counts;
static char output[512];
static size_t used;

uint8_t board_i2c_write(uint8_t reg, uint8_t value)
{
    ++transactions;
    assert((transactions-1u)%7u==0 && reg==0x10 && value==1);
    return transactions!=bad_transaction;
}
uint8_t board_i2c_read(uint8_t reg, uint8_t IDATA *value)
{
    unsigned index=(transactions-1u)%7u;
    assert(index<6 && reg==registers[index]);
    ++transactions;
    if(transactions==bad_transaction) return 0;
    *value=reply[index];
    return 1;
}
static void step(uint8_t uart_ready)
{
    unsigned before=transactions;
    timer_count=(uint16_t)(timer_count+4000u); total_counts+=4000u;
    TH2=(uint8_t)(timer_count>>8); TL2=(uint8_t)timer_count;
    if(uart_ready) SCON|=2u; else SCON&=(uint8_t)~2u;
    board2_diag_poll();
    assert(ticks==(uint16_t)(total_counts/4000u));
    assert(transactions-before<=1);
    assert(!IE && !IE_EX && !UPD_INT_EN && !UPD_CTRL && !PD_START && !CC1_CTRL && !CC2_CTRL);
    assert((P3&0x10u) && P3_DIR_PU==0x12 && P1_DIR_PU==0);
    if(uart_ready && !(SCON&2u)) {
        assert(used+1<sizeof(output)); output[used++]=(char)SBUF; output[used]=0;
    }
}
static void init(unsigned fail)
{
    bad_transaction=fail; transactions=timer_count=0; total_counts=0; used=0; output[0]=0;
    memset(reply,0,sizeof(reply)); reply[0]=1;
    IE=IE_EX=0xff; UPD_INT_EN=0xff; CC1_CTRL=CC2_CTRL=0xff; PD_START=1;
    CLOCK_CFG=0x10; board2_diag_init();
    assert(CLOCK_CFG==7 && (POWER_CFG&8) && !SAFE_MOD);
    assert(TMOD==0x20 && T2MOD==0xa0 && TH1==230 && TL1==230);
    assert((PCON&0x80) && SCON==0x42 && TR1 && TR2 && !PIN_FUNC);
    assert(P1_MOD_OC==0x20 && P3_MOD_OC==0x04);
    P1=0xa5; P3=0x36; /* raw input snapshot; P3.4 is still HIGH */
}
static void get_line(void)
{
    unsigned i;
    for(i=0;i<300 && (!used || output[used-1]!='\n');++i) step(1);
    assert(used && output[used-1]=='\n');
}
int main(void)
{
    unsigned i;
    const char *fields[]={"R10=--","CVR=--","R11=--","R30=--","R31=--","R32=--"};
    init(0); get_line();
    assert(!strcmp(output,"B2DIAG1 PD=OFF EN#=1 A7=48 W10=1 R10=01 CVR=00 R11=00 R30=00 R31=00 R32=00 P1=A5 P3=36\r\n"));
    assert(transactions==7);
    for(i=1;i<=7;i++) {
        init(i); get_line(); assert(transactions==7);
        if(i==1) assert(strstr(output,"W10=0 R10=01"));
        else assert(strstr(output,fields[i-2]));
    }
    /* An unexpected PDR/CVR/readback must be reported as read, not as success. */
    init(0); reply[0]=0; reply[1]=0x3f; reply[2]=0x10; get_line();
    assert(strstr(output,"W10=1 R10=00 CVR=3F R11=10"));
    init(0);
    for(i=0;i<70000u;i++) step(0); /* UART stalled for >16-bit ms wrap */
    assert(!used && transactions==7 && phase==9);
    get_line(); used=0; output[0]=0;
    for(i=0;i<1200u && !used;i++) step(1);
    assert(transactions==14 && used); get_line();
    assert(strstr(output,"W10=1 R10=01"));
    /* A later bus failure must not reuse the prior value as a valid reading. */
    used=0; output[0]=0; bad_transaction=16;
    for(i=0;i<1200u && !used;i++) step(1);
    get_line(); assert(strstr(output,"R10=--"));
    puts("Board2 diagnostic: UART setup/framing, safe-only I2C, all 7 transaction failures, raw readbacks, no-UART progress and timer wrap passed (model)");
    return 0;
}
