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
GBA_INLINE void gba_dma_refresh_ready(gba_t *g) {
#if GBA_FAST_RUN
    unsigned mask = 0;
    for (unsigned ch = 0; ch < 4; ch++) {
        const gba_dma_t *d = &g->machine.dma[ch];
        if (d->pending && d->remaining && !d->startup)
            mask |= 1u << ch;
    }
    g->dma_ready = mask;
#else
    (void)g;
#endif
}
GBA_INLINE unsigned gba_dma_ready_mask(const gba_t *g) {
#if GBA_FAST_RUN
    return g->dma_ready;
#else
    unsigned mask = 0;
    for (unsigned ch = 0; ch < 4; ch++) {
        const gba_dma_t *d = &g->machine.dma[ch];
        if (d->pending && d->remaining && !d->startup)
            mask |= 1u << ch;
    }
    return mask;
#endif
}
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
GBA_INLINE void gba_idle_invalidate(gba_t *g) {
    if (g->idle_valid)
        g->idle_reject_pc = g->idle_pc;
    g->idle_valid = 0;
}
#else
#define gba_idle_invalidate(g) ((void)0)
#endif
GBA_INLINE void gba_irq(gba_t *g, unsigned mask) {
#if GBA_BATCH_EVENTS
    if (mask)
        gba_flush_events(g);
#endif
#if GBA_NATIVE_LE
    /* IF is ordinary emulator RAM. A zero-mask refresh changes only IRQ line
     * bookkeeping; avoid writing the same bytes on every instruction twice. */
    if (mask)
#endif
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
