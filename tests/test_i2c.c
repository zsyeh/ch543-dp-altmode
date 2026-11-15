/* Executes the target software master with pin hooks, checks actual bus bits.
 * Slave ACK/data are scripted; no claim about analogue rise/hold timings. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ch9445.h"
static uint8_t scl,sda,stuck,samples[20],ns,pos;
static uint8_t bits[3][40],nb[3],starts,stops;
static unsigned clocks;
static unsigned stuck_after;
void i2c_scl_set(uint8_t release) {
 if(release && !scl && starts) {
  assert(starts<=3 && nb[starts-1]<40);
  bits[starts-1][nb[starts-1]++]=sda;
 }
 scl=release;
}
void i2c_sda_set(uint8_t release) {
 if(scl && sda!=release) { if(release) stops++; else starts++; }
 sda=release;
}
uint8_t i2c_scl_get(void) { clocks++; return (stuck || (stuck_after && clocks>=stuck_after))?0:scl; }
uint8_t i2c_sda_get(void) { assert(pos<ns); return samples[pos++]; }
static void reset(void) {
 scl=sda=1; stuck=ns=pos=starts=stops=0; clocks=stuck_after=0;
 memset(bits,0,sizeof bits); memset(nb,0,sizeof nb);
}
static uint8_t byte_at(unsigned phase,unsigned offset) {
 uint8_t b=0; unsigned i;
 for(i=0;i<8;i++) b=(uint8_t)((b<<1)|bits[phase][offset+i]);
 return b;
}
int main(void) {
 unsigned i; uint8_t result;
 reset(); samples[0]=1; samples[1]=samples[2]=samples[3]=0; ns=4;
 assert(board_i2c_write(0x31,0xa1)); assert(pos==4 && starts==1 && stops==1);
 assert(nb[0]==28 && byte_at(0,0)==0x90 && byte_at(0,9)==0x31 && byte_at(0,18)==0xa1);
 assert(bits[0][8] && bits[0][17] && bits[0][26]);
 reset(); samples[0]=1; samples[1]=samples[2]=0; samples[3]=1; samples[4]=0;
 for(i=0;i<8;i++) samples[5+i]=(0xa5u>>(7-i))&1u;
 ns=13; assert(board_i2c_read(0x30,&result)); assert(result==0xa5);
 assert(pos==13 && starts==2 && stops==1);
 assert(nb[0]==19 && byte_at(0,0)==0x90 && byte_at(0,9)==0x30);
 assert(nb[1]==19 && byte_at(1,0)==0x91 && bits[1][17]==1); /* NACK final byte */
 for(i=1;i<=3;i++) {
  reset(); samples[0]=1; samples[1]=samples[2]=samples[3]=0;
  samples[i]=1; ns=(uint8_t)(i+1);
  assert(!board_i2c_write(0x10,1)); assert(pos==ns && stops==1 && scl && sda);
 }
 reset(); stuck=1; assert(!board_i2c_write(0x10,1)); assert(clocks==64);
 reset(); samples[0]=1; samples[1]=samples[2]=samples[3]=0; ns=4; stuck_after=29;
 assert(!board_i2c_write(0x10,1)); assert(clocks==60); /* STOP must succeed too */
 for(i=1;i<=4;i++) {
  if(i==3) continue;
  reset(); samples[0]=samples[3]=1; samples[1]=samples[2]=samples[4]=0;
  samples[i]=1; ns=(uint8_t)(i+1); result=0xcc;
  assert(!board_i2c_read(0x30,&result)); assert(pos==ns && result==0xcc);
 }
 reset(); samples[0]=0; ns=1; assert(!board_i2c_write(0x10,1)); assert(pos==1);
 puts("Software I2C: wire bits, repeated START, final NACK, write/read NACKs, failed STOP and bounded stuck lines passed (pin model)");
 return 0;
}
