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
#define GBA_VERSION_STRING "0.1.0"
#define GBA_NAME "GBA"
#define GBA_KEY_A 1u
#define GBA_KEY_B 2u
#define GBA_KEY_SELECT 4u
#define GBA_KEY_START 8u
#define GBA_KEY_RIGHT 16u
#define GBA_KEY_LEFT 32u
#define GBA_KEY_UP 64u
#define GBA_KEY_DOWN 128u
#define GBA_KEY_R 256u
#define GBA_KEY_L 512u
typedef enum {
    GBA_SAVE_AUTO,
    GBA_SAVE_NONE,
    GBA_SAVE_SRAM,
    GBA_SAVE_FLASH64,
    GBA_SAVE_FLASH128,
    GBA_SAVE_EEPROM512,
    GBA_SAVE_EEPROM8K
} gba_save_type_t;
typedef size_t (*gba_rom_read_fn)(void *user, uint32_t offset, void *data, size_t size);
typedef struct {
    uint8_t *ewram, *iwram, *vram, *palette, *oam, *save, *rom_cache;
    size_t ewram_size, iwram_size, vram_size, palette_size, oam_size;
    size_t save_size, rom_cache_size;
    gba_color_t *framebuffer;
    size_t framebuffer_pixels;
} gba_memory_t;

/* Scalar-only hardware state: serialized field-by-field as little endian words. */
typedef struct {
    uint32_t r[16], bank[6][7], spsr[6], cpsr;
    uint32_t pipeline[2], pipeline_valid, fetch_sequential;
    uint32_t halted, stopped, wait_mask, wait_discard, hle_irq, hle_lr;
    uint32_t irq_line, irq_delay;
} gba_cpu_t;
typedef struct {
    uint32_t counter, reload, control, phase, startup, reload_delay;
} gba_timer_t;
typedef struct {
    uint32_t source, destination, count, remaining, control, pending;
    uint32_t initial_destination, sequential, fifo;
    uint32_t latch, startup;
} gba_dma_t;
typedef struct {
    uint32_t line, phase, ref_x[2], ref_y[2];
    uint32_t window_active;
    uint32_t bg_delay[4];
    uint32_t oam_latch[256], oam_latch_valid;
} gba_ppu_t;
typedef struct {
    uint32_t enabled, phase, position, length, volume, envelope_phase;
    uint32_t sweep_phase, sweep_frequency, sweep_negated, lfsr;
} gba_apu_channel_t;
typedef struct {
    gba_apu_channel_t channel[4];
    uint32_t fifo[2][32], fifo_read[2], fifo_count[2], fifo_sample[2];
    uint32_t wave[32], sample_phase, sequencer_phase, sequencer_step;
} gba_apu_t;
typedef struct {
    uint32_t type, size, dirty, flash_phase, flash_bank, flash_id;
    uint32_t eeprom_bits[82], eeprom_count, eeprom_read, eeprom_address;
    uint32_t gpio_data, gpio_direction, gpio_control, rtc_enabled;
} gba_cart_t;
typedef struct {
    uint32_t year, month, day, weekday, hour, minute, second, milliseconds;
    uint32_t last_ticks, control, chip_select, clock, command, bits;
    uint32_t transfer[7], transfer_size, reading, output, dirty;
} gba_rtc_t;
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
typedef struct gba {
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
    uint32_t event_cycles; /* Derived deadline for a continuous run; not serialized. */
    int fault;
    gba_save_type_t save_override;
    uint32_t pixel_top[GBA_WIDTH], pixel_second[GBA_WIDTH];
    uint8_t object_window[GBA_WIDTH];
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
} gba_t;
size_t gba_memory_size(void);
int gba_memory_bind(gba_memory_t *memory, void *buffer, size_t size);
int gba_init_static(gba_t *gba, const gba_memory_t *memory);
int gba_deinit_static(gba_t *gba);
gba_t *gba_init(void);
int gba_deinit(gba_t *gba);
int gba_reset(gba_t *gba);
void gba_run(gba_t *gba);
uint32_t gba_step(gba_t *gba);
uint32_t gba_run_cycles(gba_t *gba, uint32_t cycles);
uint32_t gba_run_frame(gba_t *gba);
int gba_load_rom(gba_t *gba, const uint8_t *rom, size_t size);
int gba_load_stream(gba_t *gba, size_t size, gba_rom_read_fn read, void *user);
int gba_unload_rom(gba_t *gba);
int gba_unload_rom_discard(gba_t *gba);
int gba_set_bios(gba_t *gba, const uint8_t *bios, size_t size);
int gba_set_save_type(gba_t *gba, gba_save_type_t type);
void gba_set_keys(gba_t *gba, uint16_t keys);
size_t gba_save_size(const gba_t *gba);
int gba_save_import(gba_t *gba, const void *data, size_t size);
int gba_save_export(gba_t *gba, void *data, size_t size);
void gba_rtc_rebase(gba_t *gba);
int gba_initex(gba_t *gba);
int gba_deinitex(gba_t *gba);
void gba_frame(gba_t *gba);
#if GBA_USE_FS
int gba_load_file(gba_t *gba, const char *path);
int gba_persist_set_path(gba_t *gba, const char *path);
int gba_persist_load(gba_t *gba);
int gba_persist_save(gba_t *gba);
#if GBA_ENABLE_SAVE_STATE
int gba_state_save_file(gba_t *gba, const char *path);
int gba_state_load_file(gba_t *gba, const char *path);
void gba_state_loaded(gba_t *gba);
#endif
#endif
#ifdef __cplusplus
}
#endif
