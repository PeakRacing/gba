/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
/* Include the real selector to compare the complete ordered evaluation list,
 * including offscreen/window objects and partial evaluation budgets. */
#include "gba_ppu.c"
#include <stdio.h>
#include <string.h>
static unsigned reference,seed=0x8f103259,checks;
unsigned gba_test_obj_index_entries;
unsigned gba_test_oam_dirty_entries;
int gba_test_render_reference(gba_t *g){(void)g;return (int)reference;}
static unsigned random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
uint32_t gba_get_ticks(void){return 0;}
void gba_frame(gba_t *g){(void)g;}
int gba_draw(int a,int b,int c,int d,gba_color_t *p){(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
int gba_sound_output(uint8_t *p,size_t n){(void)p;(void)n;return 0;}
static int compare(gba_t *g,unsigned round){
    for(unsigned line=0;line<160;line++)for(unsigned display=0;display<8;display++) {
        uint32_t a[128],b[128];g->machine.ppu.line=line;
        gba_put16(g->io,(uint16_t)(((display&1)?0x1000:0)|((display&2)?0x8000:0)|((display&4)?0x20:0)));
        reference=1;unsigned na=gba_objects_select(g,a);
        reference=0;unsigned nb=gba_objects_select(g,b);
        if(na!=nb || memcmp(a,b,na*sizeof(*a))) {
            printf("FAIL round=%u line=%u display=%u count=%u/%u\n",round,line,display,na,nb);return 1;
        }
        checks++;
    }
    return 0;
}
static int latch(gba_t *g){
    gba_ppu_oam_latch(g);
    for(unsigned word=0;word<256;word++)
        if(g->machine.ppu.oam_latch[word]!=gba_le32(g->memory.oam+word*4))return 1;
    return 0;
}
int main(void){
    gba_t *g=gba_init();if(!g)return 2;
    /* Dense sets cover both evaluation budgets, affine doubling, invalid
     * shapes/modes, disabled ordinary objects and y=255 wraparound. */
    for(unsigned round=0;round<80;round++) {
        for(unsigned obj=0;obj<128;obj++) {
            unsigned a0=random_word()&0xffff,a1=random_word()&0xffff;
            if(round<32)a0=(round%3<<14)|((round&3)<<8)|((round>>2&3)<<10)|((round&8)?255:round);
            if(round<32)a1=(a1&0x3fff)|((round&3)<<14);
            gba_put16(g->memory.oam+obj*8,(uint16_t)a0);
            gba_put16(g->memory.oam+obj*8+2,(uint16_t)a1);
        }
        gba_video_invalidate(g); /* notify external edits to borrowed OAM */
        if(!round && compare(g,round))return 3; /* live fallback */
        if(latch(g) || compare(g,round))return 4;
        for(unsigned mutation=0;mutation<12;mutation++) {
            unsigned address=0x07000000+(random_word()&0x3fe);
            gba_bus_write(g,address,random_word(),(mutation&1)?2:4,0);
            if(compare(g,round))return 5; /* old latch still owns the index */
            if(latch(g) || compare(g,round))return 6;
            if(latch(g))return 11; /* clean repeated latch */
        }
        /* External live edit + invalidation cannot alias an older latch. */
        g->memory.oam[random_word()&1023]^=0x97;gba_video_invalidate(g);
        if(compare(g,round))return 7;
        if(latch(g) || compare(g,round))return 8;
    }
    /* Real immediate DMA uses the same write tracker, including both halfword
     * fields of each OAM word; compare all 256 latched words after completion. */
    for(unsigned i=0;i<0x400;i++)g->memory.ewram[i]=(uint8_t)random_word();
    gba_write32(g,0x040000d4,0x02000000);
    gba_write32(g,0x040000d8,0x07000000);
    gba_write32(g,0x040000dc,0x84000100);
    for(unsigned poll=0;g->machine.dma[3].pending && poll<2048;poll++) {
        g->access_cycles=0;unsigned cycles=gba_dma_step(g);
        gba_advance(g,cycles?cycles:1);gba_flush_events(g);
    }
    if(g->machine.dma[3].pending || memcmp(g->memory.oam,g->memory.ewram,0x400) || latch(g) || compare(g,80))return 13;
    if(!gba_test_obj_index_entries)return 9;
#if GBA_PPU_OAM_DIRTY
    if(!gba_test_oam_dirty_entries)return 12;
#endif
    printf("OBJ_INDEX checks=%u actual_index_entries=%u PASS\n",checks,gba_test_obj_index_entries);
    return gba_deinit(g)?10:0;
}
