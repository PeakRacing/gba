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
static gba_t gba_instance;
static _Alignas(4) uint8_t gba_memory[0x60800 + GBA_SAVE_CAPACITY + GBA_DRAW_SIZE * sizeof(gba_color_t) +
                                      (GBA_ROM_STREAM ? GBA_ROM_PAGE_SIZE * GBA_ROM_CACHE_SLOTS : 0)];
static const uint8_t gba_rom[192] = {0xfe, 0xff, 0xff, 0xea};
void *gba_malloc(size_t size) {
    (void)size;
    return NULL;
}
void gba_free(void *pointer) {
    if (pointer) {
        for (;;) {
        }
    }
}
void *gba_memcpy(void *destination, const void *source, size_t size) {
    uint8_t *d = destination;
    const uint8_t *s = source;
    while (size--) {
        *d++ = *s++;
    }
    return destination;
}
void *gba_memset(void *destination, int value, size_t size) {
    uint8_t *d = destination;
    while (size--) {
        *d++ = (uint8_t)value;
    }
    return destination;
}
uint32_t gba_get_ticks(void) {
    return 0;
}
int gba_initex(gba_t *g) {
    (void)g;
    return 0;
}
int gba_deinitex(gba_t *g) {
    (void)g;
    return 0;
}
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)pixels;
    return 0;
}
int gba_sound_output(uint8_t *bytes, size_t length) {
    (void)bytes;
    (void)length;
    return 0;
}
void gba_frame(gba_t *g) {
    g->gba_quit = 1;
}
#if GBA_USE_FS
int gba_fclose(gba_file_t *file) {
    (void)file;
    return -1;
}
gba_file_t *gba_fopen(const char *path, const char *mode) {
    (void)path;
    (void)mode;
    return NULL;
}
size_t gba_fread(void *data, size_t size, size_t count, gba_file_t *file) {
    (void)data;
    (void)size;
    (void)count;
    (void)file;
    return 0;
}
size_t gba_fwrite(const void *data, size_t size, size_t count, gba_file_t *file) {
    (void)data;
    (void)size;
    (void)count;
    (void)file;
    return 0;
}
int gba_file_exists(const char *path) {
    (void)path;
    return 0;
}
int gba_file_sync(gba_file_t *file) {
    (void)file;
    return -1;
}
int gba_file_remove(const char *path) {
    (void)path;
    return -1;
}
int gba_file_replace(const char *source, const char *destination) {
    (void)source;
    (void)destination;
    return -1;
}
int gba_fseek(gba_file_t *file, long offset, int origin) {
    (void)file;
    (void)offset;
    (void)origin;
    return -1;
}
long gba_ftell(gba_file_t *file) {
    (void)file;
    return -1;
}
#if GBA_ENABLE_SAVE_STATE
void gba_state_loaded(gba_t *g) {
    (void)g;
}
#endif
#endif
int gba_smoke_entry(void) {
    gba_memory_t memory;
    int result = gba_memory_bind(&memory, gba_memory, sizeof(gba_memory));
    if (!result) {
        result = gba_init_static(&gba_instance, &memory);
    }
    if (!result) {
        result = gba_load_rom(&gba_instance, gba_rom, sizeof(gba_rom));
    }
    if (!result) {
        gba_run_frame(&gba_instance);
    }
#if GBA_USE_FS
    gba_load_file(&gba_instance, "smoke.gba");
#if GBA_ENABLE_SAVE_STATE
    gba_state_save_file(&gba_instance, "smoke.state");
    gba_state_load_file(&gba_instance, "smoke.state");
#endif
#endif
    gba_deinit_static(&gba_instance);
    return result;
}
