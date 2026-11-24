/* SPDX-License-Identifier: Apache-2.0 */
#include "gba_ppu.c"
#include <stdio.h>
#include <string.h>
static unsigned reference,seed=0x71c4b029;
static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int gba_test_render_reference(gba_t *g){(void)g;return (int)reference;}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int a,int b,int c,int d,gba_color_t *p){(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
int gba_sound_output(uint8_t *p,size_t n){(void)p;(void)n;return 0;}
static int expected_values(gba_t *g){
    static const unsigned input[]={0,0x7fff,0x7c00,31};
    static const unsigned effect[]={2,3,2,3},amount[]={16,16,8,8};
#if GBA_COLOR_DEPTH == 16
    static const unsigned expected[]={0xffff,0,0x7bdf,0x8000};
#else
    static const unsigned expected[]={0xffffffff,0xff000000,0xff7b7bff,0xff830000};
#endif
    for(unsigned n=0;n<4;n++){
        uint8_t shade[32];gba_color_t line[GBA_WIDTH];
        for(unsigned k=0;k<32;k++)shade[k]=(uint8_t)(effect[n]==2?k+(31-k)*amount[n]/16:k-k*amount[n]/16);
        for(unsigned x=0;x<GBA_WIDTH;x++){line[x]=gba_color(input[n]);g->object_window[x]=1;}
        gba_scanline_brightness(g,line,0,GBA_WIDTH,1,shade);
        unsigned want=expected[n];
#if GBA_COLOR_DEPTH == 16 && GBA_COLOR_SWAP
        want=((want<<8)|(want>>8))&65535;
#endif
        for(unsigned x=0;x<GBA_WIDTH;x++)if(line[x]!=want)return 1;
        for(unsigned x=0;x<GBA_WIDTH;x++)line[x]=gba_color(input[n]);
        gba_scanline_brightness(g,line,0,GBA_WIDTH,2,shade);
        for(unsigned x=0;x<GBA_WIDTH;x++)if(line[x]!=gba_color(input[n]))return 1;
    }
    return 0;
}
int main(void){
    gba_t *g=gba_init();if(!g)return 2;
    if(expected_values(g)){puts("FAIL independent brightness values/target masking");return 5;}
    unsigned checks=0;
    for(unsigned n=0;n<0x18000;n++)g->memory.vram[n]=(uint8_t)rng();
    for(unsigned n=0;n<0x400;n++)g->memory.palette[n]=(uint8_t)rng();
    for(unsigned scene=0;scene<2048;scene++){
        unsigned mode=scene&1,effect=(scene%17)?2+((scene>>1)&1):1;
        unsigned display=0x1f00|mode|((scene&4)?0x6000:0)|((scene&8)?0x40:0);
        gba_io_write(g,0,display,2);gba_io_write(g,2,(scene>>4)&1,2);
        for(unsigned bg=0;bg<4;bg++){
            unsigned control=((rng()&3)<<14)|((rng()&31)<<8)|((rng()&3)<<2)|(rng()&3)|((scene&16)?0x80:0);
            gba_io_write(g,8+bg*2,control,2);
            gba_io_write(g,0x10+bg*4,rng()&511,2);gba_io_write(g,0x12+bg*4,rng()&511,2);
            g->machine.ppu.bg_delay[bg]=0;
        }
        gba_io_write(g,0x20,256,2);gba_io_write(g,0x22,0,2);gba_io_write(g,0x24,0,2);gba_io_write(g,0x26,256,2);
        gba_io_write(g,0x40,rng(),4);gba_io_write(g,0x44,rng(),4);gba_io_write(g,0x48,rng(),4);
        gba_io_write(g,0x4c,(scene%29)?0:rng(),2);
        gba_io_write(g,0x50,(effect<<6)|(scene&63)|((rng()&63)<<8),2);
        gba_io_write(g,0x52,rng(),2);gba_io_write(g,0x54,(scene>>5)&31,2);
        for(unsigned obj=0;obj<128;obj++){
            unsigned a0=0x200,a1=0,a2=0;
            if(obj<16){
                a0=((scene+obj*7)%160)|((obj%3)<<14)|((obj&1)?0x2000:0);
                if(scene%31==0)a0|=(obj&1)?0x400:0x800;
                a1=((obj*19+scene)%300)|((obj&3)<<14)|((obj&1)?0x1000:0);
                a2=(rng()&1023)|((rng()&3)<<10)|((rng()&15)<<12);
            }
            gba_put16(g->memory.oam+obj*8,a0);gba_put16(g->memory.oam+obj*8+2,a1);gba_put16(g->memory.oam+obj*8+4,a2);
        }
        for(unsigned width=1;width<=4;width*=2)gba_bus_write(g,0x05000000+(scene%64)*4,rng(),width,0);
        for(unsigned sample=0;sample<4;sample++){
            unsigned line=(scene+sample*37)%160;
            g->machine.ppu.line=line;g->machine.ppu.oam_latch_valid=0;
            g->machine.ppu.window_active=scene&3;g->machine.ppu.ref_x[0]=(scene&127)<<8;g->machine.ppu.ref_y[0]=line<<8;
            gba_color_t expected[GBA_WIDTH];
            reference=1;gba_video_invalidate(g);gba_ppu_render(g);
            memcpy(expected,g->memory.framebuffer+line%GBA_DRAW_LINES*GBA_WIDTH,sizeof(expected));
            reference=0;gba_video_invalidate(g);gba_ppu_render(g);
            const gba_color_t *actual=g->memory.framebuffer+line%GBA_DRAW_LINES*GBA_WIDTH;
            if(memcmp(expected,actual,sizeof(expected))){
                for(unsigned x=0;x<GBA_WIDTH;x++)if(expected[x]!=actual[x]){
                    printf("FAIL scene=%u line=%u x=%u expected=%08x actual=%08x effect=%u\n",scene,line,x,(unsigned)expected[x],(unsigned)actual[x],effect);return 3;
                }
            }
            if(scene%64==0) {
                for(unsigned at=0;at<512;at+=4)gba_bus_write(g,0x05000000+at,rng(),4,0);
                reference=0;gba_ppu_render(g);
                gba_color_t cached[GBA_WIDTH];memcpy(cached,actual,sizeof(cached));
                reference=1;gba_video_invalidate(g);gba_ppu_render(g);
                if(memcmp(cached,actual,sizeof(cached))){printf("FAIL cached palette scene=%u line=%u\n",scene,line);return 6;}
                reference=0;
            }
            checks+=GBA_WIDTH;
        }
    }
    if(gba_test_scanline_entries<100)return 4;
    printf("PASS pixels=%u fast_rows=%u depth=%u swap=%u cache=%u\n",checks,gba_test_scanline_entries,GBA_COLOR_DEPTH,GBA_COLOR_SWAP,GBA_VIDEO_CACHE);
    gba_free(g);return 0;
}
