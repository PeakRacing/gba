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

#include "gba_default.h"
#define GBA_DMA_EVENT_DELAY   3u
#define GBA_DMA_START_DELAY   2u
#define GBA_TIMER_START_DELAY 2u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t counter, reload, control, phase, startup, reload_delay;
} gba_timer_t;

typedef struct {
    uint32_t source, destination, count, remaining, control, pending;
    uint32_t initial_destination, sequential, fifo;
    uint32_t latch, startup;
} gba_dma_t;

void gba_flush_events(gba_t *g);
uint32_t gba_next_event(const gba_t *g);
void gba_advance(gba_t *g, uint32_t cycles);
void gba_dma_trigger(gba_t *g, unsigned timing);
void gba_dma_enable(gba_t *g, unsigned channel);
uint32_t gba_dma_step(gba_t *g);
void gba_dma_drain(gba_t *g);

#ifdef __cplusplus
}
#endif
