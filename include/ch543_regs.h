#ifndef CH543_REGS_H
#define CH543_REGS_H
/* Minimal register declarations, independently transcribed and checked against
 * CH543DS1 v1.6 chapters 6/7/9/18 and WCH EVT Public/CH543.H (2023-06-03).
 * No undocumented register or fictional CRC_OK flag is used. */
#if defined(CH543_PHY_TEST)
/* Functional register model only: byte variables do not model bit aliases,
 * write-one-to-clear or FIFO reset side effects. The test harness implements
 * those explicitly. This branch is never used for a target build. */
#define SFR(name, address) volatile uint8_t name
#define SBIT(name, address) volatile uint8_t name
void ch543_test_clear_fifo(void);
uint8_t ch543_test_dma_address(const uint8_t *buffer);
#define CH543_CLEAR_OBSERVED() ch543_test_clear_fifo()
#define CH543_DMA_ADDRESS(buffer) ch543_test_dma_address(buffer)
#elif defined(__SDCC)
#define SFR(name, address) __sfr __at(address) name
#define SBIT(name, address) __sbit __at(address) name
#elif defined(__C51__)
#define SFR(name, address) sfr name = address
#define SBIT(name, address) sbit name = address
#else
#error "CH543 register driver requires SDCC mcs51 or Keil C51"
#endif
#if !defined(CH543_PHY_TEST)
#define CH543_CLEAR_OBSERVED() do { } while (0)
#define CH543_DMA_ADDRESS(buffer) ((uint8_t)(uint16_t)(buffer))
#endif
SFR(P1,0x90); SFR(P3,0xb0);
SFR(P1_MOD_OC,0x92); SFR(P1_DIR_PU,0x93);
SFR(P3_MOD_OC,0x96); SFR(P3_DIR_PU,0x97);
SFR(P1_ANA_PIN,0x94); SFR(P3_ANA_PIN,0x95);
SFR(PIN_MISC,0x91); SFR(PIN_FUNC,0xaa);
SFR(SAFE_MOD,0xa1); SFR(CLOCK_CFG,0xb9); SFR(POWER_CFG,0xba);
SFR(XBUS_AUX,0xa2);
SFR(IE,0xa8); SFR(IE_EX,0xe8); SFR(IP,0xb8); SFR(IP_EX,0xe9);
SBIT(EA,0xaf); SBIT(ET0,0xa9); SBIT(ET1,0xab); SBIT(ET2,0xad);
SBIT(EX0,0xa8); SBIT(IE_USBPD,0xec);
SFR(TCON,0x88); SFR(TMOD,0x89);
SFR(TL0,0x8a); SFR(TL1,0x8b); SFR(TH0,0x8c); SFR(TH1,0x8d);
SBIT(IT0,0x88); SBIT(IE0,0x89); SBIT(TR0,0x8c); SBIT(TF0,0x8d);
SBIT(TR1,0x8e); SBIT(TF1,0x8f);
SFR(T2CON,0xc8); SFR(T2MOD,0xc9);
SFR(RCAP2L,0xca); SFR(RCAP2H,0xcb); SFR(TL2,0xcc); SFR(TH2,0xcd);
SBIT(TR2,0xca); SBIT(TF2,0xcf);
SFR(CC1_CTRL,0xc6); SFR(CC2_CTRL,0xc7);
SFR(UPD_TIMER,0xbb); SFR(UPD_COUNT,0xbc); SFR(UPD_T_LEN,0xbd);
SFR(UPD_T_SOP,0xbe); SFR(UPD_CRC32,0xbf); SFR(UPD_INT_FG,0xc0);
SFR(UPD_INT_EN,0xc1); SFR(UPD_CTRL,0xc2); SFR(UPD_DATA,0xc3);
SFR(UPD_DMA_L,0xc4);
SBIT(PIF_TX_END,0xc7); SBIT(PIF_RX_RST,0xc6); SBIT(PIF_RX_ACT,0xc5);
SBIT(PIF_RX_BYTE,0xc4); SBIT(PD_START,0xc2);
#define bCC_CE 0x80u
#define bCC_LVO 0x40u
#define bCC_CVS0 0x10u
#define bCC_PD 0x02u
#define bCC_CMPO 0x01u
#define bPD_DMA_EN 0x08u
#define bPD_CC_SEL 0x04u
#define bPD_CLR_ALL 0x02u
#define bPD_TX_EN 0x01u
#define bPIE_TX_END 0x80u
#define bPIE_RX_RST 0x40u
#define bPIE_RX_ACT 0x20u
#define bPIE_RX_BYTE 0x10u
#define MASK_PD_STAT 0x03u
#define PD_RX_SOP0 0x01u
#define PD_RX_SOP1_HRST 0x02u
#define UPD_SOP0 0x00u
#define UPD_HARD_RESET 0xa9u
#define bIP_USBPD 0x10u
#define bINT0_POLAR 0x10u
#define bLDO_CORE_VOL 0x08u
#endif
