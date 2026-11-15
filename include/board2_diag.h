#ifndef BOARD2_DIAG_H
#define BOARD2_DIAG_H
#include "platform.h"
/* Standalone bring-up image: no PD, HPD output or high-speed routing. */
void board2_diag_init(void);
void board2_diag_poll(void);
#endif
