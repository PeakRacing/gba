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
#include "gba.h"
#ifndef GBA_PROFILE_ENTER
#define GBA_PROFILE_ENTER(stage) ((void)0)
#define GBA_PROFILE_LEAVE()      ((void)0)
#endif
#ifndef GBA_WAIT_DIAG
#define GBA_WAIT_DIAG(reason) ((void)0)
#endif

GBA_INLINE uint16_t gba_le16(const uint8_t *p) {
#if GBA_FAST_CPU && defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    typedef uint16_t alias16 __attribute__((may_alias));
    if (!((uintptr_t)p&1u)) return *(const alias16 *)(const void *)p;
#endif
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
GBA_INLINE uint32_t gba_le32(const uint8_t *p) {
#if GBA_FAST_CPU && defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    typedef uint32_t alias32 __attribute__((may_alias));
    if (!((uintptr_t)p&3u)) return *(const alias32 *)(const void *)p;
#endif
    return (uint32_t)gba_le16(p) | ((uint32_t)gba_le16(p + 2) << 16);
}
GBA_INLINE void gba_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
GBA_INLINE void gba_put32(uint8_t *p, uint32_t v) {
    gba_put16(p, (uint16_t)v);
    gba_put16(p + 2, (uint16_t)(v >> 16));
}

GBA_INLINE int32_t gba_sign(uint32_t value, unsigned bits) {
    uint32_t s = 1u << (bits - 1);
    return (value & s) ? -1 - (int32_t)((~value) & (s - 1)) : (int32_t)(value & (s - 1));
}
GBA_INLINE uint32_t gba_ror(uint32_t x, unsigned n) {
    n &= 31;
    return n ? (x >> n) | (x << (32 - n)) : x;
}

/* Game Pak divisors are a tiny fixed set. Constant C divisions let targets
 * without a hardware divider avoid a general software divide on every fetch. */

GBA_INLINE int32_t gba_fixed8(int32_t value) {
    return value >= 0 ? value / 256 : -1 - (-1 - value) / 256;
}

GBA_INLINE void gba_fail(gba_t *g, int error) {
    if (!g->fault) {
        g->fault = error;
        g->gba_quit = 1;
    }
}
#if GBA_TEST_MODE
void gba_test_instruction(gba_t *g, uint32_t pc, uint32_t opcode);
#endif
#if GBA_TEST_PROFILE
uint32_t gba_profile_ticks(void);
#endif

#include "gba_bus_inline.h"
#include "gba_cpu_inline.h"
#include "gba_timer_inline.h"
#include "gba_ppu_inline.h"
#include "gba_cart_inline.h"

#if GBA_DYNAREC_PAGE_INDEX && GBA_DYNAREC_STATS
void gba_dynarec_same_value(gba_t *g);
#endif
