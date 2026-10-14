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
#if GBA_USE_FS
#include <stdio.h>
static int gba_path_join(char *out, const char *base, const char *suffix) {
    size_t n = 0, s = 0;
    while (base[n]) {
        n++;
    }
    while (suffix[s]) {
        s++;
    }
    if (n + s >= GBA_PATH_MAX) {
        return GBA_ERROR_FORMAT;
    }
    gba_memcpy(out, base, n);
    gba_memcpy(out + n, suffix, s + 1);
    return 0;
}
#if GBA_ROM_STREAM
static size_t gba_file_reader(void *user, uint32_t offset, void *data, size_t size) {
    gba_file_t *file = (gba_file_t *)user;
    if (gba_fseek(file, (long)offset, SEEK_SET)) {
        return 0;
    }
    return gba_fread(data, 1, size, file);
}
#endif
int gba_load_file(gba_t *g, const char *path) {
    gba_file_t *file;
    long length;
    int result;
    if (!g || !path || g->running || g->rom_size) {
        return GBA_ERROR_BUSY;
    }
    file = gba_fopen(path, "rb");
    if (!file) {
        return GBA_ERROR_IO;
    }
    if (gba_fseek(file, 0, SEEK_END) || (length = gba_ftell(file)) < 0 || gba_fseek(file, 0, SEEK_SET)) {
        gba_fclose(file);
        return GBA_ERROR_IO;
    }
    if (length < 0xc0 || length > 0x2000000) {
        gba_fclose(file);
        return GBA_ERROR_ROM;
    }
#if GBA_ROM_STREAM
    result = gba_load_stream(g, (size_t)length, gba_file_reader, file);
    if (g->rom_size) {
        g->rom_file = file;
    } else {
        gba_fclose(file);
    }
#else
    uint8_t *rom = (uint8_t *)gba_malloc((size_t)length);
    if (!rom) {
        gba_fclose(file);
        return GBA_ERROR_ALLOC;
    }
    int good = gba_fread(rom, 1, (size_t)length, file) == (size_t)length;
    if (gba_fclose(file)) {
        good = 0;
    }
    if (!good) {
        gba_free(rom);
        return GBA_ERROR_IO;
    }
    result = gba_load_rom(g, rom, (size_t)length);
    if (g->rom_size) {
        g->owned_rom = rom;
    } else {
        gba_free(rom);
    }
#endif
    if (!result) {
        result = gba_persist_set_path(g, path);
    }
    if (!result) {
        result = gba_persist_load(g);
    }
    return result;
}
int gba_persist_set_path(gba_t *g, const char *path) {
    if (!g || g->running) {
        return GBA_ERROR_BUSY;
    }
    if (!path) {
        g->save_path[0] = 0;
        g->persist_blocked = 0;
        return 0;
    }
    int result = gba_path_join(g->save_path, path, "");
    if (!result) {
        g->persist_blocked = 0;
    }
    return result;
}
static int gba_write_atomic(gba_t *g, const char *path, const void *data, size_t size) {
    char temporary[GBA_PATH_MAX];
    int result = gba_path_join(temporary, path, ".tmp");
    if (result) {
        return result;
    }
    gba_file_t *file = gba_fopen(temporary, "wb");
    if (!file) {
        return GBA_ERROR_IO;
    }
    int good = gba_fwrite(data, 1, size, file) == size;
    if (good && gba_file_sync(file)) {
        good = 0;
    }
    if (gba_fclose(file)) {
        good = 0;
    }
    if (good && gba_file_replace(temporary, path)) {
        good = 0;
    }
    if (!good) {
        gba_file_remove(temporary);
        return GBA_ERROR_IO;
    }
    (void)g;
    return 0;
}
static int gba_read_exact_file(const char *path, void *data, size_t size, size_t *auto_size, size_t capacity) {
    int exists = gba_file_exists(path);
    if (!exists) {
        return 1;
    }
    if (exists < 0) {
        return GBA_ERROR_IO;
    }
    gba_file_t *file = gba_fopen(path, "rb");
    if (!file) {
        return GBA_ERROR_IO;
    }
    if (auto_size) {
        long length;
        if (gba_fseek(file, 0, SEEK_END) || (length = gba_ftell(file)) < 0 || gba_fseek(file, 0, SEEK_SET)) {
            gba_fclose(file);
            return GBA_ERROR_IO;
        }
        if (length != 512 && length != 8192) {
            gba_fclose(file);
            return GBA_ERROR_FORMAT;
        }
        if ((size_t)length > capacity) {
            gba_fclose(file);
            return GBA_ERROR_MEMORY;
        }
        size = (size_t)length;
    }
    uint8_t extra;
    int good = gba_fread(data, 1, size, file) == size && gba_fread(&extra, 1, 1, file) == 0;
    if (gba_fclose(file)) {
        good = 0;
    }
    if (good && auto_size) {
        *auto_size = size;
    }
    return good ? 0 : GBA_ERROR_FORMAT;
}
static void gba_rtc_encode(gba_t *g, uint8_t data[48]) {
    gba_rtc_t *r = &g->machine.rtc;
    const uint32_t fields[9] = {
        r->year, r->month, r->day, r->weekday, r->hour, r->minute, r->second, r->milliseconds, r->control};
    gba_memcpy(data, "GART", 4);
    gba_put32(data + 4, 1);
    for (unsigned i = 0; i < 9; i++) {
        gba_put32(data + 8 + i * 4, fields[i]);
    }
    gba_put32(data + 44, ~gba_crc32(~0u, data, 44));
}
static int gba_rtc_decode(gba_t *g, const uint8_t data[48]) {
    uint32_t fields[9];
    if (gba_le32(data) != 0x54524147 || gba_le32(data + 4) != 1 || gba_le32(data + 44) != ~gba_crc32(~0u, data, 44)) {
        return GBA_ERROR_FORMAT;
    }
    for (unsigned i = 0; i < 9; i++) {
        fields[i] = gba_le32(data + 8 + i * 4);
    }
    if (fields[0] > 99 || fields[1] < 1 || fields[1] > 12 || fields[2] < 1 ||
        fields[2] > gba_month_days(fields[0], fields[1]) || fields[3] > 6 || fields[4] > 23 || fields[5] > 59 ||
        fields[6] > 59 || fields[7] > 999 || (fields[8] & ~0xeau)) {
        return GBA_ERROR_FORMAT;
    }
    gba_rtc_t *r = &g->machine.rtc;
    r->year = fields[0];
    r->month = fields[1];
    r->day = fields[2];
    r->weekday = fields[3];
    r->hour = fields[4];
    r->minute = fields[5];
    r->second = fields[6];
    r->milliseconds = fields[7];
    r->control = fields[8];
    r->dirty = 0;
    gba_rtc_rebase(g);
    return 0;
}
int gba_persist_load(gba_t *g) {
    char path[GBA_PATH_MAX];
    int result;
    if (!g || g->running) {
        return GBA_ERROR_BUSY;
    }
    if (!g->save_path[0]) {
        return 0;
    }
    if (g->machine.cart.size) {
        result = gba_path_join(path, g->save_path, ".sav");
        if (!result) {
            size_t size = g->machine.cart.size;
            int automatic = g->save_override == GBA_SAVE_AUTO && g->machine.cart.type == GBA_SAVE_EEPROM512;
            size_t capacity = g->memory.save_size < GBA_SAVE_CAPACITY ? g->memory.save_size : GBA_SAVE_CAPACITY;
            result = gba_read_exact_file(path, g->memory.save, size, automatic ? &size : NULL, capacity);
            if (!result && automatic && size == 8192) {
                g->machine.cart.type = GBA_SAVE_EEPROM8K;
                g->machine.cart.size = 8192;
            }
        }
        if (result < 0) {
            g->persist_blocked = 1;
            return result;
        }
        g->machine.cart.dirty = 0;
    }
    if (g->machine.cart.rtc_enabled) {
        uint8_t data[48];
        result = gba_path_join(path, g->save_path, ".rtc");
        if (!result) {
            result = gba_read_exact_file(path, data, sizeof(data), NULL, 0);
        }
        if (!result) {
            result = gba_rtc_decode(g, data);
        }
        if (result < 0) {
            g->persist_blocked = 1;
            return result;
        }
    }
    g->persist_blocked = 0;
    return 0;
}
int gba_persist_save(gba_t *g) {
    char path[GBA_PATH_MAX];
    int result;
    if (!g || g->running) {
        return GBA_ERROR_BUSY;
    }
    if (g->fault) {
        return g->fault;
    }
    if (!g->save_path[0]) {
        return 0;
    }
    if (g->persist_blocked) {
        return GBA_ERROR_FORMAT;
    }
    gba_rtc_sync(g);
    if (g->machine.cart.size && g->machine.cart.dirty) {
        result = gba_path_join(path, g->save_path, ".sav");
        if (!result) {
            result = gba_write_atomic(g, path, g->memory.save, g->machine.cart.size);
        }
        if (result) {
            return result;
        }
        g->machine.cart.dirty = 0;
    }
    if (g->machine.cart.rtc_enabled && g->machine.rtc.dirty) {
        uint8_t data[48];
        gba_rtc_encode(g, data);
        result = gba_path_join(path, g->save_path, ".rtc");
        if (!result) {
            result = gba_write_atomic(g, path, data, sizeof(data));
        }
        if (result) {
            return result;
        }
        g->machine.rtc.dirty = 0;
    }
    return 0;
}
#if GBA_ENABLE_SAVE_STATE
typedef struct {
    gba_file_t *file;
    uint32_t crc, count;
    int mode, error;
    gba_machine_t *candidate;
} gba_codec_t;
static void gba_codec_chunk(gba_codec_t *c, const char *tag, uint32_t length) {
    uint8_t expected[8], actual[8];
    if (c->error) {
        return;
    }
    gba_memcpy(expected, tag, 4);
    gba_put32(expected + 4, length);
    if (c->mode >= 2) {
        if (gba_fread(actual, 1, 8, c->file) != 8) {
            c->error = GBA_ERROR_IO;
            return;
        }
        if (gba_le32(actual) != gba_le32(expected) || gba_le32(actual + 4) != length) {
            c->error = GBA_ERROR_FORMAT;
            return;
        }
    } else {
        gba_memcpy(actual, expected, 8);
        if (c->mode == 1 && gba_fwrite(actual, 1, 8, c->file) != 8) {
            c->error = GBA_ERROR_IO;
            return;
        }
    }
    c->crc = gba_crc32(c->crc, actual, 8);
    c->count += 8;
}
static void gba_codec_bytes(gba_codec_t *c, void *data, size_t size) {
    uint8_t scratch[256];
    uint8_t *p = (uint8_t *)data;
    while (size && !c->error) {
        size_t n = size > sizeof(scratch) ? sizeof(scratch) : size;
        void *bytes = c->mode == 2 ? scratch : p;
        if (c->mode >= 2) {
            if (gba_fread(bytes, 1, n, c->file) != n) {
                c->error = GBA_ERROR_IO;
                break;
            }
        } else if (c->mode == 1 && gba_fwrite(bytes, 1, n, c->file) != n) {
            c->error = GBA_ERROR_IO;
            break;
        }
        c->crc = gba_crc32(c->crc, bytes, n);
        c->count += (uint32_t)n;
        p += n;
        size -= n;
    }
}
static int gba_valid_word(size_t off, uint32_t value) {
#define GBA_FIELD(field) (offsetof(gba_machine_t, field))
    if (off == GBA_FIELD(cpu.cpsr)) {
        unsigned mode = value & 31;
        return mode == 0x10 || mode == 0x11 || mode == 0x12 || mode == 0x13 || mode == 0x17 || mode == 0x1b ||
               mode == 0x1f;
    }
    if (off == GBA_FIELD(ppu.line)) {
        return value < 228;
    }
    if (off == GBA_FIELD(ppu.phase)) {
        return value < 1232;
    }
    if (off == GBA_FIELD(ppu.window_active)) {
        return value < 4;
    }
    if (off == GBA_FIELD(ppu.oam_latch_valid)) {
        return value <= 1;
    }
    if (off >= GBA_FIELD(ppu.bg_delay) && off < GBA_FIELD(ppu.bg_delay) + sizeof(((gba_ppu_t *)0)->bg_delay)) {
        return value <= 3;
    }
    if (off == GBA_FIELD(cart.type)) {
        return value >= GBA_SAVE_NONE && value <= GBA_SAVE_EEPROM8K;
    }
    if (off == GBA_FIELD(cart.size)) {
        return value == 0 || value == 512 || value == 8192 || value == 32768 || value == 65536 || value == 131072;
    }
    if (off == GBA_FIELD(cart.flash_bank)) {
        return value < 2;
    }
    if (off == GBA_FIELD(cart.eeprom_count)) {
        return value <= 82;
    }
    if (off == GBA_FIELD(cart.eeprom_read)) {
        return value <= 68;
    }
    if (off == GBA_FIELD(cpu.pipeline_valid)) {
        return value <= 1;
    }
    if (off == GBA_FIELD(dma_bus_valid)) {
        return value <= 1;
    }
    if (off == GBA_FIELD(cpu.fetch_sequential)) {
        return value <= 1;
    }
    if (off == GBA_FIELD(prefetch_phase)) {
        return value < 9;
    }
    if (off == GBA_FIELD(cpu.irq_line)) {
        return value <= 1;
    }
    if (off == GBA_FIELD(cpu.irq_delay)) {
        return value <= GBA_IRQ_DELAY;
    }
    if (off == GBA_FIELD(cpu.hle_irq)) {
        return value <= 1024;
    }
    if (off == GBA_FIELD(apu.sample_phase)) {
        return value < GBA_CPU_CLOCK;
    }
    if (off == GBA_FIELD(apu.sequencer_phase)) {
        return value < 32768;
    }
    if (off == GBA_FIELD(apu.sequencer_step)) {
        return value < 8;
    }
    for (unsigned i = 0; i < 2; i++) {
        if (off == GBA_FIELD(apu.fifo_read) + i * 4) {
            return value < 32;
        }
        if (off == GBA_FIELD(apu.fifo_count) + i * 4) {
            return value <= 32;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        if (off == GBA_FIELD(timer) + i * sizeof(gba_timer_t) + offsetof(gba_timer_t, counter)) {
            return value < 65536;
        }
        if (off == GBA_FIELD(timer) + i * sizeof(gba_timer_t) + offsetof(gba_timer_t, phase)) {
            return value < 1024;
        }
        if (off == GBA_FIELD(timer) + i * sizeof(gba_timer_t) + offsetof(gba_timer_t, startup)) {
            return value <= GBA_TIMER_START_DELAY;
        }
        if (off == GBA_FIELD(timer) + i * sizeof(gba_timer_t) + offsetof(gba_timer_t, reload)) {
            return value < 65536;
        }
        if (off == GBA_FIELD(timer) + i * sizeof(gba_timer_t) + offsetof(gba_timer_t, reload_delay)) {
            return value <= 1;
        }
        if (off == GBA_FIELD(dma) + i * sizeof(gba_dma_t) + offsetof(gba_dma_t, remaining)) {
            return value <= 65536;
        }
        if (off == GBA_FIELD(dma) + i * sizeof(gba_dma_t) + offsetof(gba_dma_t, pending)) {
            return value <= 1;
        }
        if (off == GBA_FIELD(dma) + i * sizeof(gba_dma_t) + offsetof(gba_dma_t, fifo)) {
            return value <= 1;
        }
        if (off == GBA_FIELD(dma) + i * sizeof(gba_dma_t) + offsetof(gba_dma_t, startup)) {
            return value <= GBA_DMA_EVENT_DELAY;
        }
        if (off == GBA_FIELD(apu.channel) + i * sizeof(gba_apu_channel_t) + offsetof(gba_apu_channel_t, enabled)) {
            return value <= 1;
        }
        if (off == GBA_FIELD(apu.channel) + i * sizeof(gba_apu_channel_t) + offsetof(gba_apu_channel_t, volume)) {
            return value < 16;
        }
        if (off == GBA_FIELD(apu.channel) + i * sizeof(gba_apu_channel_t) + offsetof(gba_apu_channel_t, phase)) {
            return value < 0x2000000;
        }
    }
    if (off >= GBA_FIELD(apu.wave) && off < GBA_FIELD(apu.wave) + sizeof(((gba_apu_t *)0)->wave)) {
        return value < 256;
    }
    if (off >= GBA_FIELD(apu.fifo) && off < GBA_FIELD(apu.fifo) + sizeof(((gba_apu_t *)0)->fifo)) {
        return value < 256;
    }
    if (off >= GBA_FIELD(cart.eeprom_bits) &&
        off < GBA_FIELD(cart.eeprom_bits) + sizeof(((gba_cart_t *)0)->eeprom_bits)) {
        return value <= 1;
    }
    if (off == GBA_FIELD(rtc.transfer_size)) {
        return value <= 7;
    }
    if (off == GBA_FIELD(rtc.bits)) {
        return value <= 56;
    }
    if (off == GBA_FIELD(rtc.control)) {
        return !(value & ~0xeau);
    }
    if (off == GBA_FIELD(rtc.command)) {
        return value <= 255;
    }
    if (off >= GBA_FIELD(rtc.transfer) && off < GBA_FIELD(rtc.transfer) + sizeof(((gba_rtc_t *)0)->transfer)) {
        return value <= 255;
    }
    if (off == GBA_FIELD(rtc.chip_select) || off == GBA_FIELD(rtc.clock) || off == GBA_FIELD(rtc.reading) ||
        off == GBA_FIELD(rtc.output) || off == GBA_FIELD(rtc.dirty)) {
        return value <= 1;
    }
    return 1;
#undef GBA_FIELD
}
static void gba_state_codec(gba_t *g, gba_codec_t *c) {
    _Static_assert(sizeof(gba_machine_t) % 4 == 0, "scalar state layout");
    gba_codec_chunk(c, "MACH", sizeof(gba_machine_t));
    for (size_t off = 0; off < sizeof(gba_machine_t) && !c->error; off += 4) {
        uint8_t bytes[4];
        uint32_t value;
        if (c->mode < 2) {
            gba_memcpy(&value, (uint8_t *)&g->machine + off, 4);
            gba_put32(bytes, value);
        }
        if (c->mode >= 2) {
            if (gba_fread(bytes, 1, 4, c->file) != 4) {
                c->error = GBA_ERROR_IO;
                break;
            }
            c->crc = gba_crc32(c->crc, bytes, 4);
            c->count += 4;
            value = gba_le32(bytes);
            if (!gba_valid_word(off, value)) {
                c->error = GBA_ERROR_FORMAT;
                break;
            }
            if (c->mode == 2 && c->candidate) {
                gba_memcpy((uint8_t *)c->candidate + off, &value, 4);
            }
            if (c->mode == 3) {
                gba_memcpy((uint8_t *)&g->machine + off, &value, 4);
            }
        } else {
            gba_codec_bytes(c, bytes, 4);
        }
    }
    gba_codec_chunk(c, "IO  ", sizeof(g->io));
    gba_codec_bytes(c, g->io, sizeof(g->io));
    gba_codec_chunk(c, "EWRM", 0x40000);
    gba_codec_bytes(c, g->memory.ewram, 0x40000);
    gba_codec_chunk(c, "IWRM", 0x8000);
    gba_codec_bytes(c, g->memory.iwram, 0x8000);
    gba_codec_chunk(c, "VRAM", 0x18000);
    gba_codec_bytes(c, g->memory.vram, 0x18000);
    gba_codec_chunk(c, "PAL ", 0x400);
    gba_codec_bytes(c, g->memory.palette, 0x400);
    gba_codec_chunk(c, "OAM ", 0x400);
    gba_codec_bytes(c, g->memory.oam, 0x400);
    gba_codec_chunk(c, "SAVE", GBA_SAVE_CAPACITY);
    gba_codec_bytes(c, g->memory.save, GBA_SAVE_CAPACITY);
#if GBA_ENABLE_SOUND
    gba_codec_chunk(c, "AUD ", 4 + sizeof(g->audio));
    uint8_t count[4];
    if (c->mode < 2) {
        gba_put32(count, g->audio_count);
        gba_codec_bytes(c, count, 4);
    } else if (!c->error) {
        if (gba_fread(count, 1, 4, c->file) == 4) {
            c->crc = gba_crc32(c->crc, count, 4);
            c->count += 4;
            if (gba_le32(count) >= GBA_APU_BUFFER_SAMPLES) {
                c->error = GBA_ERROR_FORMAT;
            } else if (c->mode == 3) {
                g->audio_count = gba_le32(count);
            }
        } else {
            c->error = GBA_ERROR_IO;
        }
    }
    for (unsigned i = 0; i < GBA_APU_BUFFER_SAMPLES * 2 && !c->error; i++) {
        uint8_t bytes[2];
        if (c->mode < 2) {
            gba_put16(bytes, (uint16_t)g->audio[i]);
            gba_codec_bytes(c, bytes, 2);
        } else if (gba_fread(bytes, 1, 2, c->file) != 2) {
            c->error = GBA_ERROR_IO;
        } else {
            c->crc = gba_crc32(c->crc, bytes, 2);
            c->count += 2;
            if (c->mode == 3) {
                g->audio[i] = (int16_t)gba_sign(gba_le16(bytes), 16);
            }
        }
    }
#endif
}
static uint32_t gba_rom_crc(gba_t *g) {
    uint32_t crc = ~0u;
    uint8_t bytes[256];
    for (uint32_t offset = 0; offset < g->rom_size && !g->fault; offset += 256) {
        size_t n = g->rom_size - offset;
        if (n > sizeof(bytes)) {
            n = sizeof(bytes);
        }
        if (gba_rom_bytes(g, offset, bytes, n)) {
            break;
        }
        crc = gba_crc32(crc, bytes, n);
    }
    return ~crc;
}
static uint32_t gba_state_config(void) {
    const uint32_t values[] = {GBA_COLOR_DEPTH, GBA_RAM_LACK, GBA_ENABLE_SOUND, GBA_ENABLE_SAVE, GBA_ENABLE_RTC,
        GBA_BIOS_HLE, GBA_FRAME_SKIP, GBA_COLOR_SWAP, GBA_APU_SAMPLE_RATE, GBA_APU_BUFFER_SAMPLES};
    uint32_t crc = ~0u;
    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
        uint8_t bytes[4];
        gba_put32(bytes, values[i]);
        crc = gba_crc32(crc, bytes, 4);
    }
    return ~crc;
}
int gba_state_save_file(gba_t *g, const char *path) {
    char temporary[GBA_PATH_MAX];
    uint8_t header[32];
    if (!g || !path || g->running || !g->rom_size || g->fault) {
        return GBA_ERROR_BUSY;
    }
    int result = gba_path_join(temporary, path, ".tmp");
    if (result) {
        return result;
    }
    gba_codec_t c = {NULL, ~0u, 0, 0, 0, NULL};
    gba_state_codec(g, &c);
    gba_memcpy(header, "GAST", 4);
    gba_put32(header + 4, 7);
    gba_put32(header + 8, c.count);
    gba_put32(header + 12, gba_rom_crc(g));
    gba_put32(header + 16, g->bios ? ~gba_crc32(~0u, g->bios, 0x4000) : 0);
    gba_put32(header + 20, gba_state_config());
    gba_put32(header + 24, ~c.crc);
    gba_put32(header + 28, 0);
    if (g->fault) {
        return g->fault;
    }
    c.file = gba_fopen(temporary, "wb");
    if (!c.file) {
        return GBA_ERROR_IO;
    }
    c.mode = 1;
    c.error = 0;
    c.count = 0;
    c.crc = ~0u;
    if (gba_fwrite(header, 1, sizeof(header), c.file) != sizeof(header)) {
        c.error = GBA_ERROR_IO;
    }
    gba_state_codec(g, &c);
    if (!c.error && gba_file_sync(c.file)) {
        c.error = GBA_ERROR_IO;
    }
    if (gba_fclose(c.file)) {
        c.error = GBA_ERROR_IO;
    }
    if (!c.error && gba_file_replace(temporary, path)) {
        c.error = GBA_ERROR_IO;
    }
    if (c.error) {
        gba_file_remove(temporary);
    }
    return c.error;
}
int gba_state_load_file(gba_t *g, const char *path) {
    uint8_t header[32], extra;
    gba_machine_t candidate;
    if (!g || !path || g->running || !g->rom_size || g->fault) {
        return GBA_ERROR_BUSY;
    }
    gba_file_t *file = gba_fopen(path, "rb");
    if (!file) {
        return GBA_ERROR_IO;
    }
    int result = 0;
    if (gba_fread(header, 1, sizeof(header), file) != sizeof(header) || gba_le32(header) != 0x54534147 ||
        gba_le32(header + 4) != 7 || gba_le32(header + 28) != 0) {
        result = GBA_ERROR_FORMAT;
    }
    uint32_t payload = (uint32_t)(sizeof(gba_machine_t) + sizeof(g->io) + 0x60800u + GBA_SAVE_CAPACITY + 64);
#if GBA_ENABLE_SOUND
    payload += 12 + sizeof(g->audio);
#endif
    if (!result && (gba_le32(header + 8) != payload || gba_le32(header + 12) != gba_rom_crc(g) ||
                       gba_le32(header + 16) != (g->bios ? ~gba_crc32(~0u, g->bios, 0x4000) : 0) ||
                       gba_le32(header + 20) != gba_state_config())) {
        result = GBA_ERROR_MISMATCH;
    }
    gba_codec_t c = {file, ~0u, 0, 2, result, &candidate};
    if (!result) {
        gba_state_codec(g, &c);
        if (!c.error && (c.count != payload || ~c.crc != gba_le32(header + 24) || gba_fread(&extra, 1, 1, file))) {
            c.error = GBA_ERROR_FORMAT;
        }
        if (!c.error) {
            static const uint32_t sizes[] = {0, 0, 32768, 65536, 131072, 512, 8192};
            const gba_cart_t *cart = &candidate.cart;
            const gba_rtc_t *rtc = &candidate.rtc;
            if (cart->size != sizes[cart->type] || cart->size > GBA_SAVE_CAPACITY ||
                (cart->size && cart->eeprom_address >= cart->size / 8) ||
                (cart->type != GBA_SAVE_FLASH128 && cart->flash_bank) || rtc->month < 1 || rtc->month > 12 ||
                rtc->day < 1 || rtc->day > gba_month_days(rtc->year, rtc->month) || rtc->year > 99 ||
                rtc->weekday > 6 || rtc->hour > 23 || rtc->minute > 59 || rtc->second > 59 || rtc->milliseconds > 999 ||
                candidate.prefetch_count > 8 || (candidate.cpu.r[15] & ((candidate.cpu.cpsr & GBA_T) ? 1u : 3u))) {
                c.error = GBA_ERROR_FORMAT;
            }
            if ((!rtc->transfer_size && rtc->bits > 8) || (rtc->transfer_size && rtc->bits > rtc->transfer_size * 8)) {
                c.error = GBA_ERROR_FORMAT;
            }
            if (candidate.cpu.irq_delay && !candidate.cpu.irq_line) {
                c.error = GBA_ERROR_FORMAT;
            }
            static const unsigned divisors[4] = {1, 64, 256, 1024};
            for (unsigned i = 0; i < 4; i++) {
                const gba_timer_t *timer = &candidate.timer[i];
                const gba_dma_t *dma = &candidate.dma[i];
                if ((dma->startup && !dma->pending) ||
                    (dma->pending && (!dma->remaining || !(dma->control & 0x8000)))) {
                    c.error = GBA_ERROR_FORMAT;
                }
                if ((timer->startup && !(timer->control & 0x80)) ||
                    ((timer->control & 0x80) && !(i && (timer->control & 4)) &&
                        timer->phase >= divisors[timer->control & 3])) {
                    c.error = GBA_ERROR_FORMAT;
                }
            }
        }
    }
    if (!c.error && gba_fseek(file, 32, SEEK_SET)) {
        c.error = GBA_ERROR_IO;
    }
    if (!c.error) {
        c.mode = 3;
        c.count = 0;
        c.crc = ~0u;
        gba_state_codec(g, &c);
        if (c.error || ~c.crc != gba_le32(header + 24)) {
            g->reload_required = 1;
            gba_fail(g, c.error ? c.error : GBA_ERROR_FORMAT);
            c.error = g->fault;
        }
    }
    if (gba_fclose(file) && !c.error) {
        c.error = GBA_ERROR_IO;
        if (c.mode == 3) {
            g->reload_required = 1;
            gba_fail(g, c.error);
        }
    }
    if (!c.error) {
        gba_memset(g->cache_valid, 0, sizeof(g->cache_valid));
        g->access_cycles = 0;
        g->event_cycles = 0;
        g->gba_quit = 0;
        gba_rtc_rebase(g);
        gba_state_loaded(g);
    }
    return c.error;
}
#endif
#endif
