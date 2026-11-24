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

typedef size_t (*gba_rom_read_fn)(void *user, uint32_t offset, void *data, size_t size);

typedef enum {
    GBA_SAVE_AUTO,
    GBA_SAVE_NONE,
    GBA_SAVE_SRAM,
    GBA_SAVE_FLASH64,
    GBA_SAVE_FLASH128,
    GBA_SAVE_EEPROM512,
    GBA_SAVE_EEPROM8K
} gba_save_type_t;

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

void gba_cart_detect(gba_t *g);
uint32_t gba_cart_read(gba_t *g, uint32_t a, unsigned width);
void gba_cart_write(gba_t *g, uint32_t a, uint32_t value, unsigned width);
void gba_rtc_gpio(gba_t *g, unsigned value);
void gba_rtc_sync(gba_t *g);

int gba_set_save_type(gba_t *gba, gba_save_type_t type);
size_t gba_save_size(const gba_t *gba);
int gba_save_import(gba_t *gba, const void *data, size_t size);
int gba_save_export(gba_t *gba, void *data, size_t size);
void gba_rtc_rebase(gba_t *gba);

#ifdef __cplusplus
}
#endif
