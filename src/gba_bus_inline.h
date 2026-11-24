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

/* Included after the shared byte/integer helpers in gba_internal.h. */
#if GBA_FAST_IO && defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/* Optional emulated-register access only, never host MMIO or borrowed buffers. */
typedef uint16_t gba_alias16_t __attribute__((may_alias));
typedef uint32_t gba_alias32_t __attribute__((may_alias));
#define GBA_NATIVE_LE 1
#else
#define GBA_NATIVE_LE 0
#endif
GBA_INLINE uint16_t gba_io16(const gba_t *g, unsigned a) {
#if GBA_NATIVE_LE
    /* gba_t is a naturally aligned object, and every configuration places io
     * on a word boundary. Preserve the arbitrary-offset fallback. */
    _Static_assert(_Alignof(gba_t) >= 4 && offsetof(gba_t, io) % 4 == 0, "GBA IO alignment");
    if (!(a & 1u))
        return *(const gba_alias16_t *)__builtin_assume_aligned(g->io + a, 2);
#endif
    return gba_le16(g->io + a);
}

GBA_INLINE uint32_t gba_io32(const gba_t *g, unsigned a) {
#if GBA_NATIVE_LE
    if (!(a & 3u))
        return *(const gba_alias32_t *)__builtin_assume_aligned(g->io + a, 4);
#endif
    return gba_le32(g->io + a);
}

GBA_INLINE unsigned gba_gamepak_seq(uint16_t wait, unsigned area) {
    return (wait & (1u << (4 + area * 3))) ? 2 : (2u << area) + 1;
}
GBA_INLINE unsigned gba_gamepak_refill_cost(uint16_t wait,unsigned width,unsigned region) {
    static const unsigned first[4]={4,3,2,8};
    unsigned area=(region-8)/2,seq=gba_gamepak_seq(wait,area);
    return first[(wait>>(2+area*3))&3]+1+seq*(width==4?3u:1u);
}

GBA_INLINE uint32_t gba_seq_div(uint32_t value, unsigned divisor) {
    switch (divisor) {
    case 2:
        return value / 2;
    case 3:
        return value / 3;
    case 5:
        return value / 5;
    case 9:
        return value / 9;
    default:
        return value / divisor;
    }
}

GBA_INLINE void gba_prefetch_idle(gba_t *g, uint32_t cycles) {
    unsigned region = g->machine.last_fetch >> 24;
    uint16_t wait = gba_io16(g, 0x204);
    if (!(wait & 0x4000) || region < 8 || region > 13 || !g->machine.cpu.pipeline_valid ||
        !g->machine.cpu.fetch_sequential || g->dma_running) {
        return;
    }
    unsigned cost = gba_gamepak_seq(wait, (region - 8) / 2);
    uint32_t phase = g->machine.prefetch_phase + cycles;
    if (g->machine.prefetch_count >= 8) {
        g->machine.prefetch_phase = 0;
        return;
    }
    if (phase < cost) {
        g->machine.prefetch_phase = phase;
        return;
    }
    uint32_t steps = gba_seq_div(phase, cost);
    uint32_t count = g->machine.prefetch_count + steps;
    g->machine.prefetch_count = count > 8 ? 8 : count;
    g->machine.prefetch_phase = count >= 8 ? 0 : phase - steps * cost;
}
