/* SPDX-License-Identifier: Apache-2.0 */

#include "gba_internal.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned audio=2166136261u,video=2166136261u,draws,bytes;
static unsigned hash(unsigned value,const void *data,size_t size){
 const unsigned char *p=data;while(size--)value=(value^*p++)*16777619u;return value;
}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int x1,int y1,int x2,int y2,gba_color_t *pixels){
 if(x1||y1||x2!=239||y2!=159)return -1;
 video=hash(video,pixels,38400*sizeof(*pixels));draws++;return 0;
}
int gba_sound_output(uint8_t *buffer,size_t count){audio=hash(audio,buffer,count);bytes+=(unsigned)count;return 0;}
int main(int argc,char **argv){
 if(argc!=3)return 2;unsigned frames=(unsigned)strtoul(argv[2],0,10);
 gba_t *g=gba_init();if(!g)return 3;if(gba_load_file(g,argv[1]))return 4;
 while(g->machine.frames<frames && !g->fault){
  unsigned frame=g->machine.frames,keys=0;
  if((frame>=120&&frame<124)||(frame>=600&&frame<604)||(frame>=900&&frame<904))keys=8;
  else if(frame>=180&&frame%60<4)keys=1;
  gba_set_keys(g,keys);if(!gba_run_frame(g))break;
  if(g->machine.frames==300 || g->machine.frames==frames){
   char path[64];snprintf(path,sizeof(path),"frame-%u.state",g->machine.frames);
   if(gba_state_save_file(g,path))return 5;
  }
 }
 printf("frames=%u cycles=%u pc=%08x fault=%d video=%08x draws=%u audio=%08x bytes=%u\n",
 g->machine.frames,g->machine.cycles,g->machine.cpu.r[15],g->fault,video,draws,audio,bytes);
 int error=g->fault || g->machine.frames!=frames;gba_unload_rom_discard(g);gba_deinit(g);return error;
}
