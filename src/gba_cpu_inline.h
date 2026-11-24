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
GBA_INLINE unsigned gba_irq_pending(const gba_t *g) {
#if GBA_FAST_RUN
    return g->machine.cpu.irq_line;
#else
    return gba_io16(g, 0x200) & gba_io16(g, 0x202);
#endif
}

#if GBA_FAST_WAIT && !GBA_TEST_MODE
GBA_INLINE void gba_wait_invalidate(gba_t *g) {
    g->wait.valid = 0;
}
#else
#define gba_wait_invalidate(g) ((void)0)
#endif
#if GBA_FAST_CPU
void gba_cpu_sync(gba_t *g);
uint32_t gba_cpu_run(gba_t *g, uint32_t budget);
uint32_t gba_bus_fetch(gba_t *g, uint32_t address, unsigned width, int sequential);
uint32_t gba_next_cpu_event(const gba_t *g);
GBA_INLINE void gba_cpu_charge(gba_t *g, uint32_t cycles) {
    if (!g->event_cycles)
        g->event_cycles = gba_next_cpu_event(g);
    g->cpu_pending += cycles;
    if (g->cpu_pending >= g->event_cycles)
        gba_cpu_sync(g);
}
#endif
