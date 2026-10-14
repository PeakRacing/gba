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
#if GBA_ENABLE_RTC
static unsigned gba_bcd(unsigned value) {
    return value / 10 * 16 + value % 10;
}
static unsigned gba_unbcd(unsigned value) {
    return (value >> 4) * 10 + (value & 15);
}
static int gba_valid_bcd(unsigned value) {
    return (value & 15) < 10 && (value >> 4) < 10;
}
#endif
void gba_rtc_rebase(gba_t *g) {
    g->machine.rtc.last_ticks = gba_get_ticks();
}
#if GBA_ENABLE_RTC
static void gba_rtc_periodic_irq(gba_t *g, int crossed_minute) {
    /* Tick-level approximation of the documented 30-second minute duty.
     * IF stays latched; an active level can request it again after software ACK. */
    if ((g->machine.rtc.control & 8) && (crossed_minute || g->machine.rtc.second < 30)) {
        gba_irq(g, 0x2000);
        if (gba_io16(g, 0x200) & 0x2000) {
            g->machine.cpu.stopped = 0;
        }
    }
}
#endif
void gba_rtc_sync(gba_t *g) {
#if GBA_ENABLE_RTC
    gba_rtc_t *r = &g->machine.rtc;
    uint32_t now = gba_get_ticks(), delta = now - r->last_ticks;
    r->last_ticks = now;
    if (!g->rom_size || !g->machine.cart.rtc_enabled || !delta) {
        return;
    }
    uint64_t milliseconds = (uint64_t)r->milliseconds + delta;
    uint32_t elapsed = (uint32_t)(milliseconds / 1000);
    r->milliseconds = (uint32_t)(milliseconds % 1000);
    if (!elapsed) {
        gba_rtc_periodic_irq(g, 0);
        return;
    }
    r->dirty = 1;
    uint32_t seconds = r->second + elapsed, minutes = r->minute + seconds / 60, hours = r->hour + minutes / 60;
    r->second = seconds % 60;
    r->minute = minutes % 60;
    r->hour = hours % 24;
    gba_rtc_periodic_irq(g, seconds >= 60);
    unsigned days = hours / 24;
    while (days--) {
        r->weekday = (r->weekday + 1) % 7;
        if (++r->day > gba_month_days(r->year, r->month)) {
            r->day = 1;
            if (++r->month > 12) {
                r->month = 1;
                r->year = (r->year + 1) % 100;
            }
        }
    }
#else
    (void)g;
#endif
}
#if GBA_ENABLE_RTC
static void gba_rtc_command(gba_t *g) {
    gba_rtc_t *r = &g->machine.rtc;
    if ((r->command & 0xf0) != 0x60) {
        unsigned reverse = 0;
        for (unsigned i = 0; i < 8; i++) {
            reverse = (reverse << 1) | ((r->command >> i) & 1);
        }
        r->command = reverse;
    }
    if ((r->command & 0xf0) != 0x60) {
        r->transfer_size = 0;
        return;
    }
    unsigned op = (r->command >> 1) & 7;
    r->reading = r->command & 1;
    /* A zero-parameter strobe consumes the command until CS is lowered. */
    r->bits = 8;
    if (op == 0) {
        r->control = 0;
        r->year = r->weekday = r->hour = r->minute = r->second = r->milliseconds = 0;
        r->month = r->day = 1;
        r->transfer_size = 0;
        gba_rtc_rebase(g);
        r->dirty = 1;
        return;
    }
    if (op == 4) {
        r->transfer_size = 0;
        gba_irq(g, 0x2000);
        if (gba_io16(g, 0x200) & 0x2000) {
            g->machine.cpu.stopped = 0;
        }
        return;
    }
    if (op == 1) {
        r->transfer_size = 1;
        r->transfer[0] = r->control;
        if (r->reading && (r->control & 0x80)) {
            r->control &= ~0x80u;
            r->dirty = 1;
        }
    } else if (op == 2 || op == 3) {
        unsigned offset = op == 2 ? 4 : 0;
        r->transfer_size = op == 2 ? 7 : 3;
        if (op == 2) {
            r->transfer[0] = gba_bcd(r->year);
            r->transfer[1] = gba_bcd(r->month);
            r->transfer[2] = gba_bcd(r->day);
            r->transfer[3] = r->weekday;
        }
        unsigned hour = (r->control & 0x40) ? r->hour : r->hour % 12;
        r->transfer[offset] = gba_bcd(hour) | (r->hour >= 12 ? 0x80 : 0);
        r->transfer[offset + 1] = gba_bcd(r->minute);
        r->transfer[offset + 2] = gba_bcd(r->second);
    } else {
        /* The three unused registers read FFh and ignore writes. */
        r->transfer_size = 1;
        r->transfer[0] = 0xff;
    }
    r->bits = 0;
}
static void gba_rtc_apply(gba_t *g) {
    gba_rtc_t *r = &g->machine.rtc;
    unsigned op = (r->command >> 1) & 7;
    if (op == 1) {
        r->control = r->transfer[0] & 0x6a;
    } else if (op == 2 || op == 3) {
        unsigned offset = op == 2 ? 4 : 0;
        unsigned encoded_hour = r->transfer[offset] & 0x3f;
        unsigned hour = gba_unbcd(encoded_hour), minute = gba_unbcd(r->transfer[offset + 1]),
                 second = gba_unbcd(r->transfer[offset + 2]);
        if (!gba_valid_bcd(encoded_hour) || (r->transfer[offset] & 0x40) || !gba_valid_bcd(r->transfer[offset + 1]) ||
            !gba_valid_bcd(r->transfer[offset + 2]) || hour >= ((r->control & 0x40) ? 24u : 12u) || minute >= 60 ||
            second >= 60) {
            return;
        }
        if (!(r->control & 0x40)) {
            hour += (r->transfer[offset] & 0x80) ? 12 : 0;
        }
        if (op == 2) {
            unsigned year = gba_unbcd(r->transfer[0]), month = gba_unbcd(r->transfer[1]),
                     day = gba_unbcd(r->transfer[2]);
            if (!gba_valid_bcd(r->transfer[0]) || !gba_valid_bcd(r->transfer[1]) || !gba_valid_bcd(r->transfer[2]) ||
                year >= 100 || month < 1 || month > 12 || day < 1 || day > gba_month_days(year, month) ||
                r->transfer[3] > 6) {
                return;
            }
            r->year = year;
            r->month = month;
            r->day = day;
            r->weekday = r->transfer[3];
        }
        r->hour = hour;
        r->minute = minute;
        r->second = second;
        r->milliseconds = 0;
        gba_rtc_rebase(g);
    } else {
        return;
    }
    r->dirty = 1;
}
#endif
void gba_rtc_gpio(gba_t *g, unsigned value) {
#if GBA_ENABLE_RTC
    gba_rtc_t *r = &g->machine.rtc;
    unsigned selected = (value >> 2) & 1, clock = value & 1;
    if (!selected) {
        r->chip_select = 0;
        r->clock = clock;
        r->command = 0;
        r->bits = 0;
        r->transfer_size = 0;
        r->output = 1;
        return;
    }
    if (!r->chip_select) {
        r->chip_select = 1;
        r->bits = 0;
        r->command = 0;
        r->transfer_size = 0;
    }
    if (clock && !r->clock) {
        unsigned bit = (value >> 1) & 1;
        if (!r->transfer_size) {
            if (r->bits >= 8) {
                r->clock = clock;
                return;
            }
            r->command |= bit << r->bits++;
            if (r->bits == 8) {
                gba_rtc_sync(g);
                gba_rtc_command(g);
            }
        } else if (r->bits < r->transfer_size * 8) {
            unsigned byte = r->bits / 8, position = r->bits & 7;
            if (r->reading) {
                r->output = (r->transfer[byte] >> position) & 1;
            } else {
                if (!position) {
                    r->transfer[byte] = 0;
                }
                r->transfer[byte] |= bit << position;
            }
            r->bits++;
            if (r->bits == r->transfer_size * 8 && !r->reading) {
                gba_rtc_apply(g);
            }
        }
    }
    r->clock = clock;
#else
    (void)g;
    (void)value;
#endif
}
