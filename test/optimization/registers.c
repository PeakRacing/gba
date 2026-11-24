/* SPDX-License-Identifier: Apache-2.0 */

#include "gba_internal.h"
#include <stdio.h>
static unsigned irq_samples;
static unsigned seed=0x35271a43,h=2166136261u,pcm=2166136261u;
static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned hash(unsigned v,const void *ptr,size_t n){const unsigned char *p=ptr;while(n--)v=(v^*p++)*16777619u;return v;}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int a,int b,int c,int d,gba_color_t *p){(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
int gba_sound_output(uint8_t *p,size_t n){pcm=hash(pcm,p,n);return 0;}
int main(void){
 unsigned char rom[16384]={0};gba_t *g=gba_init();if(!g||gba_load_rom(g,rom,sizeof(rom)))return 1;
 for(unsigned scene=0;scene<128;scene++){
  if(gba_reset(g))return 2;
  for(unsigned r=0;r<15;r++)g->machine.cpu.r[r]=rng();
  g->machine.cpu.r[13]=0x03004000;g->machine.cpu.cpsr=(rng()&0xf0000000)|0x9f;
  unsigned thumb=scene>=64,in_rom=(scene&32)!=0;
  unsigned char *code=in_rom?rom+0x1000:g->memory.iwram+0x1000;
  g->machine.cpu.r[15]=in_rom?0x08001000:0x03001000;
  if(thumb)g->machine.cpu.cpsr|=GBA_T;
  if(scene&8)gba_io_write(g,0x204,0x4000|(rng()&0x7ff),2);
  if(scene&4){g->machine.cpu.hle_irq=1;g->machine.cpu.wait_mask=1;}
  for(unsigned i=0;i<32;i++){
   unsigned cond=(rng()%15)<<28,rd=rng()%12,rn=rng()%16,rm=rng()%16,op;
   switch(i%4){
   case 0:op=cond|0x02000000|((rng()%16)<<21)|((rng()&1)<<20)|(rn<<16)|(rd<<12)|(rng()&0xfff);break;
   case 1:op=cond|((rng()%16)<<21)|((rng()&1)<<20)|(rn<<16)|(rd<<12)|((rng()&1)?(((rng()%16)<<8)|((rng()%4)<<5)|0x10):((rng()&0x7f)<<5))|rm;break;
   case 2:op=cond|0x00000090|((rng()&1)<<21)|((rng()&1)<<20)|(rd<<16)|((rn%12)<<12)|((rng()%16)<<8)|rm;break;
   default:op=cond|0x00800090|((rng()&3)<<21)|((rng()&1)<<20)|(rd<<16)|((rn%12)<<12)|((rng()%16)<<8)|rm;break;
   }
   if(!thumb)gba_put32(code+i*4,op);
   else {
    unsigned d=rng()%8,s=rng()%8,t;
    switch(i%7){
    case 0:t=((rng()%3)<<11)|((rng()%32)<<6)|(s<<3)|d;break;
    case 1:t=0x1800|((rng()&3)<<9)|((rng()%8)<<6)|(s<<3)|d;break;
    case 2:t=0x2000|((rng()%4)<<11)|(d<<8)|(rng()&255);break;
    case 3:t=0x4000|((rng()%16)<<6)|(s<<3)|d;break;
    case 4:t=0x4400|((rng()%3)<<8)|((rng()&1)<<7)|((rng()&1)<<6)|(s<<3)|d;break;
    case 5:t=0xa000|((rng()&1)<<11)|(d<<8)|(rng()&255);break;
    default:t=0xb000;break;
    }
    gba_put16(code+i*2,(uint16_t)t);
   }
  }
  if(!thumb){
   gba_put32(code+0x80,0xeaffffde);
   gba_put32(code+0x30,0xe59d1000);gba_put32(code+0x34,0xe58d2000);
   gba_put32(code+0x44,0xe10f1000);gba_put32(code+0x60,0xe128f003);
  } else {
   gba_put16(code+0x40,0xe7de);gba_put16(code+0x18,0x9900);gba_put16(code+0x1a,0x9200);
  }
  if(scene&1){gba_io_write(g,0x200,8,2);gba_io_write(g,0x100,0xff00,2);gba_io_write(g,0x102,0xc0,2);}
  if(scene&2){
   gba_put32(g->memory.ewram,thumb?0x46c01c08:0xe2800001);gba_io_write(g,0xd4,0x02000000,4);
   gba_io_write(g,0xd8,in_rom?0x03006000:0x03001028,4);gba_io_write(g,0xdc,1,2);gba_io_write(g,0xde,0xa400,2);
  }

  if(scene&1){
   /* Real HLE IRQ entry/return with a small handler that acknowledges timer 0.
    * The handler executes from separate IWRAM and returns through BIOS HLE. */
   gba_put32(g->memory.iwram+0x7ffc,0x03001800);
   gba_put32(g->memory.iwram+0x1800,0xe59f000c);
   gba_put32(g->memory.iwram+0x1804,0xe3a01008);
   gba_put32(g->memory.iwram+0x1808,0xe1c010b0);
   gba_put32(g->memory.iwram+0x180c,0xe12fff1e);
   gba_put32(g->memory.iwram+0x1810,0xe1a00000);
   gba_put32(g->memory.iwram+0x1814,0x04000202);
   g->machine.cpu.cpsr &= ~0x80u;gba_io_write(g,0x208,1,2);
  }
  for(unsigned i=0;i<256;i++){
   if(i==96){if(thumb)gba_put16(code+0x10,0x3371);else gba_put32(code+0x10,0xe2b33071);}
   unsigned budget=1+(i*131u)%4096,cycles=gba_run_cycles(g,budget);if(!cycles||g->fault)return 3;
   irq_samples += (g->machine.cpu.cpsr & 31) == 0x12;h=hash(h,&cycles,sizeof(cycles));h=hash(h,&g->machine,sizeof(g->machine));h=hash(h,g->io,sizeof(g->io));
   h=hash(h,g->memory.iwram,0x8000);h=hash(h,&pcm,sizeof(pcm));
  }
 }
 if(!irq_samples)return 4;printf("IRQ_SAMPLES %u\n",irq_samples);printf("SCENES 128 BOUNDARIES 32768 %08x\n",h);gba_unload_rom_discard(g);gba_deinit(g);return 0;
}
