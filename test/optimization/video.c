/* SPDX-License-Identifier: Apache-2.0 */

#include "gba_internal.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned h=2166136261u,r=0x1367123;
static unsigned rng(void){r^=r<<13;r^=r>>17;r^=r<<5;return r;}
static void hash(const void *p,size_t n){const unsigned char *b=p;while(n--)h=(h^*b++)*16777619u;}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int a,int b,int c,int d,gba_color_t *p){(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
int gba_sound_output(uint8_t *p,size_t n){(void)p;(void)n;return 0;}
int main(void){
 gba_t *g=gba_init();if(!g)return 1;
 for(unsigned i=0;i<0x18000;i++)g->memory.vram[i]=(uint8_t)rng();
 for(unsigned i=0;i<0x400;i++){g->memory.palette[i]=(uint8_t)rng();g->memory.oam[i]=(uint8_t)rng();}
 gba_video_invalidate(g);
 for(unsigned scene=0;scene<96;scene++){
  gba_io_write(g,0,0x1f00|(scene%6)|((scene%3)?0xe000:0),2);
  for(unsigned bg=0;bg<4;bg++){
   gba_io_write(g,8+bg*2,(rng()&0xffff),2);gba_io_write(g,0x10+bg*4,rng()&511,2);
  }
  gba_io_write(g,0x40,rng(),4);gba_io_write(g,0x44,rng(),4);gba_io_write(g,0x48,rng(),4);
  gba_io_write(g,0x4c,rng(),2);gba_io_write(g,0x50,rng(),2);gba_io_write(g,0x52,rng(),4);
  for(unsigned step=0;step<16;step++){
   unsigned line=(scene*29u)%160;g->machine.ppu.line=line;
   if(step==2)gba_bus_write(g,0x06000000+(rng()%0x18000),rng(),1,0);
   if(step==3)gba_bus_write(g,0x05000000+(rng()&0x3fc),rng(),4,0);
   if(step==4)gba_bus_write(g,0x07000000+(rng()&0x3fe),rng(),2,0); /* old latch remains */
   if(step==5||step==10)gba_ppu_oam_latch(g);
   if(step==6)gba_io_write(g,2,1,2); /* green swap must not run twice on a hit */
   if(step==7)gba_io_write(g,0x10,rng(),2);
   if(step==8){g->machine.ppu.ref_x[0]+=73;g->machine.ppu.ref_y[1]+=91;}
   if(step==9)gba_bus_write(g,0x06018000,rng(),4,0); /* mirrored VRAM */
   if(step==11){g->memory.palette[0]^=0xf7;gba_video_invalidate(g);}
   if(step==12){gba_memset(g->memory.framebuffer,0,38400*sizeof(gba_color_t));gba_video_invalidate(g);}
   if(step==13)g->machine.ppu.window_active^=3;
   if(step==14){g->memory.oam[0]^=0x3f;gba_video_invalidate(g);}
   if(step==15)gba_ppu_oam_latch(g);
   gba_ppu_render(g);hash(g->memory.framebuffer+line*240,240*sizeof(gba_color_t));
  }
 }
 /* A pending external OAM edit must be rendered from the old latch before
  * the next latch, then from the new live data; invalidation cannot alias them. */
 gba_io_write(g,0,0x1040,2);gba_io_write(g,0x50,0,2);gba_io_write(g,2,0,2);
 for(unsigned obj=0;obj<128;obj++)gba_put16(g->memory.oam+obj*8,0x200);
 gba_put16(g->memory.oam,0);gba_put16(g->memory.oam+2,0);gba_put16(g->memory.oam+4,0);
 gba_put16(g->memory.palette,0x001f);gba_put16(g->memory.palette+514,0x03e0);
 gba_memset(g->memory.vram+0x10000,0x11,32);g->machine.ppu.line=0;
 gba_video_invalidate(g);gba_ppu_oam_latch(g);gba_ppu_render(g);hash(g->memory.framebuffer,480);
 gba_put16(g->memory.oam+2,24);gba_video_invalidate(g);
 gba_ppu_render(g);hash(g->memory.framebuffer,480);
 gba_ppu_oam_latch(g);gba_ppu_render(g);hash(g->memory.framebuffer,480);
 unsigned display=gba_io16(g,0);g->machine.cpu.r[0]=0x1c;gba_bios_swi(g,1);
 gba_io_write(g,0,display,2);gba_ppu_render(g);hash(g->memory.framebuffer,480);
 printf("SCENES 96 FRAME_VARIANTS 1540 %08x\n",h);gba_deinit(g);return 0;
}
