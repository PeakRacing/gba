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
    uint8_t *ewram, *iwram, *vram, *palette, *oam, *save, *rom_cache;
    size_t ewram_size, iwram_size, vram_size, palette_size, oam_size;
    size_t save_size, rom_cache_size;
    gba_color_t *framebuffer;
    size_t framebuffer_pixels;
} gba_memory_t;

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

#ifdef __cplusplus
}
#endif
