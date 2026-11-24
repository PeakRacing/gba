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
    uint32_t line, phase, ref_x[2], ref_y[2];
    uint32_t window_active;
    uint32_t bg_delay[4];
    uint32_t oam_latch[256], oam_latch_valid;
} gba_ppu_t;

#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
typedef struct {
    uint32_t valid, generation, oam_generation;
    uint32_t refs[4], bg_delay[4], window_active, latch_valid;
    uint8_t io[88];
} gba_video_row_t;
#endif

void gba_ppu_render(gba_t *g);
void gba_ppu_affine_write(gba_t *g, unsigned address);
void gba_ppu_window_update(gba_t *g);
void gba_ppu_oam_latch(gba_t *g);

#ifdef __cplusplus
}
#endif
