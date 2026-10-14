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
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "gba_conf.h"

#ifndef GBA_ENABLE_SOUND
#define GBA_ENABLE_SOUND 0
#endif
#ifndef GBA_USE_FS
#define GBA_USE_FS 0
#endif
#ifndef GBA_ENABLE_SAVE_STATE
#define GBA_ENABLE_SAVE_STATE 0
#endif
#ifndef GBA_ENABLE_SAVE
#define GBA_ENABLE_SAVE 1
#endif
#ifndef GBA_ENABLE_RTC
#define GBA_ENABLE_RTC 1
#endif
#ifndef GBA_BIOS_HLE
#define GBA_BIOS_HLE 1
#endif
#ifndef GBA_ROM_STREAM
#define GBA_ROM_STREAM 0
#endif
#ifndef GBA_ROM_CACHE_SLOTS
#define GBA_ROM_CACHE_SLOTS 4
#endif
#ifndef GBA_RAM_LACK
#define GBA_RAM_LACK 0
#endif
#ifndef GBA_COLOR_DEPTH
#define GBA_COLOR_DEPTH 16
#endif
#ifndef GBA_COLOR_SWAP
#define GBA_COLOR_SWAP 0
#endif
#ifndef GBA_FRAME_SKIP
#define GBA_FRAME_SKIP 0
#endif
#ifndef GBA_APU_SAMPLE_RATE
#define GBA_APU_SAMPLE_RATE 32000
#endif
#ifndef GBA_APU_BUFFER_SAMPLES
#define GBA_APU_BUFFER_SAMPLES 256
#endif
#ifndef GBA_TEST_MODE
#define GBA_TEST_MODE 0
#endif
#ifndef GBA_TEST_PROFILE
#define GBA_TEST_PROFILE 0
#endif
#ifndef GBA_PATH_MAX
#define GBA_PATH_MAX 256
#endif
#if GBA_COLOR_DEPTH == 16
typedef uint16_t gba_color_t;
#elif GBA_COLOR_DEPTH == 32
typedef uint32_t gba_color_t;
#else
#error "GBA_COLOR_DEPTH must be 16 or 32"
#endif
#if GBA_RAM_LACK != 0 && GBA_RAM_LACK != 1
#error "GBA_RAM_LACK must be 0 or 1"
#endif
#if GBA_ROM_CACHE_SLOTS < 1 || GBA_APU_SAMPLE_RATE < 8000 || GBA_APU_SAMPLE_RATE > 96000
#error "invalid cache or audio configuration"
#endif
#if GBA_APU_BUFFER_SAMPLES < 1 || GBA_APU_BUFFER_SAMPLES > 65535 || GBA_FRAME_SKIP < 0 || GBA_FRAME_SKIP > 255
#error "invalid audio buffer or frame skip configuration"
#endif
#if GBA_ENABLE_SAVE_STATE && !GBA_USE_FS
#error "file save states require GBA_USE_FS"
#endif
#define GBA_WIDTH 240
#define GBA_HEIGHT 160
#define GBA_DRAW_LINES (GBA_HEIGHT / (GBA_RAM_LACK + 1))
#define GBA_DRAW_SIZE (GBA_WIDTH * GBA_DRAW_LINES)
#define GBA_CPU_CLOCK 16777216u
#define GBA_CYCLES_PER_LINE 1232u
#define GBA_HBLANK_START 1006u
#define GBA_LINES_PER_FRAME 228u
#define GBA_CYCLES_PER_FRAME (GBA_CYCLES_PER_LINE * GBA_LINES_PER_FRAME)
#define GBA_ROM_PAGE_SIZE 8192u
#ifndef GBA_SAVE_CAPACITY
#define GBA_SAVE_CAPACITY (GBA_ENABLE_SAVE ? 131072u : 0u)
#endif
#if GBA_SAVE_CAPACITY > 131072 || (GBA_ENABLE_SAVE && GBA_SAVE_CAPACITY < 512) ||                                      \
    (!GBA_ENABLE_SAVE && GBA_SAVE_CAPACITY)
#error "invalid save capacity"
#endif
#if defined(__GNUC__) || defined(__ARMCC_VERSION)
#define GBA_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define GBA_WEAK __weak
#else
#define GBA_WEAK
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum {
    GBA_OK = 0,
    GBA_ERROR = -1,
    GBA_ERROR_ALLOC = -2,
    GBA_ERROR_ROM = -3,
    GBA_ERROR_IO = -4,
    GBA_ERROR_UNSUPPORTED = -5,
    GBA_ERROR_FORMAT = -6,
    GBA_ERROR_MISMATCH = -7,
    GBA_ERROR_MEMORY = -8,
    GBA_ERROR_BUSY = -9
};
void *gba_malloc(size_t size);
void gba_free(void *address);
void *gba_memcpy(void *destination, const void *source, size_t size);
void *gba_memset(void *destination, int value, size_t size);
int gba_log_printf(const char *format, ...);
uint32_t gba_get_ticks(void);
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels);
int gba_sound_output(uint8_t *buffer, size_t bytes);
#if GBA_USE_FS
typedef void gba_file_t;
gba_file_t *gba_fopen(const char *path, const char *mode);
size_t gba_fread(void *data, size_t size, size_t count, gba_file_t *file);
size_t gba_fwrite(const void *data, size_t size, size_t count, gba_file_t *file);
int gba_fseek(gba_file_t *file, long offset, int origin);
long gba_ftell(gba_file_t *file);
int gba_fclose(gba_file_t *file);
int gba_file_sync(gba_file_t *file);
int gba_file_replace(const char *temporary, const char *destination);
int gba_file_remove(const char *path);
int gba_file_exists(const char *path);
#endif
#ifdef __cplusplus
}
#endif
