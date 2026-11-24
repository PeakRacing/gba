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
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
GBA_INLINE void gba_video_read(gba_t *g, unsigned region, uint32_t address) {
    if (!g->video_cache.active_mask)
        return;
    unsigned block = region == 5 ? 3072 + ((address & 0x3ff) >> 3) : (address >> 5);
#if GBA_PPU_READ_DEDUP
    /* Palette lookups interleave tile bytes. Keep separate exact last-block
     * tags so an eight-pixel text tile registers its VRAM dependency once. */
    uint32_t *last=region==5?&g->video_cache.last_palette_block:&g->video_cache.last_vram_block;
    if(*last==block)return;
    *last=block;
#endif
    g->video_cache.dependency[block][g->video_cache.active_word] |= g->video_cache.active_mask;
}
void gba_video_write(gba_t *g, unsigned region, uint32_t address, unsigned width, uint32_t value);
#else
#if GBA_PPU_OAM_DIRTY || GBA_PPU_SCANLINE_FAST
void gba_video_write(gba_t *g, unsigned region, uint32_t address, unsigned width, uint32_t value);
#else
#define gba_video_write(g, region, address, width, value) ((void)0)
#endif
#define gba_video_read(g, region, address)                ((void)0)
#endif
