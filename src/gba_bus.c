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

int gba_rom_bytes(gba_t *g, uint32_t offset, void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    if (offset > g->rom_size || size > g->rom_size - offset) {
        return GBA_ERROR_ROM;
    }
    if (g->rom) {
        gba_memcpy(out, g->rom + offset, size);
        return GBA_OK;
    }
#if GBA_ROM_STREAM
    while (size && !g->fault) {
        uint32_t page = offset / GBA_ROM_PAGE_SIZE, slot = GBA_ROM_CACHE_SLOTS;
        size_t bytes = GBA_ROM_PAGE_SIZE - (offset & (GBA_ROM_PAGE_SIZE - 1));
        for (uint32_t i = 0; i < GBA_ROM_CACHE_SLOTS; i++) {
            if (g->cache_valid[i] && g->cache_tag[i] == page) {
                slot = i;
                break;
            }
        }
        if (slot == GBA_ROM_CACHE_SLOTS) {
            uint32_t oldest = 0;
            for (uint32_t i = 0; i < GBA_ROM_CACHE_SLOTS; i++) {
                if (!g->cache_valid[i]) {
                    oldest = i;
                    break;
                }
                if ((uint32_t)(g->cache_clock - g->cache_age[i]) > (uint32_t)(g->cache_clock - g->cache_age[oldest])) {
                    oldest = i;
                }
            }
            slot = oldest;
            g->cache_valid[slot] = 0;
            size_t available = g->rom_size - page * GBA_ROM_PAGE_SIZE;
            if (available > GBA_ROM_PAGE_SIZE) {
                available = GBA_ROM_PAGE_SIZE;
            }
            if (!g->rom_read || g->rom_read(g->rom_user, page * GBA_ROM_PAGE_SIZE,
                                    g->memory.rom_cache + slot * GBA_ROM_PAGE_SIZE, available) != available) {
                gba_fail(g, GBA_ERROR_IO);
                return GBA_ERROR_IO;
            }
            g->cache_tag[slot] = page;
            g->cache_valid[slot] = 1;
#if GBA_TEST_PROFILE
            g->cache_misses++;
#endif
        } else {
#if GBA_TEST_PROFILE
            g->cache_hits++;
#endif
        }
        g->cache_age[slot] = ++g->cache_clock;
        if (bytes > size) {
            bytes = size;
        }
        gba_memcpy(out, g->memory.rom_cache + slot * GBA_ROM_PAGE_SIZE + (offset & (GBA_ROM_PAGE_SIZE - 1)), bytes);
        out += bytes;
        offset += (uint32_t)bytes;
        size -= bytes;
    }
    return g->fault;
#else
    (void)out;
    gba_fail(g, GBA_ERROR_UNSUPPORTED);
    return GBA_ERROR_UNSUPPORTED;
#endif
}
uint8_t gba_rom_byte(gba_t *g, uint32_t offset) {
    uint8_t value = 0xff;
    if (offset < g->rom_size) {
        gba_rom_bytes(g, offset, &value, 1);
    }
    return value;
}
static unsigned gba_bus_wait(gba_t *g, uint32_t a, unsigned width, int sequential, int fetch) {
    unsigned region = a >> 24, cost = 1;
    /* CPU data accesses interrupt its instruction address stream. With
     * Game Pak prefetch enabled the ROM controller can continue fetching
     * while the CPU accesses internal memory. DMA has its own sequence. */
    if (g->stepping && !g->dma_running && !fetch && !(gba_io16(g, 0x204) & 0x4000)) {
        g->machine.cpu.fetch_sequential = 0;
    }
    if (!fetch && region >= 8 && region <= 15) {
        g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        if (g->stepping && !g->dma_running) {
            g->machine.cpu.fetch_sequential = 0;
        }
    }
    if (region == 2) {
        cost = width == 4 ? 6 : 3;
    }
    if (region == 5 || region == 6) {
        cost = width == 4 ? 2 : 1;
    }
    if (region >= 8 && region <= 13) {
        static const unsigned first[4] = {4, 3, 2, 8};
        uint16_t wait = gba_io16(g, 0x204);
        unsigned area = (region - 8) / 2, shift = 2 + area * 3;
        unsigned nonseq = first[(wait >> shift) & 3] + 1;
        unsigned seq = gba_gamepak_seq(wait, area);
        if ((a & 0x1ffffu) == 0) {
            sequential = 0;
        }
        cost = sequential ? seq : nonseq;
        if (width == 4) {
            cost += seq;
        }
        if (fetch && (wait & 0x4000) && sequential) {
            unsigned units = width == 4 ? 2 : 1;
            unsigned used = g->machine.prefetch_count < units ? g->machine.prefetch_count : units;
            g->machine.prefetch_count -= used;
            if (used == units) {
                cost = 1;
                gba_prefetch_idle(g, cost);
            } else {
                /* An unfinished prefetch continues from its elapsed phase;
                 * fetching instructions never creates additional idle time. */
                cost = (units - used) * seq - g->machine.prefetch_phase % seq;
                g->machine.prefetch_phase = 0;
            }
        } else if (!fetch || !sequential) {
            g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        }
    }
    if (region == 14 || region == 15) {
        static const unsigned waits[4] = {5, 4, 3, 9};
        cost = waits[gba_io16(g, 0x204) & 3];
    }
    if (g->stepping && (region < 8 || region > 15)) {
        gba_prefetch_idle(g, cost);
    }
    g->access_cycles += cost;
    if (g->stepping) {
        gba_advance(g, cost);
    }
    return cost;
}
static uint8_t *gba_region(gba_t *g, uint32_t a) {
    switch (a >> 24) {
    case 2:
        return g->memory.ewram + (a & 0x3ffff);
    case 3:
        return g->memory.iwram + (a & 0x7fff);
    case 5:
        return g->memory.palette + (a & 0x3ff);
    case 6: {
        uint32_t off = a & 0x1ffff;
        if (off >= 0x18000) {
            off -= 0x8000;
        }
        return g->memory.vram + off;
    }
    case 7:
        return g->memory.oam + (a & 0x3ff);
    default:
        return NULL;
    }
}
static uint32_t gba_io_read_mask(unsigned a) {
    if (a >= 0x90 && a < 0xa0) {
        return 0xffff;
    }
    if (a >= 0xb0 && a <= 0xde) {
        unsigned field = (a - 0xb0) % 12;
        return field == 8 ? 0 : field == 10 ? (a == 0xde ? 0xffe0 : 0xf7e0) : 0x10000;
    }
    if (a >= 0x100 && a < 0x110) {
        return (a & 2) ? (a == 0x102 ? 0xc3 : 0xc7) : 0xffff;
    }
    switch (a) {
    case 0:
        return 0xffff;
    case 2:
        return 0xffff;
    case 4:
        return 0xff3f;
    case 6:
        return 0xff;
    case 8:
    case 10:
        return 0xdfff;
    case 12:
    case 14:
        return 0xffff;
    case 0x48:
    case 0x4a:
        return 0x3f3f;
    case 0x50:
        return 0x3fff;
    case 0x52:
        return 0x1f1f;
    case 0x60:
        return 0x7f;
    case 0x62:
    case 0x68:
        return 0xffc0;
    case 0x64:
    case 0x6c:
    case 0x74:
        return 0x4000;
    case 0x70:
        return 0xe0;
    case 0x72:
        return 0xe000;
    case 0x78:
        return 0xff00;
    case 0x7c:
        return 0x40ff;
    case 0x80:
        return 0xff77;
    case 0x82:
        return 0x770f;
    case 0x84:
        return 0x8f;
    case 0x88:
        return 0xc3ff;
    case 0x66:
    case 0x6a:
    case 0x6e:
    case 0x76:
    case 0x7a:
    case 0x7e:
    case 0x86:
    case 0x8a:
    case 0x136:
    case 0x142:
    case 0x15a:
    case 0x206:
    case 0x20a:
    case 0x302:
        return 0;
    case 0x120:
    case 0x122:
    case 0x124:
    case 0x126:
    case 0x128:
    case 0x12a:
    case 0x134:
    case 0x140:
    case 0x150:
    case 0x152:
    case 0x154:
    case 0x156:
    case 0x158:
        return 0xffff;
    case 0x130:
        return 0x3ff;
    case 0x132:
        return 0xc3ff;
    case 0x200:
    case 0x202:
        return 0x3fff;
    case 0x204:
        return 0x7fff;
    case 0x208:
        return 1;
    case 0x300:
        return 1;
    default:
        return 0x10000;
    }
}
static uint32_t gba_open_bus(const gba_t *g) {
    unsigned width = (g->machine.cpu.cpsr & GBA_T) ? 2 : 4;
    if (g->machine.dma_bus_valid && (g->dma_running || g->machine.cpu.r[15] == g->machine.dma_pc + width)) {
        return g->machine.dma_bus;
    }
    return g->machine.open_bus;
}
static uint32_t gba_io_read(gba_t *g, unsigned a, unsigned width) {
    uint8_t bytes[4];
    for (unsigned i = 0; i < width; i++) {
        unsigned index = a + i, aligned = index & ~1u;
        uint16_t value;
        uint32_t mask = gba_io_read_mask(aligned);
        if (index >= 0x400 || mask == 0x10000) {
            bytes[i] = (uint8_t)(gba_open_bus(g) >> ((index & 3) * 8));
            continue;
        }
        if (aligned >= 0x100 && aligned < 0x110 && !(aligned & 2)) {
            value = (uint16_t)g->machine.timer[(aligned - 0x100) / 4].counter;
        } else if (aligned == 0x84) {
            value = gba_io16(g, 0x84) & 0x80;
            for (unsigned ch = 0; ch < 4; ch++) {
                value |= (uint16_t)(g->machine.apu.channel[ch].enabled << ch);
            }
        } else if (index >= 0x90 && index < 0xa0) {
            unsigned bank = ((gba_io16(g, 0x70) >> 6) & 1) ^ 1;
            bytes[i] = (uint8_t)g->machine.apu.wave[bank * 16 + index - 0x90];
            continue;
        } else {
            value = gba_io16(g, aligned);
        }
        bytes[i] = (uint8_t)((value & mask) >> ((index & 1) * 8));
    }
    return width == 4 ? gba_le32(bytes) : width == 2 ? gba_le16(bytes) : bytes[0];
}
uint32_t gba_bus_read(gba_t *g, uint32_t address, unsigned width, int sequential, int fetch) {
    uint32_t a = address & ~(uint32_t)(width - 1), value;
    const uint8_t *p;
    gba_bus_wait(g, a, width, sequential, fetch);
    unsigned region = a >> 24;
    uint32_t offset = a & 0x1ffffff;
    if (region >= 8 && region <= 12 && g->rom && offset < g->rom_size && width <= g->rom_size - offset &&
        !(g->machine.cart.rtc_enabled && g->machine.cart.gpio_control && offset >= 0xc4 && offset <= 0xc9)) {
        p = g->rom + offset;
    } else {
        p = gba_region(g, a);
    }
    if (p) {
        value = width == 4 ? gba_le32(p) : width == 2 ? gba_le16(p) : *p;
    } else if ((a >> 24) == 4) {
        value = gba_io_read(g, a & 0xffffffu, width);
    } else if ((a >> 24) == 0 && a < 0x4000) {
        if (g->bios && (g->machine.cpu.r[15] < 0x4000 || fetch)) {
            value = gba_le32(g->bios + (a & ~3u));
            g->machine.bios_latch = value;
        } else if (!g->bios && GBA_BIOS_HLE && g->machine.cpu.hle_irq && (g->machine.cpu.r[15] < 0x4000 || fetch)) {
            /* Reimplemented BIOS dispatch sequence from the documented IRQ ABI.
             * Execute it through the CPU so stack accesses and PC/LR are visible. */
            switch (a & ~3u) {
            case 0x18:
                value = 0xea000042;
                break; /* b 0x128 */
            case 0x128:
                value = 0xe92d500f;
                break; /* stmdb sp!, {r0-r3,r12,lr} */
            case 0x12c:
                value = 0xe3a00301;
                break; /* mov r0, #0x04000000 */
            case 0x130:
                value = 0xe28fe000;
                break; /* add lr, pc, #0 */
            case 0x134:
                value = 0xe510f004;
                break; /* ldr pc, [r0, #-4] */
            case 0x138:
                value = 0xe8bd500f;
                break; /* ldmia sp!, {r0-r3,r12,lr} */
            case 0x13c:
                value = 0xe25ef004;
                break; /* subs pc, lr, #4 */
            default:
                value = 0xe1a00000;
                break; /* pipeline read beyond dispatch */
            }
            g->machine.bios_latch = value;
        } else {
            value = g->machine.bios_latch;
        }
        value >>= (a & 3) * 8;
        if (width == 1) {
            value &= 0xff;
        } else if (width == 2) {
            value &= 0xffff;
        }
    } else if ((a >> 24) >= 8 && (a >> 24) <= 15) {
        /* The 8-bit save bus decodes the original byte address for wide reads. */
        value = gba_cart_read(g, (a >> 24) >= 14 ? address : a, width);
    } else {
        value = gba_ror(gba_open_bus(g), (a & 3) * 8);
    }
    if (fetch) {
        g->machine.last_fetch = a;
        g->machine.open_bus = width == 2 ? value | (value << 16) : value;
    }
    gba_dma_drain(g);
    return width == 1 ? value & 0xff : width == 2 ? value & 0xffff : value;
}
void gba_bus_write(gba_t *g, uint32_t address, uint32_t value, unsigned width, int sequential) {
    uint32_t a = address & ~(uint32_t)(width - 1);
    uint8_t *p = gba_region(g, a);
    gba_bus_wait(g, a, width, sequential, 0);
    if (p) {
        unsigned region = a >> 24;
        if (width == 1 && region >= 5 && region <= 7) {
            if (region == 7) {
                gba_dma_drain(g);
                return;
            }
            if (region == 6) {
                unsigned boundary = (gba_io16(g, 0) & 7) >= 3 ? 0x14000 : 0x10000;
                if ((a & 0x1ffff) >= boundary) {
                    gba_dma_drain(g);
                    return;
                }
            }
            p = gba_region(g, a & ~1u);
            gba_put16(p, (uint16_t)((value & 0xff) * 0x101u));
        } else if (width == 4) {
            gba_put32(p, value);
        } else if (width == 2) {
            gba_put16(p, (uint16_t)value);
        } else {
            *p = (uint8_t)value;
        }
    } else if ((a >> 24) == 4) {
        gba_io_write(g, a & 0xffffff, value, width);
    } else if ((a >> 24) >= 8 && (a >> 24) <= 15) {
        gba_cart_write(g, address, value, width);
    }
    gba_dma_drain(g);
}
uint32_t gba_read8(gba_t *g, uint32_t a) {
    return gba_bus_read(g, a, 1, 0, 0);
}
uint32_t gba_read16(gba_t *g, uint32_t a) {
    return gba_ror(gba_bus_read(g, a, 2, 0, 0), (a & 1) * 8);
}
uint32_t gba_read32(gba_t *g, uint32_t a) {
    return gba_ror(gba_bus_read(g, a, 4, 0, 0), (a & 3) * 8);
}
void gba_write8(gba_t *g, uint32_t a, uint32_t v) {
    gba_bus_write(g, a, v, 1, 0);
}
void gba_write16(gba_t *g, uint32_t a, uint32_t v) {
    gba_bus_write(g, a, v, 2, 0);
}
void gba_write32(gba_t *g, uint32_t a, uint32_t v) {
    gba_bus_write(g, a, v, 4, 0);
}

void gba_io_write(gba_t *g, unsigned a, uint32_t value, unsigned width) {
    g->event_cycles = 0;
    if (a >= 0x400) {
        return;
    }
    if (width == 4) {
        gba_io_write(g, a, value & 0xffff, 2);
        gba_io_write(g, a + 2, value >> 16, 2);
        return;
    }
    unsigned base = a & ~1u;
    uint16_t old = gba_io16(g, base), merged = (uint16_t)value;
    if (width == 1) {
        unsigned shift = (a & 1) * 8;
        merged = (uint16_t)((old & ~(0xffu << shift)) | ((value & 0xff) << shift));
    }
    if (a == 0x301) {
        if (value & 0x80) {
            g->machine.cpu.stopped = 1;
        } else {
            g->machine.cpu.halted = 1;
        }
        return;
    }
    if (base == 0x6 || base == 0x130) {
        return;
    }
    if (base == 8 || base == 10) {
        merged &= 0xdfff;
    }
    if (base == 0x48 || base == 0x4a) {
        merged &= 0x3f3f;
    }
    if (base == 0x50 || base == 0x200) {
        merged &= 0x3fff;
    }
    if (base == 0x52) {
        merged &= 0x1f1f;
    }
    if (base == 0x54) {
        merged &= 0x1f;
    }
    if (base == 0x80) {
        merged &= 0xff77;
    }
    if (base == 0x204) {
        merged &= 0x7fff;
        if (merged != old) {
            g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        }
    }
    if (base == 0x208) {
        merged &= 1;
    }
    if (base == 0x132) {
        merged &= 0xc3ff;
    }
    if (base >= 0x102 && base <= 0x10e && (base & 2)) {
        merged &= base == 0x102 ? 0xc3 : 0xc7;
    }
    if (base >= 0xba && base <= 0xde && (base - 0xba) % 12 == 0) {
        merged &= base == 0xde ? 0xffe0 : 0xf7e0;
    }
    if (base == 0x4) {
        merged = (uint16_t)((old & 7) | (merged & 0xff38));
    }
    if (base == 0x202) {
        uint16_t clear = (uint16_t)value;
        if (width == 1) {
            clear = (uint16_t)((value & 0xff) << ((a & 1) * 8));
        }
        merged = old & (uint16_t)~clear;
    }
    if (base >= 0xa0 && base <= 0xa6) {
        unsigned fifo = (base - 0xa0) / 4;
        gba_apu_t *apu = &g->machine.apu;
        for (unsigned i = 0; i < width; i++) {
            if (apu->fifo_count[fifo] < 32) {
                unsigned pos = (apu->fifo_read[fifo] + apu->fifo_count[fifo]++) & 31;
                apu->fifo[fifo][pos] = (value >> (i * 8)) & 0xff;
            }
        }
        return;
    }
    if (a >= 0x90 && a < 0xa0) {
        unsigned bank = ((gba_io16(g, 0x70) >> 6) & 1) ^ 1;
        for (unsigned i = 0; i < width; i++) {
            g->machine.apu.wave[bank * 16 + a + i - 0x90] = (value >> (i * 8)) & 0xff;
        }
        return;
    }
    gba_put16(g->io + base, merged);
    if (base == 0) {
        for (unsigned bg = 0; bg < 4; bg++) {
            unsigned enabled = 1u << (8 + bg);
            if (!(merged & enabled)) {
                g->machine.ppu.bg_delay[bg] = 0;
            } else if (!(old & enabled)) {
                g->machine.ppu.bg_delay[bg] = g->stepping ? 3 : 0;
            }
        }
    }
    if (base == 0x44 || base == 0x46) {
        gba_ppu_window_update(g);
    }
    if (base == 0x200 || base == 0x202 || base == 0x208) {
        gba_irq(g, 0);
    }
    if (base >= 0x100 && base < 0x110) {
        gba_timer_t *timer = &g->machine.timer[(base - 0x100) / 4];
        if (!(base & 2)) {
            if (g->stepping && (timer->control & 0x80)) {
                /* Keep the old overflow latch through the next clock;
                 * the pending value already resides in the I/O register. */
                timer->reload_delay = 1;
            } else {
                timer->reload = merged;
                timer->reload_delay = 0;
            }
        } else {
            if ((merged & 0x80) && !(timer->control & 0x80)) {
                timer->reload = gba_io16(g, base - 2);
                timer->reload_delay = 0;
                timer->counter = timer->reload;
                timer->phase = 0;
                timer->startup = GBA_TIMER_START_DELAY;
            } else if ((merged & 0x80) && ((merged ^ timer->control) & 7)) {
                static const unsigned divisors[4] = {1, 64, 256, 1024};
                timer->phase = g->machine.cycles & (divisors[merged & 3] - 1);
            }
            if (!(merged & 0x80)) {
                timer->startup = 0;
            }
            timer->control = merged & 0xc7;
        }
    }
    if (base >= 0xba && base <= 0xde && (base - 0xba) % 12 == 0) {
        unsigned ch = (base - 0xba) / 12;
        if ((merged & 0x8000) && !(old & 0x8000)) {
            gba_dma_enable(g, ch);
        }
        if (!(merged & 0x8000)) {
            g->machine.dma[ch].control = merged;
            g->machine.dma[ch].pending = 0;
            g->machine.dma[ch].startup = 0;
        }
    }
    if (base >= 0x28 && base <= 0x3e) {
        gba_ppu_affine_write(g, base);
    }
    if (base >= 0x60 && base <= 0x88) {
        gba_apu_write(g, base, old);
    }
    if (base == 0x132) {
        gba_set_keys(g, (uint16_t)g->machine.keys);
    }
    if (base == 0x204) {
        g->machine.prefetch_count = g->machine.prefetch_phase = 0;
    }
}
