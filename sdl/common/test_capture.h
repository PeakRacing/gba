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
/* Included only by the desktop test frontend, never by production or MCU. */
static uint16_t gba_sdl_test_pixels[GBA_WIDTH * GBA_HEIGHT];
static uint32_t gba_sdl_test_draws, gba_sdl_test_audio = 2166136261u, gba_sdl_test_audio_bytes;
static uint32_t gba_sdl_test_audio_nonzero;
static uint32_t gba_sdl_test_frame_audio = 2166136261u;
static uint32_t gba_sdl_test_replay_hash[1024], gba_sdl_test_replayed, gba_sdl_test_replay_failed;
static unsigned gba_sdl_test_input[1024][3], gba_sdl_test_inputs;
static int gba_sdl_test_input_load(void) {
    const char *path = getenv("GBA_TEST_INPUT");
    if (!path) {
        return 0;
    }
    FILE *file = (FILE *)gba_fopen(path, "rb");
    if (!file) {
        return GBA_ERROR_IO;
    }
    char line[128];
    int result = 0;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        unsigned frame, length, keys;
        if (gba_sdl_test_inputs == 1024 || sscanf(line, "%u,%u,%u", &frame, &length, &keys) != 3 || !length ||
            frame > UINT32_MAX - length || keys > 1023) {
            result = GBA_ERROR_FORMAT;
            break;
        }
        unsigned *event = gba_sdl_test_input[gba_sdl_test_inputs++];
        event[0] = frame;
        event[1] = frame + length;
        event[2] = keys;
    }
    if (gba_fclose(file) && !result) {
        result = GBA_ERROR_IO;
    }
    return result;
}
static uint16_t gba_sdl_test_keys(unsigned frame) {
    uint16_t keys = 0;
    for (unsigned i = 0; i < gba_sdl_test_inputs; i++) {
        const unsigned *event = gba_sdl_test_input[i];
        if (frame >= event[0] && frame < event[1]) {
            keys |= (uint16_t)event[2];
        }
    }
    return keys;
}
static uint32_t gba_sdl_test_hash(uint32_t hash, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}
static void gba_sdl_test_replay(gba_t *g, unsigned save_at, unsigned load_at, int loaded) {
    unsigned frame = g->machine.frames;
    if (save_at && load_at > save_at && load_at - save_at <= 1024 && frame > save_at && frame <= load_at) {
        gba_machine_t machine = g->machine;
        /* Loading marks battery resources dirty so the restored contents will be
         * persisted. These flags do not belong to emulated hardware behavior. */
        machine.cart.dirty = machine.rtc.dirty = 0;
        uint32_t hash = gba_sdl_test_hash(2166136261u, &machine, sizeof(machine));
        hash = gba_sdl_test_hash(hash, g->io, sizeof(g->io));
        hash = gba_sdl_test_hash(hash, g->memory.ewram, 0x40000);
        hash = gba_sdl_test_hash(hash, g->memory.iwram, 0x8000);
        hash = gba_sdl_test_hash(hash, g->memory.vram, 0x18000);
        hash = gba_sdl_test_hash(hash, g->memory.palette, 0x400);
        hash = gba_sdl_test_hash(hash, g->memory.oam, 0x400);
        hash = gba_sdl_test_hash(hash, g->memory.save, gba_save_size(g));
        hash = gba_sdl_test_hash(hash, gba_sdl_test_pixels, sizeof(gba_sdl_test_pixels));
        hash = gba_sdl_test_hash(hash, &gba_sdl_test_frame_audio, sizeof(gba_sdl_test_frame_audio));
        unsigned index = frame - save_at - 1;
        if (loaded) {
            gba_sdl_test_replayed++;
            if (hash != gba_sdl_test_replay_hash[index]) {
                gba_sdl_test_replay_failed++;
                fprintf(stderr, "test replay mismatch frame=%u expected=%08x actual=%08x\n", frame,
                    gba_sdl_test_replay_hash[index], hash);
            }
        } else {
            gba_sdl_test_replay_hash[index] = hash;
        }
    }
    gba_sdl_test_frame_audio = 2166136261u;
}
static void gba_sdl_test_draw(int x1, int y1, int x2, int y2, const gba_color_t *pixels) {
    unsigned width = (unsigned)(x2 - x1 + 1);
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            unsigned index = (unsigned)(y - y1) * width + x - x1;
#if GBA_COLOR_DEPTH == 16
            uint16_t pixel = pixels[index];
#if GBA_COLOR_SWAP
            pixel = (uint16_t)((pixel << 8) | (pixel >> 8));
#endif
            gba_sdl_test_pixels[y * GBA_WIDTH + x] = (pixel >> 11) | (((pixel >> 6) & 31) << 5) | ((pixel & 31) << 10);
#else
            uint32_t pixel = pixels[index];
            gba_sdl_test_pixels[y * GBA_WIDTH + x] = (((pixel >> 16) & 255) * 31 + 127) / 255 |
                                                     ((((pixel >> 8) & 255) * 31 + 127) / 255 << 5) |
                                                     (((pixel & 255) * 31 + 127) / 255 << 10);
#endif
        }
    }
    gba_sdl_test_draws++;
}
static void gba_sdl_test_sound(const uint8_t *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        gba_sdl_test_audio = (gba_sdl_test_audio ^ buffer[i]) * 16777619u;
        gba_sdl_test_frame_audio = (gba_sdl_test_frame_audio ^ buffer[i]) * 16777619u;
        gba_sdl_test_audio_nonzero += buffer[i] != 0;
    }
    gba_sdl_test_audio_bytes += (uint32_t)size;
}
static void gba_sdl_test_report(uint32_t frames, uint32_t cycles, uint32_t pc) {
    const char *prefix = getenv("GBA_TEST_REPORT");
    uint32_t image = 2166136261u;
    for (unsigned i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++) {
        image = (image ^ (gba_sdl_test_pixels[i] & 255)) * 16777619u;
        image = (image ^ (gba_sdl_test_pixels[i] >> 8)) * 16777619u;
    }
    fprintf(stderr, "test frames=%u cycles=%u pc=%08x image=%08x draws=%u audio=%08x audio_bytes=%u audio_nonzero=%u\n",
        frames, cycles, pc, image, gba_sdl_test_draws, gba_sdl_test_audio, gba_sdl_test_audio_bytes,
        gba_sdl_test_audio_nonzero);
    fprintf(stderr, "test replay_frames=%u replay_failures=%u\n", gba_sdl_test_replayed, gba_sdl_test_replay_failed);
    if (prefix) {
        char path[GBA_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s.ppm", prefix) >= (int)sizeof(path)) {
            return;
        }
        FILE *file = fopen(path, "wb");
        if (!file) {
            return;
        }
        fprintf(file, "P6\n240 160\n255\n");
        for (unsigned i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++) {
            unsigned rgb = gba_sdl_test_pixels[i];
            uint8_t pixel[3] = {(rgb & 31) * 255 / 31, ((rgb >> 5) & 31) * 255 / 31, ((rgb >> 10) & 31) * 255 / 31};
            fwrite(pixel, 1, 3, file);
        }
        fclose(file);
    }
}
