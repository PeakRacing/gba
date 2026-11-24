/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#pragma once
#if GBA_PPU_SCANLINE_FAST
#ifdef GBA_TEST_SCANLINE_COUNTERS
unsigned gba_test_scanline_entries, gba_test_scanline_reject[16];
#define GBA_SCANLINE_REJECT(reason) do{gba_test_scanline_reject[(reason)]++;return 0;}while(0)
#else
#define GBA_SCANLINE_REJECT(reason) return 0
#endif
/* Mode-0 opaque compositor. It never mutates guest state. The existing OBJ
 * selector owns evaluation budgets and OAM-latch semantics for both paths. */
static void gba_scanline_palette_read(gba_t *g,unsigned index,uint32_t seen[4]) {
    (void)g;
    unsigned block=index>>2,word=block>>5,bit=1u<<(block&31);
    if(!(seen[word]&bit)) {seen[word]|=bit;gba_video_read(g,5,index*2);}
}
#if GBA_PPU_SCANLINE_SPANS
/* The two entry points specialize tile format once per BG segment. */
GBA_FORCE_INLINE void gba_scanline_text_body(gba_t *g,gba_color_t *out,unsigned bg,unsigned begin,unsigned end,
#else
static GBA_NOINLINE void gba_scanline_text(gba_t *g,gba_color_t *out,unsigned bg,unsigned begin,unsigned end,
#endif
    unsigned depth,unsigned rank,uint32_t seen[4]) {
    unsigned control=gba_io16(g,8+bg*2),size=control>>14;
    unsigned width=(size&1)?512:256,height=(size&2)?512:256;
    unsigned y=(g->machine.ppu.line+gba_io16(g,0x12+bg*4))&(height-1);
    unsigned scroll=gba_io16(g,0x10+bg*4),screen=((control>>8)&31)*0x800;
    unsigned base=((control>>2)&3)*0x4000;
    for(unsigned x=begin;x<end;) {
        unsigned sx=(x+scroll)&(width-1),span=8-(sx&7);
        if(span>end-x)span=end-x;
        unsigned block=(sx>>8)+(y>>8)*(width/256);
        unsigned entry=gba_vram16(g,screen+block*0x800+((y&255)/8*32+(sx&255)/8)*2);
        unsigned ty=(entry&0x800)?7-(y&7):y&7;
        unsigned row=(base+(entry&1023)*(depth?64:32)+ty*(depth?8:4))&0xffff;
        const uint8_t *data=g->memory.vram+row;
#if GBA_PPU_SCANLINE_SPANS
        uint32_t packed=depth?0:gba_le32(data);
        unsigned palette_bits=0;
#endif
        gba_video_read(g,6,row);
        if(GBA_PPU_SCANLINE_ZERO_ROWS && !gba_le32(data) && (!depth || !gba_le32(data+4))) {
            x+=span;continue;
        }
        if(!GBA_PPU_SCANLINE_PALETTE_DEDUP)gba_memset(seen,0,4*sizeof(*seen));
        for(unsigned n=0;n<span;n++) {
            if(rank>=g->object_window[x+n])continue;
            unsigned tx=(sx+n)&7;if(entry&0x400)tx=7-tx;
#if GBA_PPU_SCANLINE_SPANS
            unsigned index=depth?data[tx]:(packed>>(tx*4))&15;
#else
            unsigned index=depth?data[tx]:(data[tx>>1]>>((tx&1)*4))&15;
#endif
            if(!index)continue;
            if(!depth)index+=(entry>>12)*16;
#if GBA_PPU_SCANLINE_SPANS
            if(depth)gba_scanline_palette_read(g,index,seen);
            else palette_bits|=1u<<((index>>2)&3);
#else
            gba_scanline_palette_read(g,index,seen);
#endif
            out[x+n]=g->scanline_palette.color[index];g->object_window[x+n]=(uint8_t)rank;
        }
#if GBA_PPU_SCANLINE_SPANS
        while(palette_bits) {
            unsigned bit=(palette_bits&1)?0:(palette_bits&2)?1:(palette_bits&4)?2:3;
            palette_bits&=palette_bits-1;
            gba_scanline_palette_read(g,(entry>>12)*16+bit*4,seen);
        }
#endif
        x+=span;
    }
}
#if GBA_PPU_SCANLINE_SPANS
static GBA_NOINLINE void gba_scanline_text4(gba_t *g,gba_color_t *out,unsigned bg,
    unsigned begin,unsigned end,unsigned rank,uint32_t seen[4]) {
    gba_scanline_text_body(g,out,bg,begin,end,0,rank,seen);
}
static GBA_NOINLINE void gba_scanline_text8(gba_t *g,gba_color_t *out,unsigned bg,
    unsigned begin,unsigned end,unsigned rank,uint32_t seen[4]) {
    gba_scanline_text_body(g,out,bg,begin,end,1,rank,seen);
}
#endif
static GBA_NOINLINE void gba_scanline_text_mosaic(gba_t *g,gba_color_t *out,unsigned bg,unsigned begin,unsigned end,
    unsigned depth,unsigned rank,uint32_t seen[4]) {
    unsigned control=gba_io16(g,8+bg*2),size=control>>14;
    unsigned width=(size&1)?512:256,height=(size&2)?512:256;
    unsigned mosaic=gba_io16(g,0x4c),mxp=(mosaic&15)+1,myp=((mosaic>>4)&15)+1;
    unsigned y=(g->machine.ppu.line-g->machine.ppu.line%myp+gba_io16(g,0x12+bg*4))&(height-1);
    unsigned scroll=gba_io16(g,0x10+bg*4),screen=((control>>8)&31)*0x800;
    unsigned base=((control>>2)&3)*0x4000;
    for(unsigned x=begin;x<end;) {
        unsigned span=mxp-x%mxp;if(span>end-x)span=end-x;
        unsigned sx=((x-x%mxp)+scroll)&(width-1),block=(sx>>8)+(y>>8)*(width/256);
        unsigned entry=gba_vram16(g,screen+block*0x800+((y&255)/8*32+(sx&255)/8)*2);
        unsigned ty=(entry&0x800)?7-(y&7):y&7,tx=sx&7;
        if(entry&0x400)tx=7-tx;
        unsigned row=(base+(entry&1023)*(depth?64:32)+ty*(depth?8:4))&0xffff;
        const uint8_t *data=g->memory.vram+row;gba_video_read(g,6,row);
        unsigned index=depth?data[tx]:(data[tx>>1]>>((tx&1)*4))&15;
        if(index && !depth)index+=(entry>>12)*16;
        if(index)gba_scanline_palette_read(g,index,seen);
        for(unsigned n=0;n<span;n++) {
            if(rank>=g->object_window[x+n] || !index)continue;
            out[x+n]=g->scanline_palette.color[index];g->object_window[x+n]=(uint8_t)rank;
        }
        x+=span;
    }
}
static GBA_NOINLINE void gba_scanline_affine(gba_t *g,gba_color_t *out,unsigned bg,unsigned begin,unsigned end,
    unsigned rank,uint32_t seen[4]) {
    unsigned index=bg-2,registers=0x20+index*16,control=gba_io16(g,8+bg*2);
    int32_t step_x=gba_sign(gba_io16(g,registers),16),step_y=gba_sign(gba_io16(g,registers+4),16);
    uint32_t fx=(uint32_t)gba_sign(g->machine.ppu.ref_x[index],32)+(uint32_t)(step_x*(int32_t)begin);
    uint32_t fy=(uint32_t)gba_sign(g->machine.ppu.ref_y[index],32)+(uint32_t)(step_y*(int32_t)begin);
    unsigned size=128u<<(control>>14),width=size,height=size;
    unsigned screen=((control>>8)&31)*0x800,character=((control>>2)&3)*0x4000;
    for(unsigned x=begin;x<end;x++,fx+=(uint32_t)step_x,fy+=(uint32_t)step_y) {
        if(rank>=g->object_window[x])continue;
        int32_t sx=gba_fixed8(gba_sign(fx,32)),sy=gba_fixed8(gba_sign(fy,32));
        if(control&0x2000) {
            sx=(int32_t)((uint32_t)sx&(size-1));sy=(int32_t)((uint32_t)sy&(size-1));
        }
        if(sx<0 || sy<0 || (unsigned)sx>=width || (unsigned)sy>=height)continue;
        unsigned tile=gba_vram8(g,(screen+(unsigned)sy/8*(size/8)+(unsigned)sx/8)&0xffff);
        unsigned pixel=gba_vram8(g,(character+tile*64+((unsigned)sy&7)*8+((unsigned)sx&7))&0xffff);
        if(!pixel)continue;
        gba_scanline_palette_read(g,pixel,seen);
        out[x]=g->scanline_palette.color[pixel];g->object_window[x]=(uint8_t)rank;
    }
}
static void gba_scanline_objects(gba_t *g,gba_color_t *out,const uint32_t *objects,unsigned count,
    unsigned begin,unsigned end,unsigned display,uint32_t seen[4]) {
    if(!(display&0x1000))return;
    unsigned mosaic=gba_io16(g,0x4c);
    for(unsigned item=0;item<count;item++) {
        unsigned obj=objects[item]&127,y=(objects[item]>>7)&255,pixels=objects[item]>>15;
        unsigned a0=gba_object16(g,obj*8),a1=gba_object16(g,obj*8+2),a2=gba_object16(g,obj*8+4);
        unsigned affine=(a0>>8)&1;
        unsigned width=gba_object_dimensions[a0>>14][a1>>14][0];
        unsigned height=gba_object_dimensions[a0>>14][a1>>14][1];
        unsigned double_size=affine && (a0&0x200),draw_width=double_size?width*2:width,
                 draw_height=double_size?height*2:height;
        int32_t left=gba_sign(a1&511,9);
        unsigned first=left<(int32_t)begin?(unsigned)((int32_t)begin-left):0,last=pixels;
        if(left>=(int32_t)end)continue;
        if((int32_t)last>(int32_t)end-left)last=(unsigned)((int32_t)end-left);
        if(first>=last)continue;
        unsigned depth=(a0&0x2000)?2:1,tile_base=a2&1023;
        if(depth==2)tile_base&=~1u;
        unsigned palette=256+(depth==1?(a2>>12)*16:0),rank=((a2>>10)&3)*5;
        if(affine) {
            unsigned matrix=((a1>>9)&31)*32;
            int32_t pa=gba_sign(gba_object16(g,matrix+6),16);
            int32_t pb=gba_sign(gba_object16(g,matrix+14),16);
            int32_t pc=gba_sign(gba_object16(g,matrix+22),16);
            int32_t pd=gba_sign(gba_object16(g,matrix+30),16);
            for(unsigned dx=first;dx<last;dx++) {
                unsigned x=(unsigned)(left+(int32_t)dx);
                if(rank>=g->object_window[x])continue;
                unsigned mx=dx;
                if(a0&0x1000)mx-=mx%(((mosaic>>8)&15)+1);
                int32_t rx=(int32_t)mx-(int32_t)draw_width/2,ry=(int32_t)y-(int32_t)draw_height/2;
                int32_t sx=gba_fixed8(pa*rx+pb*ry)+(int32_t)width/2;
                int32_t sy=gba_fixed8(pc*rx+pd*ry)+(int32_t)height/2;
                if(sx<0 || sy<0 || (unsigned)sx>=width || (unsigned)sy>=height)continue;
                unsigned tile=tile_base;
                tile+=(unsigned)sy/8*((display&0x40)?width/8*depth:32)+(unsigned)sx/8*depth;
                tile&=1023;
                unsigned row=0x10000+((tile*32+((unsigned)sy&7)*(depth==2?8:4))&0x7fff);
                gba_video_read(g,6,row);
                const uint8_t *data=g->memory.vram+row;
                unsigned tx=(unsigned)sx&7;
                unsigned index=depth==2?data[tx]:(data[tx>>1]>>((tx&1)*4))&15;
                if(!index)continue;
                index+=palette;gba_scanline_palette_read(g,index,seen);
                out[x]=g->scanline_palette.color[index];g->object_window[x]=(uint8_t)rank;
            }
            continue;
        }
        unsigned flip=!!(a1&0x1000),sy=(a1&0x2000)?height-1-y:y;
        tile_base+=(sy>>3)*((display&0x40)?(width>>3)*depth:32);
        for(unsigned dx=first;dx<last;) {
            unsigned sx=flip?width-1-dx:dx,span=flip?(sx&7)+1:8-(sx&7);
            if(span>last-dx)span=last-dx;
            unsigned tile=(tile_base+(sx>>3)*depth)&1023;
            unsigned row=0x10000+((tile*32+(sy&7)*(depth==2?8:4))&0x7fff);
            const uint8_t *data=g->memory.vram+row;gba_video_read(g,6,row);
            if(GBA_PPU_SCANLINE_ZERO_ROWS && !gba_le32(data) && (depth==1 || !gba_le32(data+4))) {
                dx+=span;continue;
            }
            if(!GBA_PPU_SCANLINE_PALETTE_DEDUP)gba_memset(seen,0,4*sizeof(*seen));
            for(unsigned n=0;n<span;n++) {
                unsigned x=(unsigned)(left+(int32_t)(dx+n));
                if(rank>=g->object_window[x])continue;
                unsigned tx=flip?(sx-n)&7:(sx+n)&7;
                unsigned index=depth==2?data[tx]:(data[tx>>1]>>((tx&1)*4))&15;
                if(!index)continue;
                index+=palette;gba_scanline_palette_read(g,index,seen);
                out[x]=g->scanline_palette.color[index];g->object_window[x]=(uint8_t)rank;
            }
            dx+=span;
        }
    }
}
#if GBA_PPU_SCANLINE_EFFECTS
static void gba_scanline_brightness(gba_t *g,gba_color_t *out,unsigned begin,unsigned end,
    unsigned targets,const uint8_t shade[32]) {
    for(unsigned x=begin;x<end;x++) {
        unsigned rank=g->object_window[x];
        unsigned layer=rank==63?5:rank%5?rank%5-1:4;
        if(!(targets&(1u<<layer)))continue;
        gba_color_t value=out[x];
#if GBA_COLOR_DEPTH == 16
#if GBA_COLOR_SWAP
        value=(gba_color_t)((value<<8)|(value>>8));
#endif
        unsigned r=(value>>11)&31,green=(value>>6)&31,b=value&31;
#else
        /* The original 5-bit components survive expansion into the high five bits. */
        unsigned r=(value>>19)&31,green=(value>>11)&31,b=(value>>3)&31;
#endif
        out[x]=gba_color(shade[r]|((unsigned)shade[green]<<5)|((unsigned)shade[b]<<10));
    }
}
#endif
static int gba_scanline_fast(gba_t *g,gba_color_t *out,unsigned display) {
    unsigned mode=display&7;
    if(mode>1)GBA_SCANLINE_REJECT(0);
    if(display&0x8000)GBA_SCANLINE_REJECT(1);
    unsigned mosaic_reg=gba_io16(g,0x4c);
    unsigned effect=(gba_io16(g,0x50)>>6)&3;
    if(effect) {
        if(!GBA_PPU_SCANLINE_IDENTITY && !(GBA_PPU_SCANLINE_EFFECTS && effect>=2))GBA_SCANLINE_REJECT(2);
        unsigned alpha=gba_io16(g,0x52),y=gba_io16(g,0x54)&31;
        /* These register settings produce the first-layer color exactly.
         * Semi-transparent/affine/mosaic objects still fall back below. */
        if(effect==1) {if((alpha&31)<16 || ((alpha>>8)&31))GBA_SCANLINE_REJECT(3);}
        else if(y && !GBA_PPU_SCANLINE_EFFECTS)GBA_SCANLINE_REJECT(4);
    }
#if GBA_TEST_RENDER_REFERENCE
    if(gba_test_render_reference(g))GBA_SCANLINE_REJECT(5);
#endif
    unsigned active=0;
    for(unsigned bg=0;bg<4;bg++)if((display&(1u<<(8+bg))) && !g->machine.ppu.bg_delay[bg]) {
        if(mode==1 && bg==3)continue;
        if(mode==1 && bg==2 && (gba_io16(g,8+bg*2)&0x40) && (mosaic_reg&0xff))GBA_SCANLINE_REJECT(6);
        active|=1u<<bg;
    }
    uint32_t objects[128];unsigned count=gba_objects_select(g,objects);
    for(unsigned n=0;n<count;n++) {
        unsigned attributes=gba_object16(g,(objects[n]&127)*8);
        if(attributes&0x0c00)GBA_SCANLINE_REJECT(7);
        unsigned object_mode=(attributes>>10)&3;
        if(object_mode && !GBA_PPU_SCANLINE_IDENTITY)GBA_SCANLINE_REJECT(8);
        if((attributes&0x1000) && (!GBA_PPU_SCANLINE_IDENTITY || (gba_io16(g,0x4c)&0xff00)))GBA_SCANLINE_REJECT(9);
    }
    if(!g->scanline_palette.valid) {
        for(unsigned i=0;i<512;i++)g->scanline_palette.color[i]=gba_color(gba_le16(g->memory.palette+i*2)&0x7fff);
        g->scanline_palette.valid=1;
    }
#ifdef GBA_TEST_SCANLINE_COUNTERS
    gba_test_scanline_entries++;
#endif
    gba_video_read(g,5,0);
    /* No guest writes can occur while a row is rendered. A palette block
     * already registered for this row stays registered across tiles, layers
     * and window segments; keep the exact dependency union with one clear. */
    uint32_t seen[4];gba_memset(seen,0,sizeof(seen));
    for(unsigned x=0;x<GBA_WIDTH;x++)out[x]=g->scanline_palette.color[0];
    gba_memset(g->object_window,63,GBA_WIDTH);
#if GBA_PPU_SCANLINE_EFFECTS
    uint8_t shade[32];
    if(effect>=2) {
        unsigned amount=gba_io16(g,0x54)&31;if(amount>16)amount=16;
        for(unsigned n=0;n<32;n++)shade[n]=(uint8_t)(effect==2?n+((31-n)*amount>>4):n-(n*amount>>4));
    }
#endif
    unsigned edges[6],edge_count=2;edges[0]=0;edges[1]=GBA_WIDTH;
    for(unsigned w=0;w<2;w++)if((display&(1u<<(13+w))) && (g->machine.ppu.window_active&(1u<<w))) {
        unsigned h=gba_io16(g,0x40+w*2);
        unsigned begin=h>>8,end=h&255;
        if(begin<GBA_WIDTH)edges[edge_count++]=begin;
        if(end<GBA_WIDTH)edges[edge_count++]=end;
    }
    for(unsigned i=1;i<edge_count;i++)for(unsigned j=i;j && edges[j]<edges[j-1];j--) {
        unsigned value=edges[j];edges[j]=edges[j-1];edges[j-1]=value;
    }
    for(unsigned segment=0;segment+1<edge_count;segment++) {
        unsigned begin=edges[segment],end=edges[segment+1];if(begin==end)continue;
        unsigned mask=gba_window_mask(g,begin);
        for(unsigned bg=0;bg<4;bg++)if(active&mask&(1u<<bg)) {
            unsigned control=gba_io16(g,8+bg*2),rank=(control&3)*5+bg+1;
            if(mode==1 && bg==2)gba_scanline_affine(g,out,bg,begin,end,rank,seen);
            else if((control&0x40) && (mosaic_reg&0xff))gba_scanline_text_mosaic(g,out,bg,begin,end,(control&0x80)!=0,rank,seen);
#if GBA_PPU_SCANLINE_SPANS
            else if(control&0x80)gba_scanline_text8(g,out,bg,begin,end,rank,seen);
            else gba_scanline_text4(g,out,bg,begin,end,rank,seen);
#else
            else if(control&0x80)gba_scanline_text(g,out,bg,begin,end,1,rank,seen);
            else gba_scanline_text(g,out,bg,begin,end,0,rank,seen);
#endif
        }
        if(mask&16)gba_scanline_objects(g,out,objects,count,begin,end,display,seen);
#if GBA_PPU_SCANLINE_EFFECTS
        if(effect>=2 && (mask&32))gba_scanline_brightness(g,out,begin,end,gba_io16(g,0x50)&63,shade);
#endif
    }
    return 1;
}
#ifdef GBA_TEST_SCANLINE_COUNTERS
#undef GBA_SCANLINE_REJECT
#endif
#endif
