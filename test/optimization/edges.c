/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>

#include "gba_internal.h"
#include <stdio.h>
static unsigned seed=0x35271a43,h=2166136261u,pcm=2166136261u;
static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned hash(unsigned v,const void *ptr,size_t n){const unsigned char *p=ptr;while(n--)v=(v^*p++)*16777619u;return v;}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int a,int b,int c,int d,gba_color_t *p){(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
int gba_sound_output(uint8_t *p,size_t n){pcm=hash(pcm,p,n);return 0;}

int main(void){
 static const uint32_t pcs[]={0x03007ff0,0x03fffff0,0x08003ff0,0x0801fff8,0x09fffff8,0x0bfffff8,0x0cfffff8,0x080000c0,0x03001001,0x08002001};
 unsigned boundaries=0,faults=0;
 uint8_t *rom=calloc(1,0x2000000);gba_t *g=gba_init();if(!rom||!g)return 1;
 for(unsigned scene=0;scene<80;scene++){
  if(gba_load_rom(g,rom,0x2000000))return 2;
  uint32_t pc=pcs[scene%10];unsigned thumb=(scene/10)&1,small=(scene/20)&1,gpio=(scene/40)&1;
  uint32_t physical=pc&0x1ffffff;
  uint8_t *mem=(pc>>24)==3?g->memory.iwram:rom;unsigned mask=(pc>>24)==3?0x7fff:0x1ffffff;
  unsigned width=thumb?2:4;
  for(unsigned i=0;i<16;i++){
   unsigned address=((physical&~(width-1u))+i*width)&mask;
   if(thumb)gba_put16(mem+address,0x3001);else gba_put32(mem+address,0xe2800001);
  }
  g->machine.cpu.cpsr=0x9f|(thumb?GBA_T:0);g->machine.cpu.r[15]=pc;
  g->machine.cpu.r[13]=0x03004000;g->machine.cart.rtc_enabled=gpio;
  if(small&&(pc>>24)!=3)g->rom_size=physical+width*2;
  gba_io_write(g,0x204,scene&1?0x47ff:0,2);
  for(unsigned i=0;i<32;i++){
   unsigned cycles=gba_run_cycles(g,1+(i*13)%96);boundaries++;
   /* Artificial short/unmapped instruction streams may reach the original
    * core's fault path. Compare the fault and state, rather than assuming a
    * valid game at these deliberately invalid PC/ROM combinations. */
   h=hash(h,&g->fault,sizeof(g->fault));
   h=hash(h,&cycles,sizeof(cycles));h=hash(h,&g->machine,sizeof(g->machine));h=hash(h,g->io,sizeof(g->io));
   h=hash(h,g->memory.iwram,0x8000);h=hash(h,&pcm,sizeof(pcm));
   if(!cycles||g->fault){faults++;break;}
  }
  gba_unload_rom_discard(g);
 }
 printf("FETCH_EDGES 80 BOUNDARIES %u FAULT_CASES %u %08x\n",boundaries,faults,h);gba_deinit(g);free(rom);return 0;
}
