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
static const int16_t gba_sine[256] = {0, 402, 803, 1205, 1605, 2005, 2404, 2801, 3196, 3589, 3980, 4369, 4756, 5139,
    5519, 5896, 6269, 6639, 7005, 7366, 7723, 8075, 8423, 8765, 9102, 9434, 9759, 10079, 10393, 10701, 11002, 11297,
    11585, 11866, 12139, 12406, 12665, 12916, 13159, 13395, 13622, 13842, 14053, 14255, 14449, 14634, 14810, 14978,
    15136, 15286, 15426, 15557, 15678, 15790, 15892, 15985, 16069, 16142, 16206, 16260, 16305, 16339, 16364, 16379,
    16384, 16379, 16364, 16339, 16305, 16260, 16206, 16142, 16069, 15985, 15892, 15790, 15678, 15557, 15426, 15286,
    15136, 14978, 14810, 14634, 14449, 14255, 14053, 13842, 13622, 13395, 13159, 12916, 12665, 12406, 12139, 11866,
    11585, 11297, 11002, 10701, 10393, 10079, 9759, 9434, 9102, 8765, 8423, 8075, 7723, 7366, 7005, 6639, 6269, 5896,
    5519, 5139, 4756, 4369, 3980, 3589, 3196, 2801, 2404, 2005, 1605, 1205, 803, 402, 0, -402, -803, -1205, -1605,
    -2005, -2404, -2801, -3196, -3589, -3980, -4369, -4756, -5139, -5519, -5896, -6269, -6639, -7005, -7366, -7723,
    -8075, -8423, -8765, -9102, -9434, -9759, -10079, -10393, -10701, -11002, -11297, -11585, -11866, -12139, -12406,
    -12665, -12916, -13159, -13395, -13622, -13842, -14053, -14255, -14449, -14634, -14810, -14978, -15136, -15286,
    -15426, -15557, -15678, -15790, -15892, -15985, -16069, -16142, -16206, -16260, -16305, -16339, -16364, -16379,
    -16384, -16379, -16364, -16339, -16305, -16260, -16206, -16142, -16069, -15985, -15892, -15790, -15678, -15557,
    -15426, -15286, -15136, -14978, -14810, -14634, -14449, -14255, -14053, -13842, -13622, -13395, -13159, -12916,
    -12665, -12406, -12139, -11866, -11585, -11297, -11002, -10701, -10393, -10079, -9759, -9434, -9102, -8765, -8423,
    -8075, -7723, -7366, -7005, -6639, -6269, -5896, -5519, -5139, -4756, -4369, -3980, -3589, -3196, -2801, -2404,
    -2005, -1605, -1205, -803, -402};
static uint32_t gba_sqrt(uint32_t value) {
    uint32_t root = 0, bit = 1u << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}
static uint32_t gba_bios_asr(uint32_t value, unsigned bits) {
    return (value >> bits) | ((value & GBA_N) ? UINT32_MAX << (32 - bits) : 0);
}
static int32_t gba_atan(gba_cpu_t *c, int32_t x) {
    static const uint32_t coefficients[] = {0x390, 0x91c, 0xfb6, 0x16aa, 0x2081, 0x3651, 0xa2f9};
    uint32_t a = 0u - gba_bios_asr((uint32_t)x * (uint32_t)x, 14), b = 0xa9;
    for (unsigned i = 0; i < 7; i++) {
        b = gba_bios_asr(a * b, 14) + coefficients[i];
    }
    c->r[1] = a;
    c->r[3] = b;
    return gba_sign(gba_bios_asr((uint32_t)x * b, 16), 32);
}
static uint32_t gba_atan2(gba_cpu_t *c, int32_t x, int32_t y) {
    if (!y) {
        return x < 0 ? 0x8000 : 0;
    }
    if (!x) {
        return y < 0 ? 0xc000 : 0x4000;
    }
    int64_t ax = x < 0 ? -(int64_t)x : x, ay = y < 0 ? -(int64_t)y : y;
    int32_t angle = ax >= ay ? gba_atan(c, (int32_t)((int64_t)y * 16384 / x))
                             : 0x4000 - gba_atan(c, (int32_t)((int64_t)x * 16384 / y));
    if (ax >= ay) {
        if (x < 0) {
            angle += 0x8000;
        }
    } else if (y < 0) {
        angle += 0x8000;
    }
    return (uint32_t)angle & 0xffff;
}
static void gba_bios_byte(gba_t *g, uint32_t address, uint8_t value, int halfword) {
    if (!halfword) {
        gba_write8(g, address, value);
    } else {
        uint32_t aligned = address & ~1u, previous = gba_bus_read(g, aligned, 2, 0, 0);
        unsigned shift = (address & 1) * 8;
        gba_write16(g, aligned, (previous & ~(255u << shift)) | ((uint32_t)value << shift));
    }
}
static void gba_bios_unpack(gba_t *g, uint32_t source, uint32_t destination, uint32_t info) {
    unsigned length = gba_read16(g, info), src_bits = gba_read8(g, info + 2), dst_bits = gba_read8(g, info + 3);
    uint32_t offset = gba_read32(g, info + 4), word = 0;
    unsigned used = 0;
    if (!(src_bits == 1 || src_bits == 2 || src_bits == 4 || src_bits == 8) ||
        !(dst_bits == 1 || dst_bits == 2 || dst_bits == 4 || dst_bits == 8 || dst_bits == 16 || dst_bits == 32) ||
        dst_bits < src_bits) {
        gba_fail(g, GBA_ERROR_FORMAT);
        return;
    }
    for (unsigned byte = 0; byte < length && !g->fault; byte++) {
        unsigned value = gba_read8(g, source + byte);
        for (unsigned bit = 0; bit < 8; bit += src_bits) {
            uint32_t unit = (value >> bit) & ((1u << src_bits) - 1);
            if (unit || (offset & GBA_N)) {
                unit += offset & ~GBA_N;
            }
            if (dst_bits < 32) {
                unit &= (1u << dst_bits) - 1;
            }
            word |= unit << used;
            used += dst_bits;
            if (used == 32) {
                gba_write32(g, destination, word);
                destination += 4;
                word = 0;
                used = 0;
            }
        }
    }
    if (used) {
        gba_write32(g, destination, word);
    }
}
static void gba_bios_decompress(gba_t *g, unsigned kind, uint32_t source, uint32_t destination) {
    uint32_t header = gba_read32(g, source), length = header >> 8, position = 0;
    int half = kind == 0x12 || kind == 0x15 || kind == 0x17;
    source += 4;
    if (kind == 0x11 || kind == 0x12) {
        if ((header & 0xff) != 0x10) {
            gba_fail(g, GBA_ERROR_FORMAT);
            return;
        }
        while (position < length && !g->fault) {
            unsigned flags = gba_read8(g, source++);
            for (unsigned bit = 0; bit < 8 && position < length; bit++) {
                if (flags & (0x80u >> bit)) {
                    unsigned first = gba_read8(g, source++), second = gba_read8(g, source++);
                    unsigned count = (first >> 4) + 3, distance = ((first & 15) << 8) + second + 1;
                    if (distance > position) {
                        gba_fail(g, GBA_ERROR_FORMAT);
                        return;
                    }
                    while (count-- && position < length) {
                        uint8_t value = (uint8_t)gba_read8(g, destination + position - distance);
                        gba_bios_byte(g, destination + position++, value, half);
                    }
                } else {
                    gba_bios_byte(g, destination + position++, (uint8_t)gba_read8(g, source++), half);
                }
            }
        }
    } else if (kind == 0x14 || kind == 0x15) {
        if ((header & 0xff) != 0x30) {
            gba_fail(g, GBA_ERROR_FORMAT);
            return;
        }
        while (position < length && !g->fault) {
            unsigned control = gba_read8(g, source++), count = (control & 127) + ((control & 128) ? 3 : 1);
            unsigned value = (control & 128) ? gba_read8(g, source++) : 0;
            while (count-- && position < length) {
                gba_bios_byte(
                    g, destination + position++, (uint8_t)((control & 128) ? value : gba_read8(g, source++)), half);
            }
        }
    } else if (kind == 0x16 || kind == 0x17 || kind == 0x18) {
        uint32_t value = 0;
        while (position < length && !g->fault) {
            if (kind == 0x18) {
                value = (value + gba_read16(g, source)) & 0xffff;
                source += 2;
                gba_write16(g, destination + position, value);
                position += 2;
            } else {
                value = (value + gba_read8(g, source++)) & 255;
                gba_bios_byte(g, destination + position++, (uint8_t)value, half);
            }
        }
    } else if (kind == 0x13) {
        unsigned bits = header & 15, tree_bytes = (gba_read8(g, source) + 1) * 2;
        if (bits != 4 && bits != 8) {
            gba_fail(g, GBA_ERROR_FORMAT);
            return;
        }
        uint32_t tree_start = source, root = source + 1, node = root, stream = (source + tree_bytes + 3) & ~3u;
        uint32_t word = 0, input = 0;
        unsigned used = 0, input_bits = 0, depth = 0;
        while (position < length && !g->fault) {
            if (!input_bits) {
                input = gba_read32(g, stream);
                stream += 4;
                input_bits = 32;
            }
            unsigned direction = input >> 31;
            input <<= 1;
            input_bits--;
            unsigned descriptor = gba_read8(g, node);
            uint32_t child = (node & ~1u) + ((descriptor & 63) + 1) * 2 + direction;
            if (child >= tree_start + tree_bytes || ++depth > 64) {
                gba_fail(g, GBA_ERROR_FORMAT);
                return;
            }
            if (descriptor & (0x80u >> direction)) {
                unsigned value = gba_read8(g, child) & ((1u << bits) - 1);
                word |= value << used;
                used += bits;
                node = root;
                depth = 0;
                if (used == 32) {
                    for (unsigned i = 0; i < 4 && position < length; i++) {
                        gba_bios_byte(g, destination + position++, (uint8_t)(word >> (i * 8)), 0);
                    }
                    word = 0;
                    used = 0;
                }
            } else {
                node = child;
            }
        }
    }
}
static void gba_bios_affine(gba_t *g, int objects) {
    uint32_t source = g->machine.cpu.r[0], destination = g->machine.cpu.r[1], count = g->machine.cpu.r[2],
             stride = objects ? g->machine.cpu.r[3] : 2;
    if (count > 65536) {
        gba_fail(g, GBA_ERROR_FORMAT);
        return;
    }
    while (count-- && !g->fault) {
        unsigned offset = objects ? 0 : 12;
        int32_t sx = gba_sign(gba_read16(g, source + offset), 16),
                sy = gba_sign(gba_read16(g, source + offset + 2), 16);
        unsigned angle = gba_read16(g, source + offset + 4) >> 8;
        int32_t sine = gba_sine[angle], cosine = gba_sine[(angle + 64) & 255];
        int32_t pa = sx * cosine / 16384, pb = -sx * sine / 16384, pc = sy * sine / 16384, pd = sy * cosine / 16384;
        gba_write16(g, destination, (uint32_t)pa);
        gba_write16(g, destination + stride, (uint32_t)pb);
        gba_write16(g, destination + stride * 2, (uint32_t)pc);
        gba_write16(g, destination + stride * 3, (uint32_t)pd);
        if (!objects) {
            int32_t dx = gba_sign(gba_read16(g, source + 8), 16), dy = gba_sign(gba_read16(g, source + 10), 16);
            gba_write32(g, destination + 8, gba_read32(g, source) - (uint32_t)(pa * dx + pb * dy));
            gba_write32(g, destination + 12, gba_read32(g, source + 4) - (uint32_t)(pc * dx + pd * dy));
        }
        source += objects ? 8 : 20;
        destination += objects ? stride * 4 : 16;
    }
}
static uint32_t gba_midi_frequency(gba_t *g, uint32_t wave, unsigned key, unsigned fine) {
    static const uint32_t ratios[12] = {16777216, 17774841, 18831788, 19951585, 21137968, 22394897, 23726566, 25137414,
        26632170, 28215801, 29894336, 31671837};
    int note = (int)(key & 255) - 180, octave = note / 12, semitone = note % 12;
    if (semitone < 0) {
        semitone += 12;
        octave--;
    }
    uint64_t value = (uint64_t)gba_read32(g, wave + 4) * ratios[semitone] >> 24;
    uint64_t factor = 16781002, adjustment = 16777216;
    for (unsigned i = 0; i < 8; i++) {
        if (fine & (1u << i)) {
            adjustment = adjustment * factor >> 24;
        }
        factor = factor * factor >> 24;
    }
    value = value * adjustment >> 24;
    if (octave < 0) {
        value >>= (unsigned)-octave;
    } else {
        value <<= octave;
    }
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}
/* SDK SoundArea ABI: 0x50-byte header, 12 64-byte channels, two 1584-byte PCM buffers.
 * Driver-private words hold the integer rate, phase and envelope state. */
static uint32_t gba_sound_area(gba_t *g) {
    return gba_read32(g, 0x03007ff0);
}
static void gba_sound_driver_dma(gba_t *g, uint32_t area) {
    gba_write16(g, 0x040000c6, 0);
    gba_write16(g, 0x040000d2, 0);
    gba_write32(g, 0x040000bc, area + 0x350);
    gba_write32(g, 0x040000c0, 0x040000a0);
    gba_write32(g, 0x040000c8, area + 0x980);
    gba_write32(g, 0x040000cc, 0x040000a4);
    gba_write16(g, 0x040000c6, 0xb640);
    gba_write16(g, 0x040000d2, 0xb640);
}
static void gba_sound_driver(gba_t *g, unsigned call) {
    uint32_t area = call == 0x1a ? g->machine.cpu.r[0] : gba_sound_area(g);
    if ((area >> 24) != 2 && (area >> 24) != 3) {
        gba_fail(g, GBA_ERROR_FORMAT);
        return;
    }
    if (call == 0x1a) {
        for (unsigned off = 0; off < 0xfb0; off += 4) {
            gba_write32(g, area + off, 0);
        }
        gba_write32(g, 0x03007ff0, area);
        gba_write32(g, area, 0x68736d53);
        gba_write8(g, area + 6, 8);
        gba_write8(g, area + 7, 15);
        gba_write32(g, area + 12, 13379);
        gba_write16(g, 0x04000084, 0x80);
        gba_write16(g, 0x04000082, 0x9a0c);
        gba_write16(g, 0x04000100, 65536 - GBA_CPU_CLOCK / 13379);
        gba_write16(g, 0x04000102, 0x80);
        gba_sound_driver_dma(g, area);
        return;
    }
    if (gba_read32(g, area) != 0x68736d53) {
        gba_fail(g, GBA_ERROR_FORMAT);
        return;
    }
    if (call == 0x1b) {
        static const unsigned rates[12] = {
            5734, 7884, 10512, 13379, 15768, 18157, 21024, 26758, 31536, 36314, 40137, 42048};
        unsigned mode = g->machine.cpu.r[0], rate = (mode >> 16) & 15;
        if (rate && rate <= 12) {
            gba_write32(g, area + 12, rates[rate - 1]);
            gba_write16(g, 0x04000100, 65536 - GBA_CPU_CLOCK / rates[rate - 1]);
        }
        if (mode & 0x80) {
            gba_write8(g, area + 5, mode & 127);
        }
        if (mode & 0xf000) {
            gba_write8(g, area + 7, (mode >> 12) & 15);
        }
        if (mode & 0xf00) {
            gba_write8(g, area + 6, ((mode >> 8) & 15) > 12 ? 12 : (mode >> 8) & 15);
        }
        if (mode & 0xf00000) {
            gba_write16(g, 0x04000088, (gba_io16(g, 0x88) & 0x3ff) | (((mode >> 20) & 3) << 14));
        }
        return;
    }
    if (call == 0x1e) {
        for (unsigned ch = 0; ch < 12; ch++) {
            gba_write8(g, area + 0x50 + ch * 64, 0);
        }
        return;
    }
    if (call == 0x28) {
        gba_write16(g, 0x040000c6, 0);
        gba_write16(g, 0x040000d2, 0);
        return;
    }
    if (call == 0x1d || call == 0x29) {
        gba_sound_driver_dma(g, area);
        return;
    }
    unsigned rate = gba_read32(g, area + 12),
             samples = (unsigned)((uint64_t)rate * GBA_CYCLES_PER_FRAME / GBA_CPU_CLOCK);
    if (rate < 5000 || rate > 48000 || samples > 0x630) {
        gba_fail(g, GBA_ERROR_FORMAT);
        return;
    }
    for (unsigned sample = 0; sample < samples; sample++) {
        int left = 0, right = 0;
        unsigned channels = gba_read8(g, area + 6);
        if (channels > 12) {
            channels = 12;
        }
        for (unsigned ch = 0; ch < channels; ch++) {
            uint32_t c = area + 0x50 + ch * 64;
            unsigned flags = gba_read8(g, c);
            if (!flags) {
                continue;
            }
            uint32_t wave = gba_read32(g, c + 36), position = gba_read32(g, c + 8), fraction = gba_read32(g, c + 12);
            unsigned envelope = gba_read8(g, c + 16);
            if (flags & 0x80) {
                position = fraction = 0;
                envelope = 0;
                flags = 1;
                gba_write8(g, c, flags);
            }
            if (!sample) {
                if (flags & 0x40) {
                    envelope = envelope * gba_read8(g, c + 7) / 256;
                    if (!envelope) {
                        gba_write8(g, c, 0);
                        continue;
                    }
                } else if (flags & 1) {
                    envelope += gba_read8(g, c + 4);
                    if (envelope >= 255) {
                        envelope = 255;
                        flags = 2;
                    }
                } else if (flags & 2) {
                    envelope = envelope * gba_read8(g, c + 5) / 256;
                    unsigned sustain = gba_read8(g, c + 6);
                    if (envelope <= sustain) {
                        envelope = sustain;
                        flags = 4;
                    }
                }
                gba_write8(g, c, flags);
                gba_write8(g, c + 16, envelope);
            }
            unsigned size = gba_read32(g, wave + 12);
            if (position >= size) {
                unsigned loop = gba_read32(g, wave + 8);
                if ((gba_read16(g, wave + 2) & 0x4000) && loop < size) {
                    position = loop + (position - loop) % (size - loop);
                } else {
                    gba_write8(g, c, 0);
                    continue;
                }
            }
            int level = gba_sign(gba_read8(g, wave + 16 + position), 8);
            right += level * (int)gba_read8(g, c + 2) * (int)envelope / 65536;
            left += level * (int)gba_read8(g, c + 3) * (int)envelope / 65536;
            uint64_t phase = (uint64_t)fraction + ((uint64_t)gba_read32(g, c + 32) << 16) / rate;
            position += (uint32_t)(phase >> 16);
            fraction = (uint32_t)phase & 0xffff;
            gba_write32(g, c + 8, position);
            gba_write32(g, c + 12, fraction);
        }
        unsigned volume = gba_read8(g, area + 7);
        if (!volume) {
            volume = 15;
        }
        left = left * (int)volume / 15;
        right = right * (int)volume / 15;
        unsigned reverb = gba_read8(g, area + 5) & 127;
        int echo =
            (gba_sign(gba_read8(g, area + 0x350 + sample), 8) + gba_sign(gba_read8(g, area + 0x980 + sample), 8)) *
            (int)reverb / 256;
        left += echo;
        right += echo;
        if (left < -128) {
            left = -128;
        }
        if (left > 127) {
            left = 127;
        }
        if (right < -128) {
            right = -128;
        }
        if (right > 127) {
            right = 127;
        }
        gba_write8(g, area + 0x350 + sample, (uint32_t)left);
        gba_write8(g, area + 0x980 + sample, (uint32_t)right);
    }
}
int gba_bios_swi(gba_t *g, unsigned number) {
    g->machine.bios_latch = 0xe3a02004;
    gba_cpu_t *c = &g->machine.cpu;
    uint32_t source = c->r[0], destination = c->r[1], control = c->r[2];
    switch (number) {
    case 0: {
        uint8_t boot = g->memory.iwram[0x7ffa];
        gba_memset(g->memory.iwram + 0x7e00, 0, 0x200);
        gba_cpu_mode(g, 0x1f);
        c->r[13] = 0x03007f00;
        c->r[14] = 0;
        c->bank[2][5] = 0x03007fa0;
        c->bank[3][5] = 0x03007fe0;
        c->r[15] = boot ? 0x02000000 : 0x08000000;
        c->pipeline_valid = 0;
        break;
    }
    case 1:
        if (source & 1) {
            gba_memset(g->memory.ewram, 0, 0x40000);
        }
        if (source & 2) {
            gba_memset(g->memory.iwram, 0, 0x7e00);
        }
        if (source & 4) {
            gba_memset(g->memory.palette, 0, 0x400);
        }
        if (source & 8) {
            gba_memset(g->memory.vram, 0, 0x18000);
        }
        if (source & 16) {
            gba_memset(g->memory.oam, 0, 0x400);
        }
        if (source & 32) {
            for (unsigned a = 0x120; a <= 0x12a; a += 2) {
                gba_write16(g, 0x04000000 + a, 0);
            }
        }
        if (source & 64) {
            for (unsigned a = 0x60; a <= 0x88; a += 2) {
                gba_write16(g, 0x04000000 + a, 0);
            }
            gba_write16(g, 0x04000088, 0x200);
        }
        if (source & 128) {
            for (unsigned a = 0; a < 0x60; a += 2) {
                gba_write16(g, 0x04000000 + a, 0);
            }
            for (unsigned a = 0xb0; a <= 0x110; a += 2) {
                gba_write16(g, 0x04000000 + a, 0);
            }
        }
        gba_write16(g, 0x04000000, 0x80);
        break;
    case 2:
        c->halted = 1;
        break;
    case 3:
        c->stopped = 1;
        break;
    case 4:
    case 5:
        if (number == 5) {
            source = 1;
            destination = 1;
        }
        if (source) {
            gba_put16(g->memory.iwram + 0x7ff8, (uint16_t)(gba_le16(g->memory.iwram + 0x7ff8) & ~destination));
        }
        c->wait_mask = destination & 0x3fff;
        gba_put16(g->io + 0x208, 1);
        break;
    case 6:
    case 7: {
        int64_t numerator = gba_sign(number == 6 ? source : destination, 32),
                divisor = gba_sign(number == 6 ? destination : source, 32);
        int64_t quotient = divisor ? numerator / divisor : numerator < 0 ? -1 : 1;
        c->r[0] = (uint32_t)quotient;
        c->r[1] = divisor ? (uint32_t)(numerator % divisor) : (uint32_t)numerator;
        c->r[3] = quotient < 0 ? 0u - (uint32_t)quotient : (uint32_t)quotient;
        break;
    }
    case 8:
        c->r[0] = gba_sqrt(source);
        break;
    case 9:
        c->r[0] = (uint32_t)gba_atan(c, gba_sign(source, 32));
        break;
    case 10:
        c->r[0] = gba_atan2(c, gba_sign(source, 32), gba_sign(destination, 32));
        c->r[3] = 0x170;
        break;
    case 11:
    case 12: {
        unsigned width = number == 12 || (control & 0x4000000) ? 4 : 2;
        unsigned count = control & 0x1fffff;
        if (number == 12) {
            count = (count + 7) & ~7u;
        }
        /* BIOS refuses its own address area; the bus handles alignment.
         * Preserve address low bits for the 8-bit save bus and odd LDRH. */
        if (source < 0x02000000 || source + count * width < 0x02000000) {
            break;
        }
        uint32_t fill = 0;
        if (control & 0x1000000) {
            fill = width == 2 ? gba_read16(g, source) : gba_bus_read(g, source, width, 0, 0);
        }
        for (unsigned i = 0; i < count && !g->fault; i++) {
            uint32_t value = (control & 0x1000000) ? fill
                             : width == 2          ? gba_read16(g, source + i * width)
                                                   : gba_bus_read(g, source + i * width, width, i != 0, 0);
            gba_bus_write(g, destination + i * width, value, width, i != 0);
        }
        break;
    }
    case 13:
        c->r[0] = 0xbaae187f;
        break;
    case 14:
    case 15:
        gba_bios_affine(g, number == 15);
        break;
    case 16:
        gba_bios_unpack(g, source, destination, control);
        break;
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
        gba_bios_decompress(g, number, source, destination);
        break;
    case 25:
        gba_write16(g, 0x04000088, (gba_io16(g, 0x88) & 0xfc00) | (source ? 0x200 : 0));
        break;
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 40:
    case 41:
        gba_sound_driver(g, number);
        break;
    case 31:
        c->r[0] = gba_midi_frequency(g, source, destination, control);
        break;
    case 38:
        g->running = 0;
        gba_reset(g);
        g->running = 1;
        break;
    case 39:
        if (destination & 0x80) {
            c->stopped = 1;
        } else {
            c->halted = 1;
        }
        break;
    default:
        gba_fail(g, GBA_ERROR_UNSUPPORTED);
        break;
    }
    return 2;
}
