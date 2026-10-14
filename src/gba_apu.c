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
static const unsigned gba_sound_base[4] = {0x62, 0x68, 0x72, 0x78};
static const unsigned gba_sound_control[4] = {0x64, 0x6c, 0x74, 0x7c};
#if GBA_ENABLE_SOUND
static const uint8_t gba_duty[4] = {0x01, 0x81, 0x87, 0x7e};
#endif

void gba_apu_write(gba_t *g, unsigned address, uint16_t old) {
    gba_apu_t *a = &g->machine.apu;
    uint16_t value = gba_io16(g, address);
    if (address == 0x82) {
        for (unsigned f = 0; f < 2; f++) {
            if (value & (1u << (11 + f * 4))) {
                a->fifo_count[f] = a->fifo_read[f] = a->fifo_sample[f] = 0;
            }
        }
        gba_put16(g->io + address, value & 0x770f);
    }
    if (address == 0x84) {
        gba_put16(g->io + address, value & 0x80);
        if (!(value & 0x80) && (old & 0x80)) {
            for (unsigned i = 0; i < 4; i++) {
                a->channel[i].enabled = 0;
            }
            gba_memset(g->io + 0x60, 0, 0x22);
        }
        return;
    }
    for (unsigned ch = 0; ch < 4; ch++) {
        gba_apu_channel_t *c = &a->channel[ch];
        if (address == gba_sound_base[ch]) {
            c->length = ch == 2 ? 256 - (value & 0xff) : 64 - (value & 0x3f);
            if (ch != 2 && !(value & 0xf800)) {
                c->enabled = 0;
            }
        }
        if (address == gba_sound_control[ch] && (value & 0x8000)) {
            uint16_t envelope = gba_io16(g, gba_sound_base[ch]);
            c->enabled = (gba_io16(g, 0x84) & 0x80) && (ch == 2 ? (gba_io16(g, 0x70) & 0x80) : (envelope & 0xf800));
            c->volume = envelope >> 12;
            c->envelope_phase = 0;
            c->phase = 0;
            c->position = 0;
            c->lfsr = 0x7fff;
            if (!c->length) {
                c->length = ch == 2 ? 256 : 64;
            }
            if (ch == 0) {
                c->sweep_frequency = value & 0x7ff;
                c->sweep_phase = 0;
                c->sweep_negated = 0;
                unsigned shift = gba_io16(g, 0x60) & 7;
                if (shift && !(gba_io16(g, 0x60) & 8) && c->sweep_frequency + (c->sweep_frequency >> shift) > 2047) {
                    c->enabled = 0;
                }
            }
            gba_put16(g->io + address, value & 0x7fff);
        }
    }
    if (address == 0x70 && !(value & 0x80)) {
        a->channel[2].enabled = 0;
    }
    if (address == 0x60 && (old & 8) && !(value & 8) && a->channel[0].sweep_negated) {
        a->channel[0].enabled = 0;
    }
}
void gba_apu_timer(gba_t *g, unsigned timer) {
    g->event_cycles = 0;
    gba_apu_t *a = &g->machine.apu;
    uint16_t control = gba_io16(g, 0x82);
    for (unsigned f = 0; f < 2; f++) {
        if (((unsigned)control >> (10 + f * 4) & 1) != timer) {
            continue;
        }
        if (a->fifo_count[f]) {
            a->fifo_sample[f] = a->fifo[f][a->fifo_read[f]];
            a->fifo_read[f] = (a->fifo_read[f] + 1) & 31;
            a->fifo_count[f]--;
        }
        if (a->fifo_count[f] <= 16) {
            for (unsigned ch = 1; ch <= 2; ch++) {
                gba_dma_t *d = &g->machine.dma[ch];
                if ((d->control & 0xb000) == 0xb000 && (d->destination & ~3u) == 0x040000a0u + f * 4 && !d->pending) {
                    d->fifo = 1;
                    d->remaining = 4;
                    d->pending = 1;
                    d->startup = GBA_DMA_EVENT_DELAY;
                    d->sequential = 0;
                }
            }
        }
    }
}
static void gba_apu_sequencer(gba_t *g) {
    gba_apu_t *a = &g->machine.apu;
    unsigned step = a->sequencer_step;
    if (!(step & 1)) {
        for (unsigned ch = 0; ch < 4; ch++) {
            gba_apu_channel_t *c = &a->channel[ch];
            if ((gba_io16(g, gba_sound_control[ch]) & 0x4000) && c->length && !--c->length) {
                c->enabled = 0;
            }
        }
    }
    if (step == 2 || step == 6) {
        gba_apu_channel_t *c = &a->channel[0];
        uint16_t control = gba_io16(g, 0x60);
        unsigned period = (control >> 4) & 7, shift = control & 7;
        if (++c->sweep_phase >= (period ? period : 8)) {
            c->sweep_phase = 0;
            if (period && shift && c->enabled) {
                uint32_t delta = c->sweep_frequency >> shift;
                uint32_t next = (control & 8) ? c->sweep_frequency - delta : c->sweep_frequency + delta;
                if (control & 8) {
                    c->sweep_negated = 1;
                }
                if (next > 2047) {
                    c->enabled = 0;
                } else {
                    c->sweep_frequency = next;
                    gba_put16(g->io + 0x64, (uint16_t)((gba_io16(g, 0x64) & 0xf800) | next));
                    if (!(control & 8) && next + (next >> shift) > 2047) {
                        c->enabled = 0;
                    }
                }
            }
        }
    }
    if (step == 7) {
        for (unsigned ch = 0; ch < 4; ch++) {
            if (ch == 2) {
                continue;
            }
            gba_apu_channel_t *c = &a->channel[ch];
            uint16_t env = gba_io16(g, gba_sound_base[ch]);
            unsigned period = (env >> 8) & 7;
            if (period && ++c->envelope_phase >= period) {
                c->envelope_phase = 0;
                if ((env & 0x800) && c->volume < 15) {
                    c->volume++;
                }
                if (!(env & 0x800) && c->volume) {
                    c->volume--;
                }
            }
        }
    }
    a->sequencer_step = (step + 1) & 7;
}
#if GBA_ENABLE_SOUND
static int gba_apu_level(gba_t *g, unsigned ch) {
    gba_apu_t *a = &g->machine.apu;
    gba_apu_channel_t *c = &a->channel[ch];
    if (!c->enabled) {
        return 0;
    }
    if (ch < 2) {
        unsigned duty = (gba_io16(g, gba_sound_base[ch]) >> 6) & 3;
        return (gba_duty[duty] & (1u << (c->position & 7))) ? (int)c->volume : -(int)c->volume;
    }
    if (ch == 2) {
        uint16_t control = gba_io16(g, 0x70), volume = gba_io16(g, 0x72);
        unsigned position = c->position & ((control & 0x20) ? 63 : 31);
        unsigned bank = ((control >> 6) & 1) * 16;
        unsigned packed = a->wave[(bank + position / 2) & 31];
        int level = (int)((position & 1) ? packed & 15 : packed >> 4) - 8;
        if (volume & 0x8000) {
            return level * 3 / 4;
        }
        unsigned shift = (volume >> 13) & 3;
        return shift ? level / (1 << (shift - 1)) : 0;
    }
    return (c->lfsr & 1) ? -(int)c->volume : (int)c->volume;
}
static void gba_apu_sample(gba_t *g) {
    uint16_t master = gba_io16(g, 0x80), direct = gba_io16(g, 0x82);
    int left = 0, right = 0;
    if (gba_io16(g, 0x84) & 0x80) {
        for (unsigned ch = 0; ch < 4; ch++) {
            int level = gba_apu_level(g, ch);
            if (master & (1u << (12 + ch))) {
                left += level;
            }
            if (master & (1u << (8 + ch))) {
                right += level;
            }
        }
        unsigned ratio = direct & 3;
        if (ratio > 2) {
            ratio = 2;
        }
        left = left * (((master >> 4) & 7) + 1) * (1 << ratio) / 4;
        right = right * ((master & 7) + 1) * (1 << ratio) / 4;
        for (unsigned f = 0; f < 2; f++) {
            int level = gba_sign(g->machine.apu.fifo_sample[f], 8) * ((direct & (1u << (2 + f))) ? 4 : 2);
            if (direct & (1u << (9 + f * 4))) {
                left += level;
            }
            if (direct & (1u << (8 + f * 4))) {
                right += level;
            }
        }
    }
    int bias = gba_io16(g, 0x88) & 0x3ff;
    left += bias;
    right += bias;
    if (left < 0) {
        left = 0;
    }
    if (left > 1023) {
        left = 1023;
    }
    if (right < 0) {
        right = 0;
    }
    if (right > 1023) {
        right = 1023;
    }
    g->audio[g->audio_count * 2] = (int16_t)((left - 512) * 64);
    g->audio[g->audio_count * 2 + 1] = (int16_t)((right - 512) * 64);
    if (++g->audio_count == GBA_APU_BUFFER_SAMPLES) {
        uint8_t *p = (uint8_t *)g->audio;
        for (unsigned i = 0; i < GBA_APU_BUFFER_SAMPLES * 2; i++) {
            uint16_t value = (uint16_t)g->audio[i];
            gba_put16(p + i * 2, value);
        }
        if (gba_sound_output(p, GBA_APU_BUFFER_SAMPLES * 4)) {
            gba_fail(g, GBA_ERROR_IO);
        }
        g->audio_count = 0;
    }
}
#endif
void gba_apu_advance(gba_t *g, uint32_t cycles) {
    gba_apu_t *a = &g->machine.apu;
    for (unsigned ch = 0; ch < 4; ch++) {
        gba_apu_channel_t *c = &a->channel[ch];
        if (!c->enabled) {
            continue;
        }
        uint32_t period;
        if (ch < 3) {
            period = (2048 - (gba_io16(g, gba_sound_control[ch]) & 0x7ff)) * (ch == 2 ? 8 : 16);
        } else {
            unsigned noise = gba_io16(g, 0x7c), divisor = noise & 7;
            period = (divisor ? divisor * 16 : 8) * 4u << ((noise >> 4) & 15);
        }
        c->phase += cycles;
        if (c->phase < period) {
            continue;
        }
        uint32_t steps = c->phase / period;
        c->phase %= period;
        if (ch == 3) {
            while (steps--) {
                uint32_t bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;
                c->lfsr = (c->lfsr >> 1) | (bit << 14);
                if (gba_io16(g, 0x7c) & 8) {
                    c->lfsr = (c->lfsr & ~0x40u) | (bit << 6);
                }
            }
        } else {
            c->position = (c->position + steps) & (ch == 2 ? 63 : 7);
        }
    }
    a->sequencer_phase += cycles;
    if (a->sequencer_phase >= 32768) {
        a->sequencer_phase -= 32768;
        gba_apu_sequencer(g);
    }
#if GBA_ENABLE_SOUND
    a->sample_phase += cycles * GBA_APU_SAMPLE_RATE;
    if (a->sample_phase >= GBA_CPU_CLOCK) {
        a->sample_phase -= GBA_CPU_CLOCK;
        gba_apu_sample(g);
    }
#endif
}
