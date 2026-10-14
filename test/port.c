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
#define GBA_CUSTOM_TICKS 1
#define GBA_CUSTOM_OUTPUT 1
#define GBA_CUSTOM_FRAME 1
#include "../port/gba_port.c"
#include "gba_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t gba_test_ticks;
uint32_t gba_test_audio_hash = 2166136261u;
uint32_t gba_test_image_hash = 2166136261u;
uint32_t gba_test_draws, gba_test_audio_bytes, gba_test_frame_limit;
uint32_t gba_test_tone, gba_test_tone_samples, gba_test_tone_edges;
static int gba_test_tone_positive;
uint32_t gba_test_validate_pc;
uint32_t gba_test_log_pc, gba_test_info_address;
static uint8_t gba_test_byte(const gba_t *g, uint32_t address) {
    switch (address >> 24) {
    case 2:
        return g->memory.ewram[address & 0x3ffff];
    case 3:
        return g->memory.iwram[address & 0x7fff];
    case 8:
    case 9: {
        unsigned offset = address & 0x1ffffff;
        return g->rom && offset < g->rom_size ? g->rom[offset] : 0;
    }
    default:
        return 0;
    }
}
static void gba_test_string(const gba_t *g, uint32_t address, char *text, unsigned capacity) {
    unsigned i = 0;
    while (i + 1 < capacity && (text[i] = (char)gba_test_byte(g, address + i))) {
        if (text[i] == '\n' || text[i] == '\r') {
            text[i] = ' ';
        }
        i++;
    }
    text[i] = 0;
}
static uint32_t gba_test_word(const gba_t *g, uint32_t address) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; i++) {
        result |= (uint32_t)gba_test_byte(g, address + i) << (i * 8);
    }
    return result;
}
void gba_test_instruction(gba_t *g, uint32_t pc, uint32_t opcode) {
    /* Read backing bytes directly: a diagnostic must not perform emulated bus accesses. */
    if (gba_test_log_pc && pc == (gba_test_log_pc & ~1u)) {
        static FILE *calls;
        char format[128], label[128];
        gba_test_string(g, g->machine.cpu.r[0], format, sizeof(format));
        gba_test_string(g, g->machine.cpu.r[1], label, sizeof(label));
        if (!calls) {
            calls = fopen("test/out/suite/calls.log", "w");
        }
        if (calls) {
            fprintf(calls,
                "suite=%u test=%d sub=%d r1=%08x r2=%08x r3=%08x s0=%08x s1=%08x s2=%08x s3=%08x fmt=%s label=%s\n",
                gba_le32(g->memory.ewram + 0x3f000) & 255, gba_test_byte(g, gba_test_info_address + 6),
                gba_sign(gba_test_word(g, gba_test_info_address + 4) & 0xffff, 16), g->machine.cpu.r[1],
                g->machine.cpu.r[2], g->machine.cpu.r[3], gba_test_word(g, g->machine.cpu.r[13]),
                gba_test_word(g, g->machine.cpu.r[13] + 4), gba_test_word(g, g->machine.cpu.r[13] + 8),
                gba_test_word(g, g->machine.cpu.r[13] + 12), format, strstr(format, "%s") ? label : "");
        }
    }
    static uint32_t trace[128][8], count;
    uint32_t *row = trace[count++ & 127];
    row[0] = pc;
    row[1] = opcode;
    row[2] = g->machine.cpu.r[0];
    row[3] = g->machine.cpu.r[1];
    row[4] = g->machine.cpu.r[12];
    row[5] = g->machine.cpu.r[13];
    row[6] = g->machine.cpu.r[14];
    row[7] = g->machine.cpu.cpsr;
    unsigned region = pc >> 24;
    if (gba_test_validate_pc && region > 15) {
        FILE *file = fopen("test/out/cpu-trace.txt", "w");
        if (file) {
            unsigned start = count > 128 ? count - 128 : 0;
            for (unsigned i = start; i < count; i++) {
                uint32_t *p = trace[i & 127];
                fprintf(file, "%08x %08x r0=%08x r1=%08x r12=%08x sp=%08x lr=%08x cpsr=%08x\n", p[0], p[1], p[2], p[3],
                    p[4], p[5], p[6], p[7]);
            }
            fclose(file);
        }
        gba_fail(g, GBA_ERROR_ROM);
    }
}
uint32_t gba_get_ticks(void) {
    return gba_test_ticks;
}
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels) {
    size_t count = (size_t)(x2 - x1 + 1) * (size_t)(y2 - y1 + 1);
    for (size_t i = 0; i < count; i++) {
#if GBA_COLOR_DEPTH == 16
        uint16_t value = pixels[i];
#if GBA_COLOR_SWAP
        value = (uint16_t)((value >> 8) | (value << 8));
#endif
        unsigned r = (value >> 11) & 31, green = (value >> 6) & 31, b = value & 31;
#else
        uint32_t value = pixels[i];
        unsigned r = (((value >> 16) & 255) * 31 + 127) / 255, green = (((value >> 8) & 255) * 31 + 127) / 255,
                 b = ((value & 255) * 31 + 127) / 255;
#endif
        unsigned rgb = r | (green << 5) | (b << 10);
        gba_test_image_hash = (gba_test_image_hash ^ (rgb & 255)) * 16777619u;
        gba_test_image_hash = (gba_test_image_hash ^ (rgb >> 8)) * 16777619u;
    }
    gba_test_draws++;
    return 0;
}
int gba_sound_output(uint8_t *buffer, size_t size) {
    if (gba_test_tone) {
        if (!gba_test_tone_samples) {
            gba_test_tone_positive = 0;
        }
        for (size_t i = 0; i + 3 < size; i += 4) {
            int positive = gba_sign(gba_le16(buffer + i), 16) > 0;
            gba_test_tone_edges += positive && !gba_test_tone_positive;
            gba_test_tone_positive = positive;
            gba_test_tone_samples++;
        }
    }
    for (size_t i = 0; i < size; i++) {
        gba_test_audio_hash = (gba_test_audio_hash ^ buffer[i]) * 16777619u;
    }
    gba_test_audio_bytes += (uint32_t)size;
    return 0;
}
void gba_frame(gba_t *g) {
    if (getenv("GBA_TEST_PLAY")) {
        unsigned frame = g->machine.frames;
        uint16_t keys = (frame >= 120 && frame < 124) || (frame >= 600 && frame % 600 < 4) ? GBA_KEY_START
                        : frame >= 180 && frame % 60 < 4                                   ? GBA_KEY_A
                                                                                           : 0;
        if (frame >= 1800) {
            keys |= frame % 600 < 120 ? GBA_KEY_RIGHT : frame % 600 < 240 ? GBA_KEY_LEFT : 0;
        }
        gba_set_keys(g, keys);
    }
    if (gba_test_frame_limit && g->machine.frames >= gba_test_frame_limit) {
        g->gba_quit = 1;
    }
}
