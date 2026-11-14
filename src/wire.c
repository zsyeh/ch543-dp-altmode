#include "wire.h"
uint16_t wire_get16(const uint8_t XDATA *p) {
 return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
uint32_t wire_get32(const uint8_t XDATA *p) {
 return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
        ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
void wire_put16(uint8_t XDATA *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
void wire_put32(uint8_t XDATA *p, uint32_t v) {
 p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
