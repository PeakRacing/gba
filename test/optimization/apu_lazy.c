#include "gba_internal.h"
#include <stdio.h>
#include <string.h>
void gba_apu_advance_reference(gba_t *,uint32_t);
static unsigned lane,seed=0x1f32a987;
static uint32_t hash[2]={2166136261u,2166136261u},bytes[2];
static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int a,int b,int c,int d,gba_color_t *p){(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
int gba_sound_output(uint8_t *p,size_t n){for(size_t i=0;i<n;i++){hash[lane]^=p[i];hash[lane]*=16777619u;}bytes[lane]+=(uint32_t)n;return 0;}
int main(void){
    gba_t *a=gba_init(),*b=gba_init();if(!a||!b)return 2;
    static const unsigned budgets[]={0,1,2,7,31,127,522,1232,4096,32768};
    for(unsigned step=0;step<50000;step++){
        unsigned address=0x60+((rng()%32)*2),value=rng();
        if(step%7==0){gba_io_write(a,address,value,2);gba_io_write(b,address,value,2);}
        if(step%5==0){unsigned f=rng()&1;value=rng();gba_io_write(a,0xa0+f*4,value,4);gba_io_write(b,0xa0+f*4,value,4);}
        if(step%11==0){unsigned timer=rng()&1;gba_apu_timer(a,timer);gba_apu_timer(b,timer);}
        /* Include already-due PCM and several sample/frame-sequencer events. */
        if(step%97==0)a->machine.apu.sample_phase=b->machine.apu.sample_phase=GBA_CPU_CLOCK*(rng()%3);
        unsigned cycles=budgets[rng()%(sizeof(budgets)/sizeof(*budgets))];
        lane=0;gba_apu_advance(a,cycles);lane=1;gba_apu_advance_reference(b,cycles);
        if(memcmp(&a->machine,&b->machine,sizeof(a->machine)) || memcmp(a->io,b->io,sizeof(a->io)) ||
            a->audio_count!=b->audio_count || memcmp(a->audio,b->audio,GBA_APU_BUFFER_SAMPLES*4) ||
            hash[0]!=hash[1] || bytes[0]!=bytes[1] || a->fault!=b->fault){printf("FAIL step=%u cycles=%u\n",step,cycles);return 3;}
    }
    printf("PASS steps=50000 pcm_bytes=%u pcm_hash=%08x\n",bytes[0],hash[0]);gba_free(a);gba_free(b);return 0;
}
