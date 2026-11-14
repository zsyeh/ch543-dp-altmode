#ifndef CH543_ISR_H
#define CH543_ISR_H
#include "platform.h"
/* SDCC builds the vector table in the translation unit containing main().
 * Every target ISR declaration MUST be visible there, even though its body
 * is in another object. Keep bank numbers identical to the definitions. */
void hpd_edge_isr(void) ISR(0,2);
void timer0_isr(void) ISR(1,2);
void timer1_isr(void) ISR(3,2);
void timer2_isr(void) ISR(5,2);
void pd_isr(void) ISR(10,1);
#endif
