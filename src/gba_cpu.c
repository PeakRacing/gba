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
#include "gba_dynarec_hot.h"
#include <string.h>

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
    gba_wait_invalidate(g);
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
    uint32_t sum = a + b, result = sum + carry;
    if (flags) {
        gba_nz(c, result);
        gba_carry(c, sum < a || result < sum);
        c->cpsr = (c->cpsr & ~GBA_V) | (((~(a ^ b) & (a ^ result)) >> 3) & GBA_V);
    }
    return result;
}
static uint32_t gba_sub(gba_cpu_t *c, uint32_t a, uint32_t b, unsigned borrow, int flags) {
    uint32_t result = a - b - borrow;
    if (flags) {
        gba_nz(c, result);
        gba_carry(c, a > b || (a == b && !borrow));
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
static uint32_t gba_arm_memory(gba_t *g, uint32_t op, uint32_t pc) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
    unsigned flags = (op >> 20) & 1;

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

static uint32_t gba_thumb_memory(gba_t *g, uint32_t op, uint32_t pc) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned rd = op & 7, rs = (op >> 3) & 7;
    uint32_t a;
    switch (op >> 11) {
    case 9:
        goto literal;
    case 10:
    case 11:
        goto regmem;
    case 12:
    case 13:
    case 14:
    case 15:
        goto mem;
    case 16:
    case 17:
        goto half;
    default:
        goto spmem;
    }
literal:
    {
        c->r[(op >> 8) & 7] = gba_read32(g, ((pc + 4) & ~3u) + (op & 0xff) * 4);
        return 1;
    }
regmem:
    {
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
mem:
    {
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
half:
    {
        a = c->r[rs] + ((op >> 6) & 31) * 2;
        if (op & 0x800) {
            c->r[rd] = gba_read16(g, a);
            return 1;
        }
        gba_write16(g, a, c->r[rd]);
        return 0;
    }
spmem:
    {
        rd = (op >> 8) & 7;
        a = c->r[13] + (op & 0xff) * 4;
        if (op & 0x800) {
            c->r[rd] = gba_read32(g, a);
            return 1;
        }
        gba_write32(g, a, c->r[rd]);
        return 0;
    }
}

#if GBA_DYNAREC
uint32_t gba_cpu_arm_memory(gba_t *g, uint32_t op, uint32_t pc) {
    if ((op >> 28) != 14 && !gba_condition(g->machine.cpu.cpsr, op >> 28))
        return 0;
    return gba_arm_memory(g, op, pc);
}
uint32_t gba_cpu_thumb_memory(gba_t *g, uint32_t op, uint32_t pc) {
    return gba_thumb_memory(g, op, pc);
}
#endif

static uint32_t gba_arm(gba_t *g, uint32_t op, uint32_t pc) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
    unsigned flags = (op >> 20) & 1, extra = 0;
    if ((op >> 28) != 14 && !gba_condition(c->cpsr, op >> 28)) {
        return 0;
    }
    switch ((op >> 25) & 7) {
    case 0:
        /* Ordinary register ALU encodings cannot be multiply/halfword or
         * control-register overlays. Bypass their decode tests on this path. */
        if ((op & 0x90) != 0x90 && (op & 0x01900000) != 0x01000000)
            goto arm_alu;
        break;
    case 1:
        if ((op & 0x0db0f000) == 0x0120f000)
            goto arm_psr;
        goto arm_alu;
    case 2:
    case 3:
        return gba_arm_memory(g, op, pc);
    case 4:
        goto arm_multiple;
    case 5:
        goto arm_branch;
    case 7:
        if ((op & 0x0f000000) == 0x0f000000)
            goto arm_swi;
        /* fall through */
    default:
        goto arm_undefined;
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
    arm_psr:;
        gba_wait_invalidate(g);
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
arm_alu:
    {
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
            b = gba_reg(c, rm, pc, reg_shift ? 12 : 8);
            if (amount || ((op >> 5) & 3))
                b = gba_shift(b, (op >> 5) & 3, amount, &carry, !reg_shift);
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

arm_multiple:
    {
        if (op & 0x400000)
            gba_wait_invalidate(g); /* Banked registers are outside the loop proof. */
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
arm_branch:
    {
        if (op & 0x1000000) {
            c->r[14] = pc + 4;
        }
        gba_branch(g, pc + 8 + (uint32_t)(gba_sign(op & 0xffffff, 24) * 4));
        return 0;
    }
arm_swi:
    {
        if (!g->bios && GBA_BIOS_HLE) {
            return (uint32_t)gba_bios_swi(g, (op >> 16) & 0xff);
        }
        gba_cpu_exception(g, 0x13, 8, pc + 4);
        return 0;
    }
arm_undefined:
    gba_cpu_exception(g, 0x1b, 4, pc + 4);
    return 0;
}
static uint32_t gba_thumb(gba_t *g, uint16_t op, uint32_t pc) {
    gba_cpu_t *c = &g->machine.cpu;
    unsigned rd = op & 7, rs = (op >> 3) & 7, extra = 0;
    uint32_t a, b, result;
    /* A standard C switch dispatches directly to opcode classes. No computed
     * goto, executable cache or architecture-specific instruction is required. */
    switch (op >> 11) {
    case 0:
    case 1:
    case 2:
    case 3:
        goto thumb_shift;
    case 4:
    case 5:
    case 6:
    case 7:
        goto thumb_imm;
    case 8:
        if (op & 0x400)
            goto thumb_hi;
        goto thumb_alu;
    case 9:
        return gba_thumb_memory(g, op, pc);
    case 10:
    case 11:
        return gba_thumb_memory(g, op, pc);
    case 12:
    case 13:
    case 14:
    case 15:
        return gba_thumb_memory(g, op, pc);
    case 16:
    case 17:
        return gba_thumb_memory(g, op, pc);
    case 18:
    case 19:
        return gba_thumb_memory(g, op, pc);
    case 20:
    case 21:
        goto thumb_address;
    case 22:
    case 23:
        if ((op & 0xff00) == 0xb000)
            goto thumb_spadd;
        if ((op & 0xf600) == 0xb400)
            goto thumb_push;
        goto thumb_undefined;
    case 24:
    case 25:
        goto thumb_multiple;
    case 26:
    case 27:
        goto thumb_cond;
    case 28:
        goto thumb_branch;
    case 30:
        goto thumb_blhi;
    case 31:
        goto thumb_bllo;
    default:
        goto thumb_undefined;
    }
thumb_shift:
    {
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
thumb_imm:
    {
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
thumb_alu:
    {
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
thumb_hi:
    {
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

thumb_address:
    {
        c->r[(op >> 8) & 7] = ((op & 0x800) ? c->r[13] : (pc + 4) & ~3u) + (op & 0xff) * 4;
        return 0;
    }
thumb_spadd:
    {
        a = (op & 0x7f) * 4;
        c->r[13] = (op & 0x80) ? c->r[13] - a : c->r[13] + a;
        return 0;
    }
thumb_push:
    {
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
thumb_multiple:
    {
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
thumb_cond:
    {
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
thumb_branch:
    {
        gba_branch(g, pc + 4 + (uint32_t)(gba_sign(op & 0x7ff, 11) * 2));
        return 0;
    }
thumb_blhi:
    {
        c->r[14] = pc + 4 + (uint32_t)(gba_sign(op & 0x7ff, 11) * 4096);
        return 0;
    }
thumb_bllo:
    {
        a = c->r[14] + (op & 0x7ff) * 2;
        c->r[14] = (pc + 2) | 1;
        gba_branch(g, a);
        return 0;
    }
thumb_undefined:
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
GBA_INLINE uint32_t gba_cpu_fetch(gba_t *g, uint32_t address, unsigned width, int sequential) {
#if GBA_FAST_CPU
    uint32_t a = address & ~(uint32_t)(width - 1);
    if (g->fetch_page && g->fetch_page_tag == (a & ~0xfffu)) {
        unsigned region = a >> 24, cost = 1;
        if (region == 2)
            cost = width == 4 ? 6 : 3;
        if (region >= 8) {
            static const unsigned first[4] = {4, 3, 2, 8};
            uint16_t wait = gba_io16(g, 0x204);
            unsigned area = (region - 8) / 2, seq = gba_gamepak_seq(wait, area);
            if (!(a & 0x1ffffu))
                sequential = 0;
            cost = sequential ? seq : first[(wait >> (2 + area * 3)) & 3] + 1;
            if (width == 4)
                cost += seq;
            if ((wait & 0x4000) && sequential) {
                unsigned units = width == 4 ? 2 : 1;
                unsigned used = g->machine.prefetch_count < units ? g->machine.prefetch_count : units;
                g->machine.prefetch_count -= used;
                if (used == units) {
                    cost = 1;
                    gba_prefetch_idle(g, 1);
                } else {
                    uint32_t phase = g->machine.prefetch_phase;
                    if (phase >= seq)
                        phase -= gba_seq_div(phase, seq) * seq;
                    cost = (units - used) * seq - phase;
                    g->machine.prefetch_phase = 0;
                }
            } else if (!sequential)
                g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        } else if (g->stepping)
            gba_prefetch_idle(g, cost);
        g->access_cycles += cost;
        if (g->stepping) {
            if (g->cpu_bulk)
                gba_cpu_charge(g, cost);
            else
                gba_advance(g, cost);
        }
        const uint8_t *p = g->fetch_page + (a & 0xfff);
        uint32_t value = width == 2 ? gba_le16(p) : gba_le32(p);
        g->machine.last_fetch = a;
        g->machine.open_bus = width == 2 ? value | (value << 16) : value;
        gba_dma_drain(g);
        return value;
    }
    return gba_bus_fetch(g, address, width, sequential);
#else
    return gba_bus_read(g, address, width, sequential, 1);
#endif
}
#if GBA_DYNAREC && defined(__GNUC__)
static __attribute__((noinline)) uint32_t gba_cpu_execute_one(gba_t *g) {
#else
GBA_INLINE uint32_t gba_cpu_execute_one(gba_t *g) {
#endif
    gba_cpu_t *c = &g->machine.cpu;
    if (g->machine.dma_bus_valid && c->r[15] != g->machine.dma_pc) {
        g->machine.dma_bus_valid = 0;
    }
#if !GBA_FAST_RUN
    gba_irq(g, 0);
#endif
    uint32_t pending = gba_irq_pending(g);
    int irq_ready = pending && !c->irq_delay && gba_io16(g, 0x208) && !(c->cpsr & 0x80);
    if (c->wait_mask && !c->hle_irq) {
#if GBA_FAST_CPU
        if (g->cpu_bulk)
            gba_cpu_sync(g);
#endif
        uint16_t bios_flags = gba_le16(g->memory.iwram + 0x7ff8);
        if (bios_flags & c->wait_mask) {
            gba_put16(g->memory.iwram + 0x7ff8, (uint16_t)(bios_flags & ~c->wait_mask));
#if GBA_DYNAREC
            gba_dynarec_invalidate(g, 0x03007ff8u, 2);
#endif
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
        c->pipeline[0] = gba_cpu_fetch(g, pc, width, 0);
        c->pipeline[1] = gba_cpu_fetch(g, pc + width, width, 1);
        c->pipeline_valid = 1;
        c->fetch_sequential = 1;
    }
    uint32_t op = c->pipeline[0];
    c->pipeline[0] = c->pipeline[1];
    unsigned sequential = c->fetch_sequential;
    c->fetch_sequential = 1;
    c->pipeline[1] = gba_cpu_fetch(g, pc + 2 * width, width, sequential);
    if (gba_irq_pending(g) && !c->irq_delay && gba_io16(g, 0x208) && !(c->cpsr & 0x80)) {
        /* The request is sampled at the fetch boundary. The fetched opcode
         * is discarded; vector refill is charged by the next execution. */
        gba_cpu_irq(g);
        return g->access_cycles;
    }
    c->r[15] = pc + width;
#if GBA_TEST_MODE
    gba_test_instruction(g, pc, op);
#endif
    if (g->fault) {
        return g->access_cycles;
    }
    /* Native work runs only through amortized blocks. A scalar fallback must
     * not decode once in the JIT and a second time in the C executor. */
#if GBA_DYNAREC && GBA_DYNAREC_STATS
    if (g->cpu_bulk) gba_dynarec_fallback(g, 1);
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
uint32_t gba_cpu_execute(gba_t *g) {
    return gba_cpu_execute_one(g);
}
#if GBA_FAST_WAIT && !GBA_TEST_MODE
#if !GBA_FAST_CPU
#error GBA_FAST_WAIT requires the portable batched CPU executor
#endif
static int gba_wait_equal(const gba_t *g) {
    const gba_cpu_t *c = &g->machine.cpu;
    for (unsigned r = 0; r < 16; r++)
        if (c->r[r] != g->wait.r[r]) {
            GBA_WAIT_DIAG(r);
            return 0;
        }
#if defined(GBA_WAIT_DIAG_ACTIVE)
    if(c->cpsr!=g->wait.cpsr) {GBA_WAIT_DIAG(16);return 0;}
    if(c->pipeline[0]!=g->wait.pipeline[0] || c->pipeline[1]!=g->wait.pipeline[1] ||
       c->pipeline_valid!=g->wait.pipeline_valid || c->fetch_sequential!=g->wait.fetch_sequential) {GBA_WAIT_DIAG(17);return 0;}
    if(g->machine.prefetch_count!=g->wait.prefetch_count || g->machine.prefetch_phase!=g->wait.prefetch_phase) {GBA_WAIT_DIAG(18);return 0;}
    if(g->machine.open_bus!=g->wait.open_bus || g->machine.last_fetch!=g->wait.last_fetch) {GBA_WAIT_DIAG(19);return 0;}
    if(g->machine.dma_pc!=g->wait.dma_pc || g->machine.dma_bus!=g->wait.dma_bus ||
       g->machine.dma_bus_valid!=g->wait.dma_bus_valid) {GBA_WAIT_DIAG(20);return 0;}
    if(g->machine.bios_latch!=g->wait.bios_latch) {GBA_WAIT_DIAG(21);return 0;}
    GBA_WAIT_DIAG(22);
#endif
    return c->cpsr == g->wait.cpsr && c->pipeline[0] == g->wait.pipeline[0] && c->pipeline[1] == g->wait.pipeline[1] &&
           c->pipeline_valid == g->wait.pipeline_valid && c->fetch_sequential == g->wait.fetch_sequential &&
           g->machine.prefetch_count == g->wait.prefetch_count && g->machine.prefetch_phase == g->wait.prefetch_phase &&
           g->machine.open_bus == g->wait.open_bus && g->machine.last_fetch == g->wait.last_fetch &&
           g->machine.dma_pc == g->wait.dma_pc && g->machine.dma_bus == g->wait.dma_bus &&
           g->machine.dma_bus_valid == g->wait.dma_bus_valid && g->machine.bios_latch == g->wait.bios_latch;
}
static void gba_wait_capture(gba_t *g, uint32_t clock) {
    const gba_cpu_t *c = &g->machine.cpu;
    for (unsigned r = 0; r < 16; r++)
        g->wait.r[r] = c->r[r];
    g->wait.cpsr = c->cpsr;
    g->wait.pc = c->r[15];
    g->wait.clock = clock;
    g->wait.valid = 1;
    g->wait.pipeline[0] = c->pipeline[0];
    g->wait.pipeline[1] = c->pipeline[1];
    g->wait.pipeline_valid = c->pipeline_valid;
    g->wait.fetch_sequential = c->fetch_sequential;
    g->wait.prefetch_count = g->machine.prefetch_count;
    g->wait.prefetch_phase = g->machine.prefetch_phase;
    g->wait.open_bus = g->machine.open_bus;
    g->wait.last_fetch = g->machine.last_fetch;
    g->wait.dma_pc = g->machine.dma_pc;
    g->wait.dma_bus = g->machine.dma_bus;
    g->wait.dma_bus_valid = g->machine.dma_bus_valid;
    g->wait.bios_latch = g->machine.bios_latch;
    g->wait.previous_read_hash = g->wait.read_hash;
    g->wait.read_hash = 2166136261u;
    g->wait.timer_read = 0;
    g->wait.ppu_read = g->wait.period = 0;
}
#if GBA_DYNAREC && defined(__GNUC__)
__attribute__((noinline))
#endif
static uint32_t gba_wait_repeat(gba_t *g, uint32_t before_pc, uint32_t budget) {
    gba_cpu_t *c = &g->machine.cpu;
    uint32_t skipped = 0, clock = g->machine.cycles + g->cpu_pending;
    if (g->wait.frame != g->machine.frames) {
        g->wait.frame = g->machine.frames;
        g->wait.reject_pc = 0;
        g->wait.valid = 0;
    }
    if (c->wait_mask || c->hle_irq || c->halted || c->stopped || c->irq_delay || g->fault ||
        (c->irq_line && gba_io16(g, 0x208) && !(c->cpsr & 0x80))) {
        g->wait.valid = 0;
        return 0;
    }
    if (g->wait.valid && c->r[15] == g->wait.pc) {
        uint32_t period = clock - g->wait.clock;
        if (period && period <= 4096 && gba_wait_equal(g)) {
            uint32_t distance = g->event_cycles ? g->event_cycles : gba_next_cpu_event(g);
            if (distance > g->cpu_pending)
                distance -= g->cpu_pending;
            else
                distance = 1;
            uint32_t limit = distance < budget ? distance : budget;
            if (g->wait.timer_read) {
                uint32_t stable = g->wait.stable_until - clock;
                if (!stable || stable > 1024)
                    limit = 0;
                else if (stable < limit)
                    limit = stable;
            }
            uint32_t loops = limit > 1 ? (limit - 1) / period : 0;
            unsigned ppu_read=g->wait.ppu_read, timer_read=g->wait.timer_read;
            skipped = loops * period;
            if (skipped) {
                GBA_WAIT_DIAG(23);
                gba_cpu_charge(g, skipped);
                g->wait.skipped_cycles += skipped;
                g->wait.skipped_loops += loops;
            }
            g->wait.retries = 0;
            gba_wait_capture(g,clock+skipped);
            g->wait.ppu_read=ppu_read;g->wait.timer_read=timer_read;
            g->wait.period=period;
            return skipped;
        } else if (g->wait.read_hash == g->wait.previous_read_hash) {
            if (++g->wait.retries >= 3) {
                GBA_WAIT_DIAG(24);
                g->wait.reject_pc = g->wait.pc;
                g->wait.valid = 0;
                return 0;
            }
        } else
            g->wait.retries = 0;
        gba_wait_capture(g, clock + skipped);
    }
    if (!g->wait.valid && c->r[15] != g->wait.reject_pc && c->r[15] <= before_pc) {
        g->wait.retries = 0;
        g->wait.read_hash = 2166136261u;
        gba_wait_capture(g, clock + skipped);
    }
    return skipped;
}
#endif
#if GBA_FAST_CPU
#if GBA_FAST_ALU && !GBA_TEST_MODE
/* Register-only resident ROM/IWRAM spans cannot change devices or code.
 * Keep ARM/Thumb operations and actual fetch/FIFO costs, and commit time at
 * the bounded span end. No instruction or guest cycle is omitted. */
static int gba_arm_register_only(uint32_t op) {
    if ((op & 0x0c000000u) != 0)
        return 0;
    if (!(op & 0x02000000u) && (op & 0x90) == 0x90) {
        if ((op & 0x0f8000f0) == 0x00800090)
            return ((op >> 16) & 15) != 15 && ((op >> 12) & 15) != 15;
        if ((op & 0x0fc000f0) == 0x00000090)
            return ((op >> 16) & 15) != 15;
        return 0;
    }
    if ((op & 0x0db0f000) == 0x0120f000 || (!(op & 0x02000000u) && (op & 0x01900000) == 0x01000000))
        return 0;
    return ((op >> 12) & 15) != 15;
}
static int gba_thumb_register_only(uint16_t op) {
    if (op < 0x4000 || (op & 0xfc00) == 0x4000)
        return 1;
    if ((op & 0xfc00) == 0x4400) {
        unsigned kind = (op >> 8) & 3, rd = (op & 7) | ((op >> 4) & 8);
        return kind != 3 && (kind == 1 || rd != 15);
    }
    return (op & 0xf000) == 0xa000 || (op & 0xff00) == 0xb000;
}
#if GBA_FAST_BLOCKS
static void gba_register_decode(gba_register_instruction_t *d, uint32_t op) {
    d->condition = (uint8_t)(op >> 28);
    d->rn = (uint8_t)((op >> 16) & 15);
    d->rd = (uint8_t)((op >> 12) & 15);
    d->rm = (uint8_t)(op & 15);
    d->rs = (uint8_t)((op >> 8) & 15);
    d->flags = (uint8_t)((op >> 20) & 1);
    if (!(op & 0x02000000u) && (op & 0x90) == 0x90) {
        d->kind = (op & 0x800000) ? 17 : 16;
        d->flags |= (uint8_t)(((op >> 21) & 3) << 1);
        return;
    }
    d->kind = (uint8_t)((op >> 21) & 15);
    d->shift = (uint8_t)((op >> 5) & 3);
    d->amount = (uint8_t)((op >> 7) & 31);
    if (op & 0x02000000u) {
        unsigned rotate = ((op >> 8) & 15) * 2;
        d->immediate = gba_ror(op & 255, rotate);
        d->flags |= (uint8_t)(2 | (rotate ? 8 : 0));
    } else if (op & 0x10)
        d->flags |= 4;
}
static uint32_t gba_register_compute(gba_cpu_t *c, const gba_register_instruction_t *d, uint32_t pc) {
    if (d->condition != 14 && !gba_condition(c->cpsr, d->condition))
        return 0;
    if (d->kind >= 16) {
        uint32_t a = gba_reg(c, d->rm, pc, 4), b = gba_reg(c, d->rs, pc, 4);
        if (d->kind == 17) {
            uint64_t result = (d->flags & 4) ? (uint64_t)((int64_t)gba_sign(a, 32) * gba_sign(b, 32)) : (uint64_t)a * b;
            if (d->flags & 2)
                result += ((uint64_t)c->r[d->rn] << 32) | c->r[d->rd];
            c->r[d->rd] = (uint32_t)result;
            c->r[d->rn] = (uint32_t)(result >> 32);
            if (d->flags & 1)
                c->cpsr = (c->cpsr & ~(GBA_N | GBA_Z)) | (c->r[d->rn] & GBA_N) | (!result ? GBA_Z : 0);
            return gba_multiply_cycles(b, (d->flags & 4) != 0) + 1 + ((d->flags >> 1) & 1);
        }
        uint32_t result = a * b;
        unsigned extra = gba_multiply_cycles(b, 1);
        if (d->flags & 2) {
            result += gba_reg(c, d->rd, pc, 4);
            extra++;
        }
        c->r[d->rn] = result;
        if (d->flags & 1)
            gba_nz(c, result);
        return extra;
    }
    unsigned operation = d->kind, reg_shift = d->flags & 4, carry = (c->cpsr >> 29) & 1;
    uint32_t a = gba_reg(c, d->rn, pc, reg_shift ? 12 : 8), b, result = 0;
    if (d->flags & 2) {
        b = d->immediate;
        if (d->flags & 8)
            carry = b >> 31;
    } else {
        unsigned amount = reg_shift ? gba_reg(c, d->rs, pc, 8) & 255 : d->amount;
        b = gba_reg(c, d->rm, pc, reg_shift ? 12 : 8);
        if (amount || d->shift)
            b = gba_shift(b, d->shift, amount, &carry, !reg_shift);
    }
    unsigned test = operation >= 8 && operation <= 11, set = (d->flags & 1) || test;
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
        result = gba_sub(c, a, b, 0, set);
        break;
    case 3:
        result = gba_sub(c, b, a, 0, set);
        break;
    case 4:
    case 11:
        result = gba_add(c, a, b, 0, set);
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
    default:
        result = ~b;
        break;
    }
    if (set && (operation < 2 || operation == 8 || operation == 9 || operation >= 12)) {
        gba_nz(c, result);
        gba_carry(c, carry);
    }
    if (!test)
        c->r[d->rd] = result;
    return reg_shift ? 1 : 0;
}
static uint32_t gba_cpu_register_block(gba_t *g, uint32_t budget) {
    gba_cpu_t *c = &g->machine.cpu;
    uint32_t pc = c->r[15], offset = pc & 0x7fff;
    /* No device/code writes or hardware event can occur within this block.
     * Keep mirrored ends and artificially unaligned PCs on the live executor. */
    if ((c->cpsr & GBA_T) || (pc & 3) || (pc >> 24) != 3 || (g->machine.last_fetch >> 24) != 3 || offset > 0x7fb8 ||
        pc > 0x03ffffb8u || budget < 8)
        return 0;
    gba_register_block_t *b = &g->register_block[(pc >> 2) & 63];
    const uint8_t *bytes = g->memory.iwram + offset + 8;
    if (!b->count || b->pc != pc || b->head[0] != c->pipeline[0] || b->head[1] != c->pipeline[1] ||
        memcmp(b->bytes, bytes, b->count * 4)) {
        b->pc = pc;
        b->head[0] = c->pipeline[0];
        b->head[1] = c->pipeline[1];
        b->count = 0;
        for (unsigned i = 0; i < 16; i++) {
            uint32_t op = i < 2 ? c->pipeline[i] : gba_le32(bytes + (i - 2) * 4);
            if (!gba_arm_register_only(op))
                break;
            gba_register_decode(&b->instruction[i], op);
            b->count++;
        }
        memcpy(b->bytes, bytes, b->count * 4);
    }
    unsigned count = 0, internal = 0;
    uint32_t total = 0;
    while (count < b->count && budget - total >= 8) {
        internal = gba_register_compute(c, &b->instruction[count], pc + count * 4);
        total += 1 + internal;
        count++;
    }
    if (!count)
        return 0;
    if (g->machine.dma_bus_valid && (count > 1 || pc != g->machine.dma_pc))
        g->machine.dma_bus_valid = 0;
    c->pipeline[0] = count == 1 ? b->head[1] : gba_le32(b->bytes + (count - 2) * 4);
    c->pipeline[1] = gba_le32(b->bytes + (count - 1) * 4);
    c->r[15] = pc + count * 4;
    c->fetch_sequential = !(internal && !(gba_io16(g, 0x204) & 0x4000));
    g->machine.last_fetch = pc + (count + 1) * 4;
    g->machine.open_bus = c->pipeline[1];
    g->access_cycles = 1;
    gba_cpu_charge(g, total);
    return total;
}
#endif
#if GBA_DYNAREC && defined(__GNUC__)
__attribute__((noinline))
#endif
static uint32_t gba_cpu_register_slice(gba_t *g, uint32_t budget) {
    gba_cpu_t *c = &g->machine.cpu;
    if (!g->stepping || !c->pipeline_valid || gba_dma_ready_mask(g) || (c->wait_mask && !c->hle_irq) ||
        (c->irq_line && !c->irq_delay && gba_io16(g, 0x208) && !(c->cpsr & 0x80)))
        return 0;
    if (!g->event_cycles)
        g->event_cycles = gba_next_event(g);
    uint32_t distance = g->event_cycles > g->cpu_pending ? g->event_cycles - g->cpu_pending : 0;
    if (distance < budget)
        budget = distance;
#if GBA_FAST_BLOCKS
    uint32_t block_cycles = gba_cpu_register_block(g, budget);
    if (block_cycles)
        return block_cycles;
#endif
    uint32_t total = 0, count = 0;
    unsigned width = (c->cpsr & GBA_T) ? 2 : 4;
    uint16_t wait = gba_io16(g, 0x204);
    unsigned prefetch = wait & 0x4000;
    while (count < 32) {
        uint32_t pc = c->r[15], op = c->pipeline[0];
        unsigned region = pc >> 24, maximum = 8;
        if (region == 3) {
            if ((g->machine.last_fetch >> 24) != 3 || pc > 0x03fffff4u)
                break;
        } else if (region >= 8 && region <= 12 && g->rom) {
            uint32_t offset = pc & 0x1ffffff;
            if (offset >= g->rom_size || width * 3 > g->rom_size - offset || ((pc + 2 * width) >> 24) > 12 ||
                (offset < 4096 && g->machine.cart.rtc_enabled))
                break;
            maximum = width == 2 ? 16 : 24;
        } else
            break;
        if (budget - total < maximum ||
            !(width == 2 ? gba_thumb_register_only((uint16_t)op) : gba_arm_register_only(op)))
            break;
        if (g->machine.dma_bus_valid && pc != g->machine.dma_pc)
            g->machine.dma_bus_valid = 0;
        c->pipeline[0] = c->pipeline[1];
        unsigned sequential = c->fetch_sequential;
        c->fetch_sequential = 1;
        uint32_t address = (pc + 2 * width) & ~(width - 1u), fetched, cost = 1;
        if (region == 3) {
            const uint8_t *p = g->memory.iwram + (address & 0x7fff);
            fetched = width == 2 ? gba_le16(p) : gba_le32(p);
        } else {
            static const unsigned first[4] = {4, 3, 2, 8};
            unsigned area = ((address >> 24) - 8) / 2, seq = gba_gamepak_seq(wait, area);
            if (!(address & 0x1ffff))
                sequential = 0;
            cost = sequential ? seq : first[(wait >> (2 + area * 3)) & 3] + 1;
            if (width == 4)
                cost += seq;
            if (prefetch && sequential) {
                unsigned units = width == 4 ? 2 : 1;
                unsigned used = g->machine.prefetch_count < units ? g->machine.prefetch_count : units;
                g->machine.prefetch_count -= used;
                if (used == units) {
                    cost = 1;
                    gba_prefetch_idle(g, 1);
                } else {
                    uint32_t phase = g->machine.prefetch_phase;
                    if (phase >= seq)
                        phase -= gba_seq_div(phase, seq) * seq;
                    cost = (units - used) * seq - phase;
                    g->machine.prefetch_phase = 0;
                }
            } else if (!sequential)
                g->machine.prefetch_count = g->machine.prefetch_phase = 0;
            const uint8_t *p = g->rom + (address & 0x1ffffff);
            fetched = width == 2 ? gba_le16(p) : gba_le32(p);
        }
        g->access_cycles = cost;
        g->machine.last_fetch = address;
        g->machine.open_bus = width == 2 ? fetched | (fetched << 16) : fetched;
        c->pipeline[1] = fetched;
        c->r[15] = pc + width;
        if (g->fault) {
            return g->access_cycles;
        }
        /* This is the bounded C fallback, never a nested single-instruction JIT. */
        uint32_t internal = width == 2 ? gba_thumb(g, (uint16_t)op, pc) : gba_arm(g, op, pc);
        if (internal && !prefetch)
            c->fetch_sequential = 0;
        gba_prefetch_idle(g, internal);
        total += g->access_cycles + internal;
        count++;
    }
    if (count) {
#if GBA_DYNAREC && GBA_DYNAREC_STATS
        gba_dynarec_fallback(g, count);
#endif
        gba_cpu_charge(g, total);
    }
    return total;
}
#endif
void gba_cpu_sync(gba_t *g) {
#if GBA_DYNAREC_HARD_CACHE
    if(gba_native_cache(g))gba_native_cache(g)->hard_valid=0;
#endif
    uint32_t cycles = g->cpu_pending, bulk = g->cpu_bulk;
    g->cpu_pending = 0;
    g->cpu_bulk = 0;
#if GBA_DYNAREC
    g->event_cycles = 0; /* CPU deadline may span several exact PCM events. */
#endif
    if (cycles)
        gba_advance(g, cycles);
    gba_flush_events(g);
    g->cpu_bulk = bulk;
#if GBA_DYNAREC
    g->event_cycles = 0;
#endif
}
GBA_INLINE unsigned gba_cpu_run_pending(const gba_t *g) {
#if GBA_DYNAREC
    const gba_cpu_t *c = &g->machine.cpu;
    unsigned irq = c->irq_line && !c->irq_delay && gba_io16(g, 0x208) && !(c->cpsr & 0x80);
#else
    unsigned irq = g->machine.cpu.irq_line;
#endif
    return irq || gba_dma_ready_mask(g);
}
#if GBA_DYNAREC && GBA_DYNAREC_CHAIN
/* Called from the shared A32 continuation stub, with guest cached registers
 * still live in host r8-r11. Do not inspect r0-r14 or replay devices here:
 * every span is bounded strictly before the next observable CPU event. */
uint64_t gba_native_chain(gba_t *g,uint32_t result,uint32_t status,uint32_t *frame) {
    gba_native_cache_t *cache=gba_native_cache(g);
    gba_native_block_t *b=(gba_native_block_t *)(uintptr_t)frame[7];
    gba_cpu_t *cpu=&g->machine.cpu;
    unsigned executed=status&255,memory=(status>>8)&255;
    (void)memory;
    frame[3]=b->regmap; /* old host-register mapping for the continuation stub */
    GBA_NATIVE_COUNT(cache,native_entries,1);
    GBA_NATIVE_COUNT(cache,zero_entries,!executed);
    GBA_NATIVE_COUNT(cache,guard_returns,status>>31);
    GBA_NATIVE_COUNT(cache,native_instructions,executed-memory);
    GBA_NATIVE_COUNT(cache,memory_instructions,memory);
    GBA_NATIVE_COUNT(cache,cache_hits,1);
#if GBA_DYNAREC_STATS
    unsigned bucket=0;while(bucket<7 && executed>(1u<<bucket))bucket++;
    cache->stats.entries_by_size[bucket]++;
#endif
    if(!executed) {
        /* A zero-prefix exit must not silently refill its next target.
         * Return to the scalar path with the unchanged pipeline state. */
#if GBA_DYNAREC_COARSE_CHAIN
        if((b->pc>>24)==3 || (b->pc>>24)>=8) {
            unsigned width=(b->flags&1)?2:4;
            cpu->r[15]=b->pc;cpu->pipeline[0]=b->head[0];cpu->pipeline[1]=b->head[1];
            cpu->pipeline_valid=cpu->fetch_sequential=1;
            g->machine.last_fetch=b->pc+width;
            g->machine.open_bus=width==2?b->head[1]*0x10001u:b->head[1];
            g->machine.prefetch_count=g->machine.prefetch_phase=g->machine.dma_bus_valid=0;
            g->access_cycles=(b->pc>>24)==3?1:b->prefix_count;
        }
#endif
        return 0;
    }
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    if(status&0x10000u) {
        if(g->wait.valid)g->wait.reject_pc=g->wait.pc;
        gba_wait_invalidate(g);
    }
#endif
    unsigned pc=b->pc,width=(b->flags&1)?2:4,count=b->count;
    unsigned source_rom=GBA_DYNAREC_ROM_CHAIN && (pc>>24)>=8;
    uint32_t fetch=executed;
#if GBA_DYNAREC_ROM_CHAIN
    if(source_rom) {
        gba_native_rom_state_t saved={cache->chain_rom_count,cache->chain_rom_phase,cache->chain_rom_sequential};
        fetch=gba_native_replay_rom(g,b,executed,&saved);
#if GBA_DYNAREC_COARSE_CHAIN
        g->access_cycles+=result>>24;
#endif
    }
#endif
    if(!source_rom && (cache->rom_data_seen&1))g->machine.prefetch_count=g->machine.prefetch_phase=0;
    if((b->flags&32) && (status>>31) && !(executed%count)) {
        cpu->pipeline_valid=cpu->fetch_sequential=0;cpu->r[15]=pc;
        g->machine.prefetch_count=g->machine.prefetch_phase=0;
    }
    unsigned final_count=(b->flags&32)?((executed-1)%count)+1:executed;
    const uint8_t *source=source_rom?g->rom+(pc&0x1ffffff):g->memory.iwram+(pc&0x7fff);
    cpu->pipeline[0]=width==2?gba_le16(source+final_count*width):gba_le32(source+final_count*width);
    cpu->pipeline[1]=width==2?gba_le16(source+(final_count+1)*width):gba_le32(source+(final_count+1)*width);
    if(cpu->pipeline_valid) {
        cpu->r[15]=pc+final_count*width;
        if(!source_rom)cpu->fetch_sequential=!(result&0x00800000u) &&
            !((result>>16) && !(gba_io16(g,0x204)&0x4000));
    }
    if(g->machine.dma_bus_valid && (executed>1 || pc!=g->machine.dma_pc))g->machine.dma_bus_valid=0;
    g->machine.last_fetch=pc+(final_count+1)*width;
    g->machine.open_bus=width==2?cpu->pipeline[1]*0x10001u:cpu->pipeline[1];
    if(!source_rom)g->access_cycles=1+(result>>24);
    uint32_t cycles=fetch+(result&0xffff);
#ifdef GBA_TEST_CHAIN_TRACE
    extern void gba_test_chain_trace(gba_t *,uint32_t,uint32_t,uint32_t,uint32_t);
    gba_test_chain_trace(g,pc,executed,fetch,result);
#endif
    /* Coarse chains can cross device deadlines. Spill cached guest registers
     * in the A32 exit stub before the outer CPU loop advances any devices. */
#if GBA_DYNAREC_COARSE_CHAIN
#if GBA_DYNAREC_LINK_CACHE
    /* Prime a missing successor through the full two-way metadata table.
     * Native hits still validate epoch/mode and live RAM before executing. */
    unsigned next_pc=cpu->r[15],next_width=(cpu->cpsr&GBA_T)?2:4,next_region=next_pc>>24;
    if(next_region==3 || (next_region>=8 && next_region<=12)) {
        unsigned key=(next_region==3?128u:GBA_DYNAREC_ROM_TRACE?240u:GBA_DYNAREC_WIDE_ROM?256u:64u)|(next_width==2);
        unsigned hash=(next_pc>>1)^(next_pc>>10)^(next_pc>>19)^(key*173u);hash^=hash>>8;
        gba_native_block_t *ways=gba_native_blocks(cache)+(hash&(cache->slots/2-1))*2;
        for(unsigned n=0;n<2;n++)if(ways[n].epoch==cache->epoch && ways[n].pc==next_pc && ways[n].cache_key==key &&
            ways[n].count>=(GBA_DYNAREC_TINY_BLOCKS?1:2)) {b->prefix_offset=(uint32_t)(uintptr_t)(ways+n);break;}
    }
#endif
    g->cpu_pending+=cycles;
    frame[4]-=cycles;frame[6]+=cycles;
    return 0;
#else
    gba_cpu_charge(g,cycles);
    frame[4]-=cycles;frame[6]+=cycles;
    if((status>>31) || g->gba_quit || g->fault || cpu->stopped || cpu->halted || gba_cpu_run_pending(g))return 0;
    pc=cpu->r[15];width=(cpu->cpsr&GBA_T)?2:4;
    unsigned target_rom=GBA_DYNAREC_ROM_CHAIN && (pc>>24)>=8 && (pc>>24)<=12 && g->rom;
    if(((pc>>24)!=3 && !target_rom) || (pc&(width-1)))return 0;
    b=cache->recent[gba_native_recent_index(pc)];
    if(!b || b->count<2 || b->pc!=pc || b->epoch!=cache->epoch ||
        b->cache_key!=((target_rom?64u:128u)|(width==2)) || (b->flags&3)!=(width==2?3u:2u) ||
        (target_rom ? !!(b->flags&32) : b->page_generation!=cache->generation[1024+((pc&0x7fff)>>8)]) ||
        (b->end_page<GBA_JIT_PAGES && b->end_generation!=cache->generation[b->end_page]))return 0;
    unsigned refill=cpu->pipeline_valid?0:target_rom?36:2;
    unsigned bound=target_rom?(cpu->fetch_sequential?b->fetch_bound_seq:b->fetch_bound_nonseq):b->count;
    if(frame[4]<=refill || bound+b->extra_bound>=frame[4]-refill)return 0;
    if(cpu->pipeline_valid) {
        if(b->head[0]!=cpu->pipeline[0] || b->head[1]!=cpu->pipeline[1])return 0;
    } else {
        g->access_cycles=0;
        cpu->pipeline[0]=gba_cpu_fetch(g,pc,width,0);
        cpu->pipeline[1]=gba_cpu_fetch(g,pc+width,width,1);
        cpu->pipeline_valid=cpu->fetch_sequential=1;
        frame[4]-=g->access_cycles;frame[6]+=g->access_cycles;
    }
    unsigned repetitions=1;
    if(b->flags&32) {
        repetitions=(frame[4]-1)/(b->count+b->extra_bound+b->memory_mask+2);
        if(!GBA_DYNAREC_LONG_LOOPS && repetitions>7)repetitions=7;
        if(repetitions>255/b->count)repetitions=255/b->count;
        if(!repetitions)repetitions=1;
    }
#if GBA_DYNAREC_ROM_CHAIN
    cache->chain_rom_count=g->machine.prefetch_count;cache->chain_rom_phase=g->machine.prefetch_phase;
    cache->chain_rom_sequential=cpu->fetch_sequential;
#endif
    cache->rom_data_seen=target_rom || repetitions*(b->count+b->extra_bound+b->memory_mask+2)-2<frame[4]?2:0;
    frame[0]=repetitions;frame[1]=0;frame[7]=(uint32_t)(uintptr_t)b;
    GBA_NATIVE_COUNT(cache,chain_links,1);
    uintptr_t entry=(uintptr_t)g->dynarec_memory.code+b->offset+(b->raw_words>>11)*4;
    return (uint64_t)entry|((uint64_t)b->regmap<<32);
#endif
}
#endif
uint32_t gba_cpu_run(gba_t *g, uint32_t budget) {
#if GBA_DYNAREC_HARD_CACHE
    if(gba_native_cache(g))gba_native_cache(g)->hard_valid=0;
#endif
    uint32_t total = 0, limit;
    gba_flush_events(g);
    limit = gba_next_cpu_event(g);
    g->event_cycles = limit;
    if (limit > budget)
        limit = budget;
    g->cpu_bulk = 1;
    while (total < limit && !g->fault && !g->gba_quit && !g->machine.cpu.stopped && !g->machine.cpu.halted) {
#if GBA_DYNAREC && GBA_FAST_WAIT && !GBA_TEST_MODE
        /* Reuse an exact write-free loop proof across unrelated device events.
         * RAM writes, relevant PPU/timer changes and public API boundaries
         * invalidate it; IRQ/DMA and the full CPU/fetch state are rechecked. */
        if(g->wait.valid && g->wait.period && !gba_cpu_run_pending(g) &&
            !g->machine.cpu.irq_delay && !g->machine.cpu.hle_irq &&
            !g->machine.cpu.wait_mask && g->machine.cpu.r[15]==g->wait.pc && gba_wait_equal(g)) {
            uint32_t span=limit-total, clock=g->machine.cycles+g->cpu_pending;
            if(g->wait.timer_read) {
                uint32_t stable=g->wait.stable_until-clock;
                if(!stable || stable>1024)span=0;
                else if(stable<span)span=stable;
            }
            uint32_t loops=span>1?(span-1)/g->wait.period:0;
            uint32_t skipped=loops*g->wait.period;
            if(skipped) {
                gba_cpu_charge(g,skipped);total+=skipped;
                g->wait.skipped_cycles+=skipped;g->wait.skipped_loops+=loops;
                g->wait.clock=clock+skipped;
            }
        }
#endif
#if GBA_DYNAREC
        uint32_t native_before_pc = g->machine.cpu.r[15];
        /* A branch target must not force its first instruction through C.
         * Fill the real two-stage pipeline first when no event can intervene;
         * then execute at least one instruction before returning to callers. */
        gba_cpu_t *native_cpu=&g->machine.cpu;
        unsigned native_region=native_before_pc>>24;
#if GBA_DYNAREC_BULK_PREFILL
        if(gba_native_cache(g))gba_native_cache(g)->prefill_cycles=0;
#endif
        if(!native_cpu->pipeline_valid && g->stepping &&
            (native_region==2 || native_region==3 ||
             (native_region>=8 && native_region<=12 && g->rom)) &&
            !native_cpu->irq_delay && !gba_cpu_run_pending(g) &&
            (!native_cpu->wait_mask || (GBA_DYNAREC_PERSISTENT_HLE_IRQ && native_cpu->hle_irq))) {
            unsigned width=(native_cpu->cpsr&GBA_T)?2:4;
            uint32_t offset=native_before_pc&0x1ffffff;
            unsigned maximum=native_region==3?1:native_region==2?6:18;
            uint32_t remaining=limit-total;
            uint32_t distance=g->event_cycles>g->cpu_pending?g->event_cycles-g->cpu_pending:0;
            if(!(native_before_pc&(width-1)) &&
                ((GBA_DYNAREC_BULK_PREFILL && gba_native_hard_distance(g)>3*maximum+20) ||
                 (remaining>3*maximum+20 && distance>3*maximum+20)) &&
                (native_before_pc&0xffffff)<=0xffffff-2*width &&
                (native_region<8 || (offset>=0x1000 && offset<g->rom_size && 3*width<=g->rom_size-offset))) {
                g->access_cycles=0;
#if GBA_DYNAREC_BULK_PREFILL
                /* Registers are in memory here. Defer device observation
                 * through the same explicit EVENT_SLIP block boundary as the
                 * native executor; charge the real two-fetch refill cost. */
                const uint8_t *base=native_region==3?g->memory.iwram:native_region==2?g->memory.ewram:g->rom;
                unsigned mask=native_region==3?0x7fff:native_region==2?0x3ffff:0x1ffffff;
                const uint8_t *p=base+(native_before_pc&mask),*next=base+((native_before_pc+width)&mask);
                native_cpu->pipeline[0]=width==2?gba_le16(p):gba_le32(p);
                native_cpu->pipeline[1]=width==2?gba_le16(next):gba_le32(next);
                unsigned refill=native_region==3?2:native_region==2?(width==4?12:6):
                    gba_gamepak_refill_cost(gba_io16(g,0x204),width,native_region);
                g->machine.last_fetch=native_before_pc+width;
                g->machine.open_bus=width==2?native_cpu->pipeline[1]*0x10001u:native_cpu->pipeline[1];
                g->machine.prefetch_count=g->machine.prefetch_phase=g->machine.dma_bus_valid=0;
                g->access_cycles=refill;g->cpu_pending+=refill;
                if(gba_native_cache(g))gba_native_cache(g)->prefill_cycles=refill;
#ifdef GBA_TEST_BULK_PREFILL_PROOF
                extern unsigned gba_test_bulk_prefill_entries;
                gba_test_bulk_prefill_entries++;
#endif
#else
                native_cpu->pipeline[0]=gba_cpu_fetch(g,native_before_pc,width,0);
                native_cpu->pipeline[1]=gba_cpu_fetch(g,native_before_pc+width,width,1);
#endif
                native_cpu->pipeline_valid=native_cpu->fetch_sequential=1;
                total+=g->access_cycles;
            }
        }
#if GBA_DYNAREC
        /* EVENT_SLIP can relax soft device interleave, never the caller's
         * control slice. Preserve the original API remainder across retries. */
        g->native_control_budget=total<budget?budget-total+1:1;
#endif
        uint32_t native_budget=total<limit?limit-total:0;
        uint32_t compiled;
#if GBA_DYNAREC_PERSISTENT_ENGINE
        compiled=gba_persistent_run(g,native_budget);
#if defined(GBA_TEST_TURBO_TRACE_DIAG) && GBA_DYNAREC_TURBO
        if(!compiled) {
            extern volatile uint32_t gba_test_turbo_reject_pc[8],gba_test_turbo_reject_count[8],gba_test_turbo_reject_total;
            unsigned slot=8,least=0;
            for(unsigned n=0;n<8;n++) {
                if(gba_test_turbo_reject_pc[n]==native_before_pc){slot=n;break;}
                if(gba_test_turbo_reject_count[n]<gba_test_turbo_reject_count[least])least=n;
            }
            if(slot==8)slot=least;
            if(gba_test_turbo_reject_pc[slot]!=native_before_pc) {
                gba_test_turbo_reject_pc[slot]=native_before_pc;gba_test_turbo_reject_count[slot]=0;
            }
            gba_test_turbo_reject_count[slot]++;gba_test_turbo_reject_total++;
        }
#endif
        if(!compiled && !g->fault)
#endif
        compiled=gba_dynarec_hot(g,native_budget);
#if GBA_DYNAREC_NEGATIVE
        if(compiled==UINT32_MAX)compiled=0;
        else
#endif
        if(!compiled && !g->fault)compiled=gba_dynarec_block(g,native_budget);
#if GBA_DYNAREC_BULK_PREFILL
        if(gba_native_cache(g))gba_native_cache(g)->prefill_cycles=0;
#endif
        if (g->fault)
            break;
        if (compiled) {
            total += compiled;
#if GBA_FAST_WAIT && !GBA_TEST_MODE
            uint32_t after_pc = g->machine.cpu.r[15];
            if (total < limit && (after_pc <= native_before_pc || (g->wait.valid && after_pc == g->wait.pc)))
                total += gba_wait_repeat(g, native_before_pc, limit - total);
#else
            (void)native_before_pc;
#endif
            if (gba_cpu_run_pending(g))
                break;
            continue;
        }
        if(total>=limit)break; /* A bulk refill alone may reach this boundary. */
#endif
#if GBA_FAST_ALU && !GBA_TEST_MODE
        gba_cpu_t *register_cpu = &g->machine.cpu;
        unsigned register_pair =
            (register_cpu->cpsr & GBA_T)
                ? gba_thumb_register_only((uint16_t)register_cpu->pipeline[0]) &&
                      gba_thumb_register_only((uint16_t)register_cpu->pipeline[1])
                : gba_arm_register_only(register_cpu->pipeline[0]) && gba_arm_register_only(register_cpu->pipeline[1]);
        uint32_t folded = register_pair ? gba_cpu_register_slice(g, limit - total) : 0;
        if (folded) {
            total += folded;
            if (gba_cpu_run_pending(g))
                break;
            continue;
        }
#endif
#if GBA_FAST_WAIT && !GBA_TEST_MODE
        uint32_t before_pc = g->machine.cpu.r[15];
#endif
        g->access_cycles = 0;
        uint32_t cycles = gba_cpu_execute_one(g);
        if (!cycles)
            cycles = 1;
        gba_cpu_charge(g, cycles - g->access_cycles);
        total += cycles;
#if GBA_FAST_WAIT && !GBA_TEST_MODE
        uint32_t after_pc = g->machine.cpu.r[15];
        if (total < limit && !g->gba_quit && (after_pc <= before_pc || (g->wait.valid && after_pc == g->wait.pc)))
            total += gba_wait_repeat(g, before_pc, limit - total);
#endif
        if (gba_cpu_run_pending(g))
            break;
    }
    gba_cpu_sync(g);
    g->cpu_bulk = 0;
#if GBA_DYNAREC
    g->native_control_budget=0;
#endif
    return total;
}
#endif
