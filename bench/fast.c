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
#include <stdio.h>
#include <stdlib.h>
#if GBA_TEST_MODE || GBA_TEST_PROFILE
#error "speed measurements require both test probes to be disabled"
#endif
#ifdef _WIN32
#include <windows.h>
static uint64_t gba_bench_clock(void) {
    LARGE_INTEGER ticks, frequency;
    QueryPerformanceCounter(&ticks);
    QueryPerformanceFrequency(&frequency);
    uint64_t count = (uint64_t)ticks.QuadPart, rate = (uint64_t)frequency.QuadPart;
    return count / rate * 1000000u + count % rate * 1000000u / rate;
}
#else
#include <time.h>
static uint64_t gba_bench_clock(void) {
    struct timespec time;
    timespec_get(&time, TIME_UTC);
    return (uint64_t)time.tv_sec * 1000000u + (uint64_t)time.tv_nsec / 1000u;
}
#endif
uint32_t gba_get_ticks(void) {
    return 0;
}
static void gba_bench_input(gba_t *g) {
    unsigned frame = g->machine.frames;
    gba_set_keys(g, frame >= 120 && frame < 124 ? GBA_KEY_START : frame >= 180 && frame % 60 < 4 ? GBA_KEY_A : 0);
}
int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    unsigned frames = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 1800;
    gba_t *g = gba_init();
    if (!g) {
        return 2;
    }
    int load = gba_load_file(g, argv[1]);
    if (load) {
        fprintf(stderr, "load_error=%d\n", load);
        gba_unload_rom_discard(g);
        gba_deinit(g);
        return 2;
    }
    for (unsigned i = 0; i < 120 && !g->fault; i++) {
        gba_bench_input(g);
        gba_run_frame(g);
    }
    uint64_t begin = gba_bench_clock();
    for (unsigned i = 0; i < frames && !g->fault; i++) {
        gba_bench_input(g);
        if (!gba_run_frame(g)) {
            break;
        }
    }
    uint64_t elapsed = gba_bench_clock() - begin;
    printf("frames=%u us=%llu pc=%08x fault=%d context=%zu memory=%zu test=%u profile=%u\n", g->machine.frames,
        (unsigned long long)elapsed, g->machine.cpu.r[15], g->fault, sizeof(*g), gba_memory_size(), GBA_TEST_MODE,
        GBA_TEST_PROFILE);
    int result = g->fault;
    gba_unload_rom_discard(g);
    gba_deinit(g);
    return result ? 1 : 0;
}
