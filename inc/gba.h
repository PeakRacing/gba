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
#include "gba_log.h"
#include "gba_dynarec.h"
#include "gba_cpu.h"
#include "gba_timer.h"
#include "gba_ppu.h"
#include "gba_apu.h"
#include "gba_cart.h"
#include "gba_bus.h"
#include "gba_bios.h"
#include "gba_state.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GBA_VERSION_MAJOR          0
#define GBA_VERSION_MINOR          1
#define GBA_VERSION_PATCH          0
#define GBA_STRINGIFY_INNER(value) #value
#define GBA_STRINGIFY(value)       GBA_STRINGIFY_INNER(value)
#define GBA_VERSION_STRING                                                                                             \
    GBA_STRINGIFY(GBA_VERSION_MAJOR) "." GBA_STRINGIFY(GBA_VERSION_MINOR) "." GBA_STRINGIFY(GBA_VERSION_PATCH)
#define GBA_URL        "https://github.com/PeakRacing/gba"
#define GBA_NAME       "GBA"
#define GBA_KEY_A      1u
#define GBA_KEY_B      2u
#define GBA_KEY_SELECT 4u
#define GBA_KEY_START  8u
#define GBA_KEY_RIGHT  16u
#define GBA_KEY_LEFT   32u
#define GBA_KEY_UP     64u
#define GBA_KEY_DOWN   128u
#define GBA_KEY_R      256u
#define GBA_KEY_L      512u

/* Scalar-only hardware state: serialized field-by-field as little endian words. */

typedef struct {
    gba_cpu_t cpu;
    gba_timer_t timer[4];
    gba_dma_t dma[4];
    gba_ppu_t ppu;
    gba_apu_t apu;
    gba_cart_t cart;
    gba_rtc_t rtc;
    uint32_t cycles, frames, keys, open_bus, bios_latch;
    uint32_t prefetch_count, prefetch_phase, last_fetch, frame_skip;
    uint32_t dma_pc, dma_bus, dma_bus_valid;
} gba_machine_t;

struct gba {
    gba_machine_t machine;
    gba_memory_t memory;
    uint8_t io[1024];
    const uint8_t *rom, *bios;
    size_t rom_size;
    gba_rom_read_fn rom_read;
    void *rom_user, *owned_memory, *owned_rom;
    uint32_t cache_tag[GBA_ROM_CACHE_SLOTS], cache_age[GBA_ROM_CACHE_SLOTS];
    uint32_t cache_valid[GBA_ROM_CACHE_SLOTS], cache_clock;
    uint32_t access_cycles, running, stepping, gba_quit, owns_context;
    uint32_t dma_running;
#if GBA_FAST_CPU
    uint32_t cpu_bulk, cpu_pending; /* Derived execution slice, excluded from GAST. */
#if GBA_DYNAREC
    uint32_t native_control_budget; /* Remaining API budget plus one; zero means no active CPU slice. */
#endif
    uint32_t fetch_page_tag;
    const uint8_t *fetch_page; /* Live backing bytes, not decoded/stale instructions. */
#endif
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    struct {
        uint32_t valid, pc, clock, reject_pc, retries, frame, read_hash, previous_read_hash;
        uint32_t r[16], cpsr, pipeline[2], pipeline_valid, fetch_sequential;
        uint32_t prefetch_count, prefetch_phase, open_bus, last_fetch;
        uint32_t dma_pc, dma_bus, dma_bus_valid, bios_latch;
        uint32_t timer_read, stable_until, ppu_read, period;
        uint64_t skipped_cycles, skipped_loops;
    } wait;
#endif
#if GBA_FAST_RUN
    uint32_t dma_ready; /* Derived runnable channel mask, rebuilt at public boundaries. */
#endif
    uint32_t event_cycles; /* Derived deadline for a continuous run; not serialized. */
#if GBA_BATCH_EVENTS
    uint32_t deferred_cycles; /* Derived work before the next observable boundary; not serialized. */
#endif
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
    /* Derived proof for one write-free loop, never part of GAST or machine state. */
    gba_cpu_t idle_cpu;
    uint32_t idle_pc, idle_valid, idle_clock, idle_reject_pc;
    uint32_t idle_prefetch_count, idle_prefetch_phase, idle_open_bus, idle_last_fetch;
    uint32_t idle_dma_pc, idle_dma_bus, idle_dma_bus_valid, idle_bios_latch;
    uint64_t idle_skipped_cycles, idle_skipped_loops;
#endif
#if GBA_DYNAREC
    gba_dynarec_memory_t dynarec_memory;
#endif
    int fault;
    gba_save_type_t save_override;
    uint32_t pixel_top[GBA_WIDTH], pixel_second[GBA_WIDTH];
    uint8_t object_window[GBA_WIDTH];
#if GBA_PPU_SCANLINE_FAST
    struct { gba_color_t color[512]; uint32_t valid; } scanline_palette;
#endif
#if GBA_PPU_OBJ_INDEX
    /* Derived OAM candidates for eight-line bands; never serialized. */
    struct { uint32_t valid, band[20][4]; } object_index;
#endif
#if GBA_PPU_OAM_DIRTY
    struct { uint32_t coherent, words[8]; } oam_dirty;
#endif
#if GBA_ENABLE_SOUND
    int16_t audio[GBA_APU_BUFFER_SAMPLES * 2];
    uint32_t audio_count;
#endif
#if GBA_USE_FS
    gba_file_t *rom_file;
    char save_path[GBA_PATH_MAX];
    uint32_t persist_blocked;
    uint32_t reload_required;
#endif
#if GBA_TEST_PROFILE
    uint32_t profile_cpu, profile_dma, profile_ppu, profile_apu;
    uint32_t cache_hits, cache_misses;
#endif
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
    struct {
        uint32_t generation, oam_generation, latched_oam_generation;
        gba_video_row_t row[GBA_HEIGHT];
        uint32_t dependency[3200][5]; /* VRAM 32-byte blocks and palette 8-byte blocks. */
        uint32_t dirty[5], pending_oam[5], active_word, active_mask;
#if GBA_PPU_READ_DEDUP
        uint32_t last_vram_block,last_palette_block;
#endif
        uint64_t framebuffer_revision;
    } video_cache; /* Cold derived keys; retain the hot CPU/bus field offsets. */
#endif
#if GBA_FAST_BLOCKS && !GBA_TEST_MODE
    gba_register_block_t register_block[64]; /* Validated live bytes; derived, never serialized. */
#endif
};
size_t gba_memory_size(void);
int gba_memory_bind(gba_memory_t *memory, void *buffer, size_t size);
int gba_init_static(gba_t *gba, const gba_memory_t *memory);
int gba_deinit_static(gba_t *gba);
gba_t *gba_init(void);
int gba_deinit(gba_t *gba);
int gba_reset(gba_t *gba);
/* With GBA_VIDEO_CACHE enabled, notify after external edits to borrowed video
 * memory/framebuffer or machine OAM latch. CPU/DMA/reset/state paths do this. */
void gba_video_invalidate(gba_t *gba);
void gba_run(gba_t *gba);
uint32_t gba_step(gba_t *gba);
uint32_t gba_run_cycles(gba_t *gba, uint32_t cycles);
uint32_t gba_run_frame(gba_t *gba);
int gba_load_rom(gba_t *gba, const uint8_t *rom, size_t size);
int gba_load_stream(gba_t *gba, size_t size, gba_rom_read_fn read, void *user);
int gba_unload_rom(gba_t *gba);
int gba_unload_rom_discard(gba_t *gba);
int gba_set_bios(gba_t *gba, const uint8_t *bios, size_t size);
void gba_set_keys(gba_t *gba, uint16_t keys);
int gba_initex(gba_t *gba);
int gba_deinitex(gba_t *gba);
void gba_frame(gba_t *gba);
#ifdef __cplusplus
}
#endif
