/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "gba_internal.h"
/* Pixel word: BGR555, layer 0..5, semitransparent flag, and ordering rank. */
#define GBA_PIXEL(color, layer, alpha, rank) ((color) | ((layer) << 16) | ((alpha) << 19) | ((rank) << 24))
static gba_color_t gba_color(unsigned c);
static void gba_pixel(gba_t *g, unsigned x, uint32_t pixel) {
    unsigned layer = (pixel >> 16) & 7, top_layer = (g->pixel_top[x] >> 16) & 7;
    if ((pixel >> 24) < (g->pixel_top[x] >> 24)) {
        if (layer != 4 || top_layer != 4) {
            g->pixel_second[x] = g->pixel_top[x];
        }
        g->pixel_top[x] = pixel;
    } else if ((layer != 4 || top_layer != 4) && (pixel >> 24) < (g->pixel_second[x] >> 24)) {
        g->pixel_second[x] = pixel;
    }
}
GBA_INLINE void gba_plot(gba_t *g,unsigned x,uint32_t pixel,unsigned replace) {
#if GBA_PPU_FRONT_CULL
    if(replace==2){if((g->pixel_top[x]>>24)==63)g->pixel_top[x]=pixel;return;}
#endif
#if GBA_PPU_SORTED_BLEND
    if(replace) {
        uint32_t previous=g->pixel_top[x];
        /* OBJ is one composed layer: a front OBJ replaces an older OBJ
         * without turning that older OBJ into an alpha blend target. */
        if(((pixel>>16)&7)!=4 || ((previous>>16)&7)!=4)g->pixel_second[x]=previous;
        g->pixel_top[x]=pixel;
        return;
    }
#elif GBA_PPU_ORDERED
    if(replace){g->pixel_top[x]=pixel;return;}
#else
    (void)replace;
#endif
    gba_pixel(g,x,pixel);
}
GBA_INLINE unsigned gba_front_covered(const gba_t *g,unsigned x,unsigned replace) {
#if GBA_PPU_FRONT_CULL
    return replace==2 && (g->pixel_top[x]>>24)!=63;
#else
    (void)g;(void)x;(void)replace;return 0;
#endif
}
#if GBA_TEST_RENDER_REFERENCE
extern int gba_test_render_reference(gba_t *g);
#endif
static int gba_window_range(unsigned coordinate, unsigned start, unsigned end) {
    return start > end ? coordinate >= start || coordinate < end : coordinate >= start && coordinate < end;
}
void gba_ppu_window_update(gba_t *g) {
    for (unsigned window = 0; window < 2; window++) {
        unsigned vertical = gba_io16(g, 0x44 + window * 2);
        if (g->machine.ppu.line == (vertical & 255)) {
            g->machine.ppu.window_active &= ~(1u << window);
        } else if (g->machine.ppu.line == (vertical >> 8)) {
            g->machine.ppu.window_active |= 1u << window;
        }
    }
}
static unsigned gba_window_mask(const gba_t *g, unsigned x) {
    unsigned display = gba_io16(g, 0);
    if (!(display & 0xe000)) {
        return 0x3f;
    }
    for (unsigned window = 0; window < 2; window++) {
        unsigned horizontal = gba_io16(g, 0x40 + window * 2);
        if ((display & (1u << (13 + window))) && (g->machine.ppu.window_active & (1u << window)) &&
            gba_window_range(x, horizontal >> 8, horizontal & 255)) {
            return (gba_io16(g, 0x48) >> (window * 8)) & 0x3f;
        }
    }
    return (gba_io16(g, 0x4a) >> (((display & 0x8000) && g->object_window[x]) ? 8 : 0)) & 0x3f;
}
static unsigned gba_palette(gba_t *g, unsigned entry) {
    gba_video_read(g, 5, (entry & 511) * 2);
    return gba_le16(g->memory.palette + (entry & 511) * 2) & 0x7fff;
}
static unsigned gba_vram16(gba_t *g, unsigned address) {
    address &= 0x1ffff;
    if (address >= 0x18000) {
        address -= 0x8000;
    }
    gba_video_read(g, 6, address & ~1u);
    return gba_le16(g->memory.vram + (address & ~1u));
}
static unsigned gba_vram8(gba_t *g, unsigned address) {
    gba_video_read(g, 6, address);
    return g->memory.vram[address];
}
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
static void gba_video_object_rows(gba_t *g, unsigned a0, unsigned a1) {
    unsigned shape = a0 >> 14;
    if (shape >= 3)
        return;
    static const unsigned height[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    unsigned h = height[shape][a1 >> 14] * (((a0 & 0x300) == 0x300) ? 2 : 1);
    uint32_t *rows = g->machine.ppu.oam_latch_valid ? g->video_cache.pending_oam : g->video_cache.dirty;
    for (unsigned i = 0; i < h; i++) {
        unsigned line = ((a0 & 255) + i) & 255;
        if (line < 160)
            rows[line >> 5] |= 1u << (line & 31);
    }
}
#endif
#if (GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK) || GBA_PPU_OAM_DIRTY || GBA_PPU_SCANLINE_FAST
void gba_video_write(gba_t *g, unsigned region, uint32_t address, unsigned width, uint32_t value) {
#if GBA_PPU_SCANLINE_FAST
    if(region==5 && g->scanline_palette.valid) {
        unsigned index=(address&0x3ff)>>1;
        g->scanline_palette.color[index]=gba_color(value&0x7fff);
        if(width==4)g->scanline_palette.color[(index+1)&511]=gba_color((value>>16)&0x7fff);
    }
#endif
#if GBA_PPU_OAM_DIRTY
    if(region==7) {
        unsigned word=(address&0x3ff)>>2;
        g->oam_dirty.words[word>>5]|=1u<<(word&31);
    }
#endif
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
    if (region == 7) {
        unsigned offset = address & 0x3ff, field = offset & 7;
        const uint8_t *p = g->memory.oam + (offset & ~7u);
        unsigned a0 = gba_le16(p), a1 = gba_le16(p + 2);
        unsigned matrix = field == 6 || (field == 4 && width == 4 && ((gba_le32(p + 4) ^ value) & 0xffff0000u));
        if (matrix) {
            uint32_t *rows = g->machine.ppu.oam_latch_valid ? g->video_cache.pending_oam : g->video_cache.dirty;
            for (unsigned i = 0; i < 5; i++)
                rows[i] = UINT32_MAX;
        } else {
            gba_video_object_rows(g, a0, a1);
            if (field == 0) {
                a0 = value & 0xffff;
                if (width == 4)
                    a1 = value >> 16;
            }
            if (field == 2)
                a1 = value & 0xffff;
            gba_video_object_rows(g, a0, a1);
        }
    } else {
        unsigned offset = address & (region == 5 ? 0x3ff : 0x1ffff);
        if (region == 6 && offset >= 0x18000)
            offset -= 0x8000;
        unsigned block = region == 5 ? 3072 + (offset >> 3) : (offset >> 5);
        for (unsigned i = 0; i < 5; i++)
            g->video_cache.dirty[i] |= g->video_cache.dependency[block][i];
    }
#else
    (void)g;(void)region;(void)address;(void)width;(void)value;
#endif
}
#endif
void gba_ppu_affine_write(gba_t *g, unsigned address) {
    for (unsigned i = 0; i < 2; i++) {
        unsigned base = 0x28 + i * 16;
        if (address == base || address == base + 2) {
            g->machine.ppu.ref_x[i] = (uint32_t)gba_sign(gba_io32(g, base), 28);
        }
        if (address == base + 4 || address == base + 6) {
            g->machine.ppu.ref_y[i] = (uint32_t)gba_sign(gba_io32(g, base + 4), 28);
        }
    }
}
static int gba_bg_text(gba_t *g, unsigned bg, unsigned x, unsigned *color) {
    unsigned control = gba_io16(g, 8 + bg * 2), mosaic = gba_io16(g, 0x4c);
    unsigned y = g->machine.ppu.line;
    if (control & 0x40) {
        x -= x % ((mosaic & 15) + 1);
        y -= y % (((mosaic >> 4) & 15) + 1);
    }
    unsigned size = control >> 14, width = (size & 1) ? 512 : 256, height = (size & 2) ? 512 : 256;
    x = (x + gba_io16(g, 0x10 + bg * 4)) & (width - 1);
    y = (y + gba_io16(g, 0x12 + bg * 4)) & (height - 1);
    unsigned screen = (control >> 8) & 31, block = (x >> 8) + (y >> 8) * (width / 256);
    unsigned entry = gba_vram16(g, screen * 0x800 + block * 0x800 + ((y & 255) / 8 * 32 + (x & 255) / 8) * 2);
    unsigned tx = x & 7, ty = y & 7;
    if (entry & 0x400) {
        tx = 7 - tx;
    }
    if (entry & 0x800) {
        ty = 7 - ty;
    }
    unsigned base = ((control >> 2) & 3) * 0x4000, index;
    if (control & 0x80) {
        index = gba_vram8(g, (base + (entry & 1023) * 64 + ty * 8 + tx) & 0xffff);
    } else {
        index = gba_vram8(g, (base + (entry & 1023) * 32 + ty * 4 + tx / 2) & 0xffff);
        index = (index >> ((tx & 1) * 4)) & 15;
        if (index) {
            index += (entry >> 12) * 16;
        }
    }
    if (!index) {
        return 0;
    }
    *color = gba_palette(g, index);
    return 1;
}
static void gba_bg_text_line(gba_t *g, unsigned bg, unsigned rank,unsigned replace) {
    unsigned control = gba_io16(g, 8 + bg * 2);
    if (control & 0x40) {
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            unsigned color;
            if (!gba_front_covered(g,x,replace) && (g->object_window[x] & (1u << bg)) && gba_bg_text(g, bg, x, &color)) {
                gba_plot(g, x, GBA_PIXEL(color, bg, 0u, rank),replace);
            }
        }
        return;
    }
    unsigned size = control >> 14, width = (size & 1) ? 512 : 256, height = (size & 2) ? 512 : 256;
    unsigned y = (g->machine.ppu.line + gba_io16(g, 0x12 + bg * 4)) & (height - 1);
    unsigned scroll = gba_io16(g, 0x10 + bg * 4), screen = ((control >> 8) & 31) * 0x800;
    unsigned base = ((control >> 2) & 3) * 0x4000;
    for (unsigned x = 0; x < GBA_WIDTH;) {
        unsigned sx = (x + scroll) & (width - 1), span = 8 - (sx & 7);
        if (span > GBA_WIDTH - x) {
            span = GBA_WIDTH - x;
        }
        unsigned block = (sx >> 8) + (y >> 8) * (width / 256);
        unsigned entry = gba_vram16(g, screen + block * 0x800 + ((y & 255) / 8 * 32 + (sx & 255) / 8) * 2);
        unsigned ty = (entry & 0x800) ? 7 - (y & 7) : y & 7;
        unsigned row = base + (entry & 1023) * ((control & 0x80) ? 64 : 32) + ty * ((control & 0x80) ? 8 : 4);
#if GBA_PPU_TILE_DIRECT
        /* A non-mosaic text row is aligned to four/eight bytes, wholly within
         * one protected 32-byte dependency block. Rendering cannot mutate
         * VRAM. Register this block once and load its live pixels directly. */
        gba_video_read(g,6,row&0xffff);
        const uint8_t *tile_row=g->memory.vram+(row&0xffff);
#endif
        for (unsigned i = 0; i < span; i++) {
            if (gba_front_covered(g,x+i,replace) || !(g->object_window[x + i] & (1u << bg))) {
                continue;
            }
            unsigned tx = (sx + i) & 7;
            if (entry & 0x400) {
                tx = 7 - tx;
            }
            unsigned pixel;
            if (control & 0x80) {
#if GBA_PPU_TILE_DIRECT
                pixel=tile_row[tx];
#else
                pixel = gba_vram8(g, (row + tx) & 0xffff);
#endif
            } else {
#if GBA_PPU_TILE_DIRECT
                pixel=(tile_row[tx/2]>>((tx&1)*4))&15;
#else
                pixel = (gba_vram8(g, (row + tx / 2) & 0xffff) >> ((tx & 1) * 4)) & 15;
#endif
                if (pixel) {
                    pixel += (entry >> 12) * 16;
                }
            }
            if (pixel) {
                gba_plot(g, x + i, GBA_PIXEL(gba_palette(g, pixel), bg, 0u, rank),replace);
            }
        }
        x += span;
    }
}
static int gba_bg_affine(gba_t *g, unsigned bg, unsigned x, unsigned mode, unsigned *color) {
    unsigned index = bg - 2, base = 0x20 + index * 16;
    unsigned control = gba_io16(g, 8 + bg * 2), mosaic = gba_io16(g, 0x4c);
    int32_t ref_x = gba_sign(g->machine.ppu.ref_x[index], 32), ref_y = gba_sign(g->machine.ppu.ref_y[index], 32);
    if (control & 0x40) {
        x -= x % ((mosaic & 15) + 1);
        unsigned off_y = g->machine.ppu.line % (((mosaic >> 4) & 15) + 1);
        ref_x = gba_sign((uint32_t)ref_x - (uint32_t)(gba_sign(gba_io16(g, base + 2), 16) * (int32_t)off_y), 32);
        ref_y = gba_sign((uint32_t)ref_y - (uint32_t)(gba_sign(gba_io16(g, base + 6), 16) * (int32_t)off_y), 32);
    }
    int32_t fx = gba_sign((uint32_t)ref_x + (uint32_t)(gba_sign(gba_io16(g, base), 16) * (int32_t)x), 32);
    int32_t fy = gba_sign((uint32_t)ref_y + (uint32_t)(gba_sign(gba_io16(g, base + 4), 16) * (int32_t)x), 32);
    int32_t sx = gba_fixed8(fx), sy = gba_fixed8(fy);
    unsigned size = 128u << (control >> 14), width = size, height = size;
    if (mode >= 3) {
        width = mode == 5 ? 160 : 240;
        height = mode == 5 ? 128 : 160;
    }
    if (mode < 3 && (control & 0x2000)) {
        sx = (int32_t)((uint32_t)sx & (size - 1));
        sy = (int32_t)((uint32_t)sy & (size - 1));
    }
    if (sx < 0 || sy < 0 || (uint32_t)sx >= width || (uint32_t)sy >= height) {
        return 0;
    }
    unsigned position = (unsigned)sy * width + (unsigned)sx;
    if (mode >= 3) {
        unsigned page = (mode != 3 && (gba_io16(g, 0) & 0x10)) ? 0xa000 : 0;
        if (mode == 4) {
            unsigned pixel = gba_vram8(g, page + position);
            if (!pixel) {
                return 0;
            }
            *color = gba_palette(g, pixel);
        } else {
            *color = gba_vram16(g, page + position * 2) & 0x7fff;
        }
        return 1;
    }
    unsigned screen = ((control >> 8) & 31) * 0x800;
    unsigned tile = gba_vram8(g, (screen + (unsigned)sy / 8 * (size / 8) + (unsigned)sx / 8) & 0xffff);
    unsigned character = ((control >> 2) & 3) * 0x4000;
    unsigned pixel = gba_vram8(g, (character + tile * 64 + ((unsigned)sy & 7) * 8 + ((unsigned)sx & 7)) & 0xffff);
    if (!pixel) {
        return 0;
    }
    *color = gba_palette(g, pixel);
    return 1;
}
#if GBA_PPU_OBJ_INDEX
static void gba_object_index_edit(gba_t *g,unsigned obj,uint32_t attributes,unsigned add);
#endif
GBA_INLINE void gba_ppu_oam_latch_word(gba_t *g,unsigned word) {
    uint32_t value=gba_le32(g->memory.oam + word * 4);
#if GBA_PPU_OBJ_INDEX
        /* Horizontal position, flip, depth and mosaic do not change the
         * vertical candidate set. Attributes are still latched in full. */
        if (!(word&1) && g->object_index.valid && ((value^g->machine.ppu.oam_latch[word])&0xc000cfff)) {
            gba_object_index_edit(g,word/2,g->machine.ppu.oam_latch[word],0);
            gba_object_index_edit(g,word/2,value,1);
        }
#endif
    g->machine.ppu.oam_latch[word] = value;
}
void gba_ppu_oam_latch(gba_t *g) {
#if GBA_PPU_OAM_DIRTY
    unsigned sparse=g->oam_dirty.coherent && g->machine.ppu.oam_latch_valid;
#if GBA_TEST_RENDER_REFERENCE
    if(gba_test_render_reference(g))sparse=0;
#endif
    if(sparse) {
        for(unsigned bank=0;bank<8;bank++) {
            uint32_t bits=g->oam_dirty.words[bank];
            for(unsigned bit=0;bits;bit++,bits>>=1)
                if(bits&1)gba_ppu_oam_latch_word(g,bank*32+bit);
        }
#ifdef GBA_TEST_OAM_DIRTY_PROOF
        extern unsigned gba_test_oam_dirty_entries;gba_test_oam_dirty_entries++;
#endif
    } else
#endif
    {
        for (unsigned word = 0; word < 256; word++)gba_ppu_oam_latch_word(g,word);
    }
#if GBA_PPU_OAM_DIRTY
    gba_memset(g->oam_dirty.words,0,sizeof(g->oam_dirty.words));
    g->oam_dirty.coherent=1;
#endif
    g->machine.ppu.oam_latch_valid = 1;
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
    g->video_cache.latched_oam_generation = g->video_cache.oam_generation;
    for (unsigned i = 0; i < 5; i++) {
        g->video_cache.dirty[i] |= g->video_cache.pending_oam[i];
        g->video_cache.pending_oam[i] = 0;
    }
#endif
}
static void gba_bg_affine_line(gba_t *g, unsigned bg, unsigned mode, unsigned rank,unsigned replace) {
    unsigned index = bg - 2, registers = 0x20 + index * 16, control = gba_io16(g, 8 + bg * 2);
    if (control & 0x40) {
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            unsigned color;
            if (!gba_front_covered(g,x,replace) && (g->object_window[x] & (1u << bg)) && gba_bg_affine(g, bg, x, mode, &color)) {
                gba_plot(g, x, GBA_PIXEL(color, bg, 0u, rank),replace);
            }
        }
        return;
    }
    uint32_t fx = g->machine.ppu.ref_x[index], fy = g->machine.ppu.ref_y[index];
    uint32_t step_x = (uint32_t)gba_sign(gba_io16(g, registers), 16);
    uint32_t step_y = (uint32_t)gba_sign(gba_io16(g, registers + 4), 16);
    unsigned size = 128u << (control >> 14), width = size, height = size;
    unsigned screen = ((control >> 8) & 31) * 0x800, character = ((control >> 2) & 3) * 0x4000;
    unsigned page = (mode != 3 && (gba_io16(g, 0) & 0x10)) ? 0xa000 : 0;
    if (mode >= 3) {
        width = mode == 5 ? 160 : 240;
        height = mode == 5 ? 128 : 160;
    }
    for (unsigned x = 0; x < GBA_WIDTH; x++, fx += step_x, fy += step_y) {
        if (gba_front_covered(g,x,replace) || !(g->object_window[x] & (1u << bg))) {
            continue;
        }
        int32_t sx = gba_fixed8(gba_sign(fx, 32)), sy = gba_fixed8(gba_sign(fy, 32));
        if (mode < 3 && (control & 0x2000)) {
            sx = (int32_t)((uint32_t)sx & (size - 1));
            sy = (int32_t)((uint32_t)sy & (size - 1));
        }
        if (sx < 0 || sy < 0 || (uint32_t)sx >= width || (uint32_t)sy >= height) {
            continue;
        }
        unsigned color, position = (unsigned)sy * width + (unsigned)sx;
        if (mode >= 3) {
            if (mode == 4) {
                unsigned pixel = gba_vram8(g, page + position);
                if (!pixel) {
                    continue;
                }
                color = gba_palette(g, pixel);
            } else {
                color = gba_vram16(g, page + position * 2) & 0x7fff;
            }
        } else {
            unsigned tile = gba_vram8(g, (screen + (unsigned)sy / 8 * (size / 8) + (unsigned)sx / 8) & 0xffff);
            unsigned pixel =
                gba_vram8(g, (character + tile * 64 + ((unsigned)sy & 7) * 8 + ((unsigned)sx & 7)) & 0xffff);
            if (!pixel) {
                continue;
            }
            color = gba_palette(g, pixel);
        }
        gba_plot(g, x, GBA_PIXEL(color, bg, 0u, rank),replace);
    }
}
static unsigned gba_object16(const gba_t *g, unsigned address) {
    if (!g->machine.ppu.oam_latch_valid) {
        return gba_le16(g->memory.oam + address);
    }
    return (g->machine.ppu.oam_latch[address / 4] >> ((address & 2) * 8)) & 0xffff;
}
static const unsigned gba_object_dimensions[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}}, {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
#if GBA_PPU_OBJ_INDEX
/* Index only latched OAM. Live OAM before the first latch retains the scalar
 * path, so ordinary writes cannot leave a stale derived index. Eight-line
 * bands bound rebuild/update work even for 128-pixel affine objects. */
static void gba_object_index_edit(gba_t *g,unsigned obj,uint32_t attributes,unsigned add) {
    unsigned a0=attributes&0xffff,a1=attributes>>16,shape=a0>>14;
    unsigned affine=(a0>>8)&1;
    if(shape==3 || ((a0>>10)&3)==3 || (!affine && (a0&0x200)))return;
    unsigned height=gba_object_dimensions[shape][a1>>14][1];
    if(affine && (a0&0x200))height*=2;
    unsigned y=a0&255,word=obj>>5,mask=1u<<(obj&31);
    for(unsigned consumed=0;consumed<height;) {
        unsigned line=(y+consumed)&255,span=8-(line&7);
        if(line<160) {
            uint32_t *bits=&g->object_index.band[line>>3][word];
            if(add)*bits|=mask;else *bits&=~mask;
        }
        consumed+=span;
    }
}
static void gba_object_index_prepare(gba_t *g) {
    if(g->object_index.valid)return;
    gba_memset(g->object_index.band,0,sizeof(g->object_index.band));
    for(unsigned obj=0;obj<128;obj++)
        gba_object_index_edit(g,obj,g->machine.ppu.oam_latch[obj*2],1);
    g->object_index.valid=1;
}
GBA_INLINE unsigned gba_object_first_bit(uint32_t bits) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz(bits);
#else
    unsigned bit=0;while(!(bits&1)){bits>>=1;bit++;}return bit;
#endif
}
#endif
static unsigned gba_objects_select(gba_t *g, uint32_t *objects) {
    unsigned display = gba_io16(g, 0), budget = (display & 0x20) ? 954 : 1210, count = 0;
    if (!(display & 0x9000)) {
        return 0;
    }
    unsigned line = g->machine.ppu.line;
#if GBA_PPU_OBJ_INDEX
    unsigned indexed=g->machine.ppu.oam_latch_valid && line<160;
#if GBA_TEST_RENDER_REFERENCE
    if(gba_test_render_reference(g))indexed=0;
#endif
    if(indexed) {
        gba_object_index_prepare(g);
#ifdef GBA_TEST_OBJ_INDEX_PROOF
        extern unsigned gba_test_obj_index_entries;gba_test_obj_index_entries++;
#endif
        for(unsigned word=0;word<4 && budget;word++) {
            uint32_t bits=g->object_index.band[line>>3][word];
            while(bits && budget) {
                unsigned obj=word*32+gba_object_first_bit(bits);bits&=bits-1;
                unsigned a0=gba_object16(g,obj*8),a1=gba_object16(g,obj*8+2);
                unsigned affine=(a0>>8)&1,shape=a0>>14;
                unsigned width=gba_object_dimensions[shape][a1>>14][0];
                unsigned height=gba_object_dimensions[shape][a1>>14][1];
                unsigned double_size=affine && (a0&0x200);
                unsigned y=(line-(a0&255))&255;
                if(y>=(double_size?height*2:height))continue;
                unsigned draw_width=double_size?width*2:width;
                unsigned cost=affine?10+draw_width*2:draw_width;
                unsigned available=budget<cost?budget:cost;
                budget=budget>cost?budget-cost:0;
                unsigned pixels=affine?(available>10?(available-10)/2:0):available;
                objects[count++]=obj|(y<<7)|(pixels<<15);
            }
        }
        return count;
    }
#endif
    for (unsigned obj = 0; obj < 128 && budget; obj++) {
        unsigned a0 = gba_object16(g, obj * 8), a1 = gba_object16(g, obj * 8 + 2);
        unsigned shape = a0 >> 14, affine = (a0 >> 8) & 1, object_mode = (a0 >> 10) & 3;
        if (shape == 3 || object_mode == 3 || (!affine && (a0 & 0x200))) {
            continue;
        }
        unsigned width = gba_object_dimensions[shape][a1 >> 14][0];
        unsigned height = gba_object_dimensions[shape][a1 >> 14][1];
        unsigned double_size = affine && (a0 & 0x200);
        unsigned draw_width = double_size ? width * 2 : width, draw_height = double_size ? height * 2 : height;
        unsigned y = (line - (a0 & 255)) & 255;
        if (y >= draw_height) {
            continue;
        }
        unsigned cost = affine ? 10 + draw_width * 2 : draw_width;
        unsigned available = budget < cost ? budget : cost;
        budget = budget > cost ? budget - cost : 0;
        unsigned pixels = affine ? (available > 10 ? (available - 10) / 2 : 0) : available;
        objects[count++] = obj | (y << 7) | (pixels << 15);
    }
    return count;
}
#if GBA_PPU_OBJ_SPANS
/* Ordinary sprites have a constant source row. Walk eight-pixel tile spans,
 * preserving the object pixel budget, horizontal/vertical flips, OBJ window
 * and blend order. Affine/mosaic sprites keep the general renderer. */
static void gba_object_spans(gba_t *g,unsigned a0,unsigned a1,unsigned a2,unsigned width,unsigned height,
    unsigned y,unsigned pixels,unsigned display,int32_t x_start,int window_pass,unsigned replace) {
#ifdef GBA_TEST_OBJ_SPAN_PROOF
    extern unsigned gba_test_obj_span_entries;
    gba_test_obj_span_entries++;
#endif
    unsigned begin=x_start<0?(unsigned)-x_start:0,end=pixels;
    if(x_start>=GBA_WIDTH)return;
    if((int32_t)end>GBA_WIDTH-x_start)end=(unsigned)(GBA_WIDTH-x_start);
    if(begin>=end)return;
    unsigned flip=!!(a1&0x1000),sy=(a1&0x2000)?height-1-y:y;
    unsigned depth=(a0&0x2000)?2:1,tile_base=a2&1023;
    if(depth==2)tile_base&=~1u;
    tile_base+=(sy>>3)*((display&0x40)?(width>>3)*depth:32);
    unsigned palette=256+(depth==1?(a2>>12)*16:0);
    uint32_t tag=GBA_PIXEL(0u,4u,((a0>>10)&3)==1,((a2>>10)&3)*5);
    for(unsigned dx=begin;dx<end;) {
        unsigned sx=flip?width-1-dx:dx,span=flip?(sx&7)+1:8-(sx&7);
        if(span>end-dx)span=end-dx;
        unsigned tile=(tile_base+(sx>>3)*depth)&1023;
        if((display&7)<3 || tile>=512) {
            unsigned row=0x10000+((tile*32+(sy&7)*(depth==2?8:4))&0x7fff);
            gba_video_read(g,6,row);
            const uint8_t *data=g->memory.vram+row;
            for(unsigned n=0;n<span;n++) {
                unsigned x=(unsigned)(x_start+(int32_t)(dx+n));
                if(!window_pass && gba_front_covered(g,x,replace))continue;
                unsigned tx=flip?(sx-n)&7:(sx+n)&7;
                unsigned pixel=depth==2?data[tx]:(data[tx>>1]>>((tx&1)*4))&15;
                if(!pixel)continue;
                if(window_pass)g->object_window[x]=1;
                else if(g->object_window[x]&16)gba_plot(g,x,tag|gba_palette(g,palette+pixel),replace);
            }
        }
        dx+=span;
    }
}
#endif
static void gba_objects(gba_t *g, int window_pass, const uint32_t *objects, unsigned count,unsigned priority) {
    unsigned display = gba_io16(g, 0), mosaic = gba_io16(g, 0x4c);
    unsigned front=GBA_PPU_FRONT_CULL && priority>=8;
    if(front)priority&=3;
    if (window_pass ? !(display & 0x8000) : !(display & 0x1000)) {
        return;
    }
#if !GBA_PPU_ORDERED && !GBA_PPU_SORTED_BLEND
    (void)priority;
#endif
    for (unsigned iteration = 0; iteration < count; iteration++) {
        unsigned item=iteration;
#if GBA_PPU_ORDERED || GBA_PPU_SORTED_BLEND || GBA_PPU_FRONT_CULL
        if(priority<4 && !front)item=count-1-iteration;
#endif
        unsigned obj = objects[item] & 127, y = (objects[item] >> 7) & 255, pixels = objects[item] >> 15;
        unsigned a0 = gba_object16(g, obj * 8), a1 = gba_object16(g, obj * 8 + 2), a2 = gba_object16(g, obj * 8 + 4);
        unsigned shape = a0 >> 14, affine = (a0 >> 8) & 1, object_mode = (a0 >> 10) & 3;
#if GBA_PPU_ORDERED || GBA_PPU_SORTED_BLEND || GBA_PPU_FRONT_CULL
        if(priority<4 && ((a2>>10)&3)!=priority)continue;
#endif
        if ((object_mode == 2) != window_pass) {
            continue;
        }
        unsigned width = gba_object_dimensions[shape][a1 >> 14][0], height = gba_object_dimensions[shape][a1 >> 14][1];
        unsigned double_size = affine && (a0 & 0x200), draw_width = double_size ? width * 2 : width,
                 draw_height = double_size ? height * 2 : height;
        int32_t x_start = gba_sign(a1 & 511, 9);
        if (a0 & 0x1000) {
            y -= y % (((mosaic >> 12) & 15) + 1);
        }
#if GBA_PPU_OBJ_SPANS
        unsigned spans=!affine && !(a0&0x1000);
#if GBA_TEST_RENDER_REFERENCE
        if(gba_test_render_reference(g))spans=0;
#endif
        if(spans) {
            gba_object_spans(g,a0,a1,a2,width,height,y,pixels,display,x_start,window_pass,front?2:priority<4);
            continue;
        }
#endif
        int32_t pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            unsigned matrix = ((a1 >> 9) & 31) * 32;
            pa = gba_sign(gba_object16(g, matrix + 6), 16);
            pb = gba_sign(gba_object16(g, matrix + 14), 16);
            pc = gba_sign(gba_object16(g, matrix + 22), 16);
            pd = gba_sign(gba_object16(g, matrix + 30), 16);
        }
        for (unsigned dx = 0; dx < pixels; dx++) {
            int32_t out_x = x_start + (int32_t)dx;
            if (out_x < 0 || out_x >= GBA_WIDTH) {
                continue;
            }
            if(!window_pass && gba_front_covered(g,(unsigned)out_x,front?2:0))continue;
            unsigned mx = dx;
            if (a0 & 0x1000) {
                mx -= mx % (((mosaic >> 8) & 15) + 1);
            }
            int32_t sx, sy;
            if (affine) {
                int32_t rx = (int32_t)mx - (int32_t)draw_width / 2, ry = (int32_t)y - (int32_t)draw_height / 2;
                sx = gba_fixed8(pa * rx + pb * ry);
                sy = gba_fixed8(pc * rx + pd * ry);
                sx += (int32_t)width / 2;
                sy += (int32_t)height / 2;
            } else {
                sx = (a1 & 0x1000) ? (int32_t)width - 1 - (int32_t)mx : (int32_t)mx;
                sy = (a1 & 0x2000) ? (int32_t)height - 1 - (int32_t)y : (int32_t)y;
            }
            if (sx < 0 || sy < 0 || (unsigned)sx >= width || (unsigned)sy >= height) {
                continue;
            }
            unsigned depth = (a0 & 0x2000) ? 2 : 1, tile = a2 & 1023;
            if (depth == 2) {
                tile &= ~1u;
            }
            tile += (unsigned)sy / 8 * ((display & 0x40) ? width / 8 * depth : 32) + (unsigned)sx / 8 * depth;
            tile &= 1023;
            if ((display & 7) >= 3 && tile < 512) {
                continue;
            }
            unsigned address = 0x10000 + tile * 32 + ((unsigned)sy & 7) * (depth == 2 ? 8 : 4) +
                               ((unsigned)sx & 7) / (depth == 2 ? 1 : 2);
            unsigned pixel = gba_vram8(g, 0x10000 + ((address - 0x10000) & 0x7fff));
            if (depth == 1) {
                pixel = (pixel >> (((unsigned)sx & 1) * 4)) & 15;
            }
            if (!pixel) {
                continue;
            }
            if (window_pass) {
                g->object_window[out_x] = 1;
                continue;
            }
            if (!(g->object_window[out_x] & 16)) {
                continue;
            }
            unsigned palette = 256 + pixel + (depth == 1 ? (a2 >> 12) * 16 : 0);
            gba_plot(g,(unsigned)out_x,GBA_PIXEL(gba_palette(g,palette),4u,object_mode==1,((a2>>10)&3)*5),front?2:priority<4);
        }
    }
}
static unsigned gba_effect(gba_t *g, unsigned x) {
    uint32_t top = g->pixel_top[x], second = g->pixel_second[x];
    unsigned color = top & 0x7fff, layer = (top >> 16) & 7, second_layer = (second >> 16) & 7;
    unsigned control = gba_io16(g, 0x50), effect = (control >> 6) & 3;
    if (!(g->object_window[x] & 32)) {
        return color;
    }
    int second_target = control & (1u << (8 + second_layer));
    if ((top & (1u << 19)) && second_target) {
        effect = 1;
    } else if (!(control & (1u << layer))) {
        return color;
    }
    unsigned result = 0, alpha = gba_io16(g, 0x52), brightness = gba_io16(g, 0x54) & 31;
    unsigned eva = alpha & 31, evb = (alpha >> 8) & 31;
    if (eva > 16) {
        eva = 16;
    }
    if (evb > 16) {
        evb = 16;
    }
    if (brightness > 16) {
        brightness = 16;
    }
    for (unsigned component = 0; component < 3; component++) {
        unsigned shift = component * 5, value = (color >> shift) & 31;
        if (effect == 1 && second_target) {
            value = (value * eva + ((second >> shift) & 31) * evb) / 16;
        } else if (effect == 2) {
            value += (31 - value) * brightness / 16;
        } else if (effect == 3) {
            value -= value * brightness / 16;
        }
        if (value > 31) {
            value = 31;
        }
        result |= value << shift;
    }
    return result;
}
static gba_color_t gba_color(unsigned c) {
    unsigned r = c & 31, green = (c >> 5) & 31, b = (c >> 10) & 31;
#if GBA_COLOR_DEPTH == 16
    uint16_t out = (uint16_t)((r << 11) | (green << 6) | (green >> 4 << 5) | b);
#if GBA_COLOR_SWAP
    out = (uint16_t)((out << 8) | (out >> 8));
#endif
    return out;
#else
    return 0xff000000u | ((r * 255 / 31) << 16) | ((green * 255 / 31) << 8) | (b * 255 / 31);
#endif
}
#include "gba_ppu_scanline.h"
void gba_ppu_render(gba_t *g) {
    unsigned line = g->machine.ppu.line, display = gba_io16(g, 0), mode = display & 7;
    gba_color_t *out = g->memory.framebuffer + line % GBA_DRAW_LINES * GBA_WIDTH;
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
    /* Exact keys, not probabilistic hashes: each retained row depends on all
     * rendering registers, reference coordinates/delays/windows and the two
     * video memory generations. OAM's latch generation differs from live RAM. */
    gba_video_row_t key = {0};
    key.valid = 1;
    key.generation = 0;
    key.latch_valid = g->machine.ppu.oam_latch_valid;
    /* An explicit invalidation can happen while old OAM is still latched.
     * Its synchronization tag must remain distinct from the next live latch. */
    key.oam_generation = key.latch_valid ? g->video_cache.latched_oam_generation : g->video_cache.oam_generation;
    for (unsigned i = 0; i < 2; i++) {
        key.refs[i * 2] = g->machine.ppu.ref_x[i];
        key.refs[i * 2 + 1] = g->machine.ppu.ref_y[i];
    }
    for (unsigned i = 0; i < 4; i++)
        key.bg_delay[i] = g->machine.ppu.bg_delay[i];
    key.window_active = g->machine.ppu.window_active;
    gba_memcpy(key.io, g->io, sizeof(key.io));
    const uint8_t *old = (const uint8_t *)&g->video_cache.row[line], *current = (const uint8_t *)&key;
    unsigned same = 1;
    for (size_t i = 0; i < sizeof(key); i++)
        if (old[i] != current[i]) {
            same = 0;
            break;
        }
    unsigned row_word = line >> 5, row_mask = 1u << (line & 31);
    if (same && !(g->video_cache.dirty[row_word] & row_mask))
        goto ppu_draw;
    g->video_cache.dirty[row_word] &= ~row_mask;
    g->video_cache.active_word = row_word;
    g->video_cache.active_mask = row_mask;
#if GBA_PPU_READ_DEDUP
    g->video_cache.last_vram_block=g->video_cache.last_palette_block=UINT32_MAX;
#endif
    if (g->video_cache.framebuffer_revision != UINT64_MAX)
        g->video_cache.framebuffer_revision++;
    g->video_cache.row[line] = key;
#endif
#if GBA_PPU_SCANLINE_FAST
    if(!(display&0x80) && gba_scanline_fast(g,out,display))goto ppu_color_swap;
#endif
    if (display & 0x80) {
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            out[x] = gba_color(0x7fff);
        }
    } else {
        unsigned backdrop = GBA_PIXEL(gba_palette(g, 0), 5u, 0u, 63u);
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            g->pixel_top[x] = g->pixel_second[x] = backdrop;
            g->object_window[x] = 0;
        }
        uint32_t objects[128];
        unsigned object_count = gba_objects_select(g, objects);
        gba_objects(g, 1, objects, object_count,4);
        /* The window pass first produces coverage bits. Reuse the same scratch
         * bytes for the resolved mask; no render scratch belongs to save states. */
        if (display & 0xe000) {
            for (unsigned x = 0; x < GBA_WIDTH; x++) {
                g->object_window[x] = (uint8_t)gba_window_mask(g, x);
            }
        } else {
            gba_memset(g->object_window, 0x3f, GBA_WIDTH);
        }
#if GBA_PPU_FRONT_CULL
        unsigned front=(gba_io16(g,0x50)&0xc0)!=0x40;
        if(front && (display&0x1000))for(unsigned n=0;n<object_count;n++) {
            if((gba_object16(g,(objects[n]&127)*8)&0xc00)==0x400){front=0;break;}
        }
#if GBA_TEST_RENDER_REFERENCE
        if(gba_test_render_reference(g))front=0;
#endif
        if(front) {
#ifdef GBA_TEST_FRONT_CULL_PROOF
            extern unsigned gba_test_front_cull_entries;gba_test_front_cull_entries++;
#endif
            for(unsigned priority=0;priority<4;priority++) {
                gba_objects(g,0,objects,object_count,8+priority);
                for(unsigned bg=0;bg<4;bg++) {
                    if(!(display&(1u<<(8+bg))) || g->machine.ppu.bg_delay[bg] || mode>5 ||
                        (mode==1 && bg==3)||(mode==2 && bg<2)||(mode>=3 && bg!=2)||
                        (gba_io16(g,8+bg*2)&3)!=priority)continue;
                    unsigned rank=priority*5+bg+1;
                    if(mode==0 || (mode==1 && bg<2))gba_bg_text_line(g,bg,rank,2);
                    else gba_bg_affine_line(g,bg,mode,rank,2);
                }
            }
        } else
#endif
        {
#if GBA_PPU_ORDERED || GBA_PPU_SORTED_BLEND
        unsigned active=0,priorities[4]={0};
        for(unsigned bg=0;bg<4;bg++) {
            if (!(display & (1u << (8 + bg))) || g->machine.ppu.bg_delay[bg] || mode > 5 || (mode == 1 && bg == 3) ||
                (mode == 2 && bg < 2) || (mode >= 3 && bg != 2))continue;
            active|=1u<<bg;priorities[bg]=gba_io16(g,8+bg*2)&3;
        }
        unsigned ordered=0;
#if GBA_PPU_SORTED_BLEND
        ordered=1;
#else
        ordered=(gba_io16(g,0x50)&0xc0)!=0x40;
        if(ordered && (display&0x1000))for(unsigned item=0;item<object_count;item++) {
            unsigned obj=objects[item]&127;
            if((gba_object16(g,obj*8)&0xc00)==0x400){ordered=0;break;}
        }
#endif
#if GBA_TEST_RENDER_REFERENCE
        if(gba_test_render_reference(g))ordered=0;
#endif
        if(ordered) {
            /* Back-to-front priorities remove depth comparisons. The blend
             * path retains the distinct second layer; reverse OAM keeps the
             * lower object index on top without a tie comparison per pixel. */
            for(unsigned priority=4;priority--;) {
                for(unsigned bg=4;bg--;)if((active&(1u<<bg)) && priorities[bg]==priority) {
                    unsigned rank=priority*5+bg+1;
                    if(mode==0 || (mode==1 && bg<2))gba_bg_text_line(g,bg,rank,1);
                    else gba_bg_affine_line(g,bg,mode,rank,1);
                }
                gba_objects(g,0,objects,object_count,priority);
            }
        } else {
            for(unsigned bg=0;bg<4;bg++)if(active&(1u<<bg)) {
                unsigned rank=priorities[bg]*5+bg+1;
                if(mode==0 || (mode==1 && bg<2))gba_bg_text_line(g,bg,rank,0);
                else gba_bg_affine_line(g,bg,mode,rank,0);
            }
            gba_objects(g,0,objects,object_count,4);
        }
#else
        for (unsigned bg = 0; bg < 4; bg++) {
            if (!(display & (1u << (8 + bg))) || g->machine.ppu.bg_delay[bg] || mode > 5 || (mode == 1 && bg == 3) ||
                (mode == 2 && bg < 2) || (mode >= 3 && bg != 2)) {
                continue;
            }
            unsigned rank = (gba_io16(g, 8 + bg * 2) & 3) * 5 + bg + 1;
            if (mode == 0 || (mode == 1 && bg < 2)) {
                gba_bg_text_line(g, bg, rank,0);
                continue;
            }
            gba_bg_affine_line(g, bg, mode, rank,0);
        }
        gba_objects(g, 0, objects, object_count,4);
#endif
        }
        unsigned effect = gba_io16(g, 0x50);
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            unsigned color =
                (!(effect & 0xc0) && !(g->pixel_top[x] & (1u << 19))) ? g->pixel_top[x] & 0x7fff : gba_effect(g, x);
            out[x] = gba_color(color);
        }
    }
#if GBA_PPU_SCANLINE_FAST
ppu_color_swap:
#endif
    if (gba_io16(g, 2) & 1) {
        for (unsigned x = 0; x < GBA_WIDTH; x += 2) {
#if GBA_COLOR_DEPTH == 16
            gba_color_t mask = GBA_COLOR_SWAP ? 0xe007u : 0x07e0u;
#else
            gba_color_t mask = 0x0000ff00u;
#endif
            gba_color_t left = out[x], right = out[x + 1];
            out[x] = (left & ~mask) | (right & mask);
            out[x + 1] = (right & ~mask) | (left & mask);
        }
    }
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
ppu_draw:
    g->video_cache.active_mask = 0;
#endif
    if (line % GBA_DRAW_LINES == GBA_DRAW_LINES - 1) {
        if (gba_draw(0, (int)(line + 1 - GBA_DRAW_LINES), GBA_WIDTH - 1, (int)line, g->memory.framebuffer)) {
            gba_fail(g, GBA_ERROR_IO);
        }
    }
}
