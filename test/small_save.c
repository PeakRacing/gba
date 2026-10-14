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
#include <stdio.h>
static const uint8_t rom[192] = {0xfe, 0xff, 0xff, 0xea};
static uint8_t data[32768];
int main(void) {
    unsigned failures = 0, assertions = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        assertions++;                                                                                                  \
        if (!(condition)) {                                                                                            \
            failures++;                                                                                                \
            printf("FAIL line=%u\n", __LINE__);                                                                        \
        }                                                                                                              \
    } while (0)
    gba_t *g = gba_init();
    CHECK(g != NULL);
    if (!g) {
        return 1;
    }
    /* Extra advertised backing capacity must not enlarge a configured device limit. */
    g->memory.save_size = 131072;
    CHECK(gba_set_save_type(g, GBA_SAVE_SRAM) == 0);
    CHECK(gba_load_rom(g, rom, sizeof(rom)) == GBA_ERROR_MEMORY);
    CHECK(gba_reset(g) == GBA_ERROR_MEMORY);
    CHECK(gba_save_import(g, data, sizeof(data)) == GBA_ERROR_FORMAT);
    CHECK(gba_save_export(g, data, sizeof(data)) == GBA_ERROR_MEMORY);
    CHECK(g->memory.framebuffer[0] == 0);
    CHECK(gba_unload_rom_discard(g) == 0);
    CHECK(gba_set_save_type(g, GBA_SAVE_EEPROM512) == 0);
    CHECK(gba_load_rom(g, rom, sizeof(rom)) == 0);
    gba_write32(g, 0x040000d4, 0x03000000);
    gba_write32(g, 0x040000d8, 0x0d000000);
    gba_write16(g, 0x040000dc, 17);
    gba_write16(g, 0x040000de, 0x8000);
    CHECK(g->fault == GBA_ERROR_MEMORY && gba_save_size(g) == 512);
    CHECK(gba_unload_rom_discard(g) == 0);
    CHECK(gba_deinit(g) == 0);
    printf("assertions=%u failures=%u context=%zu memory=%zu config=save_capacity512\n", assertions, failures,
        sizeof(gba_t), gba_memory_size());
    return failures ? 1 : 0;
}
