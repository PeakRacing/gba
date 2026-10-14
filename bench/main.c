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
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("gba-bench ROM [frames]\n");
        return 2;
    }
    gba_t *g = gba_init();
    if (!g) {
        return 2;
    }
    if (gba_load_file(g, argv[1])) {
        gba_unload_rom_discard(g);
        gba_deinit(g);
        return 2;
    }
    unsigned frames = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 600;
    uint32_t begin = gba_get_ticks();
    for (unsigned i = 0; i < frames && !g->fault; i++) {
        if (!gba_run_frame(g)) {
            break;
        }
    }
    printf("frames=%u ms=%u context=%zu memory=%zu cpu_ms=%u dma_ms=%u ppu_ms=%u apu_ms=%u cache_hits=%u "
           "cache_misses=%u fault=%d\n",
        g->machine.frames, gba_get_ticks() - begin, sizeof(*g), gba_memory_size(), g->profile_cpu, g->profile_dma,
        g->profile_ppu, g->profile_apu, g->cache_hits, g->cache_misses, g->fault);
    int result = g->fault;
    if (result) {
        gba_unload_rom_discard(g);
    }
    gba_deinit(g);
    return result ? 1 : 0;
}
