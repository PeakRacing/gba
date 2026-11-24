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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t enabled, phase, position, length, volume, envelope_phase;
    uint32_t sweep_phase, sweep_frequency, sweep_negated, lfsr;
} gba_apu_channel_t;

typedef struct {
    gba_apu_channel_t channel[4];
    uint32_t fifo[2][32], fifo_read[2], fifo_count[2], fifo_sample[2];
    uint32_t wave[32], sample_phase, sequencer_phase, sequencer_step;
} gba_apu_t;

void gba_apu_write(gba_t *g, unsigned address, uint16_t old);
void gba_apu_advance(gba_t *g, uint32_t cycles);
void gba_apu_timer(gba_t *g, unsigned timer);

#ifdef __cplusplus
}
#endif
