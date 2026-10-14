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
static uint32_t gba_cart_capacity(gba_save_type_t type) {
    switch (type) {
    case GBA_SAVE_SRAM:
        return 32768;
    case GBA_SAVE_FLASH64:
        return 65536;
    case GBA_SAVE_FLASH128:
        return 131072;
    case GBA_SAVE_EEPROM512:
        return 512;
    case GBA_SAVE_EEPROM8K:
        return 8192;
    default:
        return 0;
    }
}
int gba_set_save_type(gba_t *g, gba_save_type_t type) {
    if (!g || g->running || (unsigned)type > GBA_SAVE_EEPROM8K) {
        return GBA_ERROR;
    }
    if (g->machine.cart.dirty) {
        return GBA_ERROR_BUSY;
    }
    g->save_override = type;
    if (g->rom_size) {
        gba_cart_detect(g);
    }
    return g->fault;
}
void gba_cart_detect(gba_t *g) {
    static const char *signatures[] = {
        "FLASH1M_V", "FLASH512_V", "FLASH_V", "EEPROM_V", "SRAM_V", "SRAM_F_V", "SIIRTC_V"};
    static const gba_save_type_t types[] = {GBA_SAVE_FLASH128, GBA_SAVE_FLASH64, GBA_SAVE_FLASH64, GBA_SAVE_EEPROM512,
        GBA_SAVE_SRAM, GBA_SAVE_SRAM, GBA_SAVE_NONE};
    uint8_t chunk[272];
    gba_cart_t *cart = &g->machine.cart;
    cart->type = GBA_SAVE_NONE;
    cart->size = 0;
    for (uint32_t offset = 0; offset < g->rom_size && !g->fault; offset += 256) {
        size_t count = g->rom_size - offset;
        if (count > sizeof(chunk)) {
            count = sizeof(chunk);
        }
        if (gba_rom_bytes(g, offset, chunk, count)) {
            break;
        }
        for (size_t i = 0; i < count && i < 256; i++) {
            for (unsigned s = 0; s < 7; s++) {
                size_t length = 0;
                while (signatures[s][length]) {
                    length++;
                }
                if (i + length > count) {
                    continue;
                }
                size_t j = 0;
                while (j < length && chunk[i + j] == (uint8_t)signatures[s][j]) {
                    j++;
                }
                if (j == length) {
                    if (s == 6) {
                        cart->rtc_enabled = GBA_ENABLE_RTC;
                    } else if (!cart->size) {
                        cart->type = types[s];
                        cart->size = gba_cart_capacity(types[s]);
                    }
                }
            }
        }
    }
    if (g->save_override != GBA_SAVE_AUTO) {
        cart->type = g->save_override;
        cart->size = gba_cart_capacity(g->save_override);
    }
#if !GBA_ENABLE_SAVE
    cart->type = GBA_SAVE_NONE;
    cart->size = 0;
#endif
    if (cart->size > GBA_SAVE_CAPACITY || cart->size > g->memory.save_size) {
        gba_fail(g, GBA_ERROR_MEMORY);
    }
}
size_t gba_save_size(const gba_t *g) {
    return g ? g->machine.cart.size : 0;
}
int gba_save_import(gba_t *g, const void *data, size_t size) {
    if (!g || g->running || !data || size != gba_save_size(g) || size > GBA_SAVE_CAPACITY ||
        size > g->memory.save_size) {
        return GBA_ERROR_FORMAT;
    }
    gba_memcpy(g->memory.save, data, size);
    g->machine.cart.dirty = 0;
    return GBA_OK;
}
int gba_save_export(gba_t *g, void *data, size_t size) {
    if (!g || g->running || !data || size < gba_save_size(g) || gba_save_size(g) > GBA_SAVE_CAPACITY ||
        gba_save_size(g) > g->memory.save_size) {
        return GBA_ERROR_MEMORY;
    }
    gba_memcpy(data, g->memory.save, gba_save_size(g));
    return GBA_OK;
}
static int gba_eeprom_region(const gba_t *g, uint32_t a) {
    return (g->machine.cart.type == GBA_SAVE_EEPROM512 || g->machine.cart.type == GBA_SAVE_EEPROM8K) &&
           (a >> 24) == 13 && (g->rom_size <= 0x1000000u || a >= 0x0dffff00u);
}
static unsigned gba_eeprom_read(gba_t *g) {
    gba_cart_t *c = &g->machine.cart;
    if (!c->eeprom_read) {
        return 1;
    }
    unsigned position = 68 - c->eeprom_read--;
    if (position < 4) {
        return 0;
    }
    position -= 4;
    return (g->memory.save[(c->eeprom_address * 8 + position / 8) % c->size] >> (7 - (position & 7))) & 1;
}
static void gba_eeprom_write(gba_t *g, unsigned bit) {
    gba_cart_t *c = &g->machine.cart;
    if (!c->eeprom_count && !bit) {
        return;
    }
    if (c->eeprom_count >= 82) {
        c->eeprom_count = 0;
    }
    c->eeprom_bits[c->eeprom_count++] = bit & 1;
    if (c->eeprom_count < 2) {
        return;
    }
    unsigned address_bits = c->size == 8192 ? 14 : 6;
    unsigned reading = c->eeprom_bits[1], expected = 2 + address_bits + (reading ? 0 : 64) + 1;
    if (c->eeprom_count != expected) {
        return;
    }
    uint32_t address = 0;
    for (unsigned i = 0; i < address_bits; i++) {
        address = (address << 1) | c->eeprom_bits[2 + i];
    }
    c->eeprom_address = address % (c->size / 8);
    if (reading) {
        c->eeprom_read = 68;
    } else {
        for (unsigned byte = 0; byte < 8; byte++) {
            uint8_t value = 0;
            for (unsigned bit_index = 0; bit_index < 8; bit_index++) {
                value = (uint8_t)((value << 1) | c->eeprom_bits[2 + address_bits + byte * 8 + bit_index]);
            }
            unsigned offset = c->eeprom_address * 8 + byte;
            if (g->memory.save[offset] != value) {
                g->memory.save[offset] = value;
                c->dirty = 1;
            }
        }
    }
    c->eeprom_count = 0;
}
uint32_t gba_cart_read(gba_t *g, uint32_t address, unsigned width) {
    gba_cart_t *c = &g->machine.cart;
    if ((address >> 24) >= 14) {
        uint32_t value = 0xff;
        if (c->size && (c->type == GBA_SAVE_SRAM || c->type == GBA_SAVE_FLASH64 || c->type == GBA_SAVE_FLASH128)) {
            unsigned offset = (address & 0xffff) + c->flash_bank * 65536;
            if (c->flash_id && (address & 0xffff) < 2) {
                value = (address & 1) ? (c->size == 131072 ? 0x13 : 0x1c) : (c->size == 131072 ? 0x62 : 0xc2);
            } else {
                value = g->memory.save[offset % c->size];
            }
        }
        return width == 4 ? value * 0x01010101u : width == 2 ? value * 0x101u : value;
    }
    if (gba_eeprom_region(g, address)) {
        return gba_eeprom_read(g);
    }
    uint32_t offset = address & 0x1ffffff;
    if (c->rtc_enabled && c->gpio_control && (offset & ~1u) >= 0xc4 && (offset & ~1u) <= 0xc8) {
        if (width == 4) {
            return gba_cart_read(g, address, 2) | (gba_cart_read(g, address + 2, 2) << 16);
        }
        uint32_t value = (offset & ~1u) == 0xc4   ? c->gpio_data
                         : (offset & ~1u) == 0xc6 ? c->gpio_direction
                                                  : c->gpio_control;
        if ((offset & ~1u) == 0xc4 && !(c->gpio_direction & 2)) {
            value = (value & ~2u) | (g->machine.rtc.output << 1);
        }
        return width == 1 ? (value >> ((offset & 1) * 8)) & 0xff : value;
    }
    if (offset >= g->rom_size || width > g->rom_size - offset) {
        unsigned low = (address >> 1) & 0xffff;
        if (width == 4) {
            return low | (((low + 1) & 0xffff) << 16);
        }
        return width == 1 ? (low >> ((address & 1) * 8)) & 255 : low;
    }
    if (g->rom) {
        const uint8_t *p = g->rom + offset;
        return width == 4 ? gba_le32(p) : width == 2 ? gba_le16(p) : *p;
    }
    uint8_t bytes[4];
    if (gba_rom_bytes(g, offset, bytes, width)) {
        return 0;
    }
    return width == 4 ? gba_le32(bytes) : width == 2 ? gba_le16(bytes) : bytes[0];
}
static void gba_save_byte(gba_t *g, unsigned offset, uint8_t value) {
    if (g->memory.save[offset] != value) {
        g->memory.save[offset] = value;
        g->machine.cart.dirty = 1;
    }
}
void gba_cart_write(gba_t *g, uint32_t address, uint32_t value, unsigned width) {
    gba_cart_t *c = &g->machine.cart;
    if (gba_eeprom_region(g, address)) {
        gba_eeprom_write(g, value & 1);
        return;
    }
    if ((address >> 24) < 14) {
        if (width == 4) {
            gba_cart_write(g, address, value & 0xffff, 2);
            gba_cart_write(g, address + 2, value >> 16, 2);
            return;
        }
        unsigned offset = address & 0x1ffffff;
        if (c->rtc_enabled && offset >= 0xc4 && offset <= 0xc8) {
            if (offset == 0xc4) {
                c->gpio_data = (c->gpio_data & ~c->gpio_direction) | (value & c->gpio_direction);
                gba_rtc_gpio(g, c->gpio_data);
            }
            if (offset == 0xc6) {
                c->gpio_direction = value & 15;
            }
            if (offset == 0xc8) {
                c->gpio_control = value & 1;
            }
        }
        return;
    }
    if (!c->size) {
        return;
    }
    value = (value >> ((address & (width - 1)) * 8)) & 255;
    unsigned a = address & 0xffff, bank = c->flash_bank * 65536;
    if (c->type == GBA_SAVE_SRAM) {
        gba_save_byte(g, a % c->size, (uint8_t)value);
        return;
    }
    if (c->type != GBA_SAVE_FLASH64 && c->type != GBA_SAVE_FLASH128) {
        return;
    }
    if (c->flash_phase == 6) {
        gba_save_byte(g, bank + a, (uint8_t)(g->memory.save[bank + a] & value));
        c->flash_phase = 0;
        return;
    }
    if (value == 0xf0) {
        c->flash_id = c->flash_phase = 0;
        return;
    }
    switch (c->flash_phase) {
    case 0:
        c->flash_phase = a == 0x5555 && value == 0xaa ? 1 : 0;
        break;
    case 1:
        c->flash_phase = a == 0x2aaa && value == 0x55 ? 2 : 0;
        break;
    case 2:
        c->flash_phase = 0;
        if (a != 0x5555) {
            break;
        }
        if (value == 0x90) {
            c->flash_id = 1;
        }
        if (value == 0xa0) {
            c->flash_phase = 6;
        }
        if (value == 0x80) {
            c->flash_phase = 3;
        }
        if (value == 0xb0 && c->size == 131072) {
            c->flash_phase = 7;
        }
        break;
    case 3:
        c->flash_phase = a == 0x5555 && value == 0xaa ? 4 : 0;
        break;
    case 4:
        c->flash_phase = a == 0x2aaa && value == 0x55 ? 5 : 0;
        break;
    case 5:
        if ((a == 0x5555 && value == 0x10) || value == 0x30) {
            unsigned begin = value == 0x10 ? 0 : bank + (a & ~0xfffu);
            unsigned count = value == 0x10 ? c->size : 4096;
            for (unsigned i = 0; i < count; i++) {
                gba_save_byte(g, begin + i, 0xff);
            }
        }
        c->flash_phase = 0;
        break;
    case 7:
        if (!a) {
            c->flash_bank = value & 1;
        }
        c->flash_phase = 0;
        break;
    default:
        c->flash_phase = 0;
        break;
    }
}
