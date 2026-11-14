#ifndef PLATFORM_H
#define PLATFORM_H
#if defined(__SDCC)
#define XDATA __xdata
#define IDATA __idata
#define CODE __code
#define ISR(n,b) __interrupt(n) __using(b)
#elif defined(__C51__)
#define XDATA xdata
#define IDATA idata
#define CODE code
#define ISR(n,b) interrupt n using b
#else
#define XDATA
#define IDATA
#define CODE
#endif
#if defined(__C51__)
typedef unsigned char uint8_t;
typedef unsigned int uint16_t;
typedef unsigned long uint32_t;
#else
#include <stdint.h>
#endif
#endif
