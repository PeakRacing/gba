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

static unsigned gba_mode_index(uint32_t mode) {
    switch (mode & 31) {
    case 0x11:
        return 1;
    case 0x12:
        return 2;
    case 0x13:
        return 3;
    case 0x17:
        return 4;
    case 0x1b:
        return 5;
    default:
        return 0;
    }
}
void gba_cpu_mode(gba_t *g, uint32_t value) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned old = gba_mode_index(c->cpsr), next = gba_mode_index(value);
    if (old != next) {
        c->bank[old][5] = c->r[13];
        c->bank[old][6] = c->r[14];
        if (old == 1 || next == 1) {
            unsigned from = old == 1 ? 1 : 0, to = next == 1 ? 1 : 0;
            for (unsigned i = 0; i < 5; i++) {
                c->bank[from][i] = c->r[8 + i];
                c->r[8 + i] = c->bank[to][i];
            }
        }
        c->r[13] = c->bank[next][5];
        c->r[14] = c->bank[next][6];
    }
    c->cpsr = value;
}
static void gba_branch(gba_t *g, uint32_t target) {
    gba_cpu_t *c = &g->machine.cpu;
    c->r[15] = target & ((c->cpsr & GBA_T) ? ~1u : ~3u);
    c->pipeline_valid = 0;
    c->fetch_sequential = 0;
    g->machine.prefetch_count = g->machine.prefetch_phase = 0;
}
void gba_cpu_exception(gba_t *g, unsigned mode, uint32_t vector, uint32_t lr) {
    gba_cpu_t *c = &g->machine.cpu;
    uint32_t saved = c->cpsr;
    gba_cpu_mode(g, (saved & ~(31u | GBA_T)) | mode | 0x80);
    c->spsr[gba_mode_index(mode)] = saved;
    c->r[14] = lr;
    gba_branch(g, vector);
}
static uint32_t gba_reg(const gba_cpu_t *c, unsigned reg, uint32_t pc, unsigned advance) {
    return reg == 15 ? pc + advance : c->r[reg];
}
static void gba_nz(gba_cpu_t *c, uint32_t result) {
    c->cpsr = (c->cpsr & ~(GBA_N | GBA_Z)) | (result & GBA_N) | (result == 0 ? GBA_Z : 0);
}
static void gba_carry(gba_cpu_t *c, unsigned carry) {
    c->cpsr = (c->cpsr & ~GBA_C) | (carry ? GBA_C : 0);
}
static uint32_t gba_add(gba_cpu_t *c, uint32_t a, uint32_t b, unsigned carry, int flags) {
    uint64_t sum = (uint64_t)a + b + carry;
    uint32_t result = (uint32_t)sum;
    if (flags) {
        gba_nz(c, result);
        gba_carry(c, (unsigned)(sum >> 32));
        c->cpsr = (c->cpsr & ~GBA_V) | (((~(a ^ b) & (a ^ result)) >> 3) & GBA_V);
    }
    return result;
}
static uint32_t gba_sub(gba_cpu_t *c, uint32_t a, uint32_t b, unsigned borrow, int flags) {
    uint64_t rhs = (uint64_t)b + borrow;
    uint32_t result = a - (uint32_t)rhs;
    if (flags) {
        gba_nz(c, result);
        gba_carry(c, (uint64_t)a >= rhs);
        c->cpsr = (c->cpsr & ~GBA_V) | ((((a ^ b) & (a ^ result)) >> 3) & GBA_V);
    }
    return result;
}
static uint32_t gba_shift(uint32_t value, unsigned kind, unsigned amount, unsigned *carry, int immediate) {
    if (!amount) {
        if (!immediate || kind == 0) {
            return value;
        }
        if (kind == 3) {
            uint32_t result = (*carry << 31) | (value >> 1);
            *carry = value & 1;
            return result;
        }
        amount = 32;
    }
    if (kind == 0) {
        *carry = amount <= 32 ? (value >> (32 - amount)) & 1 : 0;
        return amount < 32 ? value << amount : 0;
    }
    if (kind == 1) {
        *carry = amount <= 32 ? (value >> (amount - 1)) & 1 : 0;
        return amount < 32 ? value >> amount : 0;
    }
    if (kind == 2) {
        if (amount >= 32) {
            *carry = value >> 31;
            return 0u - *carry;
        }
        *carry = (value >> (amount - 1)) & 1;
        return (value >> amount) | ((value & GBA_N) ? (~0u << (32 - amount)) : 0);
    }
    value = gba_ror(value, amount);
    *carry = value >> 31;
    return value;
}
static int gba_condition(uint32_t flags, unsigned condition) {
    unsigned n = flags >> 31, z = (flags >> 30) & 1, c = (flags >> 29) & 1, v = (flags >> 28) & 1;
    switch (condition) {
    case 0:
        return z;
    case 1:
        return !z;
    case 2:
        return c;
    case 3:
        return !c;
    case 4:
        return n;
    case 5:
        return !n;
    case 6:
        return v;
    case 7:
        return !v;
    case 8:
        return c && !z;
    case 9:
        return !c || z;
    case 10:
        return n == v;
    case 11:
        return n != v;
    case 12:
        return !z && n == v;
    case 13:
        return z || n != v;
    case 14:
        return 1;
    default:
        return 0;
    }
}
static unsigned gba_multiply_cycles(uint32_t multiplier, int is_signed) {
    for (unsigned n = 1; n < 4; n++) {
        uint32_t high = multiplier >> (n * 8);
        if (!high || (is_signed && high == (UINT32_MAX >> (n * 8)))) {
            return n;
        }
    }
    return 4;
}
static uint32_t gba_user_read(gba_cpu_t *c, unsigned r, uint32_t pc) {
    unsigned mode = gba_mode_index(c->cpsr);
    if (r == 15) {
        return pc + 12;
    }
    if (r >= 8 && r <= 12 && mode == 1) {
        return c->bank[0][r - 8];
    }
    if (r >= 13 && mode) {
        return c->bank[0][r - 8];
    }
    return c->r[r];
}
static void gba_user_write(gba_cpu_t *c, unsigned r, uint32_t value) {
    unsigned mode = gba_mode_index(c->cpsr);
    if ((r >= 8 && r <= 12 && mode == 1) || (r >= 13 && mode)) {
        c->bank[0][r - 8] = value;
    } else {
        c->r[r] = value;
    }
}
static unsigned gba_popcount(uint32_t list) {
    unsigned count = 0;
    while (list) {
        count++;
        list &= list - 1;
    }
    return count;
}
static uint32_t gba_arm(gba_t *g, uint32_t op, uint32_t pc) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
    unsigned flags = (op >> 20) & 1, extra = 0;
    if (!gba_condition(c->cpsr, op >> 28)) {
        return 0;
    }
    if ((op & 0x0ffffff0) == 0x012fff10) {
        uint32_t target = gba_reg(c, rm, pc, 8);
        c->cpsr = (c->cpsr & ~GBA_T) | ((target & 1) ? GBA_T : 0);
        gba_branch(g, target);
        return 0;
    }
    if ((op & 0x0fbf0fff) == 0x010f0000) {
        c->r[rd] = (op & 0x400000) ? c->spsr[gba_mode_index(c->cpsr)] : c->cpsr;
        return 0;
    }
    if ((op & 0x0db0f000) == 0x0120f000) {
        uint32_t value = (op & 0x2000000) ? gba_ror(op & 0xff, ((op >> 8) & 15) * 2) : gba_reg(c, rm, pc, 8);
        uint32_t mask = 0;
        for (unsigned i = 0; i < 4; i++) {
            if (op & (1u << (16 + i))) {
                mask |= 0xffu << (i * 8);
            }
        }
        unsigned mode = gba_mode_index(c->cpsr);
        if (op & 0x400000) {
            if (mode) {
                c->spsr[mode] = (c->spsr[mode] & ~mask) | (value & mask);
            }
        } else {
            if ((c->cpsr & 31) == 0x10) {
                mask &= 0xff000000;
            }
            gba_cpu_mode(g, (c->cpsr & ~mask) | (value & mask));
        }
        return 0;
    }
    if ((op & 0x0f8000f0) == 0x00800090) {
        unsigned high = rn, low = rd, rs = (op >> 8) & 15;
        uint32_t a = c->r[rm], b = c->r[rs];
        uint64_t result;
        if (op & 0x400000) {
            result = (uint64_t)((int64_t)gba_sign(a, 32) * gba_sign(b, 32));
        } else {
            result = (uint64_t)a * b;
        }
        if (op & 0x200000) {
            result += ((uint64_t)c->r[high] << 32) | c->r[low];
        }
        c->r[low] = (uint32_t)result;
        c->r[high] = (uint32_t)(result >> 32);
        if (flags) {
            c->cpsr = (c->cpsr & ~(GBA_N | GBA_Z)) | (c->r[high] & GBA_N) | (!result ? GBA_Z : 0);
        }
        return gba_multiply_cycles(b, (op & 0x400000) != 0) + 1 + ((op >> 21) & 1);
    }
    if ((op & 0x0fc000f0) == 0x00000090) {
        unsigned rs = (op >> 8) & 15;
        uint32_t result = c->r[rm] * c->r[rs];
        extra = gba_multiply_cycles(c->r[rs], 1);
        if (op & 0x200000) {
            result += c->r[rd];
            extra++;
        }
        c->r[rn] = result;
        if (flags) {
            gba_nz(c, result);
        }
        return extra;
    }
    if ((op & 0x0fb00ff0) == 0x01000090) {
        uint32_t address = gba_reg(c, rn, pc, 8), value = gba_reg(c, rm, pc, 12), loaded;
        if (op & 0x400000) {
            loaded = gba_read8(g, address);
            gba_write8(g, address, value);
        } else {
            loaded = gba_read32(g, address);
            gba_write32(g, address, value);
        }
        c->r[rd] = loaded;
        return 1;
    }
    if ((op & 0x0e000090) == 0x00000090 && (op & 0x60)) {
        uint32_t base = gba_reg(c, rn, pc, 8);
        uint32_t offset = (op & 0x400000) ? ((op >> 4) & 0xf0) | (op & 15) : gba_reg(c, rm, pc, 8);
        uint32_t updated = (op & 0x800000) ? base + offset : base - offset;
        uint32_t address = (op & 0x1000000) ? updated : base, result;
        unsigned kind = (op >> 5) & 3;
        if (flags) {
            if (kind == 1) {
                result = gba_read16(g, address);
            } else if (kind == 2 || (address & 1)) {
                result = (uint32_t)gba_sign(gba_read8(g, address), 8);
            } else {
                result = (uint32_t)gba_sign(gba_read16(g, address), 16);
            }
            if (!(op & 0x1000000) || (op & 0x200000)) {
                if (rn != rd) {
                    c->r[rn] = updated;
                }
            }
            if (rd == 15) {
                gba_branch(g, result);
            } else {
                c->r[rd] = result;
            }
            return 1;
        }
        if (kind != 1) {
            gba_cpu_exception(g, 0x1b, 4, pc + 4);
            return 0;
        }
        gba_write16(g, address, gba_reg(c, rd, pc, 12));
        if (!(op & 0x1000000) || (op & 0x200000)) {
            c->r[rn] = updated;
        }
        return 0;
    }
    if ((op & 0x0c000000) == 0) {
        unsigned operation = (op >> 21) & 15, carry = (c->cpsr >> 29) & 1;
        unsigned reg_shift = !(op & 0x2000000) && (op & 0x10);
        uint32_t a = gba_reg(c, rn, pc, reg_shift ? 12 : 8), b, result = 0;
        if (op & 0x2000000) {
            unsigned rotate = ((op >> 8) & 15) * 2;
            b = gba_ror(op & 0xff, rotate);
            if (rotate) {
                carry = b >> 31;
            }
        } else {
            unsigned amount = reg_shift ? gba_reg(c, (op >> 8) & 15, pc, 8) & 0xff : (op >> 7) & 31;
            b = gba_shift(gba_reg(c, rm, pc, reg_shift ? 12 : 8), (op >> 5) & 3, amount, &carry, !reg_shift);
            extra = reg_shift ? 1 : 0;
        }
        int test = operation >= 8 && operation <= 11;
        int set = (flags || test) && rd != 15;
        switch (operation) {
        case 0:
        case 8:
            result = a & b;
            break;
        case 1:
        case 9:
            result = a ^ b;
            break;
        case 2:
        case 10:
            result = gba_sub(c, a, b, 0, set || test);
            break;
        case 3:
            result = gba_sub(c, b, a, 0, set);
            break;
        case 4:
        case 11:
            result = gba_add(c, a, b, 0, set || test);
            break;
        case 5:
            result = gba_add(c, a, b, (c->cpsr >> 29) & 1, set);
            break;
        case 6:
            result = gba_sub(c, a, b, !(c->cpsr & GBA_C), set);
            break;
        case 7:
            result = gba_sub(c, b, a, !(c->cpsr & GBA_C), set);
            break;
        case 12:
            result = a | b;
            break;
        case 13:
            result = b;
            break;
        case 14:
            result = a & ~b;
            break;
        case 15:
            result = ~b;
            break;
        }
        if ((set || test) && (operation < 2 || operation == 8 || operation == 9 || operation >= 12)) {
            gba_nz(c, result);
            gba_carry(c, carry);
        }
        /* ARM7 also restores SPSR for test encodings with Rd=PC, without a branch. */
        if (test && rd == 15 && flags && gba_mode_index(c->cpsr)) {
            gba_cpu_mode(g, c->spsr[gba_mode_index(c->cpsr)]);
        }
        if (!test) {
            if (rd == 15) {
                if (flags && gba_mode_index(c->cpsr)) {
                    gba_cpu_mode(g, c->spsr[gba_mode_index(c->cpsr)]);
                }
                gba_branch(g, result);
            } else {
                c->r[rd] = result;
            }
        }
        return extra;
    }
    if ((op & 0x0c000000) == 0x04000000) {
        uint32_t base = gba_reg(c, rn, pc, 8), offset = op & 0xfff;
        if (op & 0x2000000) {
            unsigned carry = (c->cpsr >> 29) & 1;
            if (op & 0x10) {
                gba_cpu_exception(g, 0x1b, 4, pc + 4);
                return 0;
            }
            offset = gba_shift(gba_reg(c, rm, pc, 8), (op >> 5) & 3, (op >> 7) & 31, &carry, 1);
        }
        uint32_t updated = (op & 0x800000) ? base + offset : base - offset;
        uint32_t address = (op & 0x1000000) ? updated : base;
        if (flags) {
            uint32_t result = (op & 0x400000) ? gba_read8(g, address) : gba_read32(g, address);
            if ((!(op & 0x1000000) || (op & 0x200000)) && rn != rd) {
                c->r[rn] = updated;
            }
            if (rd == 15) {
                gba_branch(g, result);
            } else {
                c->r[rd] = result;
            }
            return 1;
        }
        uint32_t value = gba_reg(c, rd, pc, 12);
        if (op & 0x400000) {
            gba_write8(g, address, value);
        } else {
            gba_write32(g, address, value);
        }
        if (!(op & 0x1000000) || (op & 0x200000)) {
            c->r[rn] = updated;
        }
        return 0;
    }
    if ((op & 0x0e000000) == 0x08000000) {
        uint32_t list = op & 0xffff, count = gba_popcount(list), base = gba_reg(c, rn, pc, 8);
        if (!count) {
            count = 16;
            list = 0x8000;
        }
        uint32_t updated = (op & 0x800000) ? base + count * 4 : base - count * 4;
        uint32_t address =
            (op & 0x800000) ? base + ((op & 0x1000000) ? 4 : 0) : base - count * 4 + ((op & 0x1000000) ? 0 : 4);
        int user = (op & 0x400000) && (!flags || !(list & 0x8000));
        unsigned sequential = 0;
        for (unsigned r = 0; r < 16; r++) {
            if (!(list & (1u << r))) {
                continue;
            }
            if (flags) {
                uint32_t value = gba_bus_read(g, address, 4, sequential, 0);
                if (r == 15) {
                    if ((op & 0x400000) && gba_mode_index(c->cpsr)) {
                        gba_cpu_mode(g, c->spsr[gba_mode_index(c->cpsr)]);
                    }
                    gba_branch(g, value);
                } else if (user) {
                    gba_user_write(c, r, value);
                } else {
                    c->r[r] = value;
                }
            } else {
                uint32_t value = user ? gba_user_read(c, r, pc) : gba_reg(c, r, pc, 12);
                if (r == rn && (op & 0x200000) && sequential) {
                    value = updated;
                }
                gba_bus_write(g, address, value, 4, sequential);
            }
            address += 4;
            sequential = 1;
        }
        if ((op & 0x200000) && (!flags || !(list & (1u << rn)))) {
            c->r[rn] = updated;
        }
        return flags ? 1 : 0;
    }
    if ((op & 0x0e000000) == 0x0a000000) {
        if (op & 0x1000000) {
            c->r[14] = pc + 4;
        }
        gba_branch(g, pc + 8 + (uint32_t)(gba_sign(op & 0xffffff, 24) * 4));
        return 0;
    }
    if ((op & 0x0f000000) == 0x0f000000) {
        if (!g->bios && GBA_BIOS_HLE) {
            return (uint32_t)gba_bios_swi(g, (op >> 16) & 0xff);
        }
        gba_cpu_exception(g, 0x13, 8, pc + 4);
        return 0;
    }
    gba_cpu_exception(g, 0x1b, 4, pc + 4);
    return 0;
}
static uint32_t gba_thumb(gba_t *g, uint16_t op, uint32_t pc) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned rd = op & 7, rs = (op >> 3) & 7, extra = 0;
    uint32_t a, b, result;
    if ((op & 0xe000) == 0) {
        if ((op & 0x1800) == 0x1800) {
            b = (op & 0x400) ? ((op >> 6) & 7) : c->r[(op >> 6) & 7];
            c->r[rd] = (op & 0x200) ? gba_sub(c, c->r[rs], b, 0, 1) : gba_add(c, c->r[rs], b, 0, 1);
        } else {
            unsigned carry = (c->cpsr >> 29) & 1;
            result = gba_shift(c->r[rs], (op >> 11) & 3, (op >> 6) & 31, &carry, 1);
            c->r[rd] = result;
            gba_nz(c, result);
            gba_carry(c, carry);
        }
        return 0;
    }
    if ((op & 0xe000) == 0x2000) {
        rd = (op >> 8) & 7;
        b = op & 0xff;
        switch ((op >> 11) & 3) {
        case 0:
            c->r[rd] = b;
            gba_nz(c, b);
            break;
        case 1:
            gba_sub(c, c->r[rd], b, 0, 1);
            break;
        case 2:
            c->r[rd] = gba_add(c, c->r[rd], b, 0, 1);
            break;
        case 3:
            c->r[rd] = gba_sub(c, c->r[rd], b, 0, 1);
            break;
        }
        return 0;
    }
    if ((op & 0xfc00) == 0x4000) {
        unsigned operation = (op >> 6) & 15, carry = (c->cpsr >> 29) & 1;
        a = c->r[rd];
        b = c->r[rs];
        result = a;
        switch (operation) {
        case 0:
        case 8:
            result = a & b;
            break;
        case 1:
            result = a ^ b;
            break;
        case 2:
        case 3:
        case 4:
        case 7:
            result = gba_shift(a, operation == 7 ? 3 : operation - 2, b & 0xff, &carry, 0);
            gba_carry(c, carry);
            extra = 1;
            break;
        case 5:
            result = gba_add(c, a, b, carry, 1);
            break;
        case 6:
            result = gba_sub(c, a, b, !carry, 1);
            break;
        case 9:
            result = gba_sub(c, 0, b, 0, 1);
            break;
        case 10:
            result = gba_sub(c, a, b, 0, 1);
            break;
        case 11:
            result = gba_add(c, a, b, 0, 1);
            break;
        case 12:
            result = a | b;
            break;
        case 13:
            result = a * b;
            extra = gba_multiply_cycles(a, 1);
            break;
        case 14:
            result = a & ~b;
            break;
        case 15:
            result = ~b;
            break;
        }
        gba_nz(c, result);
        if (operation != 8 && operation != 10 && operation != 11) {
            c->r[rd] = result;
        }
        return extra;
    }
    if ((op & 0xfc00) == 0x4400) {
        rd |= (op >> 4) & 8;
        rs = (op >> 3) & 15;
        a = gba_reg(c, rd, pc, 4);
        b = gba_reg(c, rs, pc, 4);
        switch ((op >> 8) & 3) {
        case 0:
            result = a + b;
            if (rd == 15) {
                gba_branch(g, result);
            } else {
                c->r[rd] = result;
            }
            break;
        case 1:
            gba_sub(c, a, b, 0, 1);
            break;
        case 2:
            if (rd == 15) {
                gba_branch(g, b);
            } else {
                c->r[rd] = b;
            }
            break;
        case 3:
            c->cpsr = (c->cpsr & ~GBA_T) | ((b & 1) ? GBA_T : 0);
            gba_branch(g, b);
            break;
        }
        return 0;
    }
    if ((op & 0xf800) == 0x4800) {
        c->r[(op >> 8) & 7] = gba_read32(g, ((pc + 4) & ~3u) + (op & 0xff) * 4);
        return 1;
    }
    if ((op & 0xf000) == 0x5000) {
        a = c->r[rs] + c->r[(op >> 6) & 7];
        switch ((op >> 9) & 7) {
        case 0:
            gba_write32(g, a, c->r[rd]);
            return 0;
        case 1:
            gba_write16(g, a, c->r[rd]);
            return 0;
        case 2:
            gba_write8(g, a, c->r[rd]);
            return 0;
        case 3:
            c->r[rd] = (uint32_t)gba_sign(gba_read8(g, a), 8);
            break;
        case 4:
            c->r[rd] = gba_read32(g, a);
            break;
        case 5:
            c->r[rd] = gba_read16(g, a);
            break;
        case 6:
            c->r[rd] = gba_read8(g, a);
            break;
        case 7:
            c->r[rd] = (uint32_t)gba_sign((a & 1) ? gba_read8(g, a) : gba_read16(g, a), (a & 1) ? 8 : 16);
            break;
        }
        return 1;
    }
    if ((op & 0xe000) == 0x6000) {
        a = c->r[rs] + ((op >> 6) & 31) * ((op & 0x1000) ? 1 : 4);
        if (op & 0x800) {
            c->r[rd] = (op & 0x1000) ? gba_read8(g, a) : gba_read32(g, a);
            return 1;
        }
        if (op & 0x1000) {
            gba_write8(g, a, c->r[rd]);
        } else {
            gba_write32(g, a, c->r[rd]);
        }
        return 0;
    }
    if ((op & 0xf000) == 0x8000) {
        a = c->r[rs] + ((op >> 6) & 31) * 2;
        if (op & 0x800) {
            c->r[rd] = gba_read16(g, a);
            return 1;
        }
        gba_write16(g, a, c->r[rd]);
        return 0;
    }
    if ((op & 0xf000) == 0x9000) {
        rd = (op >> 8) & 7;
        a = c->r[13] + (op & 0xff) * 4;
        if (op & 0x800) {
            c->r[rd] = gba_read32(g, a);
            return 1;
        }
        gba_write32(g, a, c->r[rd]);
        return 0;
    }
    if ((op & 0xf000) == 0xa000) {
        c->r[(op >> 8) & 7] = ((op & 0x800) ? c->r[13] : (pc + 4) & ~3u) + (op & 0xff) * 4;
        return 0;
    }
    if ((op & 0xff00) == 0xb000) {
        a = (op & 0x7f) * 4;
        c->r[13] = (op & 0x80) ? c->r[13] - a : c->r[13] + a;
        return 0;
    }
    if ((op & 0xf600) == 0xb400) {
        uint32_t list = op & 0xff;
        if (op & 0x100) {
            list |= (op & 0x800) ? 0x8000 : 0x4000;
        }
        unsigned count = gba_popcount(list), sequential = 0;
        a = (op & 0x800) ? c->r[13] : c->r[13] - count * 4;
        uint32_t start = a;
        for (unsigned r = 0; r < 16; r++) {
            if (!(list & (1u << r))) {
                continue;
            }
            if (op & 0x800) {
                result = gba_bus_read(g, a, 4, sequential, 0);
                if (r == 15) {
                    gba_branch(g, result);
                } else {
                    c->r[r] = result;
                }
            } else {
                gba_bus_write(g, a, c->r[r], 4, sequential);
            }
            a += 4;
            sequential = 1;
        }
        c->r[13] = (op & 0x800) ? a : start;
        return (op & 0x800) ? 1 : 0;
    }
    if ((op & 0xf000) == 0xc000) {
        unsigned base_reg = (op >> 8) & 7, sequential = 0;
        uint32_t list = op & 0xff, count = gba_popcount(list);
        a = c->r[base_reg];
        if (!count) {
            if (op & 0x800) {
                gba_branch(g, gba_bus_read(g, a, 4, 0, 0));
            } else {
                gba_bus_write(g, a, pc + 4, 4, 0);
            }
            c->r[base_reg] = a + 64;
            return (op & 0x800) ? 1 : 0;
        }
        uint32_t final = a + count * 4;
        for (unsigned r = 0; r < 8; r++) {
            if (!(list & (1u << r))) {
                continue;
            }
            if (op & 0x800) {
                c->r[r] = gba_bus_read(g, a, 4, sequential, 0);
            } else {
                gba_bus_write(g, a, r == base_reg && sequential ? final : c->r[r], 4, sequential);
            }
            a += 4;
            sequential = 1;
        }
        if (!(op & 0x800) || !(list & (1u << base_reg))) {
            c->r[base_reg] = final;
        }
        return (op & 0x800) ? 1 : 0;
    }
    if ((op & 0xf000) == 0xd000) {
        unsigned condition = (op >> 8) & 15;
        if (condition == 15) {
            if (!g->bios && GBA_BIOS_HLE) {
                return (uint32_t)gba_bios_swi(g, op & 0xff);
            }
            gba_cpu_exception(g, 0x13, 8, pc + 2);
        } else if (condition == 14) {
            gba_cpu_exception(g, 0x1b, 4, pc + 2);
        } else if (gba_condition(c->cpsr, condition)) {
            gba_branch(g, pc + 4 + (uint32_t)(gba_sign(op & 0xff, 8) * 2));
        }
        return 0;
    }
    if ((op & 0xf800) == 0xe000) {
        gba_branch(g, pc + 4 + (uint32_t)(gba_sign(op & 0x7ff, 11) * 2));
        return 0;
    }
    if ((op & 0xf800) == 0xf000) {
        c->r[14] = pc + 4 + (uint32_t)(gba_sign(op & 0x7ff, 11) * 4096);
        return 0;
    }
    if ((op & 0xf800) == 0xf800) {
        a = c->r[14] + (op & 0x7ff) * 2;
        c->r[14] = (pc + 2) | 1;
        gba_branch(g, a);
        return 0;
    }
    gba_cpu_exception(g, 0x1b, 4, pc + 2);
    return 0;
}
static void gba_cpu_irq(gba_t *g) {
    gba_cpu_t *c = &g->machine.cpu;
    gba_cpu_exception(g, 0x12, 0x18, c->r[15] + 4);
    if (!g->bios && GBA_BIOS_HLE) {
        if (gba_le32(g->memory.iwram + 0x7ffc)) {
            c->hle_irq++;
        } else {
            gba_fail(g, GBA_ERROR_UNSUPPORTED);
        }
    }
}
uint32_t gba_cpu_execute(gba_t *g) {
    gba_cpu_t *c = &g->machine.cpu;
    if (g->machine.dma_bus_valid && c->r[15] != g->machine.dma_pc) {
        g->machine.dma_bus_valid = 0;
    }
    gba_irq(g, 0);
    uint32_t pending = gba_io16(g, 0x200) & gba_io16(g, 0x202);
    int irq_ready = pending && !c->irq_delay && gba_io16(g, 0x208) && !(c->cpsr & 0x80);
    if (c->wait_mask && !c->hle_irq) {
        uint16_t bios_flags = gba_le16(g->memory.iwram + 0x7ff8);
        if (bios_flags & c->wait_mask) {
            gba_put16(g->memory.iwram + 0x7ff8, (uint16_t)(bios_flags & ~c->wait_mask));
            c->wait_mask = 0;
            if (!g->bios && GBA_BIOS_HLE) {
                g->machine.bios_latch = 0xe3a02004;
            }
        } else if (!irq_ready) {
            return gba_next_event(g);
        }
    }
    if (irq_ready) {
        gba_cpu_irq(g);
        /* The two vector refill accesses are charged by the next execution;
         * this discarded fetch completes the exception's 2S+1N sequence. */
        return g->access_cycles + 1;
    }
    uint32_t pc = c->r[15], width = (c->cpsr & GBA_T) ? 2 : 4;
    if (!c->pipeline_valid) {
        c->pipeline[0] = gba_bus_read(g, pc, width, 0, 1);
        c->pipeline[1] = gba_bus_read(g, pc + width, width, 1, 1);
        c->pipeline_valid = 1;
        c->fetch_sequential = 1;
    }
    uint32_t op = c->pipeline[0];
    c->pipeline[0] = c->pipeline[1];
    unsigned sequential = c->fetch_sequential;
    c->fetch_sequential = 1;
    c->pipeline[1] = gba_bus_read(g, pc + 2 * width, width, sequential, 1);
    if ((gba_io16(g, 0x200) & gba_io16(g, 0x202)) && !c->irq_delay && gba_io16(g, 0x208) && !(c->cpsr & 0x80)) {
        /* The request is sampled at the fetch boundary. The fetched opcode
         * is discarded; vector refill is charged by the next execution. */
        gba_cpu_irq(g);
        return g->access_cycles;
    }
    c->r[15] = pc + width;
#if GBA_TEST_MODE
    gba_test_instruction(g, pc, op);
#endif
    uint32_t internal = width == 2 ? gba_thumb(g, (uint16_t)op, pc) : gba_arm(g, op, pc);
    /* The Game Pak prefetch-disable bug turns the fetch following an
     * internal cycle into a first access, even for register-only shifts
     * and multiplication. Keep this separate from pipeline invalidation. */
    if (internal && !(gba_io16(g, 0x204) & 0x4000)) {
        c->fetch_sequential = 0;
    }
    if (c->hle_irq && pc == 0x13c && op == 0xe25ef004) {
        c->hle_irq--;
        g->machine.bios_latch = 0xe55ec002;
    }
    gba_prefetch_idle(g, internal);
    return g->access_cycles + internal;
}
