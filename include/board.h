#ifndef BOARD_H
#define BOARD_H
/* Reference GPIO contract, not an off-the-shelf MUX register map.
 * A board decoder must implement C=01, D=10, E=11 with video direction
 * and cable orientation. Drive MUX_EN low before changing any selectors.
 * CH543D QFN20: signal / GPIO / pin number:
 * CC1/P1.0/13, CC2/P1.1/12; 5V window-supervisor VBUS_OK/P1.2/20;
 * MUX_EN/P1.3/19, MODE0/P1.4/4, FLIP/P1.5/6, AUX_EN/P1.6/7;
 * HPD_OUT/P1.7/8, MODE1/P3.0/11, VIDEO_SOURCE/P3.1/10, HPD_IN/P3.2/5.
 * VBUS_OK and HPD_IN MUST be level translated to VDD. Never connect VBUS
 * directly to an IO. External pulldowns hold MUX/AUX disabled during reset.
 * AUX polarity uses FLIP. MUX_EN is active high, not necessarily an IC's /OE.
 * No VBUS switch is connected or enabled. The reference board is PD Sink.
 * Timer0: GoodCRC; Timer1: HPD low-pulse capture/output; Timer2: millisecond.
 * P1.4/1.6/1.7 are also ISP pins: isolate board signals during programming. */
#define BOARD_VBUS_OK 0x04u
#define BOARD_MUX_EN 0x08u
#define BOARD_MODE0 0x10u
#define BOARD_FLIP 0x20u
#define BOARD_AUX_EN 0x40u
#define BOARD_HPD_OUT 0x80u
#define BOARD_MODE1 0x01u
#define BOARD_VIDEO_SOURCE 0x02u
#define BOARD_HPD_IN 0x04u
void board_init(void);
#endif
