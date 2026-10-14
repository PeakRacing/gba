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
#pragma once
#include "gba.h"
#define GBA_N 0x80000000u
#define GBA_Z 0x40000000u
#define GBA_C 0x20000000u
#define GBA_V 0x10000000u
#define GBA_T 0x20u
#define GBA_IRQ_DELAY 7u
#define GBA_TIMER_START_DELAY 2u
#define GBA_DMA_START_DELAY 2u
#define GBA_DMA_EVENT_DELAY 3u
static inline uint16_t gba_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t gba_le32(const uint8_t *p) {
    return (uint32_t)gba_le16(p) | ((uint32_t)gba_le16(p + 2) << 16);
}
static inline void gba_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void gba_put32(uint8_t *p, uint32_t v) {
    gba_put16(p, (uint16_t)v);
    gba_put16(p + 2, (uint16_t)(v >> 16));
}
static inline uint16_t gba_io16(const gba_t *g, unsigned a) {
    return gba_le16(g->io + a);
}
static inline uint32_t gba_io32(const gba_t *g, unsigned a) {
    return gba_le32(g->io + a);
}
static inline int32_t gba_sign(uint32_t value, unsigned bits) {
    uint32_t s = 1u << (bits - 1);
    return (value & s) ? -1 - (int32_t)((~value) & (s - 1)) : (int32_t)(value & (s - 1));
}
static inline uint32_t gba_ror(uint32_t x, unsigned n) {
    n &= 31;
    return n ? (x >> n) | (x << (32 - n)) : x;
}
static inline unsigned gba_gamepak_seq(uint16_t wait, unsigned area) {
    return (wait & (1u << (4 + area * 3))) ? 2 : (2u << area) + 1;
}
static inline unsigned gba_month_days(unsigned year, unsigned month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month >= 1 && month <= 12 ? days[month - 1] + (month == 2 && year % 4 == 0) : 0;
}
static inline int32_t gba_fixed8(int32_t value) {
    return value >= 0 ? value / 256 : -1 - (-1 - value) / 256;
}
static inline void gba_prefetch_idle(gba_t *g, uint32_t cycles) {
    unsigned region = g->machine.last_fetch >> 24;
    uint16_t wait = gba_io16(g, 0x204);
    if (!(wait & 0x4000) || region < 8 || region > 13 || !g->machine.cpu.pipeline_valid ||
        !g->machine.cpu.fetch_sequential || g->dma_running) {
        return;
    }
    unsigned cost = gba_gamepak_seq(wait, (region - 8) / 2);
    uint32_t phase = g->machine.prefetch_phase + cycles;
    uint32_t count = g->machine.prefetch_count + phase / cost;
    g->machine.prefetch_count = count > 8 ? 8 : count;
    g->machine.prefetch_phase = count >= 8 ? 0 : phase % cost;
}
static inline void gba_irq(gba_t *g, unsigned mask) {
    gba_put16(g->io + 0x202, (uint16_t)(gba_io16(g, 0x202) | mask));
    unsigned line = (gba_io16(g, 0x200) & gba_io16(g, 0x202)) != 0;
    if (line && !g->machine.cpu.irq_line) {
        g->event_cycles = 0;
        /* The request line is independent of IME and the CPU's I mask. */
        g->machine.cpu.irq_delay = GBA_IRQ_DELAY;
    } else if (!line) {
        if (g->machine.cpu.irq_delay) {
            g->event_cycles = 0;
        }
        g->machine.cpu.irq_delay = 0;
    }
    g->machine.cpu.irq_line = line;
}
static inline void gba_fail(gba_t *g, int error) {
    if (!g->fault) {
        g->fault = error;
        g->gba_quit = 1;
    }
}
uint32_t gba_bus_read(gba_t *g, uint32_t a, unsigned width, int sequential, int fetch);
void gba_bus_write(gba_t *g, uint32_t a, uint32_t value, unsigned width, int sequential);
uint32_t gba_read8(gba_t *g, uint32_t a);
uint32_t gba_read16(gba_t *g, uint32_t a);
uint32_t gba_read32(gba_t *g, uint32_t a);
void gba_write8(gba_t *g, uint32_t a, uint32_t value);
void gba_write16(gba_t *g, uint32_t a, uint32_t value);
void gba_write32(gba_t *g, uint32_t a, uint32_t value);
int gba_rom_bytes(gba_t *g, uint32_t offset, void *data, size_t size);
uint8_t gba_rom_byte(gba_t *g, uint32_t offset);
void gba_io_write(gba_t *g, unsigned a, uint32_t value, unsigned width);
uint32_t gba_cpu_execute(gba_t *g);
void gba_cpu_mode(gba_t *g, uint32_t cpsr);
void gba_cpu_exception(gba_t *g, unsigned mode, uint32_t vector, uint32_t lr);
int gba_bios_swi(gba_t *g, unsigned number);
void gba_advance(gba_t *g, uint32_t cycles);
uint32_t gba_next_event(const gba_t *g);
void gba_dma_trigger(gba_t *g, unsigned timing);
void gba_dma_enable(gba_t *g, unsigned channel);
uint32_t gba_dma_step(gba_t *g);
void gba_dma_drain(gba_t *g);
void gba_ppu_render(gba_t *g);
void gba_ppu_affine_write(gba_t *g, unsigned address);
void gba_ppu_window_update(gba_t *g);
void gba_ppu_oam_latch(gba_t *g);
void gba_apu_write(gba_t *g, unsigned address, uint16_t old);
void gba_apu_advance(gba_t *g, uint32_t cycles);
void gba_apu_timer(gba_t *g, unsigned timer);
void gba_cart_detect(gba_t *g);
uint32_t gba_cart_read(gba_t *g, uint32_t a, unsigned width);
void gba_cart_write(gba_t *g, uint32_t a, uint32_t value, unsigned width);
void gba_rtc_gpio(gba_t *g, unsigned value);
void gba_rtc_sync(gba_t *g);
uint32_t gba_crc32(uint32_t crc, const void *data, size_t size);
#if GBA_TEST_MODE
void gba_test_instruction(gba_t *g, uint32_t pc, uint32_t opcode);
#endif
#if GBA_TEST_PROFILE
uint32_t gba_profile_ticks(void);
#endif
