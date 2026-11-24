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
#include "gba_internal.h"

size_t gba_memory_size(void) {
    return 0x40000u + 0x8000u + 0x18000u + 0x400u + 0x400u + GBA_SAVE_CAPACITY + GBA_DRAW_SIZE * sizeof(gba_color_t) +
           (GBA_ROM_STREAM ? GBA_ROM_CACHE_SLOTS * GBA_ROM_PAGE_SIZE : 0u);
}

int gba_memory_bind(gba_memory_t *m, void *buffer, size_t size) {
    uint8_t *p = (uint8_t *)buffer;
    if (!m || !buffer || size < gba_memory_size() || ((uintptr_t)buffer & 3u)) {
        return GBA_ERROR_MEMORY;
    }
    gba_memset(m, 0, sizeof(*m));
#define GBA_BIND(member, amount)                                                                                       \
    m->member = p;                                                                                                     \
    m->member##_size = (amount);                                                                                       \
    p += (amount)
    GBA_BIND(ewram, 0x40000u);
    GBA_BIND(iwram, 0x8000u);
    GBA_BIND(vram, 0x18000u);
    GBA_BIND(palette, 0x400u);
    GBA_BIND(oam, 0x400u);
    GBA_BIND(save, GBA_SAVE_CAPACITY);
#undef GBA_BIND
    m->framebuffer = (gba_color_t *)p;
    m->framebuffer_pixels = GBA_DRAW_SIZE;
    p += GBA_DRAW_SIZE * sizeof(gba_color_t);
#if GBA_ROM_STREAM
    m->rom_cache = p;
    m->rom_cache_size = GBA_ROM_CACHE_SLOTS * GBA_ROM_PAGE_SIZE;
#endif
    return GBA_OK;
}

static int gba_memory_disjoint(const gba_t *g, const gba_memory_t *m) {
    const void *addresses[] = {
        g, m->ewram, m->iwram, m->vram, m->palette, m->oam, m->save, m->framebuffer, m->rom_cache};
    const size_t lengths[] = {sizeof(*g), 0x40000, 0x8000, 0x18000, 0x400, 0x400, GBA_SAVE_CAPACITY,
        GBA_DRAW_SIZE * sizeof(gba_color_t), GBA_ROM_STREAM ? GBA_ROM_CACHE_SLOTS * GBA_ROM_PAGE_SIZE : 0};
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(*addresses); i++) {
        uintptr_t begin = (uintptr_t)addresses[i];
        if (!lengths[i]) {
            continue;
        }
        if (begin > UINTPTR_MAX - lengths[i]) {
            return 0;
        }
        for (unsigned j = 0; j < i; j++) {
            uintptr_t other = (uintptr_t)addresses[j];
            if (lengths[j] && begin < other + lengths[j] && other < begin + lengths[i]) {
                return 0;
            }
        }
    }
    return 1;
}
int gba_init_static(gba_t *g, const gba_memory_t *m) {
    if (!g || !m || !m->ewram || m->ewram_size < 0x40000u || !m->iwram || m->iwram_size < 0x8000u || !m->vram ||
        m->vram_size < 0x18000u || !m->palette || m->palette_size < 0x400u || !m->oam || m->oam_size < 0x400u ||
        !m->framebuffer || m->framebuffer_pixels < GBA_DRAW_SIZE ||
        ((uintptr_t)m->framebuffer % _Alignof(gba_color_t))) {
        return GBA_ERROR_MEMORY;
    }
#if GBA_ENABLE_SAVE
    if (!m->save || m->save_size < GBA_SAVE_CAPACITY) {
        return GBA_ERROR_MEMORY;
    }
#endif
#if GBA_ROM_STREAM
    if (!m->rom_cache || m->rom_cache_size < GBA_ROM_CACHE_SLOTS * GBA_ROM_PAGE_SIZE) {
        return GBA_ERROR_MEMORY;
    }
#endif
    if ((uintptr_t)g % _Alignof(gba_t) || !gba_memory_disjoint(g, m)) {
        return GBA_ERROR_MEMORY;
    }
    gba_memory_t memory;
    gba_memcpy(&memory, m, sizeof(memory));
    gba_memset(g, 0, sizeof(*g));
    gba_memcpy(&g->memory, &memory, sizeof(memory));
    g->machine.rtc.year = 0;
    g->machine.rtc.month = 1;
    g->machine.rtc.day = 1;
    g->machine.rtc.weekday = 6;
    g->machine.rtc.control = 0x40;
    gba_reset(g);
    return gba_initex(g);
}

gba_t *gba_init(void) {
    gba_t *g = (gba_t *)gba_malloc(sizeof(*g));
    gba_memory_t memory;
    void *buffer;
    if (!g) {
        return NULL;
    }
    buffer = gba_malloc(gba_memory_size());
    if (!buffer || gba_memory_bind(&memory, buffer, gba_memory_size()) || gba_init_static(g, &memory)) {
        gba_free(buffer);
        gba_free(g);
        return NULL;
    }
    g->owned_memory = buffer;
    g->owns_context = 1;
    return g;
}

void gba_video_invalidate(gba_t *g) {
#if GBA_PPU_SCANLINE_FAST
    if(g)g->scanline_palette.valid=0;
#endif
#if GBA_PPU_OBJ_INDEX
    if (g) g->object_index.valid = 0;
#endif
#if GBA_PPU_OAM_DIRTY
    if (g) g->oam_dirty.coherent = 0;
#endif
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
    if (g) {
        uint64_t revision = g->video_cache.framebuffer_revision;
        gba_memset(&g->video_cache, 0, sizeof(g->video_cache));
        /* The currently latched OAM may differ from live OAM. Keep its unknown
         * generation distinct until the next actual hardware latch. */
        g->video_cache.generation = g->video_cache.oam_generation = 1;
        g->video_cache.framebuffer_revision = revision == UINT64_MAX ? revision : revision + 1;
    }
#else
    (void)g;
#endif
}
int gba_reset(gba_t *g) {
#if GBA_DYNAREC
    if (g)
        gba_dynarec_invalidate(g, 0, SIZE_MAX);
#endif
    gba_cart_t cart;
    gba_rtc_t rtc;
    if (!g || g->running) {
        return GBA_ERROR_BUSY;
    }
    if (g->machine.cart.size > GBA_SAVE_CAPACITY || g->machine.cart.size > g->memory.save_size) {
        return GBA_ERROR_MEMORY;
    }
#if GBA_USE_FS
    if (g->reload_required && g->rom_size) {
        return g->fault ? g->fault : GBA_ERROR_FORMAT;
    }
    g->reload_required = 0;
#endif
    gba_memcpy(&cart, &g->machine.cart, sizeof(cart));
    gba_video_invalidate(g);
    g->event_cycles = 0;
    gba_wait_invalidate(g);
#if GBA_FAST_CPU
    g->cpu_bulk = g->cpu_pending = 0;
#if GBA_DYNAREC
    g->native_control_budget=0;
#endif
    g->fetch_page = NULL;
#endif
    gba_idle_invalidate(g);
#if GBA_BATCH_EVENTS
    g->deferred_cycles = 0;
#endif
    gba_memcpy(&rtc, &g->machine.rtc, sizeof(rtc));
    gba_memset(&g->machine, 0, sizeof(g->machine));
    gba_dma_refresh_ready(g);
    gba_memcpy(&g->machine.cart, &cart, sizeof(cart));
    gba_memcpy(&g->machine.rtc, &rtc, sizeof(rtc));
    g->machine.cart.flash_phase = g->machine.cart.flash_id = g->machine.cart.eeprom_count = 0;
    gba_memset(g->io, 0, sizeof(g->io));
    gba_memset(g->memory.ewram, 0, 0x40000);
    gba_memset(g->memory.iwram, 0, 0x8000);
    gba_memset(g->memory.vram, 0, 0x18000);
    gba_memset(g->memory.palette, 0, 0x400);
    gba_memset(g->memory.oam, 0, 0x400);
    gba_memset(g->memory.framebuffer, 0, GBA_DRAW_SIZE * sizeof(gba_color_t));
    gba_memset(g->cache_valid, 0, sizeof(g->cache_valid));
    gba_put16(g->io + 0x130, 0x3ff);
    gba_put16(g->io + 0x088, 0x200);
    gba_put16(g->io + 0x20, 0x100);
    gba_put16(g->io + 0x26, 0x100);
    gba_put16(g->io + 0x30, 0x100);
    gba_put16(g->io + 0x36, 0x100);
    g->fault = 0;
    g->gba_quit = 0;
    g->access_cycles = 0;
    g->machine.cpu.cpsr = g->bios ? 0xd3 : 0x1f;
    if (!g->bios) {
        g->machine.bios_latch = 0xe129f000;
        g->machine.cpu.r[15] = 0x08000000;
        g->machine.cpu.r[13] = 0x03007f00;
        g->machine.cpu.bank[2][5] = 0x03007fa0;
        g->machine.cpu.bank[3][5] = 0x03007fe0;
        gba_put16(g->io, 0x80);
        g->io[0x300] = 1;
    }
    gba_rtc_rebase(g);
#if GBA_ENABLE_SOUND
    g->audio_count = 0;
#endif
    return GBA_OK;
}

static int gba_load_begin(gba_t *g, size_t size) {
    if (!g || size < 0xc0u || size > 0x2000000u) {
        return GBA_ERROR_ROM;
    }
    if (g->running || g->rom_size) {
        return GBA_ERROR_BUSY;
    }
#if !GBA_BIOS_HLE
    if (!g->bios) {
        return GBA_ERROR_UNSUPPORTED;
    }
#endif
    g->rom_size = size;
#if GBA_ENABLE_SAVE
    gba_memset(g->memory.save, 0xff, GBA_SAVE_CAPACITY);
#endif
    return GBA_OK;
}
int gba_load_rom(gba_t *g, const uint8_t *rom, size_t size) {
    int result;
    if (!rom) {
        return GBA_ERROR_ROM;
    }
    result = gba_load_begin(g, size);
    if (result) {
        return result;
    }
    g->rom = rom;
    gba_reset(g);
    gba_cart_detect(g);
    return g->fault;
}
int gba_load_stream(gba_t *g, size_t size, gba_rom_read_fn reader, void *user) {
#if GBA_ROM_STREAM
    int result;
    if (!reader) {
        return GBA_ERROR_ROM;
    }
    result = gba_load_begin(g, size);
    if (result) {
        return result;
    }
    g->rom_read = reader;
    g->rom_user = user;
    gba_reset(g);
    gba_cart_detect(g);
    return g->fault;
#else
    (void)g;
    (void)size;
    (void)reader;
    (void)user;
    return GBA_ERROR_UNSUPPORTED;
#endif
}
int gba_set_bios(gba_t *g, const uint8_t *bios, size_t size) {
    if (!g || g->running || g->rom_size) {
        return GBA_ERROR_BUSY;
    }
    if ((bios && size != 0x4000u) || (!bios && size)) {
        return GBA_ERROR_FORMAT;
    }
    g->bios = bios;
    return gba_reset(g);
}
int gba_unload_rom_discard(gba_t *g) {
    if (!g || g->running) {
        return GBA_ERROR_BUSY;
    }
#if GBA_USE_FS
    if (g->rom_file) {
        gba_file_t *file = g->rom_file;
        g->rom_file = NULL;
        g->rom_read = NULL;
        g->rom_user = NULL;
        if (gba_fclose(file)) {
            g->reload_required = 1;
            gba_fail(g, GBA_ERROR_IO);
            return GBA_ERROR_IO;
        }
    }
    g->save_path[0] = 0;
    g->persist_blocked = 0;
#endif
    gba_free(g->owned_rom);
    g->owned_rom = NULL;
    g->rom = NULL;
    g->rom_read = NULL;
    g->rom_user = NULL;
#if GBA_DYNAREC
    gba_dynarec_invalidate(g, 0, SIZE_MAX);
#endif
    g->rom_size = 0;
    gba_memset(&g->machine.cart, 0, sizeof(g->machine.cart));
    return gba_reset(g);
}
int gba_unload_rom(gba_t *g) {
    if (!g || g->running) {
        return GBA_ERROR_BUSY;
    }
    if (g->fault) {
        return g->fault;
    }
#if GBA_USE_FS
    {
        int result = gba_persist_save(g);
        if (result) {
            return result;
        }
    }
#endif
    return gba_unload_rom_discard(g);
}
int gba_deinit_static(gba_t *g) {
    int result = gba_unload_rom(g);
    return result ? result : gba_deinitex(g);
}
int gba_deinit(gba_t *g) {
    int result;
    void *memory;
    if (!g || !g->owns_context) {
        return GBA_ERROR;
    }
    memory = g->owned_memory;
    result = gba_deinit_static(g);
    if (!result) {
        gba_free(memory);
        gba_free(g);
    }
    return result;
}
static uint32_t gba_step_internal(gba_t *g, uint32_t budget) {
    uint32_t cycles;
    gba_cpu_t *cpu;
    if (!g || !g->rom_size || g->fault || g->gba_quit) {
        return 0;
    }
    cpu = &g->machine.cpu;
#if !GBA_FAST_RUN
    gba_irq(g, 0);
#endif
    g->access_cycles = 0;
    if (cpu->stopped) {
        return 0;
    }
    {
        GBA_PROFILE_ENTER(GBA_PROF_DMA);
        cycles = gba_dma_step(g);
        GBA_PROFILE_LEAVE();
    }
    if (cycles) {
        gba_advance(g, cycles - g->access_cycles);
        return cycles;
    }
    if (cpu->halted && (!gba_irq_pending(g) || cpu->irq_delay)) {
        cycles = gba_next_event(g);
    } else {
        cpu->halted = 0;
#if GBA_FAST_CPU
        if (budget) {
            GBA_PROFILE_ENTER(GBA_PROF_CPU);
            cycles = gba_cpu_run(g, budget);
            GBA_PROFILE_LEAVE();
            return cycles;
        }
#else
        (void)budget;
#endif
#if GBA_TEST_PROFILE
        uint32_t start = gba_profile_ticks();
#endif
        {
            GBA_PROFILE_ENTER(GBA_PROF_CPU);
            cycles = gba_cpu_execute(g);
            GBA_PROFILE_LEAVE();
        }
#if GBA_TEST_PROFILE
        g->profile_cpu += gba_profile_ticks() - start;
#endif
    }
    if (!cycles) {
        cycles = 1;
    }
    gba_advance(g, cycles - g->access_cycles);
    return cycles;
}
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
#if !GBA_BATCH_EVENTS
#error Idle loop proof requires event-bounded deferred progression
#endif
static int gba_idle_cpu_equal(const gba_cpu_t *a, const gba_cpu_t *b) {
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    /* All CPU state is uint32_t: byte comparison includes pipeline and banks. */
    for (size_t i = 0; i < sizeof(*a); i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}
/* An execution memo, rather than an address database or a HALT heuristic.
 * A complete iteration must leave CPU/pipeline/prefetch/bus state unchanged,
 * read only stable RAM/ROM, write nothing and cross no hardware deadline.
 * Consequently another whole iteration has exactly the same cost and effects.
 * We repeat only whole proven iterations strictly before the next event and
 * execution budget. Hardware then runs its original event path normally. */
static uint32_t gba_idle_repeat(gba_t *g, uint32_t previous_pc, uint32_t budget) {
    gba_cpu_t *c = &g->machine.cpu;
    gba_machine_t *m = &g->machine;
    uint32_t clock = m->cycles + g->deferred_cycles, skipped = 0;
    if (g->idle_valid && c->r[15] == g->idle_pc) {
        uint32_t period = clock - g->idle_clock;
        if (period && period <= 4096 && gba_idle_cpu_equal(c, &g->idle_cpu) &&
            m->prefetch_count == g->idle_prefetch_count && m->prefetch_phase == g->idle_prefetch_phase &&
            m->open_bus == g->idle_open_bus && m->last_fetch == g->idle_last_fetch && m->dma_pc == g->idle_dma_pc &&
            m->dma_bus == g->idle_dma_bus && m->dma_bus_valid == g->idle_dma_bus_valid &&
            m->bios_latch == g->idle_bios_latch) {
            uint32_t distance = gba_next_event(g);
            uint32_t limit = distance < budget ? distance : budget;
            uint32_t loops = limit > 1 ? (limit - 1) / period : 0;
            if (loops) {
                skipped = loops * period;
                gba_advance(g, skipped);
                g->idle_skipped_cycles += skipped;
                g->idle_skipped_loops += loops;
            }
        } else
            g->idle_reject_pc = g->idle_pc;
        g->idle_valid = 0;
    }
    unsigned region = c->r[15] >> 24;
    if (!g->idle_valid && c->r[15] != g->idle_reject_pc && c->r[15] <= previous_pc && !c->halted && !c->stopped &&
        !c->wait_mask && !c->hle_irq && !g->fault && g->rom &&
        (region == 2 || region == 3 || (region >= 8 && region <= 12))) {
        gba_memcpy(&g->idle_cpu, c, sizeof(*c));
        g->idle_pc = c->r[15];
        g->idle_valid = 1;
        g->idle_clock = clock + skipped;
        g->idle_prefetch_count = m->prefetch_count;
        g->idle_prefetch_phase = m->prefetch_phase;
        g->idle_open_bus = m->open_bus;
        g->idle_last_fetch = m->last_fetch;
        g->idle_dma_pc = m->dma_pc;
        g->idle_dma_bus = m->dma_bus;
        g->idle_dma_bus_valid = m->dma_bus_valid;
        g->idle_bios_latch = m->bios_latch;
    }
    return skipped;
}
#endif
uint32_t gba_step(gba_t *g) {
    if (!g || g->stepping) {
        return 0;
    }
    uint32_t previous = g->running;
    gba_wait_invalidate(g);
#if GBA_FAST_RUN
    gba_irq(g, 0);
#endif
    gba_idle_invalidate(g);
    gba_dma_refresh_ready(g);
    g->event_cycles = 0;
    g->running = g->stepping = 1;
    uint32_t cycles = gba_step_internal(g, 0);
#if GBA_BATCH_EVENTS
    gba_flush_events(g);
#endif
    g->stepping = 0;
    g->running = previous;
    return cycles;
}
uint32_t gba_run_cycles(gba_t *g, uint32_t budget) {
    uint64_t total = 0;
    uint32_t previous;
    if (!g || g->running) {
        return 0;
    }
    previous = g->running;
    gba_wait_invalidate(g);
#if GBA_FAST_RUN
    gba_irq(g, 0);
#endif
    gba_idle_invalidate(g);
    gba_dma_refresh_ready(g);
    g->running = g->stepping = 1;
    g->event_cycles = 0;
    while (total < budget && !g->fault && !g->gba_quit) {
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
        uint32_t previous_pc = g->machine.cpu.r[15];
#endif
        uint32_t cycles = gba_step_internal(g, budget - (uint32_t)total);
        if (!cycles) {
            break;
        }
        total += cycles;
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
        if (total < budget && !g->gba_quit)
            total += gba_idle_repeat(g, previous_pc, budget - (uint32_t)total);
#endif
    }
#if GBA_BATCH_EVENTS
    gba_flush_events(g);
#endif
    g->stepping = 0;
    g->running = previous;
    return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}
uint32_t gba_run_frame(gba_t *g) {
    uint32_t frame, begin;
    if (!g || g->running) {
        return 0;
    }
    if (g->machine.cpu.stopped) {
        gba_rtc_sync(g);
    }
    frame = g->machine.frames;
    gba_wait_invalidate(g);
#if GBA_FAST_RUN
    gba_irq(g, 0);
#endif
    gba_idle_invalidate(g);
    gba_dma_refresh_ready(g);
    begin = g->machine.cycles;
    g->running = g->stepping = 1;
    g->event_cycles = 0;
    while (g->machine.frames == frame && !g->fault && !g->gba_quit) {
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
        uint32_t previous_pc = g->machine.cpu.r[15];
#endif
        if (!gba_step_internal(g, UINT32_MAX)) {
            break;
        }
#if GBA_IDLE_LOOPS && !GBA_TEST_MODE
        if (g->machine.frames == frame && !g->fault && !g->gba_quit)
            gba_idle_repeat(g, previous_pc, UINT32_MAX);
#endif
    }
#if GBA_BATCH_EVENTS
    gba_flush_events(g);
#endif
    g->running = g->stepping = 0;
    return g->machine.cycles - begin;
}
void gba_run(gba_t *g) {
    if (!g || g->running) {
        return;
    }
    while (!g->gba_quit && !g->fault) {
        if (!gba_run_frame(g)) {
            gba_frame(g);
            gba_rtc_sync(g);
        }
    }
}
void gba_set_keys(gba_t *g, uint16_t keys) {
    uint16_t control, selected;
    if (!g) {
        return;
    }
    if(g->machine.keys!=(keys&0x3ff))gba_wait_invalidate(g);
    g->machine.keys = keys & 0x3ff;
    gba_put16(g->io + 0x130, (uint16_t)(0x3ff ^ g->machine.keys));
    control = gba_io16(g, 0x132);
    selected = control & 0x3ff;
    if ((control & 0x4000) && selected && ((control & 0x8000) ? ((keys & selected) == selected) : (keys & selected))) {
        gba_irq(g, 0x1000);
        g->machine.cpu.stopped = 0;
    }
}
uint32_t gba_crc32(uint32_t crc, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    while (size--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}
