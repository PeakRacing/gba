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
static unsigned gba_palette(const gba_t *g, unsigned entry) {
    return gba_le16(g->memory.palette + (entry & 511) * 2) & 0x7fff;
}
static unsigned gba_vram16(const gba_t *g, unsigned address) {
    address &= 0x1ffff;
    if (address >= 0x18000) {
        address -= 0x8000;
    }
    return gba_le16(g->memory.vram + (address & ~1u));
}
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
        index = g->memory.vram[(base + (entry & 1023) * 64 + ty * 8 + tx) & 0xffff];
    } else {
        index = g->memory.vram[(base + (entry & 1023) * 32 + ty * 4 + tx / 2) & 0xffff];
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
static void gba_bg_text_line(gba_t *g, unsigned bg, unsigned rank) {
    unsigned control = gba_io16(g, 8 + bg * 2);
    if (control & 0x40) {
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            unsigned color;
            if ((g->object_window[x] & (1u << bg)) && gba_bg_text(g, bg, x, &color)) {
                gba_pixel(g, x, GBA_PIXEL(color, bg, 0u, rank));
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
        for (unsigned i = 0; i < span; i++) {
            if (!(g->object_window[x + i] & (1u << bg))) {
                continue;
            }
            unsigned tx = (sx + i) & 7;
            if (entry & 0x400) {
                tx = 7 - tx;
            }
            unsigned pixel;
            if (control & 0x80) {
                pixel = g->memory.vram[(row + tx) & 0xffff];
            } else {
                pixel = (g->memory.vram[(row + tx / 2) & 0xffff] >> ((tx & 1) * 4)) & 15;
                if (pixel) {
                    pixel += (entry >> 12) * 16;
                }
            }
            if (pixel) {
                gba_pixel(g, x + i, GBA_PIXEL(gba_palette(g, pixel), bg, 0u, rank));
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
            unsigned pixel = g->memory.vram[page + position];
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
    unsigned tile = g->memory.vram[(screen + (unsigned)sy / 8 * (size / 8) + (unsigned)sx / 8) & 0xffff];
    unsigned character = ((control >> 2) & 3) * 0x4000;
    unsigned pixel = g->memory.vram[(character + tile * 64 + ((unsigned)sy & 7) * 8 + ((unsigned)sx & 7)) & 0xffff];
    if (!pixel) {
        return 0;
    }
    *color = gba_palette(g, pixel);
    return 1;
}
void gba_ppu_oam_latch(gba_t *g) {
    for (unsigned word = 0; word < 256; word++) {
        g->machine.ppu.oam_latch[word] = gba_le32(g->memory.oam + word * 4);
    }
    g->machine.ppu.oam_latch_valid = 1;
}
static void gba_bg_affine_line(gba_t *g, unsigned bg, unsigned mode, unsigned rank) {
    unsigned index = bg - 2, registers = 0x20 + index * 16, control = gba_io16(g, 8 + bg * 2);
    if (control & 0x40) {
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            unsigned color;
            if ((g->object_window[x] & (1u << bg)) && gba_bg_affine(g, bg, x, mode, &color)) {
                gba_pixel(g, x, GBA_PIXEL(color, bg, 0u, rank));
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
        if (!(g->object_window[x] & (1u << bg))) {
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
                unsigned pixel = g->memory.vram[page + position];
                if (!pixel) {
                    continue;
                }
                color = gba_palette(g, pixel);
            } else {
                color = gba_vram16(g, page + position * 2) & 0x7fff;
            }
        } else {
            unsigned tile = g->memory.vram[(screen + (unsigned)sy / 8 * (size / 8) + (unsigned)sx / 8) & 0xffff];
            unsigned pixel =
                g->memory.vram[(character + tile * 64 + ((unsigned)sy & 7) * 8 + ((unsigned)sx & 7)) & 0xffff];
            if (!pixel) {
                continue;
            }
            color = gba_palette(g, pixel);
        }
        gba_pixel(g, x, GBA_PIXEL(color, bg, 0u, rank));
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
static unsigned gba_objects_select(gba_t *g, uint32_t *objects) {
    unsigned display = gba_io16(g, 0), budget = (display & 0x20) ? 954 : 1210, count = 0;
    if (!(display & 0x9000)) {
        return 0;
    }
    unsigned line = g->machine.ppu.line;
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
static void gba_objects(gba_t *g, int window_pass, const uint32_t *objects, unsigned count) {
    unsigned display = gba_io16(g, 0), mosaic = gba_io16(g, 0x4c);
    if (window_pass ? !(display & 0x8000) : !(display & 0x1000)) {
        return;
    }
    for (unsigned item = 0; item < count; item++) {
        unsigned obj = objects[item] & 127, y = (objects[item] >> 7) & 255, pixels = objects[item] >> 15;
        unsigned a0 = gba_object16(g, obj * 8), a1 = gba_object16(g, obj * 8 + 2), a2 = gba_object16(g, obj * 8 + 4);
        unsigned shape = a0 >> 14, affine = (a0 >> 8) & 1, object_mode = (a0 >> 10) & 3;
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
            unsigned pixel = g->memory.vram[0x10000 + ((address - 0x10000) & 0x7fff)];
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
            gba_pixel(
                g, (unsigned)out_x, GBA_PIXEL(gba_palette(g, palette), 4u, object_mode == 1, ((a2 >> 10) & 3) * 5));
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
void gba_ppu_render(gba_t *g) {
    unsigned line = g->machine.ppu.line, display = gba_io16(g, 0), mode = display & 7;
    gba_color_t *out = g->memory.framebuffer + line % GBA_DRAW_LINES * GBA_WIDTH;
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
        gba_objects(g, 1, objects, object_count);
        /* The window pass first produces coverage bits. Reuse the same scratch
         * bytes for the resolved mask; no render scratch belongs to save states. */
        if (display & 0xe000) {
            for (unsigned x = 0; x < GBA_WIDTH; x++) {
                g->object_window[x] = (uint8_t)gba_window_mask(g, x);
            }
        } else {
            gba_memset(g->object_window, 0x3f, GBA_WIDTH);
        }
        for (unsigned bg = 0; bg < 4; bg++) {
            if (!(display & (1u << (8 + bg))) || g->machine.ppu.bg_delay[bg] || mode > 5 || (mode == 1 && bg == 3) ||
                (mode == 2 && bg < 2) || (mode >= 3 && bg != 2)) {
                continue;
            }
            unsigned rank = (gba_io16(g, 8 + bg * 2) & 3) * 5 + bg + 1;
            if (mode == 0 || (mode == 1 && bg < 2)) {
                gba_bg_text_line(g, bg, rank);
                continue;
            }
            gba_bg_affine_line(g, bg, mode, rank);
        }
        gba_objects(g, 0, objects, object_count);
        unsigned effect = gba_io16(g, 0x50);
        for (unsigned x = 0; x < GBA_WIDTH; x++) {
            unsigned color =
                (!(effect & 0xc0) && !(g->pixel_top[x] & (1u << 19))) ? g->pixel_top[x] & 0x7fff : gba_effect(g, x);
            out[x] = gba_color(color);
        }
    }
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
    if (line % GBA_DRAW_LINES == GBA_DRAW_LINES - 1) {
        if (gba_draw(0, (int)(line + 1 - GBA_DRAW_LINES), GBA_WIDTH - 1, (int)line, g->memory.framebuffer)) {
            gba_fail(g, GBA_ERROR_IO);
        }
    }
}
