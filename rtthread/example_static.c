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
#include "gba.h"
/* Put these arrays in your linker sections when external RAM is available. */
static gba_t gba_instance;
static int gba_example_active;
static uint8_t gba_ewram[0x40000], gba_iwram[0x8000], gba_vram[0x18000];
static uint8_t gba_palette[0x400], gba_oam[0x400];
#if GBA_ENABLE_SAVE
static uint8_t gba_save[GBA_SAVE_CAPACITY];
#endif
#if GBA_ROM_STREAM
static uint8_t gba_cache[GBA_ROM_PAGE_SIZE * GBA_ROM_CACHE_SLOTS];
#endif
static gba_color_t gba_pixels[GBA_DRAW_SIZE];
int gba_example_start(const uint8_t *rom, size_t length) {
    if (gba_example_active) {
        return GBA_ERROR_BUSY;
    }
    gba_memory_t memory = {0};
    memory.ewram = gba_ewram;
    memory.ewram_size = sizeof(gba_ewram);
    memory.iwram = gba_iwram;
    memory.iwram_size = sizeof(gba_iwram);
    memory.vram = gba_vram;
    memory.vram_size = sizeof(gba_vram);
    memory.palette = gba_palette;
    memory.palette_size = sizeof(gba_palette);
    memory.oam = gba_oam;
    memory.oam_size = sizeof(gba_oam);
#if GBA_ENABLE_SAVE
    memory.save = gba_save;
    memory.save_size = sizeof(gba_save);
#endif
#if GBA_ROM_STREAM
    memory.rom_cache = gba_cache;
    memory.rom_cache_size = sizeof(gba_cache);
#endif
    memory.framebuffer = gba_pixels;
    memory.framebuffer_pixels = GBA_DRAW_SIZE;
    int result = gba_init_static(&gba_instance, &memory);
    if (!result) {
        gba_example_active = 1;
        result = gba_load_rom(&gba_instance, rom, length);
    }
    return result;
}
uint32_t gba_example_frame(void) {
    return gba_run_frame(&gba_instance);
}
int gba_example_stop(void) {
    if (!gba_example_active) {
        return GBA_ERROR;
    }
    int result = gba_deinit_static(&gba_instance);
    if (!result) {
        gba_example_active = 0;
    }
    return result;
}
int gba_example_discard(void) {
    if (!gba_example_active) {
        return GBA_ERROR;
    }
    int result = gba_unload_rom_discard(&gba_instance);
    if (!result) {
        result = gba_example_stop();
    }
    return result;
}
