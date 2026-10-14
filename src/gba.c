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

int gba_reset(gba_t *g) {
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
    g->event_cycles = 0;
    gba_memcpy(&rtc, &g->machine.rtc, sizeof(rtc));
    gba_memset(&g->machine, 0, sizeof(g->machine));
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
static uint32_t gba_step_internal(gba_t *g) {
    uint32_t cycles;
    gba_cpu_t *cpu;
    if (!g || !g->rom_size || g->fault || g->gba_quit) {
        return 0;
    }
    cpu = &g->machine.cpu;
    gba_irq(g, 0);
    g->access_cycles = 0;
    if (cpu->stopped) {
        return 0;
    }
    cycles = gba_dma_step(g);
    if (cycles) {
        gba_advance(g, cycles - g->access_cycles);
        return cycles;
    }
    if (cpu->halted && (!(gba_io16(g, 0x200) & gba_io16(g, 0x202)) || cpu->irq_delay)) {
        cycles = gba_next_event(g);
    } else {
        cpu->halted = 0;
#if GBA_TEST_PROFILE
        uint32_t start = gba_profile_ticks();
#endif
        cycles = gba_cpu_execute(g);
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
uint32_t gba_step(gba_t *g) {
    if (!g || g->stepping) {
        return 0;
    }
    uint32_t previous = g->running;
    g->event_cycles = 0;
    g->running = g->stepping = 1;
    uint32_t cycles = gba_step_internal(g);
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
    g->running = g->stepping = 1;
    g->event_cycles = 0;
    while (total < budget && !g->fault && !g->gba_quit) {
        uint32_t cycles = gba_step_internal(g);
        if (!cycles) {
            break;
        }
        total += cycles;
    }
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
    begin = g->machine.cycles;
    g->running = g->stepping = 1;
    g->event_cycles = 0;
    while (g->machine.frames == frame && !g->fault && !g->gba_quit) {
        if (!gba_step_internal(g)) {
            break;
        }
    }
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
