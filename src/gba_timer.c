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
static const uint32_t gba_timer_divisor[4] = {1, 64, 256, 1024};
static const unsigned gba_timer_shift[4] = {0, 6, 8, 10};

void gba_dma_enable(gba_t *g, unsigned ch) {
    g->event_cycles = 0;
    unsigned base = 0xb0 + ch * 12;
    gba_dma_t *d = &g->machine.dma[ch];
    d->source = gba_io32(g, base) & (ch ? 0x0fffffffu : 0x07ffffffu);
    d->destination = gba_io32(g, base + 4) & (ch == 3 ? 0x0fffffffu : 0x07ffffffu);
    d->control = gba_io16(g, base + 10);
    unsigned width = (d->control & 0x400) ? 4 : 2;
    d->source &= ~(width - 1u);
    d->destination &= ~(width - 1u);
    d->initial_destination = d->destination;
    d->count = gba_io16(g, base + 8) & (ch == 3 ? 0xffffu : 0x3fffu);
    if (!d->count) {
        d->count = ch == 3 ? 0x10000 : 0x4000;
    }
    d->remaining = d->count;
    d->sequential = 0;
    d->fifo = 0;
    d->pending = ((d->control >> 12) & 3) == 0;
    d->startup = d->pending ? GBA_DMA_START_DELAY : 0;
    if (ch == 3 && (d->destination >> 24) == 13 && (d->count == 17 || d->count == 81)) {
        if (g->machine.cart.type == GBA_SAVE_EEPROM512) {
            if (GBA_SAVE_CAPACITY < 8192 || g->memory.save_size < 8192) {
                gba_fail(g, GBA_ERROR_MEMORY);
                return;
            }
            g->machine.cart.type = GBA_SAVE_EEPROM8K;
            g->machine.cart.size = 8192;
        }
    }
}
void gba_dma_trigger(gba_t *g, unsigned timing) {
    g->event_cycles = 0;
    for (unsigned ch = 0; ch < 4; ch++) {
        gba_dma_t *d = &g->machine.dma[ch];
        if ((d->control & 0x8000) && ((d->control >> 12) & 3) == timing && !d->pending) {
            d->remaining = d->count;
            d->pending = 1;
            d->sequential = 0;
            d->startup = GBA_DMA_EVENT_DELAY;
        }
    }
}
static uint32_t gba_dma_unit(gba_t *g) {
    for (unsigned ch = 0; ch < 4; ch++) {
        gba_dma_t *d = &g->machine.dma[ch];
        if (!d->pending || !d->remaining || d->startup) {
            continue;
        }
        /* Taking the bus breaks a paused channel's sequential access stream. */
        for (unsigned other = 0; other < 4; other++) {
            if (other != ch && g->machine.dma[other].pending) {
                g->machine.dma[other].sequential = 0;
            }
        }
        unsigned width = (d->fifo || (d->control & 0x400)) ? 4 : 2;
        uint32_t value;
#if GBA_TEST_PROFILE
        uint32_t begin = gba_profile_ticks();
#endif
        if (d->source >= 0x02000000) {
            value = gba_bus_read(g, d->source, width, d->sequential, 0);
            d->latch = width == 2 ? value | (value << 16) : value;
        } else {
            value = width == 2 ? (d->latch >> ((d->destination & 2) * 8)) & 0xffff : d->latch;
            g->access_cycles++;
            if (g->stepping) {
                gba_advance(g, 1);
            }
        }
        gba_bus_write(g, d->destination, value, width, d->sequential);
        unsigned source_mode = (d->control >> 7) & 3, destination_mode = (d->control >> 5) & 3;
        /* Game Pak ROM always increments, even for fixed/decrement control. */
        unsigned source_region = d->source >> 24;
        if (source_mode == 0 || (source_region >= 8 && source_region <= 13)) {
            d->source += width;
        } else if (source_mode == 1) {
            d->source -= width;
        }
        if (!d->fifo) {
            if (destination_mode == 0 || destination_mode == 3) {
                d->destination += width;
            } else if (destination_mode == 1) {
                d->destination -= width;
            }
        }
        g->machine.open_bus = width == 2 ? value | (value << 16) : value;
        g->machine.dma_pc = g->machine.cpu.r[15];
        g->machine.dma_bus = g->machine.open_bus;
        g->machine.dma_bus_valid = 1;
        d->sequential = 1;
        if (!--d->remaining) {
            /* The startup event does not include bus release. A transfer
             * involving internal memory retains the CPU for two final
             * control cycles, before completion IRQ and enable clearing. */
            if (source_region < 8 || (d->destination >> 24) < 8) {
                g->access_cycles += 2;
                if (g->stepping) {
                    gba_advance(g, 2);
                }
            }
            unsigned base = 0xb0 + ch * 12;
            if (d->control & 0x4000) {
                gba_irq(g, 1u << (8 + ch));
            }
            d->pending = 0;
            if ((d->control & 0x200) && (d->control & 0x3000)) {
                d->count = gba_io16(g, base + 8) & (ch == 3 ? 0xffffu : 0x3fffu);
                if (!d->count) {
                    d->count = ch == 3 ? 0x10000 : 0x4000;
                }
                if (destination_mode == 3) {
                    d->destination = d->initial_destination;
                }
            } else {
                d->control &= ~0x8000u;
                gba_put16(g->io + base + 10, (uint16_t)d->control);
            }
        }
#if GBA_TEST_PROFILE
        g->profile_dma += gba_profile_ticks() - begin;
#endif
        return g->access_cycles;
    }
    return 0;
}
uint32_t gba_dma_step(gba_t *g) {
    if (g->dma_running) {
        return 0;
    }
    g->dma_running = 1;
    uint32_t cycles = gba_dma_unit(g);
    g->dma_running = 0;
    return cycles;
}
void gba_dma_drain(gba_t *g) {
    if (!g->stepping || g->dma_running || g->machine.cpu.stopped) {
        return;
    }
    while (!g->fault && gba_dma_step(g)) {
    }
}
uint32_t gba_next_event(const gba_t *g) {
    const gba_ppu_t *p = &g->machine.ppu;
    uint32_t nearest = (p->phase < GBA_HBLANK_START ? GBA_HBLANK_START : GBA_CYCLES_PER_LINE) - p->phase;
    if (g->machine.cpu.irq_delay && g->machine.cpu.irq_delay < nearest) {
        nearest = g->machine.cpu.irq_delay;
    }
    for (unsigned ch = 0; ch < 4; ch++) {
        if (g->machine.dma[ch].startup && g->machine.dma[ch].startup < nearest) {
            nearest = g->machine.dma[ch].startup;
        }
    }
    for (unsigned ch = 0; ch < 4; ch++) {
        const gba_timer_t *t = &g->machine.timer[ch];
        if (t->reload_delay && t->reload_delay < nearest) {
            nearest = t->reload_delay;
        }
        if (!(t->control & 0x80)) {
            continue;
        }
        if (t->startup) {
            if (t->startup < nearest) {
                nearest = t->startup;
            }
            continue;
        }
        if (ch && (t->control & 4)) {
            continue;
        }
        uint32_t remaining = (0x10000u - t->counter) * gba_timer_divisor[t->control & 3] - t->phase;
        if (remaining < nearest) {
            nearest = remaining;
        }
    }
    uint32_t apu = 32768u - g->machine.apu.sequencer_phase;
    if (apu < nearest) {
        nearest = apu;
    }
#if GBA_ENABLE_SOUND
    apu = (GBA_CPU_CLOCK - g->machine.apu.sample_phase + GBA_APU_SAMPLE_RATE - 1) / GBA_APU_SAMPLE_RATE;
    if (apu < nearest) {
        nearest = apu;
    }
#endif
    return nearest ? nearest : 1;
}
static void gba_timer_increment(gba_t *g, unsigned ch, uint32_t count) {
    gba_timer_t *t = &g->machine.timer[ch];
    while (count) {
        uint32_t distance = 0x10000u - t->counter;
        if (count < distance) {
            t->counter += count;
            break;
        }
        count -= distance;
        t->counter = t->reload;
        if (t->control & 0x40) {
            gba_irq(g, 1u << (3 + ch));
        }
        if (ch < 2) {
            gba_apu_timer(g, ch);
        }
        if (ch < 3 && (g->machine.timer[ch + 1].control & 0x84) == 0x84 && !g->machine.timer[ch + 1].startup) {
            gba_timer_increment(g, ch + 1, 1);
        }
    }
}
static void gba_ppu_event(gba_t *g) {
    gba_ppu_t *p = &g->machine.ppu;
    uint16_t status = gba_io16(g, 4);
    if (p->phase == GBA_HBLANK_START) {
        if (p->line < 160) {
#if GBA_TEST_PROFILE
            uint32_t begin = gba_profile_ticks();
#endif
            if (!g->machine.frame_skip) {
                gba_ppu_render(g);
            }
#if GBA_TEST_PROFILE
            g->profile_ppu += gba_profile_ticks() - begin;
#endif
            gba_dma_trigger(g, 2);
        }
        gba_ppu_oam_latch(g);
        status |= 2;
        if (status & 0x10) {
            gba_irq(g, 2);
        }
    } else if (p->phase == GBA_CYCLES_PER_LINE) {
        p->phase = 0;
        for (unsigned bg = 0; bg < 4; bg++) {
            if (p->bg_delay[bg]) {
                p->bg_delay[bg]--;
            }
        }
        status &= 0xfffdu;
        if (p->line < 160) {
            for (unsigned i = 0; i < 2; i++) {
                p->ref_x[i] += (uint32_t)gba_sign(gba_io16(g, 0x22 + i * 16), 16);
                p->ref_y[i] += (uint32_t)gba_sign(gba_io16(g, 0x26 + i * 16), 16);
            }
        }
        p->line++;
        if (p->line == 160) {
            status |= 1;
            if (status & 8) {
                gba_irq(g, 1);
            }
            gba_dma_trigger(g, 1);
        }
        if (p->line == 227) {
            status &= 0xfffeu;
        }
        if (p->line == 228) {
            p->line = 0;
            g->machine.frames++;
            g->machine.frame_skip = GBA_FRAME_SKIP ? g->machine.frames % (GBA_FRAME_SKIP + 1) : 0;
            for (unsigned i = 0; i < 2; i++) {
                p->ref_x[i] = (uint32_t)gba_sign(gba_io32(g, 0x28 + i * 16), 28);
                p->ref_y[i] = (uint32_t)gba_sign(gba_io32(g, 0x2c + i * 16), 28);
            }
            gba_rtc_sync(g);
            gba_frame(g);
        }
        gba_put16(g->io + 6, (uint16_t)p->line);
        gba_ppu_window_update(g);
        if (p->line == (unsigned)(status >> 8)) {
            status |= 4;
            if (status & 0x20) {
                gba_irq(g, 4);
            }
        } else {
            status &= 0xfffbu;
        }
    }
    gba_put16(g->io + 4, status);
}
void gba_advance(gba_t *g, uint32_t cycles) {
    if (!g->stepping) {
        g->event_cycles = 0;
    }
    while (cycles) {
        if (!g->event_cycles) {
            g->event_cycles = gba_next_event(g);
        }
        uint32_t part = g->event_cycles;
        if (part > cycles) {
            part = cycles;
        }
        g->event_cycles -= part;
        g->machine.cycles += part;
        for (unsigned ch = 0; ch < 4; ch++) {
            if (g->machine.dma[ch].startup) {
                g->machine.dma[ch].startup -= part;
            }
        }
        if (g->machine.cpu.irq_delay) {
            g->machine.cpu.irq_delay -= part;
        }
        g->machine.ppu.phase += part;
        for (unsigned ch = 0; ch < 4; ch++) {
            gba_timer_t *t = &g->machine.timer[ch];
            if (!(t->control & 0x80)) {
                continue;
            }
            if (t->startup) {
                t->startup -= part;
                if (!t->startup) {
                    t->phase = g->machine.cycles & (gba_timer_divisor[t->control & 3] - 1);
                }
                continue;
            }
            if (ch && (t->control & 4)) {
                continue;
            }
            unsigned shift = gba_timer_shift[t->control & 3];
            t->phase += part;
            uint32_t count = t->phase >> shift;
            t->phase &= (1u << shift) - 1;
            if (count) {
                gba_timer_increment(g, ch, count);
            }
        }
        /* Commit after all counters and cascades use the old reload latch. */
        for (unsigned ch = 0; ch < 4; ch++) {
            gba_timer_t *t = &g->machine.timer[ch];
            if (t->reload_delay) {
                t->reload_delay -= part;
                if (!t->reload_delay) {
                    t->reload = gba_io16(g, 0x100 + ch * 4);
                }
            }
        }
#if GBA_TEST_PROFILE
        uint32_t start = gba_profile_ticks();
#endif
        gba_apu_advance(g, part);
#if GBA_TEST_PROFILE
        g->profile_apu += gba_profile_ticks() - start;
#endif
        if (g->machine.ppu.phase == GBA_HBLANK_START || g->machine.ppu.phase == GBA_CYCLES_PER_LINE) {
            gba_ppu_event(g);
        }
        cycles -= part;
    }
}
