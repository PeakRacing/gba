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
#include "gba_dynarec_internal.h"

#if GBA_DYNAREC

#ifdef GBA_TEST_HOT_BLOCKS
uint32_t gba_test_hot_count[1024],gba_test_hot_pc[1024],gba_test_hot_words[1024];
#endif

/* A32 instruction encodings are emitted independently from the ARM ARM.
 * No gpSP source or emitter macros are included in this implementation. */
_Static_assert(sizeof(uintptr_t) == 4 && offsetof(gba_t, machine) == 0 && offsetof(gba_machine_t, cpu) == 0 &&
                   offsetof(gba_cpu_t, r) == 0,
    "A32 CPU prefix ABI");

#if GBA_DYNAREC_TURBO_LIGHT
typedef struct {
    uint32_t *site[128];
    uint32_t value[128];
    unsigned count;
} gba_turbo_literal_pool_t;
#endif
typedef struct {
    uint32_t *begin, *next, *end;
#if GBA_DYNAREC_TURBO_LIGHT
    gba_turbo_literal_pool_t *literal_pool;
#endif
#if GBA_DYNAREC_TURBO_LOCAL_ALU
    unsigned local_guest[GBA_DYNAREC_TURBO_LOCAL_REGS]; /* guest plus one; r14/r12 within plain ALU runs */
#endif
    unsigned failed, cached_base, bounded, resumable;
    uint32_t chain;
    unsigned cached_guest[4], map[16];
} gba_a32_t;

#if GBA_DYNAREC_FAST_BUS
GBA_INLINE unsigned gba_native_fetch_floor(uint16_t wait,unsigned width,unsigned region) {
    if(wait&0x4000)return 1;
    static const unsigned first[4]={4,3,2,8};
    unsigned area=(region-8)/2,seq=gba_gamepak_seq(wait,area),nonseq=first[(wait>>(2+area*3))&3]+1;
    return (seq<nonseq?seq:nonseq)+(width==4?seq:0);
}
GBA_INLINE unsigned gba_native_refill_cost(uint16_t wait,unsigned width,unsigned region) {
    return gba_gamepak_refill_cost(wait,width,region);
}
#endif
static void gba_a32_emit(gba_a32_t *e, uint32_t instruction) {
    if (e->next == e->end)
        e->failed = 1;
    else
        *e->next++ = instruction;
}
#include "gba_dynarec_turbo_light.h"
/* Inline literal load followed by a branch around its data. No flags change. */
static void gba_a32_literal(gba_a32_t *e,unsigned rd,uint32_t value) {
#if GBA_DYNAREC_TURBO_LIGHT
    if(e->literal_pool) {gba_turbo_light_literal(e,rd,value);return;}
#endif
    gba_a32_emit(e,0xe59f0000u|(rd<<12));gba_a32_emit(e,0xea000000u);gba_a32_emit(e,value);
}
/* Recognize the ARM rotated-byte immediate without a 16-way cold compile
 * search. A wrapping byte becomes contiguous after rotating left by eight. */
static uint32_t gba_a32_immediate(uint32_t value) {
    if(!value)return 0;
    unsigned shift=(unsigned)__builtin_ctz(value)&~1u;
    uint32_t byte=value>>shift;
    if(byte<=255)return ((((32-shift)&31)/2)<<8)|byte;
    uint32_t wrapped=gba_ror(value,24);
    shift=(unsigned)__builtin_ctz(wrapped)&~1u;byte=wrapped>>shift;
    return byte<=255?((((8-shift)&31)/2)<<8)|byte:UINT32_MAX;
}
static void gba_a32_constant(gba_a32_t *e, unsigned rd, uint32_t value) {
    uint32_t immediate=gba_a32_immediate(value);
    if(immediate!=UINT32_MAX){gba_a32_emit(e,0xe3a00000u|(rd<<12)|immediate);return;}
    immediate=gba_a32_immediate(~value);
    if(immediate!=UINT32_MAX){gba_a32_emit(e,0xe3e00000u|(rd<<12)|immediate);return;}
    uint32_t part=value&255u,rest=value&~255u;
    immediate=gba_a32_immediate(rest);
    if(part && immediate!=UINT32_MAX) {
        gba_a32_emit(e,0xe3a00000u|(rd<<12)|part);
        gba_a32_emit(e,0xe3800000u|(rd<<16)|(rd<<12)|immediate);return;
    }
    gba_a32_literal(e,rd,value);
}
static void gba_a32_load(gba_a32_t *e, unsigned rd, unsigned rn, unsigned offset) {
    if (offset > 4095) {
        if (rd == rn) { e->failed = 1; return; }
        gba_a32_constant(e, rd, offset);
        gba_a32_emit(e, 0xe7900000u | (rn << 16) | (rd << 12) | rd);
        return;
    }
    gba_a32_emit(e, 0xe5900000u | (rn << 16) | (rd << 12) | offset);
}
static void gba_a32_store(gba_a32_t *e, unsigned rd, unsigned rn, unsigned offset) {
    if (offset > 4095) {
        e->failed = 1;
        return;
    }
    gba_a32_emit(e, 0xe5800000u | (rn << 16) | (rd << 12) | offset);
}
static void gba_a32_guest_read(gba_a32_t *e, unsigned host, unsigned guest, uint32_t pc) {
    if (guest == 15)
        gba_a32_constant(e, host, pc);
    else if (e->map[guest])
        gba_a32_emit(e, 0xe1a00000u | (host << 12) | (e->map[guest]));
    else
        gba_a32_load(e, host, 4, guest * 4);
}
static void gba_a32_guest_write(gba_a32_t *e, unsigned guest, unsigned host) {
    if (e->map[guest])
        gba_a32_emit(e, 0xe1a00000u | ((e->map[guest]) << 12) | host);
    else
        gba_a32_store(e, host, 4, guest * 4);
}
/* Convert Thumb register instructions to equivalent A32 ALU encodings. */
static unsigned gba_native_decode(uint32_t raw, unsigned width, uint32_t *arm) {
    uint32_t op = raw;
    if (width == 2) {
        unsigned rd = op & 7, rs = (op >> 3) & 7;
        if (op < 0x1800) {
            *arm = 0xe1b00000u | (rd << 12) | rs | (((op >> 11) & 3) << 5) | (((op >> 6) & 31) << 7);
        } else if (op < 0x2000) {
            *arm = ((op & 0x200) ? 0xe0500000u : 0xe0900000u) | (rs << 16) | (rd << 12);
            if (op & 0x400)
                *arm |= 0x02000000u | ((op >> 6) & 7);
            else
                *arm |= (op >> 6) & 7;
        } else if (op < 0x4000) {
            static const uint32_t codes[] = {0xe3b00000, 0xe3500000, 0xe2900000, 0xe2500000};
            rd = (op >> 8) & 7;
            *arm = codes[(op >> 11) & 3] | (rd << 16) | (rd << 12) | (op & 255);
        } else if ((op & 0xfc00) == 0x4000) {
            unsigned kind = (op >> 6) & 15;
            static const uint8_t alu[16] = {0, 1, 13, 13, 13, 5, 6, 13, 8, 3, 10, 11, 12, 0, 14, 15};
            if (kind == 13) {
                *arm = 0xe0100090u | (rd << 16) | (rd << 8) | rs;
                return GBA_JIT_MUL;
            }
            *arm = 0xe0100000u | ((uint32_t)alu[kind] << 21) | (rd << 16) | (rd << 12) | rs;
            if (kind == 2 || kind == 3 || kind == 4 || kind == 7) {
                *arm = 0xe1b00010u | (rd << 12) | rd | (rs << 8) | ((kind == 7 ? 3 : kind - 2) << 5);
            } else if (kind == 9)
                *arm = 0xe2700000u | (rs << 16) | (rd << 12);
        } else if ((op & 0xfc00) == 0x4400) {
            rd |= (op >> 4) & 8;
            rs = (op >> 3) & 15;
            unsigned kind = (op >> 8) & 3;
            if (kind == 3)
                return GBA_JIT_BRANCH;
            *arm = (kind == 0 ? 0xe0800000u : kind == 1 ? 0xe1500000u : 0xe1a00000u) | (rd << 16) | (rd << 12) | rs;
            if (rd == 15 && kind != 1)
                return GBA_JIT_BRANCH;
        } else if ((op & 0xf000) == 0xa000) {
            rd = (op >> 8) & 7;
            *arm = 0xe2800f00u | (((op & 0x800) ? 13u : 15u) << 16) | (rd << 12) | (op & 255);
        } else if ((op & 0xff00) == 0xb000) {
            *arm = ((op & 128) ? 0xe24ddf00u : 0xe28ddf00u) | (op & 127);
        } else if ((op & 0xf800) == 0x4800 || (op >= 0x5000 && op < 0xa000)) {
            return GBA_JIT_THUMB_MEMORY;
        } else if (GBA_DYNAREC_BL_PREFIX && (op&0xf800)==0xf000) {
            /* The first BL half only computes LR. It neither branches nor
             * refills the pipeline, so keep the second half in this span. */
            *arm=0xe1a0e00fu;
        } else if (((op & 0xf000) == 0xd000 && ((op >> 8) & 15) < 14) || (op & 0xf800) == 0xe000 ||
                   (op & 0xf000) == 0xf000) {
            return GBA_JIT_BRANCH;
        } else
            return 0;
        return GBA_JIT_ALU;
    }
    *arm = op;
    if ((op & 0x0fc000f0) == 0x00000090) {
        if (((op >> 16) & 15) == 15 || (op & 15) == 15 || ((op >> 8) & 15) == 15 ||
            ((op & 0x200000) && ((op >> 12) & 15) == 15))
            return 0;
        return GBA_JIT_MUL;
    }
    if ((op & 0x0e000000) == 0x0a000000 || (op & 0x0ffffff0) == 0x012fff10)
        return GBA_JIT_BRANCH;
    if ((op & 0x0c000000) == 0x04000000 && !((op & 0x02000010) == 0x02000010))
        return GBA_JIT_ARM_MEMORY;
    if ((op & 0x0c000000) != 0 || (op & 0x01900000) == 0x01000000 || (!(op & 0x02000000) && (op & 0x90) == 0x90))
        return 0;
    if (((op >> 12) & 15) == 15 && (op & 0x00100000))
        return 0;
    if (((op >> 12) & 15) == 15 && !(((op >> 21) & 15) >= 8 && ((op >> 21) & 15) <= 11)) {
        return (op & 0x00100000) ? 0 : GBA_JIT_BRANCH;
    }
    return GBA_JIT_ALU;
}
#include "gba_dynarec_persistent.h"
static void gba_a32_flags_out(gba_a32_t *e) {
    gba_a32_emit(e, 0xe10fc000u); /* MRS ip,CPSR */
    gba_a32_load(e, 0, 4, offsetof(gba_cpu_t, cpsr));
    gba_a32_emit(e, 0xe3c0020fu); /* BIC r0,r0,#0xf0000000 */
    gba_a32_emit(e, 0xe20cc20fu); /* AND ip,ip,#0xf0000000 */
    gba_a32_emit(e, 0xe180000cu);
    gba_a32_store(e, 0, 4, offsetof(gba_cpu_t, cpsr));
}
/* Preserve carry/overflow around the multiplier latency calculation. */
static void gba_a32_multiply(gba_a32_t *e, uint32_t op, uint32_t pc) {
    unsigned rm = op & 15, rs = (op >> 8) & 15, rd = (op >> 16) & 15;
    unsigned host_rm=e->map[rm]?e->map[rm]:0, host_rs=e->map[rs]?e->map[rs]:1;
    if(!e->map[rm])gba_a32_guest_read(e,0,rm,pc+8);
    if(!e->map[rs])gba_a32_guest_read(e,1,rs,pc+8);
    /* Set bit seven after sign normalization: CLZ is then at most 24,
     * which folds the clamp into the operand without touching guest NZCV. */
    gba_a32_emit(e, 0xe0202fc0u|(host_rs<<16)|host_rs); /* EOR r2,Rs,Rs,ASR #31 */
    gba_a32_emit(e, 0xe3822080u);
    gba_a32_emit(e, 0xe16f2f12u); /* CLZ r2,r2 */
    gba_a32_emit(e, 0xe1a021a2u); /* MOV r2,r2,LSR #3 */
    gba_a32_emit(e, 0xe2626004u); /* RSB r6,r2,#4 */
    unsigned host_rd=e->map[rd]?e->map[rd]:2;
    /* Pre-v6 ARM requires Rd != Rm. Multiplication is commutative, but
     * latency above always uses the original guest Rs. */
    if(host_rd==host_rm && host_rd!=host_rs) {unsigned t=host_rm;host_rm=host_rs;host_rs=t;}
    if(host_rd==host_rm)host_rd=2;
    if (op & 0x200000) {
        unsigned rn=(op>>12)&15, host_rn=e->map[rn]?e->map[rn]:3;
        if(!e->map[rn])gba_a32_guest_read(e,3,rn,pc+8);
        gba_a32_emit(e,0xe0200090u|(host_rd<<16)|(host_rn<<12)|(host_rs<<8)|host_rm);
        gba_a32_emit(e, 0xe2866001u);
    } else
        gba_a32_emit(e,0xe0000090u|(host_rd<<16)|(host_rs<<8)|host_rm);
    if (op & 0x100000)
        gba_a32_emit(e,0xe1100000u|(host_rd<<16)|host_rd); /* TST keeps C and V. */
    if(e->map[rd]!=host_rd)gba_a32_guest_write(e,rd,host_rd);
}
static void gba_a32_alu(gba_a32_t *e, uint32_t raw, uint32_t op, uint32_t pc, unsigned width) {
    if(GBA_DYNAREC_BL_PREFIX && width==2 && (raw&0xf800)==0xf000) {
        gba_a32_constant(e,0,pc+4+(uint32_t)(gba_sign(raw&0x7ff,11)*4096));
        gba_a32_guest_write(e,14,0);
        return;
    }
    unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    unsigned shifted = !(op & 0x02000000) && (op & 16);
    unsigned operation = (op >> 21) & 15, test = operation >= 8 && operation <= 11;
    unsigned ignore_rn = operation == 13 || operation == 15;
    unsigned immediate = op & 0x02000000u, rm = op & 15, rs = (op >> 8) & 15;
    /* Remap cached guest registers directly into the host encoding. Avoid
     * moving every operand through r0/r1/r2 for a one-instruction ALU result. */
    if ((ignore_rn || e->map[rn]) && (test || e->map[rd]) &&
        (immediate || (e->map[rm] && (!shifted || e->map[rs])))) {
        uint32_t operand = op & 0xfff;
        if (!immediate) {
            operand = (operand & ~15u) | (e->map[rm]);
            if (shifted) operand = (operand & ~0xf00u) | ((e->map[rs]) << 8);
        }
        uint32_t encoded = 0xe0000000u | (op & 0x03f00000u) | operand;
        if (!ignore_rn) encoded |= (e->map[rn]) << 16;
        if (!test) encoded |= (e->map[rd]) << 12;
        else encoded |= 0x00100000u;
        gba_a32_emit(e, encoded);
        if (shifted) gba_a32_emit(e, 0xe3a06001u);
        return;
    }
    uint32_t operand = op & 0xfff;
    uint32_t advance = width == 2 ? 4 : shifted ? 12 : 8;
    uint32_t source_pc = pc + advance;
    if (width == 2 && (raw & 0xf800) == 0xa000)
        source_pc &= ~3u;
#if GBA_DYNAREC_PARTIAL_REGS
    unsigned host_rn=ignore_rn?0:e->map[rn]?e->map[rn]:0;
    unsigned host_rd=test?0:e->map[rd]?e->map[rd]:2;
    if(!ignore_rn && !e->map[rn])gba_a32_guest_read(e,0,rn,source_pc);
    if(!immediate) {
        unsigned host_rm=e->map[rm]?e->map[rm]:1;
        if(!e->map[rm])gba_a32_guest_read(e,1,rm,pc+advance);
        operand=(operand&~15u)|host_rm;
        if(shifted) {
            unsigned host_rs=e->map[rs]?e->map[rs]:3;
            if(!e->map[rs])gba_a32_guest_read(e,3,rs,pc+(width==2?4:8));
            operand=(operand&~0xf00u)|(host_rs<<8);gba_a32_emit(e,0xe3a06001u);
        }
    }
    uint32_t encoded=0xe0000000u|(op&0x03f00000u)|operand|(host_rn<<16)|(host_rd<<12);
    if(test)encoded|=0x00100000u;
    gba_a32_emit(e,encoded);
    if(!test && !e->map[rd])gba_a32_guest_write(e,rd,2);
#else
    if (!ignore_rn) gba_a32_guest_read(e, 0, rn, source_pc);
    if (!(op & 0x02000000)) {
        gba_a32_guest_read(e, 1, op & 15, pc + advance);
        operand = (operand & ~15u) | 1;
        if (shifted) {
            gba_a32_guest_read(e, 3, (op >> 8) & 15, pc + (width == 2 ? 4 : 8));
            operand = (operand & ~0xf00u) | 0x300;
            gba_a32_emit(e, 0xe3a06001u);
        }
    }
    uint32_t encoded = 0xe0002000u | (op & 0x03f00000u) | operand;
    /* Architecturally ignored Rn fields of MOV/MVN and Rd of tests are zero. */
    if (operation == 13 || operation == 15)
        encoded &= ~0xf0000u;
    if (test)
        encoded = (encoded & ~0xf000u) | 0x00100000u;
    gba_a32_emit(e, encoded);
    if (!test)
        gba_a32_guest_write(e, rd, 2);
#endif
}
static void gba_a32_branch_target(gba_a32_t *e, unsigned target, int exchange, unsigned thumb) {
    if (exchange) {
        gba_a32_load(e, 0, 4, offsetof(gba_cpu_t, cpsr));
        gba_a32_emit(e, 0xe3c00020u);
        gba_a32_emit(e, 0xe2002001u | (target << 16));
        gba_a32_emit(e, 0xe1800282u); /* ORR r0,r0,r2,LSL #5 */
        gba_a32_store(e, 0, 4, offsetof(gba_cpu_t, cpsr));
        gba_a32_emit(e, 0xe1a02082u); /* MOV r2,r2,LSL #1 */
        gba_a32_emit(e, 0xe2222003u);
        gba_a32_emit(e, 0xe1c01002u | (target << 16));
    } else
        gba_a32_emit(e, 0xe3c01000u | (target << 16) | (thumb ? 1 : 3));
    gba_a32_store(e, 1, 4, 60);
    gba_a32_emit(e, 0xe3a00000u);
    gba_a32_store(e, 0, 4, offsetof(gba_cpu_t, pipeline_valid));
    gba_a32_store(e, 0, 4, offsetof(gba_cpu_t, fetch_sequential));
    gba_a32_store(e, 0, 4, offsetof(gba_t, machine.prefetch_count));
    gba_a32_store(e, 0, 4, offsetof(gba_t, machine.prefetch_phase));
}
static void gba_a32_branch(gba_a32_t *e, uint32_t raw, uint32_t pc, unsigned width) {
    if (width == 4 && (raw & 0x0ffffff0) == 0x012fff10) {
        gba_a32_guest_read(e, 1, raw & 15, pc + 8);
        gba_a32_branch_target(e, 1, 1, 0);
    } else if (width == 4 && (raw & 0x0e000000) == 0x0a000000) {
        if (raw & 0x01000000) {
            gba_a32_constant(e, 0, pc + 4);
            gba_a32_guest_write(e, 14, 0);
        }
        gba_a32_constant(e, 1, pc + 8 + (uint32_t)(gba_sign(raw & 0xffffff, 24) * 4));
        gba_a32_branch_target(e, 1, 0, 0);
    } else if (width == 4) {
        gba_a32_alu(e, raw, raw, pc, width);
        gba_a32_branch_target(e, 2, 0, 0);
    } else if ((raw & 0xfc00) == 0x4400) {
        unsigned rd = (raw & 7) | ((raw >> 4) & 8), rs = (raw >> 3) & 15, kind = (raw >> 8) & 3;
        gba_a32_guest_read(e, 1, rs, pc + 4);
        if (kind == 0) {
            gba_a32_guest_read(e, 0, rd, pc + 4);
            gba_a32_emit(e, 0xe0801001u);
        }
        gba_a32_branch_target(e, 1, kind == 3, 1);
    } else if ((raw & 0xf800) == 0xf000) {
        gba_a32_constant(e, 0, pc + 4 + (uint32_t)(gba_sign(raw & 0x7ff, 11) * 4096));
        gba_a32_guest_write(e, 14, 0);
    } else if ((raw & 0xf800) == 0xf800) {
        gba_a32_guest_read(e, 1, 14, pc + 4);
        gba_a32_constant(e, 0, (raw & 0x7ff) * 2);
        gba_a32_emit(e, 0xe0811000u);
        gba_a32_constant(e, 0, (pc + 2) | 1);
        gba_a32_guest_write(e, 14, 0);
        gba_a32_branch_target(e, 1, 0, 1);
    } else {
        uint32_t target = (raw & 0xf000) == 0xd000 ? pc + 4 + (uint32_t)(gba_sign(raw & 255, 8) * 2)
                                                   : pc + 4 + (uint32_t)(gba_sign(raw & 0x7ff, 11) * 2);
        gba_a32_constant(e, 1, target);
        gba_a32_branch_target(e, 1, 0, 1);
    }
}
static void gba_a32_cache_transfer(gba_a32_t *e,unsigned load) {
    if(e->cached_base<15) {
        if(e->cached_base)gba_a32_emit(e,0xe284c000u|e->cached_base*4);
        gba_a32_emit(e,(load?0xe8900f00u:0xe8800f00u)|(e->cached_base?0xc0000u:0x40000u));
    } else for(unsigned i=0;i<4;i++) {
        if(load)gba_a32_load(e,8+i,4,e->cached_guest[i]*4);
        else gba_a32_store(e,8+i,4,e->cached_guest[i]*4);
    }
}
static void gba_a32_prologue(gba_a32_t *e) {
#if GBA_DYNAREC_FASTCALL
    unsigned frame=e->chain?32:e->bounded?24:16;
    gba_a32_emit(e,0xe24dd000u|(frame+8));gba_a32_emit(e,0xe58de000u|frame);
#else
    gba_a32_emit(e, 0xe92d5ff0u); /* r4-r12,lr; preserve AAPCS and 8-byte alignment. */
    gba_a32_emit(e,e->chain?0xe24dd020u:e->bounded?0xe24dd018u:0xe24dd010u);
#endif
    if(e->chain) {
        gba_a32_emit(e,0xe58d2010u);gba_a32_emit(e,0xe58d301cu);
        gba_a32_emit(e,0xe1a0cfa1u);gba_a32_emit(e,0xe58dc014u);
        gba_a32_emit(e,0xe3a0c000u);gba_a32_emit(e,0xe58dc018u);
    }
#if GBA_DYNAREC_INTERIOR
    if(e->resumable) {
        gba_a32_emit(e,0xe201c101u); /* bit30: interior entry */
        gba_a32_emit(e,0xe58dc00cu);gba_a32_emit(e,0xe3c11101u);
    }
#endif
    if(e->bounded) {
        gba_a32_emit(e,0xe1a0c821u); /* budget in argument bits 16..31 */
        gba_a32_emit(e,0xe58dc010u);
    }
    gba_a32_emit(e, e->bounded ? 0xe3a0c001u : 0xe1a0c421u);
    if(e->chain)gba_a32_emit(e,0xe20cc0ffu);
    gba_a32_emit(e, 0xe58dc000u);
    gba_a32_emit(e, 0xe1a04000u);
    gba_a32_cache_transfer(e,1);
    gba_a32_emit(e, 0xe3a05000u);
    gba_a32_emit(e, 0xe3a07000u);
    if(e->bounded)gba_a32_emit(e,0xe3a06000u);
    gba_a32_emit(e, 0xe58d7004u);
#if GBA_DYNAREC_INTERIOR
    if(e->resumable) {
        gba_a32_emit(e,0xe59d000cu);gba_a32_emit(e,0xe3500000u);
        uint32_t *normal=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_load(e,0,4,offsetof(gba_t,dynarec_memory.data));
        gba_a32_load(e,7,0,offsetof(gba_native_cache_t,resume_seed));
        gba_a32_load(e,12,0,offsetof(gba_native_cache_t,resume_ip));
        gba_a32_emit(e,0xe1a0e00cu);
        gba_a32_load(e,12,0,offsetof(gba_native_cache_t,resume_failure));gba_a32_emit(e,0xe58dc008u);
        gba_a32_emit(e,0xe3a06000u);
        gba_a32_load(e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(e,0xe128f00cu);
        gba_a32_emit(e,0xe12fff1eu);
        *normal|=(uint32_t)(e->next-normal-2)&0xffffff;
    }
#endif
    gba_a32_load(e, 12, 4, offsetof(gba_cpu_t, cpsr));
    gba_a32_emit(e, 0xe128f00cu);
}
static void gba_a32_epilogue(gba_a32_t *e) {
    gba_a32_flags_out(e);
    if(e->chain) {
        gba_a32_emit(e,0xe59d0014u);gba_a32_emit(e,0xe3500000u);
        gba_a32_emit(e,0x1a000000u|((uint32_t)(((uint32_t *)(uintptr_t)e->chain)-e->next-2)&0xffffff));
    }
    gba_a32_cache_transfer(e,0);
    gba_a32_emit(e, 0xe1850806u); /* r0 = total internal | last internal << 16 */
    gba_a32_emit(e, 0xe1a01007u); /* r1 = executed prefix and native read count. */
#if GBA_DYNAREC_FASTCALL
    unsigned frame=e->chain?32:e->bounded?24:16;
    gba_a32_emit(e,0xe59de000u|frame);gba_a32_emit(e,0xe28dd000u|(frame+8));gba_a32_emit(e,0xe12fff1eu);
#else
    gba_a32_emit(e,e->chain?0xe28dd020u:e->bounded?0xe28dd018u:0xe28dd010u);
    gba_a32_emit(e, 0xe8bd9ff0u);
#endif
}
#if GBA_DYNAREC_CHAIN
/* One host call frame spans a chain. Only a changed guest-register mapping
 * spills/reloads the four cached registers; slow bus/events return to C. */
#if GBA_DYNAREC_DIRECT && !GBA_DYNAREC_STATS && !GBA_DYNAREC_COARSE_CHAIN
/* A full IWRAM fallthrough can commit and enter a cached successor without C.
 * All guards precede the commit. Guest cached registers remain untouched. */
static void gba_a32_direct(gba_t *g,gba_a32_t *e) {
    gba_native_cache_t *c=gba_native_cache(g);
    uint32_t *fail[32];unsigned failures=0;
#define REJECT(cond) do{fail[failures++]=e->next;gba_a32_emit(e,(cond)|0x0a000000u);}while(0)
#define LOADB(rd,rn,off) gba_a32_emit(e,0xe5d00000u|((rn)<<16)|((rd)<<12)|(off))
#define LOADH(rd,rn,off) gba_a32_emit(e,0xe1d000b0u|((rn)<<16)|((rd)<<12)|(((off)&0xf0)<<4)|((off)&15))
#define SAVE(rd,off) do {if((off)<=4095)gba_a32_store(e,rd,4,off);else {gba_a32_constant(e,14,off);gba_a32_emit(e,0xe784000eu|((rd)<<12));}}while(0)
    gba_a32_emit(e,0xe3170102u);REJECT(0x10000000u); /* guard failure */
    gba_a32_load(e,0,4,offsetof(gba_cpu_t,pipeline_valid));gba_a32_emit(e,0xe58d0004u);
    gba_a32_emit(e,0xe59d001cu);
#if GBA_DYNAREC_ROM_CHAIN
    gba_a32_load(e,3,0,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe1a03c23u);
    gba_a32_emit(e,0xe3530003u);REJECT(0x10000000u);
#endif
#if GBA_DYNAREC_COARSE_CHAIN
    gba_a32_emit(e,0xe20730ffu);gba_a32_emit(e,0xe3530000u);REJECT(0x00000000u);
#else
    LOADB(3,0,offsetof(gba_native_block_t,count));gba_a32_emit(e,0xe207c0ffu);gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
#endif
    gba_a32_emit(e,0xe085c003u);
    gba_a32_emit(e,0xe59d0004u);gba_a32_emit(e,0xe3500000u);gba_a32_emit(e,0x028cc002u);
    gba_a32_emit(e,0xe58dc008u); /* completed cycles including a taken refill */
    gba_a32_emit(e,0xe59d001cu);
    LOADB(12,0,offsetof(gba_native_block_t,flags));
#if GBA_DYNAREC_COARSE_CHAIN
    gba_a32_emit(e,0xe31c0020u);gba_a32_emit(e,0x15d03000u|offsetof(gba_native_block_t,count));
#endif
    gba_a32_emit(e,0xe31c0001u);gba_a32_emit(e,0xe3a0c004u);gba_a32_emit(e,0x13a0c002u);
    gba_a32_emit(e,0xe58dc00cu);
    gba_a32_emit(e,0xe003039cu); /* MUL r3,ip,r3 */
    gba_a32_load(e,2,0,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe0822003u);
    gba_a32_emit(e,0xe59dc004u);gba_a32_emit(e,0xe35c0000u);
    gba_a32_emit(e,0x0594203cu); /* taken branch target r15 */
    gba_a32_load(e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(e,0xe31c0020u);
    gba_a32_emit(e,0xe3a0c004u);gba_a32_emit(e,0x13a0c002u);gba_a32_emit(e,0xe58dc00cu);
    gba_a32_emit(e,0xe1a03c22u);gba_a32_emit(e,0xe3530003u);REJECT(0x10000000u);
    gba_a32_emit(e,0xe1a030a2u);gba_a32_emit(e,0xe0233422u);gba_a32_emit(e,0xe203303fu);
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)c->recent);gba_a32_emit(e,0xe79c1103u);
    gba_a32_emit(e,0xe3510000u);REJECT(0x00000000u);
    gba_a32_load(e,12,1,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe15c0002u);REJECT(0x10000000u);
    LOADH(12,1,offsetof(gba_native_block_t,cache_key));gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);
    gba_a32_emit(e,0xe3a03080u);gba_a32_emit(e,0x03833001u);gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    gba_a32_load(e,3,1,offsetof(gba_native_block_t,epoch));gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->epoch);gba_a32_emit(e,0xe59cc000u);
    gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    gba_a32_emit(e,0xe1a03422u);gba_a32_emit(e,0xe203307fu);gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->generation[1024]);
    gba_a32_emit(e,0xe79cc103u);gba_a32_load(e,3,1,offsetof(gba_native_block_t,page_generation));gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    LOADH(3,1,offsetof(gba_native_block_t,end_page));gba_a32_constant(e,12,(uint32_t)(uintptr_t)c->generation);
    gba_a32_emit(e,0xe79cc103u);gba_a32_load(e,3,1,offsetof(gba_native_block_t,end_generation));gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    LOADB(3,1,offsetof(gba_native_block_t,count));gba_a32_emit(e,0xe3530002u);REJECT(0x30000000u);
    LOADH(12,1,offsetof(gba_native_block_t,extra_bound));gba_a32_emit(e,0xe083300cu);
    gba_a32_emit(e,0xe59dc008u);gba_a32_emit(e,0xe083300cu);gba_a32_emit(e,0xe2833002u);
    gba_a32_emit(e,0xe59dc010u);gba_a32_emit(e,0xe15c0003u);REJECT(0x90000000u);
    /* Compare both live pipeline words before committing any architectural state. */
    gba_a32_emit(e,0xe1a0c882u);gba_a32_emit(e,0xe1a0c8acu);
    gba_a32_load(e,3,4,offsetof(gba_t,memory.iwram));gba_a32_emit(e,0xe083c00cu);
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);
    gba_a32_emit(e,0x01dc30b0u);gba_a32_emit(e,0x159c3000u);
    gba_a32_load(e,0,1,offsetof(gba_native_block_t,head));gba_a32_emit(e,0xe1530000u);REJECT(0x10000000u);
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);
    gba_a32_emit(e,0x01dc30b2u);gba_a32_emit(e,0x159c3004u);
    gba_a32_load(e,0,1,offsetof(gba_native_block_t,head)+4);gba_a32_emit(e,0xe1530000u);REJECT(0x10000000u);
    /* No rejection follows this point: commit the exact completed span. */
    gba_a32_emit(e,0xe59d0008u);gba_a32_emit(e,0xe59d3010u);gba_a32_emit(e,0xe0433000u);gba_a32_emit(e,0xe58d3010u);
    gba_a32_emit(e,0xe59d3018u);gba_a32_emit(e,0xe0833000u);gba_a32_emit(e,0xe58d3018u);
    gba_a32_load(e,3,4,offsetof(gba_t,cpu_pending));gba_a32_emit(e,0xe0833000u);SAVE(3,offsetof(gba_t,cpu_pending));
    SAVE(2,offsetof(gba_cpu_t,r)+15*4);
    gba_a32_load(e,0,1,offsetof(gba_native_block_t,head));SAVE(0,offsetof(gba_cpu_t,pipeline));
    gba_a32_load(e,0,1,offsetof(gba_native_block_t,head)+4);SAVE(0,offsetof(gba_cpu_t,pipeline)+4);
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);gba_a32_emit(e,0x01800800u);SAVE(0,offsetof(gba_t,machine.open_bus));
    gba_a32_emit(e,0xe0822003u);SAVE(2,offsetof(gba_t,machine.last_fetch));
    gba_a32_emit(e,0xe3a00000u);SAVE(0,offsetof(gba_t,machine.dma_bus_valid));
    gba_a32_emit(e,0xe1a00426u);gba_a32_emit(e,0xe2800001u);
    gba_a32_emit(e,0xe59d3004u);gba_a32_emit(e,0xe3530000u);gba_a32_emit(e,0x03a00002u);
    SAVE(0,offsetof(gba_t,access_cycles));
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->rom_data_seen);gba_a32_emit(e,0xe59c0000u);gba_a32_emit(e,0xe3100001u);
    uint32_t *no_rom=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3a00000u);SAVE(0,offsetof(gba_t,machine.prefetch_count));SAVE(0,offsetof(gba_t,machine.prefetch_phase));
    *no_rom|=(uint32_t)(e->next-no_rom-2)&0xffffff;
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)(g->io+0x204));gba_a32_emit(e,0xe1dc00b0u);gba_a32_emit(e,0xe3100901u);
    uint32_t *enabled=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe3560000u);uint32_t *set_seq=e->next;gba_a32_emit(e,0xea000000u);
    *enabled|=(uint32_t)(e->next-enabled-2)&0xffffff;gba_a32_emit(e,0xe3160080u);
    *set_seq|=(uint32_t)(e->next-set_seq-2)&0xffffff;
    gba_a32_emit(e,0x03a00001u);gba_a32_emit(e,0x13a00000u);
    gba_a32_emit(e,0xe59d3004u);gba_a32_emit(e,0xe3530000u);gba_a32_emit(e,0x03a00001u);
    SAVE(0,offsetof(gba_cpu_t,fetch_sequential));
    gba_a32_emit(e,0xe3a00001u);SAVE(0,offsetof(gba_cpu_t,pipeline_valid));
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    gba_a32_emit(e,0xe3170801u);
    uint32_t *no_write=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3a00000u);SAVE(0,offsetof(gba_t,wait.valid));
    *no_write|=(uint32_t)(e->next-no_write-2)&0xffffff;
#endif
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->direct_links);gba_a32_emit(e,0xe59c0000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe58c0000u);
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->rom_data_seen);gba_a32_emit(e,0xe3a00000u);gba_a32_emit(e,0xe58c0000u);
    gba_a32_emit(e,0xe59d001cu);LOADH(0,0,offsetof(gba_native_block_t,regmap));
    LOADH(3,1,offsetof(gba_native_block_t,regmap));gba_a32_emit(e,0xe1500003u);
    uint32_t *same_map=e->next;gba_a32_emit(e,0x0a000000u);
    for(unsigned n=0;n<4;n++) {
        gba_a32_emit(e,0xe200c00fu);gba_a32_emit(e,0xe784810cu|(n<<12));
        if(n<3)gba_a32_emit(e,0xe1a00220u);
    }
    for(unsigned n=0;n<4;n++) {
        gba_a32_emit(e,0xe203c00fu);gba_a32_emit(e,0xe794810cu|(n<<12));
        if(n<3)gba_a32_emit(e,0xe1a03223u);
    }
    *same_map|=(uint32_t)(e->next-same_map-2)&0xffffff;
    gba_a32_emit(e,0xe58d101cu);
    LOADH(0,1,offsetof(gba_native_block_t,raw_words));gba_a32_emit(e,0xe1a005a0u);
    gba_a32_load(e,12,1,offsetof(gba_native_block_t,offset));gba_a32_emit(e,0xe08cc100u);
    gba_a32_constant(e,14,(uint32_t)(uintptr_t)g->dynarec_memory.code);gba_a32_emit(e,0xe08ce00eu);
    gba_a32_emit(e,0xe3a00001u);gba_a32_emit(e,0xe58d0000u);gba_a32_emit(e,0xe3a05000u);gba_a32_emit(e,0xe3a06000u);gba_a32_emit(e,0xe3a07000u);gba_a32_emit(e,0xe58d7004u);
    gba_a32_load(e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(e,0xe128f00cu);gba_a32_emit(e,0xe12fff1eu);
    for(unsigned n=0;n<failures;n++)*fail[n]|=(uint32_t)(e->next-fail[n]-2)&0xffffff;
#undef REJECT
#undef LOADB
#undef LOADH
#undef SAVE
}
#endif
#include "gba_dynarec_coarse.h"
static int gba_native_chain_stub(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    if(c->chain_stub || c->code_base==GBA_JIT_RESERVE)return 0;
    uint32_t *code=(uint32_t *)((uint8_t *)g->dynarec_memory.code+GBA_JIT_RESERVE);
    unsigned stub_bytes=GBA_DYNAREC_COARSE_CHAIN?4096:2048;
    if(gba_code_begin_write(code,stub_bytes))return -1;
    gba_a32_t e={.begin=code,.next=code,.end=code+stub_bytes/4};
#if GBA_DYNAREC_COARSE_CHAIN
    gba_a32_direct_rom(g,&e);
#endif
#if GBA_DYNAREC_DIRECT && !GBA_DYNAREC_STATS && !GBA_DYNAREC_COARSE_CHAIN
    gba_a32_direct(g,&e);
#endif
    gba_a32_emit(&e,0xe1851806u); /* r1=internal/last, r2=count, r3=frame */
    gba_a32_emit(&e,0xe1a02007u);gba_a32_emit(&e,0xe1a0300du);gba_a32_emit(&e,0xe1a00004u);
    gba_a32_constant(&e,12,(uint32_t)(uintptr_t)gba_native_chain);gba_a32_emit(&e,0xe12fff3cu);
    gba_a32_emit(&e,0xe58d0008u);gba_a32_emit(&e,0xe58d1004u);
    gba_a32_emit(&e,0xe59d200cu); /* old register map, preserved by C */
    gba_a32_emit(&e,0xe3500000u);
    uint32_t *stop=e.next;gba_a32_emit(&e,0x0a000000u);
    gba_a32_emit(&e,0xe1510002u);
    uint32_t *same=e.next;gba_a32_emit(&e,0x0a000000u);
    uint32_t *spill=e.next;
    for(unsigned n=0;n<4;n++) {
        gba_a32_emit(&e,0xe202300fu);
        gba_a32_emit(&e,0xe7848103u|(n<<12)); /* STR cached,[g,index,LSL#2] */
        if(n<3)gba_a32_emit(&e,0xe1a02222u);
    }
    gba_a32_emit(&e,0xe59d0008u);gba_a32_emit(&e,0xe3500000u);
    uint32_t *done=e.next;gba_a32_emit(&e,0x0a000000u);
    gba_a32_emit(&e,0xe59d1004u);
    for(unsigned n=0;n<4;n++) {
        gba_a32_emit(&e,0xe201300fu);
        gba_a32_emit(&e,0xe7948103u|(n<<12));
        if(n<3)gba_a32_emit(&e,0xe1a01221u);
    }
    *same|=(uint32_t)(e.next-same-2)&0xffffff;
    gba_a32_emit(&e,0xe59dc008u);
    gba_a32_emit(&e,0xe3a05000u);gba_a32_emit(&e,0xe3a06000u);gba_a32_emit(&e,0xe3a07000u);
    gba_a32_emit(&e,0xe58d7004u);
    gba_a32_emit(&e,0xe1a0e00cu);
    gba_a32_load(&e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(&e,0xe128f00cu);
    gba_a32_emit(&e,0xe12fff1eu);
    *stop|=(uint32_t)(spill-stop-2)&0xffffff;
    *done|=(uint32_t)(e.next-done-2)&0xffffff;
    gba_a32_emit(&e,0xe59d0018u);gba_a32_emit(&e,0xe3a01101u); /* chain result marker bit 62 */
    gba_a32_emit(&e,0xe28dd020u);gba_a32_emit(&e,0xe8bd9ff0u);
    if(e.failed || gba_code_publish(code,(size_t)(e.next-code)*4))return -1;
    c->chain_stub=GBA_JIT_RESERVE;
    return 0;
}
#endif
/* First mixed spans: unconditional, pre-indexed, no-writeback word/byte loads.
 * Dynamic alignment/device guards leave an exact committed prefix for C.
 * Stores, devices, misalignment and mode-changing reads keep the shared bus. */
static unsigned gba_native_ram_read(uint32_t op, unsigned width) {
    if (width == 4) {
        unsigned memory = (op>>28)<15 && (op & 0x0c000000u) == 0x04000000u && (op & 0x02000010u) != 0x02000010u;
        unsigned sh=(op>>5)&3;
        unsigned half = (op>>28)<15 && (op & 0x0e000090u) == 0x00000090u &&
            (sh==1 || (sh>1 && (op&0x100000u)));
        unsigned update = !(op & 0x1000000u) || (op & 0x200000u);
        return (memory || half) && ((op >> 12) & 15) != 15 && (!update || ((op >> 16) & 15) != 15);
    }
    return (op & 0xf800u) == 0x4800u || (op & 0xe000u) == 0x6000u ||
        (op & 0xf000u) == 0x5000u || (op & 0xf000u) == 0x8000u || (op & 0xf000u) == 0x9000u;
}
static unsigned gba_native_is_load(uint32_t op, unsigned width) {
    if (width == 4) return !!(op & 0x100000u);
    if ((op & 0xf000u) == 0x5000u) return ((op >> 9) & 7) >= 3;
    return (op & 0xf800u) == 0x4800u || !!(op & 0x800u);
}
static unsigned gba_native_multiple(uint32_t op, unsigned width) {
    if(width==4) {
        if((op&0xfe400000u)!=0xe8000000u || !(op&0xffff) ||
            ((op&0x8000)&&!(op&0x100000)) || ((op>>16)&15)==15)return 0;
        return op;
    }
    if((op&0xf600)==0xb400) {
        unsigned load=!!(op&0x800),list=op&255;
        if(op&0x100)list|=load?0x8000:0x4000;
        if(!list)return 0;
        return (load?0xe8bd0000u:0xe92d0000u)|list;
    }
    if((op&0xf000)==0xc000 && (op&255))
        return ((op&0x800)?0xe8b00000u:0xe8a00000u)|(((op>>8)&7)<<16)|(op&255);
    return 0;
}
/* Event-bounded RAM-only multiple transfer. Validate the entire operation before
 * mutation; no MMIO, code-page writes, wrapping or banked/user/PC semantics. */
static uint32_t gba_native_multiple_ram(gba_t *g, uint32_t op) {
    gba_cpu_t *cpu = &g->machine.cpu;
    unsigned rn = (op >> 16) & 15, load = !!(op & 0x100000), count = 0;
    uint32_t list = op & 0xffff, base = cpu->r[rn];
    for (uint32_t bits=list;bits;bits&=bits-1) count++;
    uint32_t updated = op & 0x800000 ? base+count*4 : base-count*4;
    uint32_t address = op & 0x800000 ? base+(op&0x1000000?4:0) : base-count*4+(op&0x1000000?0:4);
    unsigned region=address>>24, size=region==2?0x40000:0x8000;
    if ((address&3) || (region!=2&&region!=3) || (address&(size-1))+count*4>size) return UINT32_MAX;
    if (!load) {
        gba_native_cache_t *cache=gba_native_cache(g);
        for(uint32_t at=address;at<address+count*4;at=(at&~31u)+32)
            if(cache->code_page[gba_native_page(at)]&(1u<<((at>>5)&7)))return UINT32_MAX;
    }
    uint8_t *p=(region==2?g->memory.ewram:g->memory.iwram)+(address&(size-1));
    unsigned sequential=0;
    for(unsigned r=0;r<16;r++) if(list&(1u<<r)) {
        if(load) {
            uint32_t value=gba_le32(p);
            if(r==15) {
                cpu->r[15]=value&((cpu->cpsr&GBA_T)?~1u:~3u);
                cpu->pipeline_valid=cpu->fetch_sequential=0;
                g->machine.prefetch_count=g->machine.prefetch_phase=0;
            } else cpu->r[r]=value;
        }
        else {
            uint32_t value=r==rn&&(op&0x200000)&&sequential?updated:cpu->r[r];
            gba_put32(p,value);
        }
        p+=4;sequential=1;
    }
    if ((op&0x200000)&&(!load||!(list&(1u<<rn)))) cpu->r[rn]=updated;
    unsigned data=count*(region==2?6:1);
    return (data+load) | ((load|(data<<8))<<16);
}
#if GBA_DYNAREC_MULTI_INLINE
/* Inline ordinary RAM multiple transfers. All address guards finish before the
 * first guest register changes. PC/banked transfers and rejected spans retain
 * the established helper. No cached guest registers are spilled on success. */
static uint32_t *gba_a32_multiple_load(gba_a32_t *e,uint32_t op,unsigned width) {
    unsigned rn=(op>>16)&15,list=op&0xffff,count=(unsigned)__builtin_popcount(list),load=!!(op&0x100000);
    gba_a32_guest_read(e,14,rn,0);
    int delta=(op&0x800000)?((op&0x1000000)?4:0):-(int)(count*4)+((op&0x1000000)?0:4);
    if(delta)gba_a32_emit(e,(delta>0?0xe28e0000u:0xe24e0000u)|(unsigned)(delta>0?delta:-delta));
    else gba_a32_emit(e,0xe1a0000eu);
    uint32_t *fail[8];unsigned nfail=0;
    gba_a32_emit(e,0xe3100003u);fail[nfail++]=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe1a02c20u);gba_a32_emit(e,0xe3520002u);
    uint32_t *ew=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3520003u);fail[nfail++]=e->next;gba_a32_emit(e,0x1a000000u);
    uint32_t *done=NULL;
    for(unsigned path=0;path<2;path++) {
        if(path)*ew|=(uint32_t)(e->next-ew-2)&0xffffff;
        unsigned shift=path?14:17,size=path?0x40000:0x8000;
        gba_a32_emit(e,0xe1a00000u|(shift<<7));
        gba_a32_emit(e,0xe1a00020u|(shift<<7));
        gba_a32_constant(e,3,size-count*4);gba_a32_emit(e,0xe1500003u);
        fail[nfail++]=e->next;gba_a32_emit(e,0x8a000000u);
        if(!load) {
            /* A whole-page zero is a conservative no-code proof. Both end
             * pages cover every possible 60-byte span; tagged pages fall back
             * before mutation to the helper's finer 32-byte line checks. */
            gba_a32_load(e,1,4,offsetof(gba_t,dynarec_memory.data));
            gba_a32_constant(e,3,offsetof(gba_native_cache_t,code_page));gba_a32_emit(e,0xe0811003u);
            if(!path)gba_a32_emit(e,0xe2811b01u); /* IWRAM page bias 1024 */
            gba_a32_emit(e,0xe1a02420u); /* normalized first page */
            gba_a32_emit(e,0xe7d13002u);gba_a32_emit(e,0xe3530000u);
            fail[nfail++]=e->next;gba_a32_emit(e,0x1a000000u);
            gba_a32_emit(e,0xe2802000u|((count-1)*4));gba_a32_emit(e,0xe1a02422u);
            gba_a32_emit(e,0xe7d13002u);gba_a32_emit(e,0xe3530000u);
            fail[nfail++]=e->next;gba_a32_emit(e,0x1a000000u);
        }
        gba_a32_load(e,1,4,path?offsetof(gba_t,memory.ewram):offsetof(gba_t,memory.iwram));
        gba_a32_emit(e,0xe0811000u);
        unsigned data=count*(path?6:1);
        gba_a32_constant(e,6,(data<<8)|load);
        gba_a32_emit(e,0xe2855000u|(data+load));
        if(!path){done=e->next;gba_a32_emit(e,0xea000000u);}
    }
    *done|=(uint32_t)(e->next-done-2)&0xffffff;
    unsigned offset=0;
    for(unsigned r=0;r<15;r++)if(list&(1u<<r)) {
        if(load) {
            unsigned host=e->map[r]?e->map[r]:3;
            gba_a32_load(e,host,1,offset);
            if(!e->map[r])gba_a32_guest_write(e,r,host);
        } else {
            if(r==rn && (op&0x200000) && offset)
                gba_a32_emit(e,(op&0x800000?0xe28e3000u:0xe24e3000u)|(count*4));
            else gba_a32_guest_read(e,3,r,0);
            gba_a32_store(e,3,1,offset);
        }
        offset+=4;
    }
    if((op&0x200000)&&(!load || !(list&(1u<<rn)))) {
        gba_a32_emit(e,(op&0x800000?0xe28e0000u:0xe24e0000u)|(count*4));
        gba_a32_guest_write(e,rn,0);
    }
    if(list&0x8000u) {
        /* ARMv4T LDM/POP PC preserves the instruction set. The final word
         * belongs to the span checked before the first register changed. */
        gba_a32_load(e,2,1,offset);
        gba_a32_emit(e,0xe3c22000u|(width==2?1u:3u));
        gba_a32_store(e,2,4,offsetof(gba_cpu_t,r)+15*4);
        gba_a32_emit(e,0xe3a03000u);
        const unsigned clear[]={offsetof(gba_cpu_t,pipeline_valid),offsetof(gba_cpu_t,fetch_sequential),
            offsetof(gba_t,machine.prefetch_count),offsetof(gba_t,machine.prefetch_phase)};
        for(unsigned n=0;n<sizeof(clear)/sizeof(clear[0]);n++) {
            if(clear[n]<=4095)gba_a32_store(e,3,4,clear[n]);
            else {gba_a32_constant(e,2,clear[n]);gba_a32_emit(e,0xe7843002u);}
        }
#ifdef GBA_TEST_INLINE_PC_PROOF
        extern uint32_t gba_test_inline_pc_entries;
        gba_a32_constant(e,2,(uint32_t)(uintptr_t)&gba_test_inline_pc_entries);
        gba_a32_emit(e,0xe5923000u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe5823000u);
#endif
    }
    gba_a32_emit(e,0xe59dc00cu);gba_a32_emit(e,0xe128f00cu);
    uint32_t *success=e->next;gba_a32_emit(e,0xea000000u);
    for(unsigned i=0;i<nfail;i++)*fail[i]|=(uint32_t)(e->next-fail[i]-2)&0xffffff;
    return success;
}
#endif
static void gba_a32_multiple(gba_a32_t *e, uint32_t op, unsigned index, unsigned resident_rom,unsigned width) {
    (void)width;
    gba_a32_emit(e,0xe10fc000u);gba_a32_emit(e,0xe58dc00cu);
#if GBA_DYNAREC_MULTI_INLINE
    uint32_t *inline_done=((op&0x100000)||GBA_DYNAREC_STORE_INLINE)&&(GBA_DYNAREC_MULTI_PC||!(op&0x8000))?gba_a32_multiple_load(e,op,width):NULL;
#endif
    gba_a32_cache_transfer(e,0);
    gba_a32_emit(e,0xe1a00004u);gba_a32_constant(e,1,op);
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)gba_native_multiple_ram);
    gba_a32_emit(e,0xe12fff3cu);
    gba_a32_emit(e,0xe3700001u);
    uint32_t *success=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe59dc00cu);gba_a32_emit(e,0xe128f00cu);gba_a32_emit(e,0xe59df008u);
    *success|=(uint32_t)(e->next-success-2)&0xffffff;
    gba_a32_emit(e,0xe1a01800u);gba_a32_emit(e,0xe1a01821u);gba_a32_emit(e,0xe0855001u);
    gba_a32_emit(e,0xe1a06820u);
    gba_a32_cache_transfer(e,1);
    gba_a32_emit(e,0xe59dc00cu);gba_a32_emit(e,0xe128f00cu);
#if GBA_DYNAREC_MULTI_INLINE
    if(inline_done)*inline_done|=(uint32_t)(e->next-inline_done-2)&0xffffff;
#endif
    if(resident_rom && !GBA_DYNAREC_FAST_BUS) {
        gba_a32_load(e,1,4,offsetof(gba_t,dynarec_memory.data));
        gba_a32_emit(e,0xe1a02426u);gba_a32_emit(e,0xe2063001u);gba_a32_emit(e,0xe1822403u);
        gba_a32_constant(e,3,offsetof(gba_native_cache_t,multiple_schedule)+index*2);
        gba_a32_emit(e,0xe18120b3u);
    }
}
#if GBA_DYNAREC_VIDEO_STORE
/* Only ordinary video memory: no IO, DMA drain or CPU/event callback. Device
 * observation is deferred by the explicit approximate block budget. Keep the
 * same byte-write quirks and cache dependencies as gba_bus_write(). */
static unsigned gba_native_video_store(gba_t *g,uint32_t address,uint32_t value,unsigned width) {
    unsigned region=address>>24,offset=address&(region==6?0x1ffffu:0x3ffu);
    unsigned cost=(region!=7 && width==4)?2:1;
    if(width==1) {
        if(region==7 || (region==6 && offset>=((gba_io16(g,0)&7)>=3?0x14000u:0x10000u)))return cost<<8;
        offset&=~1u;address&=~1u;width=2;value=(value&255)*0x101u;
    }
    if(region==6 && offset>=0x18000)offset-=0x8000;
    uint8_t *p=(region==5?g->memory.palette:region==6?g->memory.vram:g->memory.oam)+offset;
    if(width==4) {
        if(gba_le32(p)!=value)gba_video_write(g,region,address,4,value);
        gba_put32(p,value);
    } else {
        if(gba_le16(p)!=(uint16_t)value)gba_video_write(g,region,address,2,value);
        gba_put16(p,(uint16_t)value);
    }
#ifdef GBA_TEST_VIDEO_STORE_PROOF
    extern unsigned gba_test_video_store_entries;
    gba_test_video_store_entries++;
#endif
    return cost<<8;
}
#endif
static void gba_a32_memory_access(gba_a32_t *e, unsigned load, unsigned byte, unsigned half,
    unsigned allow_rom, uint16_t wait, uint32_t **fail, unsigned *fail_count) {
    gba_a32_emit(e, 0xe10fc000u); /* Save guest flags across address guards. */
    if (!byte) {
        gba_a32_emit(e, half ? 0xe3100001u : 0xe3100003u);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x1a000000u);
    }
    gba_a32_emit(e, 0xe1a02c20u); /* r2 = address >> 24 */
    gba_a32_emit(e, 0xe3520002u);
    uint32_t *ewram = e->next; gba_a32_emit(e, 0x0a000000u);
    gba_a32_emit(e, 0xe3520003u);
    uint32_t *rom = e->next; gba_a32_emit(e, 0x1a000000u);
    if (!load) {
        /* Writes to any page ever translated in this cache fall back before
         * mutation, including a store into the currently executing block. */
        gba_a32_load(e, 1, 4, offsetof(gba_t, dynarec_memory.data));
        gba_a32_constant(e, 3, offsetof(gba_native_cache_t, code_page));
        gba_a32_emit(e, 0xe0811003u);
        gba_a32_emit(e, 0xe1a03420u); /* r3 = address >> 8 */
        gba_a32_emit(e, 0xe203307fu);
        gba_a32_emit(e, 0xe2833b01u); /* +1024 pages */
        gba_a32_emit(e, 0xe0811003u);
        gba_a32_emit(e, 0xe5d13000u);
        gba_a32_emit(e, 0xe1a012a0u); /* address >> 5 selects one 32-byte code line */
        gba_a32_emit(e, 0xe2011007u);
        gba_a32_emit(e, 0xe1a03133u); /* MOV r3,r3,LSR r1 */
        gba_a32_emit(e, 0xe3130001u);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x1a000000u);
    }
    gba_a32_load(e, 1, 4, offsetof(gba_t, memory.iwram));
    gba_a32_emit(e, 0xe1a00880u); /* mask mirrored IWRAM to 15 address bits */
    gba_a32_emit(e, 0xe1a008a0u);
    if (!load) gba_a32_emit(e, 0xe59d2004u);
    gba_a32_emit(e, load ? (half ? 0xe19120b0u : byte ? 0xe7d12000u : 0xe7912000u) : (half ? 0xe18120b0u : byte ? 0xe7c12000u : 0xe7812000u));
    gba_a32_emit(e, 0xe3a06c01u);
    if (load) gba_a32_emit(e, 0xe2866001u);
    gba_a32_emit(e, load ? 0xe2855002u : 0xe2855001u);
    uint32_t *done = e->next; gba_a32_emit(e, 0xea000000u);
    *ewram |= (uint32_t)(e->next - ewram - 2) & 0xffffff;
    if (!load) {
        gba_a32_load(e, 1, 4, offsetof(gba_t, dynarec_memory.data));
        gba_a32_constant(e, 3, offsetof(gba_native_cache_t, code_page));
        gba_a32_emit(e, 0xe0811003u);
        gba_a32_emit(e, 0xe1a03700u);
        gba_a32_emit(e, 0xe1a03b23u); /* address low 18 bits >> 8 */
        gba_a32_emit(e, 0xe0811003u);
        gba_a32_emit(e, 0xe5d13000u);
        gba_a32_emit(e, 0xe1a012a0u); /* address >> 5 selects one 32-byte code line */
        gba_a32_emit(e, 0xe2011007u);
        gba_a32_emit(e, 0xe1a03133u); /* MOV r3,r3,LSR r1 */
        gba_a32_emit(e, 0xe3130001u);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x1a000000u);
    }
    gba_a32_load(e, 1, 4, offsetof(gba_t, memory.ewram));
    gba_a32_emit(e, 0xe1a00700u); /* mask mirrored EWRAM to 18 address bits */
    gba_a32_emit(e, 0xe1a00720u);
    if (!load) gba_a32_emit(e, 0xe59d2004u);
    gba_a32_emit(e, load ? (half ? 0xe19120b0u : byte ? 0xe7d12000u : 0xe7912000u) : (half ? 0xe18120b0u : byte ? 0xe7c12000u : 0xe7812000u));
    gba_a32_emit(e, (byte || half) ? 0xe3a06c03u : 0xe3a06c06u);
    if (load) gba_a32_emit(e, 0xe2866001u);
    gba_a32_emit(e, load ? ((byte || half) ? 0xe2855004u : 0xe2855007u) : ((byte || half) ? 0xe2855003u : 0xe2855006u));
    uint32_t *ram_done = e->next; gba_a32_emit(e, 0xea000000u);
    *rom |= (uint32_t)(e->next - rom - 2) & 0xffffff;
    uint32_t *io_done=NULL;
    if (load) {
        /* These registers are constant until the bounded PPU/input event.
         * No timer, sound, DMA, serial or open-bus MMIO is handled here. */
        gba_a32_emit(e,0xe3520004u);
        uint32_t *not_io=e->next;gba_a32_emit(e,0x1a000000u);
        gba_a32_emit(e,0xe3c004ffu); /* BIC r0,r0,#0xff000000 */
        gba_a32_emit(e,0xe3c03003u); /* aligned I/O word offset */
        gba_a32_emit(e,0xe3530004u);
        uint32_t *ppu=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_emit(e,0xe3530e13u); /* CMP r3,#0x130 */
        fail[(*fail_count)++]=e->next;gba_a32_emit(e,0x1a000000u);
        gba_a32_constant(e,1,0xc3ff03ffu);
        uint32_t *mask_ready=e->next;gba_a32_emit(e,0xea000000u);
        *ppu|=(uint32_t)(e->next-ppu-2)&0xffffff;
#if GBA_FAST_WAIT && !GBA_TEST_MODE
        gba_a32_emit(e,0xe3a01001u);
        gba_a32_constant(e,2,offsetof(gba_t,wait.ppu_read));
        gba_a32_emit(e,0xe7841002u);
#endif
        gba_a32_constant(e,1,0x00ffff3fu);
        *mask_ready|=(uint32_t)(e->next-mask_ready-2)&0xffffff;
        gba_a32_constant(e,2,offsetof(gba_t,io));
        gba_a32_emit(e,0xe0842002u);gba_a32_emit(e,0xe7922003u);
        gba_a32_emit(e,0xe0022001u);
        gba_a32_emit(e,0xe2003003u);gba_a32_emit(e,0xe1a03183u);
        gba_a32_emit(e,0xe1a02332u);
        if(byte)gba_a32_emit(e,0xe20220ffu);
        else if(half){gba_a32_emit(e,0xe1a02802u);gba_a32_emit(e,0xe1a02822u);}
        gba_a32_emit(e,0xe3a06c01u);gba_a32_emit(e,0xe2866001u);
        gba_a32_emit(e,0xe2855002u);
        io_done=e->next;gba_a32_emit(e,0xea000000u);
        /* Both RAM completion branches land at the shared flag restore. */
        *not_io|=(uint32_t)(e->next-not_io-2)&0xffffff;
    }
    uint32_t *video_done=NULL;
#if GBA_DYNAREC_VIDEO_STORE
    if(!load) {
        gba_a32_emit(e,0xe3520005u);fail[(*fail_count)++]=e->next;gba_a32_emit(e,0x3a000000u);
        gba_a32_emit(e,0xe3520007u);fail[(*fail_count)++]=e->next;gba_a32_emit(e,0x8a000000u);
        /* The shared store stub owns an aligned 8-byte frame: LR, value.
         * Save guest NZCV around the C-only video invalidation helper. Cached
         * guest registers and native counters are AAPCS callee-saved. */
        gba_a32_emit(e,0xe52dc008u);
        gba_a32_emit(e,0xe1a01000u);gba_a32_emit(e,0xe1a00004u);
        gba_a32_emit(e,0xe59d200cu);gba_a32_emit(e,0xe3a03000u|(byte?1:half?2:4));
        gba_a32_constant(e,12,(uint32_t)(uintptr_t)gba_native_video_store);gba_a32_emit(e,0xe12fff3cu);
        gba_a32_emit(e,0xe1a06000u);gba_a32_emit(e,0xe0855420u);
        gba_a32_emit(e,0xe49dc008u);
        video_done=e->next;gba_a32_emit(e,0xea000000u);
    } else
#endif
    if (!load || !allow_rom) {
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0xea000000u);
    } else {
        gba_a32_load(e,1,4,offsetof(gba_t,dynarec_memory.data));
        gba_a32_constant(e,3,offsetof(gba_native_cache_t,rom_data_seen));
        gba_a32_emit(e,0xe7913003u);gba_a32_emit(e,0xe3130002u);
        fail[(*fail_count)++]=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_emit(e, 0xe3520008u);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x3a000000u);
        gba_a32_emit(e, 0xe352000cu);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x8a000000u);
        gba_a32_emit(e, 0xe1a0e002u);
        gba_a32_emit(e, 0xe1a00380u); gba_a32_emit(e, 0xe1a003a0u);
        gba_a32_emit(e, 0xe3500a01u); /* Reject GPIO/RTC page conservatively. */
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x3a000000u);
        gba_a32_load(e, 1, 4, offsetof(gba_t, rom_size));
        gba_a32_emit(e, 0xe2411000u | (byte ? 1 : half ? 2 : 4));
        gba_a32_emit(e, 0xe1500001u);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x8a000000u);
        gba_a32_load(e, 1, 4, offsetof(gba_t, rom));
        gba_a32_emit(e, 0xe3510000u);
        fail[(*fail_count)++] = e->next; gba_a32_emit(e, 0x0a000000u);
        gba_a32_emit(e, half ? 0xe19120b0u : byte ? 0xe7d12000u : 0xe7912000u);
        static const unsigned first[4] = {4,3,2,8};
        uint32_t costs = 0;
        for (unsigned a = 0; a < 3; a++) {
            unsigned cost = first[(wait >> (2+a*3)) & 3] + 1;
            if (!byte && !half) cost += gba_gamepak_seq(wait, a);
            costs |= cost << (a*8);
        }
        gba_a32_constant(e, 1, costs);
        gba_a32_emit(e, 0xe24e3008u);
        gba_a32_emit(e, 0xe1a030a3u); gba_a32_emit(e, 0xe1a03183u);
        gba_a32_emit(e, 0xe1a06331u);
        gba_a32_emit(e, 0xe20660ffu);
        gba_a32_emit(e, 0xe0855006u); gba_a32_emit(e, 0xe2855001u);
        gba_a32_emit(e, 0xe1a06406u); gba_a32_emit(e, 0xe3866081u);
        gba_a32_load(e,1,4,offsetof(gba_t,dynarec_memory.data));
        gba_a32_constant(e,3,offsetof(gba_native_cache_t,rom_data_seen));
        gba_a32_emit(e,0xe0811003u);gba_a32_emit(e,0xe5913000u);
        gba_a32_emit(e,0xe3833001u);gba_a32_emit(e,0xe5813000u);
    }
    if(video_done)*video_done|=(uint32_t)(e->next-video_done-2)&0xffffff;
    if(io_done)*io_done|=(uint32_t)(e->next-io_done-2)&0xffffff;
    *done |= (uint32_t)(e->next - done - 2) & 0xffffff;
    *ram_done |= (uint32_t)(e->next - ram_done - 2) & 0xffffff;
    gba_a32_emit(e, 0xe128f00cu);
}
#if GBA_DYNAREC_STATS
static void gba_native_guard_fail(gba_t *g,uint32_t address,unsigned kind) {
    gba_native_cache_t *c=gba_native_cache(g);
    unsigned load=kind&1,byte=kind&2,half=kind&4,region=address>>24;
    if(!byte && (address&(half?1u:3u)))c->stats.guard_alignment++;
    else if(!load && (region==2 || region==3))c->stats.guard_code_write++;
    else c->stats.guard_other++;
}
#endif
static uint32_t *gba_native_memory_stub(gba_t *g, unsigned load, unsigned byte, unsigned half, unsigned allow_rom) {
    gba_native_cache_t *c = gba_native_cache(g);
    unsigned slot = (allow_rom ? 6 : 0) + (load ? 0 : 3) + (byte ? 1 : half ? 2 : 0);
    uint8_t *base = (uint8_t *)g->dynarec_memory.code;
    if (c->stub[slot]) return (uint32_t *)(base + c->stub[slot] - 1);
    uint32_t *code = (uint32_t *)(base + slot*672);
    if (gba_code_begin_write(code, 672)) { gba_fail(g, GBA_ERROR_IO); return NULL; }
    gba_a32_t e = {.begin=code,.next=code,.end=code+168};
    uint32_t *fail[16]; unsigned failures = 0;
    gba_a32_emit(&e, 0xe52de008u); /* helper return address; keep stack aligned */
    if (!load) gba_a32_emit(&e, 0xe58d2004u);
    gba_a32_memory_access(&e,load,byte,half,allow_rom,gba_io16(g,0x204),fail,&failures);
    gba_a32_emit(&e, 0xe49de008u); gba_a32_emit(&e, 0xe12fff1eu);
    uint32_t *failure = e.next;
#if GBA_DYNAREC_STATS
    gba_a32_emit(&e,0xe58dc004u); /* Saved guest flags survive the diagnostic C call. */
    gba_a32_emit(&e,0xe1a01000u);gba_a32_emit(&e,0xe1a00004u);
    gba_a32_constant(&e,2,load|(byte<<1)|(half<<2));
    gba_a32_constant(&e,12,(uint32_t)(uintptr_t)gba_native_guard_fail);
    gba_a32_emit(&e,0xe12fff3cu);gba_a32_emit(&e,0xe59dc004u);
#endif
    gba_a32_emit(&e, 0xe128f00cu);
    gba_a32_emit(&e, 0xe49de008u);
    gba_a32_emit(&e, 0xe59df008u); /* caller's exact-prefix failure epilogue */
    for (unsigned i=0;i<failures;i++) *fail[i] |= (uint32_t)(failure-fail[i]-2)&0xffffff;
    if (e.failed || gba_code_publish(code,(size_t)(e.next-code)*4)) {
        gba_fail(g, e.failed ? GBA_ERROR_MEMORY : GBA_ERROR_IO); return NULL;
    }
    c->stub[slot] = slot*672 + 1;
    return code;
}
static unsigned gba_native_literal_cost(const gba_t *g,uint32_t op,uint32_t pc,unsigned width) {
    if(width!=2 || (op&0xf800)!=0x4800 || (pc>>24)<8 || !g->rom || ((uintptr_t)g->rom&3u))return 0;
    uint32_t address=((pc+4)&~3u)+(op&255)*4, offset=address&0x1ffffff;
    if((address>>24)<8 || (address>>24)>12 || offset<0x1000 || offset>=g->rom_size || 4>g->rom_size-offset)return 0;
    static const unsigned first[4]={4,3,2,8};
    unsigned area=((address>>24)-8)/2, wait=gba_io16(g,0x204);
    return first[(wait>>(2+area*3))&3]+2+gba_gamepak_seq(wait,area);
}
/* Exact worst data/internal cost for the emitted memory guard. Stores never
 * reach ROM, and byte/halfword accesses never pay a second Game Pak beat. */
static unsigned gba_native_memory_bound(gba_t *g,uint32_t op,uint32_t pc,unsigned width) {
    unsigned literal=gba_native_literal_cost(g,op,pc,width);
    if(literal)return literal;
    if((pc>>24)<8)return 7; /* Existing IWRAM ROM-data guard adds its own margin. */
    unsigned word;
    if(width==4)word=!!(op&0x04000000u)&&!(op&0x400000u);
    else if((op&0xf000u)==0x5000u)word=((op>>9)&3)==0;
    else word=(op&0xf800u)==0x4800u || (op&0xf000u)==0x9000u || (op&0xf000u)==0x6000u;
    unsigned load=gba_native_is_load(op,width),bound=(word?6:3)+load;
    if(load) {
        static const unsigned first[4]={4,3,2,8};
        unsigned wait=gba_io16(g,0x204);
        for(unsigned area=0;area<3;area++) {
            unsigned cost=first[(wait>>(2+area*3))&3]+2+(word?gba_gamepak_seq(wait,area):0);
            if(cost>bound)bound=cost;
        }
    }
    return bound;
}
static void gba_a32_ram_read(gba_t *g, gba_a32_t *e, uint32_t op, uint32_t pc, unsigned width,
    unsigned index) {
    unsigned rd, rn, offset, byte = 0, half = 0, subtract = 0, load = gba_native_is_load(op,width);
    unsigned register_offset = 0, update = 0, pre = 1, signed_bits = 0;
    unsigned half_transfer=width==4 && !(op&0x04000000u);
    unsigned literal_cost=gba_native_literal_cost(g,op,pc,width);
    if(literal_cost) {
        uint32_t address=((pc+4)&~3u)+(op&255)*4, offset=address&0x1ffffff;
            unsigned cost=literal_cost-1;
            /* A PC-relative resident literal has a compile-time host address.
             * Load its live value; no region dispatch, mirroring or shared
             * helper call is needed. ROM is never copied into emitted code. */
            gba_a32_constant(e,0,(uint32_t)(uintptr_t)(g->rom+offset));
            gba_a32_emit(e,0xe5902000u);
            gba_a32_emit(e,0xe2855000u|(cost+1));
            gba_a32_constant(e,6,(cost<<8)|129u);
            gba_a32_guest_write(e,(op>>8)&7,2);
            gba_a32_load(e,1,4,offsetof(gba_t,dynarec_memory.data));
            gba_a32_constant(e,3,offsetof(gba_native_cache_t,rom_data_seen));
            gba_a32_emit(e,0xe7912003u);gba_a32_emit(e,0xe3822001u);gba_a32_emit(e,0xe7812003u);
            if(!GBA_DYNAREC_FAST_BUS) {
                gba_a32_constant(e,3,offsetof(gba_native_cache_t,schedule)+index);
                gba_a32_emit(e,0xe3a02000u|(cost|160));gba_a32_emit(e,0xe7c12003u);
            }
            return;
    }
    if (width == 4) {
        rd = (op >> 12) & 15; rn = (op >> 16) & 15; offset = op & 4095;
        byte = !!(op & 0x400000); subtract = !(op & 0x800000);
        pre = !!(op & 0x1000000); update = !pre || !!(op & 0x200000);
        register_offset = !!(op & 0x2000000);
        if (!(op & 0x04000000)) {
            unsigned sh=(op>>5)&3;
            half=sh!=2;byte=sh==2;signed_bits=sh>1?(sh==2?8:16):0;
            offset = ((op >> 4) & 0xf0) | (op & 15);
            register_offset = !(op & 0x400000);
        }
    } else if ((op & 0xf800) == 0x4800) {
        rd = (op >> 8) & 7; rn = 15; offset = (op & 255) * 4;
    } else if ((op & 0xf000) == 0x9000) {
        rd = (op >> 8) & 7; rn = 13; offset = (op & 255) * 4;
    } else if ((op & 0xf000) == 0x5000) {
        unsigned kind = (op >> 9) & 7;
        rd = op & 7; rn = (op >> 3) & 7; offset = (op >> 6) & 7; register_offset = 1;
        byte = kind == 2 || kind == 3 || kind == 6;
        half = kind == 1 || kind == 5 || kind == 7;
        signed_bits = kind == 3 ? 8 : kind == 7 ? 16 : 0;
    } else {
        rd = op & 7; rn = (op >> 3) & 7; byte = !!(op & 0x1000);
        offset = ((op >> 6) & 31) * (byte ? 1 : 4);
        if ((op & 0xf000) == 0x8000) { half = 1; byte = 0; offset = ((op >> 6) & 31) * 2; }
    }
    gba_a32_guest_read(e, 0, rn, width == 2 ? (pc + 4) & ~3u : pc + 8);
    unsigned destination = update ? 3 : 0;
    if (register_offset) {
        gba_a32_guest_read(e,1,width==4 ? op & 15 : offset,pc+(width==4?8:4));
        if (width==4 && !half_transfer)
            gba_a32_emit(e,0xe1a01001u|(op & 0xfe0u));
        gba_a32_emit(e,(subtract?0xe0400001u:0xe0800001u)|(destination<<12));
    } else if (offset) {
        if (offset <= 255) gba_a32_emit(e, (subtract ? 0xe2400000u : 0xe2800000u) | offset | (destination<<12));
        else {
            gba_a32_constant(e, 1, offset);
            gba_a32_emit(e, (subtract ? 0xe0400001u : 0xe0800001u) | (destination<<12));
        }
    } else if (update) gba_a32_emit(e,0xe1a03000u);
    if (update) {
        gba_a32_emit(e,0xe58d300cu);
        if (pre) gba_a32_emit(e,0xe1a00003u);
    }
    if (!load) gba_a32_guest_read(e,2,rd,pc+(width==4?12:4));
    uint32_t *stub = gba_native_memory_stub(g,load,byte,half,1);
    if (!stub) { e->failed=1; return; }
    uint32_t *odd_done=NULL;
    if(signed_bits==16) {
        uint32_t *byte_stub=gba_native_memory_stub(g,1,1,0,1);
        if(!byte_stub){e->failed=1;return;}
        gba_a32_emit(e,0xe10fc000u);gba_a32_emit(e,0xe3100001u);
        uint32_t *even=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_emit(e,0xe128f00cu);
        gba_a32_emit(e,0xeb000000u|((uint32_t)(byte_stub-e->next-2)&0xffffff));
        gba_a32_emit(e,0xe1a02c02u);gba_a32_emit(e,0xe1a02c42u);
        odd_done=e->next;gba_a32_emit(e,0xea000000u);
        *even|=(uint32_t)(e->next-even-2)&0xffffff;
        gba_a32_emit(e,0xe128f00cu);
    }
    uint32_t *rom_inline_done=NULL;
    unsigned hint=rn<15?g->machine.cpu.r[rn]>>24:0;
    if(load && (pc>>24)==3 && hint>=8 && hint<=12 && g->rom && !((uintptr_t)g->rom&3)) {
        /* Guard a commonly observed immutable data region locally. Failure
         * enters the general bus stub before mutation, so this hint never
         * changes guest semantics or relies on a game/address signature. */
        uint32_t *fallback[6];unsigned failures=0;
        gba_a32_emit(e,0xe10fc000u);
        gba_a32_emit(e,0xe1a01c20u);gba_a32_emit(e,0xe3510000u|hint);
        fallback[failures++]=e->next;gba_a32_emit(e,0x1a000000u);
        if(!byte) {
            gba_a32_emit(e,0xe3100000u|(half?1:3));
            fallback[failures++]=e->next;gba_a32_emit(e,0x1a000000u);
        }
        gba_a32_emit(e,0xe1a03380u);gba_a32_emit(e,0xe1a033a3u);
        gba_a32_emit(e,0xe3530a01u);
        fallback[failures++]=e->next;gba_a32_emit(e,0x3a000000u);
        gba_a32_constant(e,1,g->rom_size-(byte?1:half?2:4));gba_a32_emit(e,0xe1530001u);
        fallback[failures++]=e->next;gba_a32_emit(e,0x8a000000u);
        uintptr_t seen=(uintptr_t)&gba_native_cache(g)->rom_data_seen;
        gba_a32_constant(e,1,(uint32_t)seen);gba_a32_emit(e,0xe5911000u);gba_a32_emit(e,0xe3110002u);
        fallback[failures++]=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_constant(e,1,(uint32_t)(uintptr_t)g->rom);
        gba_a32_emit(e,half?0xe19120b3u:byte?0xe7d12003u:0xe7912003u);
        unsigned area=(hint-8)/2;static const unsigned first[4]={4,3,2,8};
        uint16_t wait=gba_io16(g,0x204);
        unsigned cost=first[(wait>>(2+area*3))&3]+1+((byte||half)?0:gba_gamepak_seq(wait,area));
        gba_a32_emit(e,0xe2855000u|(cost+1));gba_a32_constant(e,6,(cost<<8)|129u);
        gba_a32_constant(e,1,(uint32_t)seen);gba_a32_emit(e,0xe5913000u);
        gba_a32_emit(e,0xe3833001u);gba_a32_emit(e,0xe5813000u);
        gba_a32_emit(e,0xe128f00cu);
        rom_inline_done=e->next;gba_a32_emit(e,0xea000000u);
        for(unsigned n=0;n<failures;n++)*fallback[n]|=(uint32_t)(e->next-fallback[n]-2)&0xffffff;
        gba_a32_emit(e,0xe128f00cu);
    }
#if GBA_DYNAREC_RAM_INLINE
    uint32_t *ram_inline_done=NULL;
    if(load && (hint==2 || hint==3)) {
        /* The observed region only selects a guarded fast path. Base registers
         * may change within a block; verify the computed effective address. */
        uint32_t *fallback[2];unsigned failures=0;
        gba_a32_emit(e,0xe10fc000u);
        gba_a32_emit(e,0xe1a01c20u);gba_a32_emit(e,0xe3510000u|hint);
        fallback[failures++]=e->next;gba_a32_emit(e,0x1a000000u);
        if(!byte) {
            gba_a32_emit(e,0xe3100000u|(half?1:3));
            fallback[failures++]=e->next;gba_a32_emit(e,0x1a000000u);
        }
        gba_a32_load(e,1,4,hint==2?offsetof(gba_t,memory.ewram):offsetof(gba_t,memory.iwram));
        unsigned shift=hint==2?14:17;
        gba_a32_emit(e,0xe1a00000u|(shift<<7));gba_a32_emit(e,0xe1a00020u|(shift<<7));
        gba_a32_emit(e,half?0xe19120b0u:byte?0xe7d12000u:0xe7912000u);
        unsigned cost=hint==3?1:(byte||half)?3:6;
        gba_a32_emit(e,0xe2855000u|(cost+1));gba_a32_constant(e,6,(cost<<8)|1u);
        gba_a32_emit(e,0xe128f00cu);
        ram_inline_done=e->next;gba_a32_emit(e,0xea000000u);
        for(unsigned n=0;n<failures;n++)*fallback[n]|=(uint32_t)(e->next-fallback[n]-2)&0xffffff;
        gba_a32_emit(e,0xe128f00cu);
    }
#endif
    gba_a32_emit(e, 0xeb000000u | ((uint32_t)(stub-e->next-2)&0xffffff));
#if GBA_DYNAREC_RAM_INLINE
    if(ram_inline_done)*ram_inline_done|=(uint32_t)(e->next-ram_inline_done-2)&0xffffff;
#endif
    if(rom_inline_done)*rom_inline_done|=(uint32_t)(e->next-rom_inline_done-2)&0xffffff;
    if (signed_bits) {
        gba_a32_emit(e,0xe1a02002u|((32-signed_bits)<<7));
        gba_a32_emit(e,0xe1a02042u|((32-signed_bits)<<7));
    }
    if(odd_done)*odd_done|=(uint32_t)(e->next-odd_done-2)&0xffffff;
    if (update && (!load || rn != rd)) {
        gba_a32_emit(e,0xe59d300cu); gba_a32_guest_write(e,rn,3);
    }
    if (load) gba_a32_guest_write(e, rd, 2);
    if ((pc >> 24) < 8 || GBA_DYNAREC_FAST_BUS) return; /* No per-access ROM schedule in approximate mode. */
    gba_a32_load(e, 1, 4, offsetof(gba_t, dynarec_memory.data));
    gba_a32_emit(e, 0xe1a02426u); /* last data wait >> 8 */
    if (load) gba_a32_emit(e, 0xe3822020u);
    gba_a32_emit(e, 0xe2063080u); gba_a32_emit(e, 0xe1822003u);
    gba_a32_constant(e, 3, offsetof(gba_native_cache_t, schedule) + index);
    gba_a32_emit(e, 0xe7c12003u);
}
static uint32_t gba_native_word(gba_t *g, uint32_t address, unsigned width) {
    const uint8_t *p;
    if ((address >> 24) == 2)
        p = g->memory.ewram + (address & 0x3ffff);
    else if ((address >> 24) == 3)
        p = g->memory.iwram + (address & 0x7fff);
    else
        p = g->rom + (address & 0x1ffffff);
    return width == 2 ? gba_le16(p) : gba_le32(p);
}
static gba_native_block_t *gba_native_find(
    gba_t *g, uint32_t pc, uint32_t head0, uint32_t head1, unsigned width, unsigned span) {
    gba_native_cache_t *c = gba_native_cache(g);
    unsigned page = gba_native_page(pc);
    uint32_t generation = page < GBA_JIT_PAGES ? c->generation[page] : 0;
    unsigned key = (width == 2 ? 1u : 0u) | ((span & 511u) << 1);
#if GBA_DYNAREC_TRACE_CONTEXT
    unsigned context_reg=gba_native_context_register(pc,head0,width);
    uint32_t context=context_reg<15?g->machine.cpu.r[context_reg]:0;
#endif
    gba_native_block_t *hot = c->recent[gba_native_recent_index(pc)];
    if (hot && hot->epoch == c->epoch && hot->pc == pc && hot->cache_key == key &&
        hot->head[0] == head0 && hot->head[1] == head1 && hot->page_generation == generation && hot->count &&
#if GBA_DYNAREC_TRACE_CONTEXT
        (context_reg==15 || hot->prefix_offset==context) &&
#endif
        (hot->end_page>=GBA_JIT_PAGES || hot->end_generation==c->generation[hot->end_page])) {
        GBA_NATIVE_COUNT(c, cache_hits, 1); return hot;
    }
    uint32_t hash = (pc >> 1) ^ (pc >> 10) ^ (pc >> 19) ^ (key * 173u);
#if GBA_DYNAREC_TRACE_CONTEXT
    if(context_reg<15)hash^=(context>>1)^(context>>11);
#endif
    hash ^= hash >> 8;
    unsigned set = hash & (c->slots / 2 - 1);
    gba_native_block_t *ways = gba_native_blocks(c) + set * 2;
    for (unsigned way = 0; way < 2; way++) {
        gba_native_block_t *candidate = &ways[way];
        /* A data write anywhere in a 256-byte RAM page changes its generation.
         * Reuse the translation when its exact instruction bytes still match;
         * stack/working data sharing a code page must not force recompilation. */
        if (page < GBA_JIT_PAGES && candidate->epoch == c->epoch && candidate->pc == pc &&
            candidate->cache_key == key && candidate->count &&
            candidate->head[0] == head0 && candidate->head[1] == head1 &&
            (candidate->page_generation != generation ||
             (candidate->end_page<GBA_JIT_PAGES && candidate->end_generation!=c->generation[candidate->end_page]))) {
            const uint8_t *live = (pc >> 24) == 2 ? g->memory.ewram + (pc & 0x3ffff) :
                g->memory.iwram + (pc & 0x7fff);
            const uint8_t *saved = (const uint8_t *)g->dynarec_memory.code + candidate->offset + (candidate->raw_words&2047u) * 4;
            unsigned equal = 1;
            for (unsigned i = 0; i < candidate->count * width; i++)
                if (live[i] != saved[i]) { equal = 0; break; }
            if (equal) {
                candidate->page_generation = generation;
                if(candidate->end_page<GBA_JIT_PAGES)candidate->end_generation=c->generation[candidate->end_page];
            }
        }
        if (candidate->epoch == c->epoch && candidate->pc == pc && candidate->cache_key == key &&
            candidate->head[0] == head0 && candidate->head[1] == head1 && candidate->page_generation == generation &&
            candidate->count &&
#if GBA_DYNAREC_TRACE_CONTEXT
            (context_reg==15 || candidate->prefix_offset==context) &&
#endif
            (candidate->end_page>=GBA_JIT_PAGES || candidate->end_generation==c->generation[candidate->end_page])) {
            c->victim[set] = (uint8_t)(way ^ 1u);
            GBA_NATIVE_COUNT(c, cache_hits, 1);
            c->hot = candidate;
            if(!(span&256))gba_native_recent_publish(c,pc,candidate);
            return candidate;
        }
    }
    return NULL;
}
#if GBA_DYNAREC_NEGATIVE
static void gba_native_reject_decode(gba_t *g,uint32_t pc,unsigned width,unsigned span) {
    gba_native_cache_t *c=gba_native_cache(g);unsigned key=(width==2)|(span<<1);
    uint32_t hash=(pc>>1)^(pc>>10)^(pc>>19)^(key*173u);hash^=hash>>8;
    unsigned set=hash&(c->slots/2-1);gba_native_block_t *ways=gba_native_blocks(c)+set*2;
    unsigned replace=c->victim[set];
    if(ways[0].epoch!=c->epoch || !ways[0].count)replace=0;
    else if(ways[1].epoch!=c->epoch || !ways[1].count)replace=1;
    gba_native_block_t *b=&ways[replace];
    b->pc=pc;b->head[0]=g->machine.cpu.pipeline[0];b->head[1]=g->machine.cpu.pipeline[1];
    b->epoch=c->epoch;b->cache_key=(uint16_t)key;b->count=0;
    c->victim[set]=(uint8_t)(replace^1);gba_native_recent_publish(c,pc,b);
}
#endif
#if GBA_DYNAREC_INTERIOR
static gba_native_block_t *gba_native_interior(gba_t *g,uint32_t pc,unsigned width,unsigned span,uint32_t budget) {
    if((pc>>24)<8 || (pc>>24)>12 || span!=32)return NULL;
    gba_native_cache_t *c=gba_native_cache(g);
    for(unsigned slot=0;slot<64;slot++) {
        const gba_native_block_t *b=c->recent[slot];
        if(!b || b->epoch!=c->epoch || b->cache_key!=(64u|(width==2)) || (b->flags&32) ||
            b->pc>=pc || pc>=b->pc+b->count*width || ((pc-b->pc)&(width-1)))continue;
        unsigned index=(pc-b->pc)/width,count=b->count-index;
        if(count<2)continue;
        const uint8_t *live=g->rom+(pc&0x1ffffff);
        uint32_t h0=width==2?gba_le16(live):gba_le32(live);
        uint32_t h1=width==2?gba_le16(live+width):gba_le32(live+width);
        if(h0!=g->machine.cpu.pipeline[0] || h1!=g->machine.cpu.pipeline[1])continue;
        const uint8_t *table=(const uint8_t *)g->dynarec_memory.code+b->offset+
            (b->raw_words&2047u)*4+b->count*width+4+index*5;
        gba_native_block_t *active=&c->interior_active;
        *active=*b;active->pc=pc;active->head[0]=h0;active->head[1]=h1;active->count=(uint8_t)count;
        active->memory_mask>>=index;active->multiple_mask>>=index;
        active->extra_bound-=(uint16_t)gba_le16(table+3);
        uint16_t wait=gba_io16(g,0x204);unsigned area=((pc>>24)-8)/2;
        unsigned seq=gba_gamepak_seq(wait,area)*(width/2);
        static const unsigned first[4]={4,3,2,8};
        unsigned nonseq=first[(wait>>(2+area*3))&3]+1+(width==4?seq/2:0);
        uint32_t bound=(count-1)*seq+(g->machine.cpu.fetch_sequential?seq:nonseq);
        uint32_t accesses=active->memory_mask&((1u<<(count-1))-1);
        if(nonseq>seq)for(;accesses;accesses&=accesses-1)bound+=nonseq-seq;
        if(bound+active->extra_bound>=budget)continue;
        active->fetch_bound_seq=bound;active->fetch_bound_nonseq=bound;
        active->cache_key|=(uint16_t)(1024u|(index<<11));
        c->resume_ip=(uint32_t)(uintptr_t)g->dynarec_memory.code+b->offset+gba_le16(table)*4;
        c->resume_seed=(256u-table[2])&255;
        c->resume_failure=gba_le32(table-index*5-4);
        c->interior_entries++;
        return active;
    }
    return NULL;
}
#endif
static __attribute__((noinline,cold)) gba_native_block_t *gba_native_compile(
    gba_t *g, uint32_t pc, uint32_t head0, uint32_t head1, unsigned width, unsigned span, gba_native_block_t *scratch) {
    gba_native_cache_t *c = gba_native_cache(g);
    unsigned page = gba_native_page(pc);
    uint32_t generation = page < GBA_JIT_PAGES ? c->generation[page] : 0;
    unsigned key = (width == 2 ? 1u : 0u) | ((span & 511u) << 1);
    unsigned bounded=!!(span&256),attached=!!(span&768);span&=255;
    uint32_t hash = (pc >> 1) ^ (pc >> 10) ^ (pc >> 19) ^ (key * 173u);
#if GBA_DYNAREC_TRACE_CONTEXT
    unsigned context_reg=gba_native_context_register(pc,head0,width);
    uint32_t context=context_reg<15?g->machine.cpu.r[context_reg]:0;
    if(context_reg<15)hash^=(context>>1)^(context>>11);
#endif
    hash ^= hash >> 8;
    unsigned set = hash & (c->slots / 2 - 1);
    gba_native_block_t *ways = gba_native_blocks(c) + set * 2;
    unsigned replace = c->victim[set];
    if (ways[0].epoch != c->epoch || !ways[0].count)
        replace = 0;
    else if (ways[1].epoch != c->epoch || !ways[1].count)
        replace = 1;
    gba_native_block_t *b = attached?scratch:&ways[replace];
    if(!attached)c->victim[set] = (uint8_t)(replace ^ 1u);
    unsigned reserve=GBA_JIT_RESERVE;
    if (g->dynarec_memory.code_size - c->used < reserve) {
        if(attached)return NULL; /* A cold prefix must not evict hot normal code. */
        gba_memset(gba_native_blocks(c), 0, c->slots * sizeof(*b));
        gba_memset(c->code_page,0,sizeof(c->code_page));
#if GBA_DYNAREC_PAGE_INDEX
        gba_memset(c->legacy_code_page,0,sizeof(c->legacy_code_page));
#endif
        c->used = c->code_base;
#if GBA_DYNAREC_PERSISTENT_ENGINE
        c->persistent_reset=1;
#endif
        c->hot = NULL;
        GBA_NATIVE_COUNT(c, flushes, 1);
    }
#if GBA_DYNAREC_CHAIN
    if(gba_native_chain_stub(g)){gba_fail(g,GBA_ERROR_IO);return NULL;}
#endif
    uint32_t *code = (uint32_t *)((uint8_t *)g->dynarec_memory.code + c->used);
    if (gba_code_begin_write(code, reserve)) {
        gba_fail(g, GBA_ERROR_IO);
        return NULL;
    }
    /* Keep the exact guest bytes next to the generated code, outside its entry.
     * No digest collision can let stale self-modifying code execute. */
    unsigned snapshots=GBA_DYNAREC_ROM_TRACE && (pc>>24)>=8?1024:384;
    gba_a32_t e = {.begin=code,.next=code,.end=code+(reserve-snapshots)/4,.bounded=bounded};
    uint32_t *budget_exits[64];unsigned budget_exit_count=0;
#if GBA_DYNAREC_INTERIOR
    e.resumable=!attached && (pc>>24)>=8;
    uint16_t entry_words[64],entry_extra[64];uint8_t entry_pending[64];
#endif
#if GBA_DYNAREC_CHAIN
    if(!attached && ((pc>>24)==3 || (GBA_DYNAREC_ROM_CHAIN && (pc>>24)>=8)))e.chain=(uint32_t)(uintptr_t)((uint8_t *)g->dynarec_memory.code+c->chain_stub);
    if(!c->chain_stub)e.chain=0;
#endif
    unsigned score[15] = {0};
    for (unsigned i = 0; i < (span ? span : 1u); i++) {
        if (((pc + (i + 2) * width) >> 8) > (pc >> 8)+((pc>>24)<8)) break;
        if ((pc >> 24) >= 8 && (pc & 0x1ffffff) + (i + 3) * width > g->rom_size) break;
        uint32_t raw = i == 0 ? head0 : i == 1 ? head1 : gba_native_word(g, pc + i * width, width), arm;
        unsigned kind = gba_native_decode(raw, width, &arm);
        if(gba_native_ram_read(raw,width)) {
            if(width==4) {
                unsigned rn=(raw>>16)&15,rd=(raw>>12)&15;
                if(rn<15)score[rn]+=2;
                if(rd<15)score[rd]++;
                unsigned reg_offset=(raw&0x04000000)?!!(raw&0x02000000):!(raw&0x400000);
                if(reg_offset && (raw&15)<15)score[raw&15]++;
            } else {
                unsigned rd=(raw&0xf800)==0x4800 || (raw&0xf000)==0x9000?(raw>>8)&7:raw&7;
                score[rd]++;
                if((raw&0xf800)!=0x4800 && (raw&0xf000)!=0x9000)score[(raw>>3)&7]+=2;
                if((raw&0xf000)==0x5000)score[(raw>>6)&7]++;
            }
            continue;
        }
        uint32_t multiple=gba_native_multiple(raw,width);
        if(multiple) {
            unsigned rn=(multiple>>16)&15;if(rn<15)score[rn]+=2;
            for(unsigned r=0;r<15;r++)if(multiple&(1u<<r))score[r]++;
            if(multiple&0x8000)break;
            continue;
        }
        if (kind != GBA_JIT_ALU && kind != GBA_JIT_MUL) break;
        unsigned fields[] = {(arm >> 12) & 15, (arm >> 16) & 15, arm & 15, (arm >> 8) & 15};
        unsigned count_fields=(arm&0x02000000)?2:(!(arm&16)?3:4);
        for (unsigned n = 0; n < count_fields; n++)
            if (fields[n] < 15) score[fields[n]]++;
    }
    unsigned best_window=0,best_score=0;
    for(unsigned base=0;base<12;base+=4) {
        unsigned sum=score[base]+score[base+1]+score[base+2]+score[base+3];
        if(sum>best_score){best_score=sum;best_window=base;}
    }
    unsigned selected=0,top_score=0;
    for(unsigned n=0;n<4;n++) {
        unsigned best=15;
        for(unsigned r=0;r<15;r++)if(!(selected&(1u<<r)) && (best==15 || score[r]>score[best]))best=r;
        e.cached_guest[n]=best;selected|=1u<<best;top_score+=score[best];
    }
    e.cached_base=top_score>best_score+4?15:best_window;
#if GBA_DYNAREC_FIXED_MAP
    /* Stable mappings trade a few high-register loads for unchanged native
     * register ownership across blocks. Both modes keep r0/r1 resident. */
    static const unsigned fixed[2][4]={{0,1,2,3},{0,1,6,14}};
    e.cached_base=GBA_DYNAREC_FIXED_MAP==1 || width==2?0:15;
#endif
    for(unsigned n=0;n<4;n++) {
#if GBA_DYNAREC_FIXED_MAP
        e.cached_guest[n]=fixed[GBA_DYNAREC_FIXED_MAP==2 && width==4][n];
#else
        if(e.cached_base<15)e.cached_guest[n]=e.cached_base+n;
#endif
        e.map[e.cached_guest[n]]=8+n;
    }
    unsigned count = 0, last_internal = 0, any_internal = 0, reads = 0, loop_count = 0, writes = 0, pending_count = 0, extra_bound = 0, any_multiple = 0,trace_pages=0;
    unsigned traced_edges=0;
    (void)trace_pages;
#if GBA_DYNAREC_TRACE_LINKS
    uint32_t source_pc[121],next_source_pc=pc;
    uint32_t trace_returns[16];unsigned trace_depth=0;
    unsigned linked_trace=(pc>>24)>=8 && span;
#endif
    uint32_t loop_cycles=0;
    unsigned side_exits=0;
    uint32_t memory_mask = 0, multiple_mask = 0;
    uint32_t *loop_body = NULL;
    uint32_t *failure_literal = NULL;
    for (unsigned i = 0; i < (span ? span : 1u); i++) {
        uint32_t op_pc=pc+i*width;
#if GBA_DYNAREC_TRACE_LINKS
        if(linked_trace)op_pc=next_source_pc;
#endif
        if ((size_t)(e.end - e.next) < 256) break;
        if (count && extra_bound>256 && !GBA_DYNAREC_WIDE_ROM) break;
        if ((pc>>24)>=8 && reads && count >= 16 && !GBA_DYNAREC_WIDE_ROM) break;
        if (span && ((op_pc + 2 * width) >> 8) > (pc >> 8)+((pc>>24)<8) && !(GBA_DYNAREC_ROM_TRACE && (pc>>24)>=8))
            break;
        if((pc>>24)<8 && (((op_pc+3*width-1)&((pc>>24)==3?0x7fff:0x3ffff)) < (pc&((pc>>24)==3?0x7fff:0x3ffff))))break;
        if (span && (pc >> 24) >= 8) {
            size_t offset = (pc & 0x1ffffff) + i*width;
#if GBA_DYNAREC_TRACE_LINKS
            if(linked_trace)offset=op_pc&0x1ffffff;
#endif
            if (offset + 3 * width > g->rom_size || (offset < 0xca && offset + 3 * width > 0xc4))
                break;
            if((op_pc>>24)!=(pc>>24) || ((op_pc+3*width-1)>>24)!=(pc>>24))break;
        }
        uint32_t op = i == 0 ? head0 : i == 1 ? head1 : gba_native_word(g, op_pc, width);
#if GBA_DYNAREC_TRACE_LINKS
        if(linked_trace && i)op=gba_native_word(g,op_pc,width);
#endif
#if GBA_DYNAREC_ROM_TRACE
        if((pc>>24)>=8 && i>1 && !((op_pc)&255)) {
            if(loop_count || count>=224 || extra_bound+count*gba_native_fetch_floor(gba_io16(g,0x204),width,pc>>24)>GBA_DYNAREC_EVENT_SLIP-128)break;
            trace_pages=1;
        }
#endif
        uint32_t arm = 0;
        unsigned kind = gba_native_decode(op, width, &arm);
        if (span && gba_native_ram_read(op, width)) kind = GBA_JIT_RAM_READ;
        if (span && gba_native_multiple(op,width)) kind=GBA_JIT_MULTIPLE;
        if (!kind || (span && kind != GBA_JIT_ALU && kind != GBA_JIT_MUL && kind != GBA_JIT_BRANCH && kind != GBA_JIT_RAM_READ && kind != GBA_JIT_MULTIPLE))
            break;
        uint32_t trace_target=0;unsigned trace_refill=0;
#if GBA_DYNAREC_TRACE_LINKS
        unsigned trace_return=0,trace_call=0,trace_guard_reg=14;
        if(linked_trace && kind==GBA_JIT_BRANCH) {
            if(width==4 && (op&0xfe000000u)==0xea000000u) {
                trace_target=op_pc+8+(uint32_t)(gba_sign(op&0xffffff,24)*4);
                trace_call=!!(op&0x01000000u);
            }
            else if(width==2 && (op&0xf800u)==0xe000u)
                trace_target=op_pc+4+(uint32_t)(gba_sign(op&0x7ff,11)*2);
            else if(width==2 && (op&0xf800u)==0xf800u && count && source_pc[count-1]+2==op_pc) {
                uint32_t high=gba_native_word(g,source_pc[count-1],2);
                if((high&0xf800u)==0xf000u) {
                    trace_target=source_pc[count-1]+4+(uint32_t)(gba_sign(high&0x7ff,11)*4096)+(op&0x7ff)*2;
                    trace_call=1;
                }
            }
#if GBA_DYNAREC_TRACE_CONTEXT
            else if(!count && context_reg<15 && (context&1)==(width==2)) {
                trace_return=context;trace_target=context&~(width-1u);trace_guard_reg=context_reg;
                if(context_reg!=14 && ((g->machine.cpu.r[14]&1)==(width==2)) && trace_depth<16)
                    trace_returns[trace_depth++]=g->machine.cpu.r[14];
            }
#endif
            else if(trace_depth && ((width==4 && op==0xe12fff1eu)||(width==2 && op==0x4770u))) {
                trace_return=trace_returns[trace_depth-1];trace_target=trace_return&~1u;
            }
            size_t offset=trace_target&0x1ffffff;
            if((trace_target>>24)!=(pc>>24) || ((trace_target+3*width-1)>>24)!=(pc>>24) || offset<0x1000 || offset>=g->rom_size || 3*width>g->rom_size-offset)trace_target=0;
            if(trace_call && trace_depth==16)trace_target=0;
            if(trace_target)trace_refill=gba_gamepak_refill_cost(gba_io16(g,0x204),width,pc>>24);
        }
#endif
#if GBA_DYNAREC_ROM_TRACE
        if((pc>>24)>=8 && count>1) {
            unsigned additional=trace_refill;
            if(kind==GBA_JIT_RAM_READ)additional=gba_native_memory_bound(g,op,op_pc,width);
            else if(kind==GBA_JIT_MULTIPLE) {
                unsigned multiple=gba_native_multiple(op,width);
                additional=(unsigned)__builtin_popcount(multiple&0xffff)*6+!!(multiple&0x100000);
            } else if(kind==GBA_JIT_MUL)additional=5;
            else if(kind==GBA_JIT_ALU && !(arm&0x02000000) && (arm&16))additional=1;
            if((count+1)*gba_native_fetch_floor(gba_io16(g,0x204),width,pc>>24)+extra_bound+additional>=GBA_DYNAREC_EVENT_SLIP-48)break;
        }
#endif
        if((pc>>24)<8 && (((op_pc+3*width-1)&((pc>>24)==3?0x7fff:0x3ffff)) < (pc&((pc>>24)==3?0x7fff:0x3ffff))))break;
#if GBA_DYNAREC_TRACE_LINKS
        if(linked_trace) {source_pc[count]=op_pc;next_source_pc=op_pc+width;}
#endif
        if (kind == GBA_JIT_ARM_MEMORY || kind == GBA_JIT_THUMB_MEMORY) {
            gba_a32_emit(&e, 0xe92d4010u);
            gba_a32_emit(&e, 0xe1a04000u);
            unsigned condition = width == 4 ? op >> 28 : 14;
            uint32_t *skip = NULL;
            if (condition != 14) {
                gba_a32_load(&e, 12, 4, offsetof(gba_cpu_t, cpsr));
                gba_a32_emit(&e, 0xe128f00cu);
                skip = e.next;
                gba_a32_emit(&e, ((condition ^ 1u) << 28) | 0x0a000000u);
            }
            gba_a32_constant(&e, 1, op);
            gba_a32_constant(&e, 2, pc);
            uintptr_t helper =
                kind == GBA_JIT_ARM_MEMORY ? (uintptr_t)gba_cpu_arm_memory : (uintptr_t)gba_cpu_thumb_memory;
            gba_a32_constant(&e, 12, (uint32_t)helper);
            gba_a32_emit(&e, 0xe12fff3cu);
            gba_a32_emit(&e, 0xe8bd8010u);
            if (skip) {
                *skip |= (uint32_t)(e.next - skip - 2) & 0xffffff;
                gba_a32_emit(&e, 0xe3a00000u);
                gba_a32_emit(&e, 0xe8bd8010u);
            }
            count = 1;
            break;
        }
        if (!count) {
            gba_a32_prologue(&e);
            loop_body = e.next;
        }
#if GBA_DYNAREC_INTERIOR
        entry_words[i]=(uint16_t)(e.next-code);entry_extra[i]=(uint16_t)extra_bound;
        entry_pending[i]=(uint8_t)pending_count;
#endif
        if(bounded) {
            /* This separately cached entry is used only when the full block
             * cannot fit. Commit a prefix strictly before a CPU event. */
            if(pending_count){gba_a32_emit(&e,0xe2877000u|pending_count);pending_count=0;}
            unsigned bound=1;
            if(kind==GBA_JIT_RAM_READ)bound+=19;
            else if(kind==GBA_JIT_MULTIPLE) {
                uint32_t bits=gba_native_multiple(op,width)&0xffff;unsigned n=0;
                for(;bits;bits&=bits-1)n++;
                bound+=6*n+1;
            } else if(kind==GBA_JIT_MUL || ((kind==GBA_JIT_ALU ||
                (kind==GBA_JIT_BRANCH && width==4 && !(op&0x0c000000u) &&
                 (op&0x0ffffff0u)!=0x012fff10u)) && !(arm&0x02000000u) && (arm&16)))bound+=5;
            gba_a32_emit(&e,0xe10fc000u); /* preserve guest NZCV */
            gba_a32_emit(&e,0xe20700ffu); /* completed instruction fetches */
            gba_a32_emit(&e,0xe0800005u); /* actual data/internal cycles */
            gba_a32_emit(&e,0xe59d1004u); /* deferred refill, if any */
            gba_a32_emit(&e,0xe0800001u);
            gba_a32_emit(&e,0xe2800000u|bound);
            gba_a32_emit(&e,0xe59d1010u);
            gba_a32_emit(&e,0xe1500001u);
            budget_exits[budget_exit_count++]=e.next;gba_a32_emit(&e,0x2a000000u);
            gba_a32_emit(&e,0xe128f00cu);
        }
        if (kind == GBA_JIT_RAM_READ || kind == GBA_JIT_MULTIPLE) {
            if (pending_count) { gba_a32_emit(&e,0xe2877000u|pending_count); pending_count=0; }
            if (!failure_literal) {
                gba_a32_literal(&e,0,0); failure_literal=e.next-1;
                gba_a32_emit(&e,0xe58d0008u);
            }
            uint32_t *condition_skip=NULL;
            if(width==4 && (op>>28)!=14) {
                condition_skip=e.next;
                gba_a32_emit(&e,(((op>>28)^1u)<<28)|0x0a000000u);
            }
            uint32_t multiple=kind==GBA_JIT_MULTIPLE?gba_native_multiple(op,width):0;
            unsigned load = multiple?!!(multiple&0x100000):gba_native_is_load(op,width);
            writes |= !load;
            /* A guard failure returns the committed prefix, including the
             * previous instruction's last-internal-cycle marker. */
            if (!count || !last_internal) gba_a32_emit(&e, 0xe3a06000u);
            if (kind==GBA_JIT_MULTIPLE) {
                unsigned n=0;for(uint32_t bits=multiple&0xffff;bits;bits&=bits-1)n++;
                extra_bound+=n*6+load;
                gba_a32_multiple(&e,multiple,i,(pc>>24)>=8,width);
                if((pc>>24)>=8 && !GBA_DYNAREC_FAST_BUS)multiple_mask|=1u<<i;
                any_multiple=1;
            } else {
                extra_bound+=gba_native_memory_bound(g,op,op_pc,width);
                gba_a32_ram_read(g, &e, op, op_pc, width, i);
            }
            if((pc>>24)>=8) {if(!GBA_DYNAREC_FAST_BUS)memory_mask |= 1u << i;}
            else if(kind!=GBA_JIT_MULTIPLE)memory_mask+=12;
#if GBA_FAST_WAIT && !GBA_TEST_MODE
            if(!load)gba_a32_emit(&e,0xe3877801u); /* actual write, status bit16 */
#endif
            gba_a32_emit(&e, 0xe2877c01u); /* one completed memory operation */
            gba_a32_emit(&e, 0xe2877001u); /* one completed instruction */
            if (!count) {
                gba_a32_emit(&e, 0xe59d0004u); gba_a32_emit(&e, 0xe0855000u);
                gba_a32_emit(&e, 0xe3a00000u); gba_a32_emit(&e, 0xe58d0004u);
            }
            if(condition_skip) {
                uint32_t *done=e.next;gba_a32_emit(&e,0xea000000u);
                *condition_skip|=(uint32_t)(e.next-condition_skip-2)&0xffffff;
                gba_a32_emit(&e,0xe3a06000u); /* Failed condition has no data/internal cycle. */
                gba_a32_emit(&e,0xe2877001u);
                if(!count) {
                    gba_a32_emit(&e,0xe59d0004u);gba_a32_emit(&e,0xe0855000u);
                    gba_a32_emit(&e,0xe3a00000u);gba_a32_emit(&e,0xe58d0004u);
                }
                if((pc>>24)>=8 && !GBA_DYNAREC_FAST_BUS) {
                    gba_a32_load(&e,1,4,offsetof(gba_t,dynarec_memory.data));
                    gba_a32_constant(&e,3,offsetof(gba_native_cache_t,schedule)+i);
                    gba_a32_emit(&e,0xe3a02000u);gba_a32_emit(&e,0xe7c12003u);
                }
                *done|=(uint32_t)(e.next-done-2)&0xffffff;
            }
            any_internal = 1; last_internal = 1; reads++; count++;
            if(multiple&0x8000)break;
            continue;
        }
        if (kind == GBA_JIT_BRANCH && pending_count) {
            gba_a32_emit(&e,0xe2877000u|pending_count); pending_count=0;
        }
        last_internal = kind == GBA_JIT_MUL || ((kind == GBA_JIT_ALU ||
            (kind==GBA_JIT_BRANCH && width==4 && (op&0x0c000000u)==0 &&
             (op&0x0ffffff0u)!=0x012fff10u)) && !(arm & 0x02000000u) && (arm & 16));
        any_internal |= last_internal;
        if(last_internal) extra_bound+=5;
        if (last_internal) gba_a32_emit(&e, 0xe3a06000u);
        unsigned condition = width == 4 ? op >> 28 : ((op & 0xf000) == 0xd000 ? (op >> 8) & 15 : 14);
        unsigned conditional_exit=0;
        uint32_t *skip = NULL;
        if (condition != 14) {
            skip = e.next;
            gba_a32_emit(&e, ((condition ^ 1u) << 28) | 0x0a000000u);
        }
        if (kind == GBA_JIT_MUL)
            gba_a32_multiply(&e, arm, op_pc);
        else if (kind == GBA_JIT_BRANCH) {
            uint32_t branch_pc = op_pc;
            uint32_t target = width == 4 ? branch_pc + 8 + (uint32_t)(gba_sign(op & 0xffffff, 24) * 4) :
                branch_pc + 4 + (uint32_t)(gba_sign(op & 255, 8) * 2);
            unsigned direct = width == 4 ? (op & 0x0f000000u) == 0x0a000000u : (op & 0xf000u) == 0xd000u;
            unsigned rom_loop=(pc>>24)>=8 && (GBA_DYNAREC_WIDE_ROM || (!reads && !any_internal && !GBA_DYNAREC_FAST_BUS));
            if (!traced_edges && !trace_pages && !bounded && ((pc >> 24) == 3 || rom_loop) && direct && target == pc && count) {
                /* Repeat a proven register-only back edge in native code.
                 * Two IWRAM refill fetches are charged for every additional
                 * iteration; no timer/DMA/IRQ deadline is crossed. */
                loop_count = count + 1;
                loop_cycles=2;
#if GBA_DYNAREC_WIDE_ROM
                if(rom_loop)loop_cycles=2*gba_native_fetch_floor(gba_io16(g,0x204),width,pc>>24);
                else
#endif
                if (rom_loop) {
                    uint16_t wait=gba_io16(g,0x204);
                    unsigned area=((pc>>24)-8)/2, seq=gba_gamepak_seq(wait,area)*(width/2);
                    static const unsigned first[4]={4,3,2,8};
                    unsigned nonseq=first[(wait>>(2+area*3))&3]+1+(width==4?seq/2:0);
                    /* Complete refill plus one whole subsequent iteration.
                     * No data/internal cycles can fill the empty ROM FIFO. */
                    loop_cycles=nonseq+(loop_count+1)*seq;
                }
                gba_a32_emit(&e, 0xe10fc000u);
                gba_a32_emit(&e, 0xe59d0000u);
                gba_a32_emit(&e, 0xe2400001u);
                gba_a32_emit(&e, 0xe58d0000u);
                gba_a32_emit(&e, 0xe3500000u);
                uint32_t *exit = e.next; gba_a32_emit(&e, 0x0a000000u);
                gba_a32_emit(&e, 0xe128f00cu);
                gba_a32_constant(&e,0,loop_cycles); gba_a32_emit(&e, 0xe58d0004u);
                gba_a32_emit(&e, 0xe2877001u);
                gba_a32_emit(&e, 0xe3a06000u);
                gba_a32_emit(&e, 0xea000000u | ((uint32_t)(loop_body - e.next - 2) & 0xffffff));
                *exit |= (uint32_t)(e.next - exit - 2) & 0xffffff;
                gba_a32_emit(&e, 0xe128f00cu);
            }
            if(trace_target) {
                traced_edges++;
#if GBA_DYNAREC_TRACE_LINKS
                if(trace_return) {
                    /* Return prediction is a runtime guard, never an assumed
                     * LR value. Mismatch commits the ordinary BX exit before
                     * any target instruction or deferred refill executes. */
                    gba_a32_emit(&e,0xe10fc000u);gba_a32_guest_read(&e,1,trace_guard_reg,op_pc+(width==2?4:8));
                    gba_a32_constant(&e,0,trace_return);gba_a32_emit(&e,0xe1510000u);
                    uint32_t *matched=e.next;gba_a32_emit(&e,0x0a000000u);
                    gba_a32_emit(&e,0xe128f00cu);gba_a32_branch(&e,op,op_pc,width);
                    gba_a32_emit(&e,0xe2877001u);gba_a32_emit(&e,0xe3a06000u);gba_a32_epilogue(&e);
                    *matched|=(uint32_t)(e.next-matched-2)&0xffffff;
                    gba_a32_emit(&e,0xe128f00cu);if(trace_depth && trace_guard_reg==14 && count)trace_depth--;side_exits++;
                }
                if(trace_call)trace_returns[trace_depth++]=width==2?(op_pc+2)|1u:op_pc+4;
#endif
                if(width==4 && (op&0xff000000u)==0xeb000000u) {
                    gba_a32_constant(&e,0,op_pc+4);gba_a32_guest_write(&e,14,0);
                } else if(width==2 && (op&0xf800u)==0xf800u) {
                    gba_a32_constant(&e,0,(op_pc+2)|1u);gba_a32_guest_write(&e,14,0);
                }
                gba_a32_constant(&e,0,trace_refill);gba_a32_emit(&e,0xe0855000u);
                extra_bound+=trace_refill;any_internal=1;
#if GBA_DYNAREC_TRACE_LINKS
                next_source_pc=trace_target;
#endif
            } else gba_a32_branch(&e, op, op_pc, width);
            if(condition!=14 && !loop_count) {
                /* Taken edge leaves the committed prefix. Fallthrough stays
                 * in the native block with guest registers still cached. */
                gba_a32_emit(&e,0xe2877001u);
                if(last_internal) {
                    gba_a32_emit(&e,0xe0855006u);
                    if((pc>>24)>=8 && !GBA_DYNAREC_FAST_BUS) {
                        memory_mask|=1u<<i;
                        gba_a32_load(&e,1,4,offsetof(gba_t,dynarec_memory.data));
                        gba_a32_constant(&e,3,offsetof(gba_native_cache_t,schedule)+i);
                        gba_a32_emit(&e,0xe3862040u);gba_a32_emit(&e,0xe7c12003u);
                    }
                } else gba_a32_emit(&e,0xe3a06000u);
                gba_a32_epilogue(&e);
                conditional_exit=1;side_exits++;
            }
        } else
            gba_a32_alu(&e, op, arm, op_pc, width);
        if (skip)
            *skip |= (uint32_t)(e.next - skip - 2) & 0xffffff;
        if (last_internal) {
            gba_a32_emit(&e, 0xe0855006u);
            if((pc>>24)>=8 && !GBA_DYNAREC_FAST_BUS) {
                /* Timing marker, not a memory operation: replay the exact
                 * internal idle cycles before the next Game Pak fetch. */
                memory_mask|=1u<<i;
                gba_a32_load(&e,1,4,offsetof(gba_t,dynarec_memory.data));
                gba_a32_constant(&e,3,offsetof(gba_native_cache_t,schedule)+i);
                gba_a32_emit(&e,0xe3862040u);gba_a32_emit(&e,0xe7c12003u);
            }
        }
        pending_count++;
        if (!count) {
            gba_a32_emit(&e, 0xe59d0004u); gba_a32_emit(&e, 0xe0855000u);
            gba_a32_emit(&e, 0xe3a00000u); gba_a32_emit(&e, 0xe58d0004u);
        }
        count++;
        if (kind == GBA_JIT_BRANCH && !conditional_exit && !trace_target) break;
    }
    if (count && span) {
        if (pending_count) gba_a32_emit(&e,0xe2877000u|pending_count);
        if (!last_internal) gba_a32_emit(&e, 0xe3a06000u);
        gba_a32_epilogue(&e);
        if (reads) {
            uint32_t *failure = e.next;
            gba_a32_emit(&e, 0xe128f00cu);
            gba_a32_emit(&e, 0xe3877102u);
            gba_a32_epilogue(&e);
            *failure_literal = (uint32_t)(uintptr_t)failure;
        }
    } else if (count) {
        uint32_t arm;
        unsigned kind = gba_native_decode(head0, width, &arm);
        if (kind != GBA_JIT_ARM_MEMORY && kind != GBA_JIT_THUMB_MEMORY) {
            if (!last_internal) gba_a32_emit(&e, 0xe3a06000u);
            gba_a32_epilogue(&e);
        }
    }
    if (!count) return NULL;
    if(budget_exit_count) {
        uint32_t *exit=e.next;
        gba_a32_emit(&e,0xe128f00cu);
        gba_a32_epilogue(&e);
        for(unsigned n=0;n<budget_exit_count;n++)
            *budget_exits[n]|=(uint32_t)(exit-budget_exits[n]-2)&0xffffff;
    }
    uint32_t *raw_code = e.next;
    for (unsigned i = 0; i < count; i++) {
        uint32_t at=pc+i*width;
#if GBA_DYNAREC_TRACE_LINKS
        if(linked_trace)at=source_pc[i];
#endif
        uint32_t raw = gba_native_word(g, at, width);
        if (width == 2) gba_put16((uint8_t *)raw_code + i * width, (uint16_t)raw);
        else gba_put32((uint8_t *)raw_code + i * width, raw);
    }
    size_t bytes = (size_t)(e.next - code) * 4 + count * width;
#if GBA_DYNAREC_TRACE_LINKS
    if(linked_trace) {
        source_pc[count]=next_source_pc;
        for(unsigned n=0;n<=count;n++)gba_put32((uint8_t *)code+bytes+n*4,source_pc[n]);
        bytes+=(count+1)*4;
    }
#endif
#if GBA_DYNAREC_ROM_TRACE
    if(trace_pages && loop_count) {gba_fail(g,GBA_ERROR_MEMORY);return NULL;}
#endif
    if(bytes>reserve || (size_t)(raw_code-code)>2047) {gba_fail(g,GBA_ERROR_MEMORY);return NULL;}
#if GBA_DYNAREC_INTERIOR
    if(e.resumable) {
        uint8_t *table=(uint8_t *)code+bytes;
        gba_put32(table,failure_literal?*failure_literal:0);table+=4;
        for(unsigned i=0;i<count;i++) {
            gba_put16(table+i*5,entry_words[i]);table[i*5+2]=entry_pending[i];gba_put16(table+i*5+3,entry_extra[i]);
        }
        bytes+=4+count*5;
    }
#endif
    if (e.failed) {
        gba_fail(g, GBA_ERROR_MEMORY);
        return NULL;
    }
    if (gba_code_publish(code, bytes)) {
        gba_fail(g, GBA_ERROR_IO);
        return NULL;
    }
    if (!count)
        return NULL;
    b->prefix_offset=0;b->prefix_count=0;
#if GBA_DYNAREC_TRACE_CONTEXT
    if(context_reg<15)b->prefix_offset=context;
#endif
    b->epoch = c->epoch;
    b->pc = pc;
    b->flags = (uint8_t)((width==2?1u:0u)|(span?2u:0u));
    b->cache_key = key;
    if (any_internal) b->flags |= 8;
    if (reads) b->flags |= 16;
    if (loop_count) b->flags |= 32;
    if (writes) b->flags |= 64;
    if (any_multiple) b->flags |= 128;
    b->head[0] = head0;
    b->head[1] = head1;
    b->page_generation = generation;
    b->offset = c->used;
    b->raw_words = (uint16_t)((raw_code-code)|((((pc>>24)==3 || GBA_DYNAREC_ROM_CHAIN) && loop_body?(loop_body-code):0)<<11));
    b->regmap=0;
    for(unsigned n=0;n<4;n++)b->regmap|=(uint16_t)(e.cached_guest[n]<<(n*4));
    b->count = (uint8_t)count;
    b->memory_mask = memory_mask;
    b->extra_bound = extra_bound;
    b->multiple_mask = multiple_mask;
    b->loop_cycles = loop_cycles;
    b->side_exits=side_exits;
    b->fetch_bound_seq=b->fetch_bound_nonseq=0;
    if((pc>>24)>=8) {
        unsigned area=((pc>>24)-8)/2;
        uint16_t wait=gba_io16(g,0x204);
        unsigned seq=gba_gamepak_seq(wait,area)*(width/2);
        static const unsigned first[4]={4,3,2,8};
        unsigned nonseq=first[(wait>>(2+area*3))&3]+1+(width==4?seq/2:0);
#if GBA_DYNAREC_FAST_BUS
        (void)nonseq;(void)seq;
        b->prefix_count=(uint8_t)gba_native_fetch_floor(wait,width,pc>>24);
        b->fetch_bound_seq=b->fetch_bound_nonseq=count*b->prefix_count;
#if GBA_DYNAREC_REFILL_MATCH
        /* Low byte retains the native self-loop cost. The upper byte is the
         * real empty-pipeline two-fetch refill for a cross-block branch. */
        b->loop_cycles|=gba_native_refill_cost(wait,width,pc>>24)<<8;
#endif
#else
        unsigned bound=count*seq;
        uint32_t accesses=memory_mask&((1u<<(count-1))-1);
        if(nonseq>seq)for(;accesses;accesses&=accesses-1)bound+=nonseq-seq;
        b->fetch_bound_seq=bound;b->fetch_bound_nonseq=bound+nonseq-seq;
#endif
    }
    b->end_page=gba_native_page(pc+(count+1)*width);
    b->end_generation=b->end_page<GBA_JIT_PAGES?c->generation[b->end_page]:0;
    if (page < GBA_JIT_PAGES) {
        /* Protect the exact code/fetch lines, including pipeline lookahead.
         * Working RAM sharing the rest of a 256-byte generation page stays
         * native; all true code-line writes still exit before mutation. */
        uint32_t end=pc+(count+2)*width;
        for(uint32_t at=pc;at<end;at=(at&~31u)+32) {
            unsigned code_page=gba_native_page(at);
            if(code_page<GBA_JIT_PAGES) {
                uint8_t mask=(uint8_t)(1u<<((at>>5)&7));
                c->code_page[code_page]|=mask;
#if GBA_DYNAREC_PAGE_INDEX
                c->legacy_code_page[code_page]|=mask;
#endif
            }
        }
    }
    uint32_t normalized;
    unsigned initial_kind = gba_native_decode(head0, width, &normalized);
    if (initial_kind == GBA_JIT_ARM_MEMORY || initial_kind == GBA_JIT_THUMB_MEMORY)
        b->flags |= 4;
    c->used = (c->used + (uint32_t)bytes + 31u) & ~31u;
    GBA_NATIVE_COUNT(c, compiled_blocks, 1);

    if(!attached){c->hot=b;gba_native_recent_publish(c,pc,b);}
    return b;
}
static uint64_t gba_native_call(gba_t *g, gba_native_block_t *b, unsigned count) {
    gba_native_cache_t *c = gba_native_cache(g);
    uint64_t (*entry)(gba_t *, unsigned) =
        (uint64_t (*)(gba_t *, unsigned))((uint8_t *)g->dynarec_memory.code + b->offset);
#if GBA_DYNAREC_INTERIOR
    if(b->cache_key&1024)count|=0x40000000u;
#endif
#if GBA_DYNAREC_FASTCALL
    uint64_t result=gba_native_invoke(g,count,(uintptr_t)entry);
#else
    uint64_t result = entry(g, count);
#endif
#if GBA_DYNAREC_INTERIOR
    if((b->cache_key&1024) && c->resume_seed && ((result>>32)&255)<c->resume_seed)
        result-=UINT64_C(1)<<40; /* carry from the initial pending-count bias */
#endif
    unsigned executed = (unsigned)(result >> 32) & 255;
    unsigned reads = (unsigned)(result >> 40) & 255;
#if GBA_DYNAREC_STATS
    if(reads>executed){gba_fail(g,GBA_ERROR_MISMATCH);return 0;}
#endif
    GBA_NATIVE_COUNT(c,native_entries,1);
    GBA_NATIVE_COUNT(c,zero_entries,!executed);
    GBA_NATIVE_COUNT(c,guard_returns,(unsigned)(result>>63));
#if GBA_DYNAREC_STATS
    unsigned bucket=0;while(bucket<7 && executed>(1u<<bucket))bucket++;
    c->stats.entries_by_size[bucket]++;
#endif
    GBA_NATIVE_COUNT(c, memory_instructions, reads);
    GBA_NATIVE_COUNT(c, native_instructions, executed - reads);
    (void)c; (void)executed; (void)reads;
    return result;
}
#if GBA_DYNAREC_STATS
void gba_dynarec_fallback(gba_t *g, unsigned count) {
    gba_native_cache_t *cache = gba_native_cache(g);
    if (cache) GBA_NATIVE_COUNT(cache, fallback_instructions, count);
}
#endif
static uint32_t gba_native_div(uint32_t n, unsigned d) {
    switch (d) {
    case 1: return n; case 2: return n/2; case 3: return n/3; case 4: return n/4;
    case 5: return n/5; case 8: return n/8; case 9: return n/9; case 17: return n/17;
    default: return n/d;
    }
}
/* Exact closed form for a sequential ALU segment, not a timing approximation.
 * A full FIFO fetch costs one cycle and may refill at most one unit; after
 * its first partial fetch, every remaining fetch has the same empty-FIFO cost. */
uint32_t gba_native_fetch_span(gba_t *g, unsigned n, unsigned width, unsigned region) {
    gba_cpu_t *cpu = &g->machine.cpu;
    uint16_t wait = gba_io16(g, 0x204);
    unsigned area = (region - 8) / 2, seq = gba_gamepak_seq(wait, area), units = width / 2;
    unsigned regular = units * seq, total = 0;
    if (!cpu->fetch_sequential) {
        static const unsigned first[4] = {4,3,2,8};
        total = first[(wait >> (2 + area * 3)) & 3] + 1 + (width == 4 ? seq : 0);
        g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        cpu->fetch_sequential = 1;
        g->access_cycles = total;
        if (!--n) return total;
    }
    if (!(wait & 0x4000)) {
        g->access_cycles = regular;
        return total + n * regular;
    }
    unsigned count = g->machine.prefetch_count, phase = g->machine.prefetch_phase;
    if (count > 8 || phase >= seq) {
        /* Defensive path for externally edited machine state. */
        for (unsigned i = 0; i < n; i++) {
            unsigned used = count < units ? count : units, cost;
            g->machine.prefetch_count = count - used;
            if (used == units) { cost = 1; gba_prefetch_idle(g, 1); }
            else { cost = (units-used)*seq - phase%seq; g->machine.prefetch_phase = 0; }
            count = g->machine.prefetch_count; phase = g->machine.prefetch_phase;
            total += cost; g->access_cycles = cost;
        }
        return total;
    }
    unsigned full = count >= units ? gba_native_div((count-units)*seq + phase, regular-1) + 1 : 0;
    if (full > n) full = n;
    unsigned elapsed = phase + full, filled = gba_native_div(elapsed, seq);
    count = count - full*units + filled;
    phase = elapsed - filled*seq;
    total += full;
    if (full == n) {
        g->machine.prefetch_count = count; g->machine.prefetch_phase = phase;
        g->access_cycles = 1;
    } else {
        unsigned partial = (units-count)*seq - phase, tail = n-full-1;
        total += partial + tail*regular;
        g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        g->access_cycles = tail ? regular : partial;
    }
    return total;
}
uint32_t gba_native_replay_rom(gba_t *g,const gba_native_block_t *b,unsigned count,const gba_native_rom_state_t *saved) {
    gba_cpu_t *cpu=&g->machine.cpu;
    unsigned region=b->pc>>24,width=(b->flags&1)?2:4;
#if GBA_DYNAREC_FAST_BUS
    /* Explicit speed-first timing: the minimum legal single-fetch latency.
     * This cannot inflate guest cycles to manufacture a frame-rate gain; it
     * generally asks the CPU to execute more guest instructions per frame.
     * Data accesses and instruction internal cycles retain their real costs. */
    (void)saved;
    unsigned cost=gba_native_fetch_floor(gba_io16(g,0x204),width,region);
    g->machine.prefetch_count=g->machine.prefetch_phase=0;
    cpu->fetch_sequential=cpu->pipeline_valid?1:0;
    g->access_cycles=cost;
    return count*cost;
#else
    unsigned branch_taken=!cpu->pipeline_valid;
    uint32_t actual_fetch=0;
        gba_native_cache_t *cache = gba_native_cache(g);
        uint16_t wait = gba_io16(g, 0x204);
        g->machine.prefetch_count = saved->count;
        g->machine.prefetch_phase = saved->phase;
        cpu->fetch_sequential = saved->sequential;
        cpu->pipeline_valid = 1;
        unsigned begin = 0;
        for (unsigned i = 0; i < count && i < 32; i++) {
            if (!(b->memory_mask & (1u << i))) continue;
            actual_fetch += gba_native_fetch_span(g, i-begin+1, width, region);
            unsigned schedule=i;
#if GBA_DYNAREC_INTERIOR
            if(b->cache_key&1024)schedule+=(b->cache_key>>11)&31;
#endif
            unsigned data = cache->schedule[schedule], data_cost = data & 31;
            if(b->multiple_mask&(1u<<i)) {
                data_cost=cache->multiple_schedule[schedule]&255;
                data=(cache->multiple_schedule[schedule]&256)?32:0;
            }
            if(data&64) {
                if(data_cost) {
                    if(!(wait&0x4000))cpu->fetch_sequential=0;
                    gba_prefetch_idle(g,data_cost);
                }
                begin=i+1;continue;
            }
            if (!data_cost && !(data&32)) {begin=i+1;continue;}
            if (data & 128) {
                cpu->fetch_sequential = 0;
                g->machine.prefetch_count = g->machine.prefetch_phase = 0;
            } else {
                gba_prefetch_idle(g, data_cost + !!(data & 32));
                if (!(wait & 0x4000)) cpu->fetch_sequential = 0;
            }
            g->access_cycles += data_cost;
            begin = i+1;
        }
        if (begin < count) actual_fetch += gba_native_fetch_span(g, count-begin, width, region);
        if (branch_taken) {
            cpu->pipeline_valid = cpu->fetch_sequential = 0;
            g->machine.prefetch_count = g->machine.prefetch_phase = 0;
        }
    return actual_fetch;
#endif
}
uint32_t gba_dynarec_block(gba_t *g, uint32_t budget) {
    gba_cpu_t *cpu = &g->machine.cpu;
    gba_native_cache_t *cache = gba_native_cache(g);
    if (!cache) {
        gba_fail(g, GBA_ERROR_MEMORY);
        return 0;
    }
    uint32_t waitcnt = gba_io16(g, 0x204);
    if (cache->waitcnt != waitcnt) {
        gba_dynarec_invalidate(g, 0, SIZE_MAX);
        cache->waitcnt = waitcnt;
        cache->used = cache->code_base;
        gba_memset(cache->stub,0,sizeof(cache->stub));
    }
    uint32_t pc = cpu->r[15];
    unsigned region = pc >> 24, width = (cpu->cpsr & GBA_T) ? 2 : 4;
    unsigned resident_rom = region >= 8 && region <= 12 && g->rom;
    if (!g->stepping || !cpu->pipeline_valid || (pc & (width - 1)) || (region != 2 && region != 3 && !resident_rom) ||
        (g->machine.last_fetch >> 24) != region || gba_dma_ready_mask(g) ||
        (cpu->irq_line && !cpu->irq_delay && gba_io16(g, 0x208) && !(cpu->cpsr & 0x80)) ||
        (cpu->wait_mask && !cpu->hle_irq))
        return 0;
#if GBA_TEST_MODE
    /* The native instruction route remains enabled for instruction trace hooks. */
    return 0;
#endif
    unsigned fetch_cost = region == 3 ? 1 : region == 2 ? (width == 4 ? 6 : 3) : (width == 4 ? 18 : 9);
    if (!g->event_cycles)
        g->event_cycles = gba_next_cpu_event(g);
    uint32_t distance = g->event_cycles > g->cpu_pending ? g->event_cycles - g->cpu_pending : 0;
    if (distance < budget)
        budget = distance;
#if GBA_DYNAREC_EVENT_SLIP
    unsigned minimum=GBA_DYNAREC_EVENT_SLIP;
#if GBA_DYNAREC_BULK_PREFILL
    minimum-=cache->prefill_cycles<minimum?cache->prefill_cycles:minimum;
#endif
    if(budget<minimum) {
        uint32_t hard=gba_native_hard_distance(g);
        if(minimum>hard)minimum=hard;
        if(budget<minimum)budget=minimum;
    }
#endif
    budget=gba_native_control_budget(g,budget);
    if (budget < 3)
        return 0;
    if (resident_rom) {
        size_t end = (pc & 0x1ffffff) + 3 * width;
        if (end > g->rom_size || ((pc & 0x1ffffff) < 0xca && end > 0xc4))
            return 0;
    }
    {
        uint32_t op=cpu->pipeline[0], address=0; unsigned known=0;
        if (width==4 && (op&0x0e000000u)==0x04000000u && (!(op&0x1000000u)||!(op&0xfffu))) {
            unsigned rn=(op>>16)&15; address=rn==15?pc+8:cpu->r[rn]; known=1;
        } else if (width==2 && ((op&0xe000u)==0x6000u||(op&0xf000u)==0x8000u) && !(op&0x7c0u)) {
            address=cpu->r[(op>>3)&7]; known=1;
        }
        unsigned data_region=address>>24;
        if (known && data_region!=2 && data_region!=3 &&
            !(GBA_DYNAREC_VIDEO_STORE && !gba_native_is_load(op,width) && data_region>=5 && data_region<=7) &&
            !(gba_native_is_load(op,width)&&((g->rom&&data_region>=8&&data_region<=12)||
                (data_region==4 && (((address&0x00fffffcu)==4)||((address&0x00fffffcu)==0x130))))))
            return 0; /* Never enter a native block only to reject its first device access. */
    }
    if (resident_rom && ((pc+2*width)>>8)!=(pc>>8) && !GBA_DYNAREC_ROM_TRACE)return 0;
    unsigned span=resident_rom?(GBA_DYNAREC_ROM_TRACE?120:GBA_DYNAREC_WIDE_ROM?128:32):64;
    gba_native_block_t *b=gba_native_find(g,pc,cpu->pipeline[0],cpu->pipeline[1],width,span);
    if(!b) {
        uint32_t arm;
        unsigned kind = gba_native_decode(cpu->pipeline[0], width, &arm);
        if (gba_native_ram_read(cpu->pipeline[0], width)) kind = GBA_JIT_RAM_READ;
        if(gba_native_multiple(cpu->pipeline[0],width))kind=GBA_JIT_MULTIPLE;
        if (kind != GBA_JIT_ALU && kind != GBA_JIT_MUL && kind != GBA_JIT_RAM_READ && kind != GBA_JIT_MULTIPLE && !((GBA_DYNAREC_TINY_BLOCKS || (GBA_DYNAREC_TRACE_LINKS && resident_rom)) && kind==GBA_JIT_BRANCH)) {
#if GBA_DYNAREC_NEGATIVE
            gba_native_reject_decode(g,pc,width,span);
#endif
            return 0;
        }
        uint32_t next_arm;
        unsigned next_kind = gba_native_decode(cpu->pipeline[1], width, &next_arm);
        if (gba_native_ram_read(cpu->pipeline[1], width)) next_kind = GBA_JIT_RAM_READ;
        if(gba_native_multiple(cpu->pipeline[1],width))next_kind=GBA_JIT_MULTIPLE;
        if (!GBA_DYNAREC_TINY_BLOCKS && !(GBA_DYNAREC_TRACE_LINKS && resident_rom && kind==GBA_JIT_BRANCH) && kind!=GBA_JIT_MULTIPLE && next_kind != GBA_JIT_ALU && next_kind != GBA_JIT_MUL && next_kind != GBA_JIT_BRANCH && next_kind != GBA_JIT_RAM_READ && next_kind != GBA_JIT_MULTIPLE) {
#if GBA_DYNAREC_NEGATIVE
            gba_native_reject_decode(g,pc,width,span);
#endif
            return 0;
        }
#if GBA_DYNAREC_INTERIOR
        b=gba_native_interior(g,pc,width,span,budget);
#endif
        if(!b)b=gba_native_compile(g,pc,cpu->pipeline[0],cpu->pipeline[1],width,span,NULL);
        if(!b)return 0;
    }
    uint32_t fetch_bound=resident_rom?
        (cpu->fetch_sequential?b->fetch_bound_seq:b->fetch_bound_nonseq):b->count*fetch_cost;
    unsigned bounded=0;
    if(fetch_bound+b->extra_bound>=budget) {
        GBA_NATIVE_COUNT(cache,budget_rejects,1);
#if GBA_DYNAREC_LINK_CACHE
        /* This mode owns prefix_offset as a successor pointer. An oversized
         * IWRAM entry must return to scalar execution, not reinterpret that
         * pointer as an attached-prefix code offset. */
        return 0;
#endif
#if GBA_DYNAREC_ROM_PREFIX
        if(resident_rom) {
            /* A short immutable-ROM prefix uses its own exact FIFO bound. It
             * shares the full entry's lifetime, not another hash-table way. */
            if(b->count<3 || (b->cache_key&1024))return 0;
            if(!b->prefix_offset) {
                if(g->dynarec_memory.code_size-cache->used<GBA_JIT_RESERVE+sizeof(*b)+32)return 0;
                gba_native_block_t *prefix=&cache->prefix_active;
                if(!gba_native_compile(g,pc,cpu->pipeline[0],cpu->pipeline[1],width,512|2,prefix))return 0;
                uint32_t offset=cache->used;
                uint8_t *record=(uint8_t *)g->dynarec_memory.code+offset;
                if(gba_code_begin_write(record,sizeof(*prefix))) {gba_fail(g,GBA_ERROR_IO);return 0;}
                gba_memcpy(record,prefix,sizeof(*prefix));
                if(gba_code_publish(record,sizeof(*prefix))) {gba_fail(g,GBA_ERROR_IO);return 0;}
                cache->used=(offset+sizeof(*prefix)+31u)&~31u;
                b->prefix_offset=offset;
            }
            gba_memcpy(&cache->prefix_active,(uint8_t *)g->dynarec_memory.code+b->prefix_offset,sizeof(*b));
            b=&cache->prefix_active;
            fetch_bound=cpu->fetch_sequential?b->fetch_bound_seq:b->fetch_bound_nonseq;
            if(b->count<2 || fetch_bound+b->extra_bound>=budget)return 0;
        } else
#endif
        {
        if(region!=3 || budget<12 || budget>65535 || b->count<2)return 0;
        bounded=1;
        if(!b->prefix_offset) {
            gba_native_block_t *prefix=&cache->prefix_active;
            if(!gba_native_compile(g,pc,cpu->pipeline[0],cpu->pipeline[1],width,span|256,prefix))return 0;
            b->prefix_offset=prefix->offset;b->prefix_count=prefix->count;
        }
        /* The normal entry's exact source bytes/generations protect this
         * prefix too. It owns no hash way and cannot displace normal code. */
        cache->prefix_active=*b;
        cache->prefix_active.offset=b->prefix_offset;
        cache->prefix_active.count=b->prefix_count;
        cache->prefix_active.flags&=(uint8_t)~32u;
        b=&cache->prefix_active;
        }
    }
    if (b->count < (GBA_DYNAREC_TINY_BLOCKS?1:2) && !(b->flags&128)) return 0;
    unsigned count = b->count, block_count = count, repetitions = 1;
    if ((b->flags & 32) && (!resident_rom || GBA_DYNAREC_WIDE_ROM)) {
        unsigned iteration=count+b->extra_bound+b->memory_mask+2;
#if GBA_DYNAREC_WIDE_ROM
        if(resident_rom)iteration=(count+2)*gba_native_fetch_floor(gba_io16(g,0x204),width,region)+b->extra_bound;
#endif
        repetitions = (budget - 1) / iteration;
        if (!GBA_DYNAREC_LONG_LOOPS && repetitions > 7) repetitions = 7;
        if(repetitions>255/count)repetitions=255/count;
        if (!repetitions) repetitions = 1;
    }
    uint32_t actual_fetch = 0;
    uint32_t saved_count = g->machine.prefetch_count, saved_phase = g->machine.prefetch_phase;
    unsigned saved_sequential = cpu->fetch_sequential;
    /* A guarded RAM/resident-ROM read is stable until the bounded event. Keep
     * the same wait-loop CPU/pipeline proof alive across translated reads.
     * A possible write conservatively rejects it before native execution. */
    cache->rom_data_seen=bounded || resident_rom ||
        repetitions*(count*fetch_cost+b->extra_bound+b->memory_mask+2)-2<budget?2:0;
    uint64_t returned = gba_native_call(g, b, count | (repetitions << 8) | (bounded?budget<<16:0));
    count = (unsigned)(returned >> 32) & 255;
    if (!count) return 0;
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    if((returned>>48)&1) {
        if(g->wait.valid)g->wait.reject_pc=g->wait.pc;
        gba_wait_invalidate(g);
    }
#endif
    if (!resident_rom && (cache->rom_data_seen&1))
        g->machine.prefetch_count=g->machine.prefetch_phase=0;
    uint32_t result = (uint32_t)returned, internal = result & 0xffff;
    if (resident_rom) {
        gba_native_rom_state_t saved={saved_count,saved_phase,saved_sequential};
        actual_fetch=gba_native_replay_rom(g,b,count,&saved);
#if GBA_DYNAREC_FAST_BUS
        g->access_cycles+=result>>24;
#endif
    }
    if ((b->flags & 32) && (returned >> 63) && !(count % block_count)) {
        cpu->pipeline_valid = cpu->fetch_sequential = 0;
        cpu->r[15] = pc;
        g->machine.prefetch_count = g->machine.prefetch_phase = 0;
    }
    unsigned final_count = (b->flags & 32) ? ((count - 1) % block_count) + 1 : count;
    uint32_t next_pc=pc+final_count*width;
#if GBA_DYNAREC_TRACE_LINKS
    if(resident_rom)next_pc=gba_native_trace_pc(g,b,final_count);
#endif
    cpu->pipeline[0] = final_count==1?b->head[1]:gba_native_word(g,next_pc,width);
#if GBA_DYNAREC_TRACE_LINKS
    if(resident_rom)cpu->pipeline[0]=gba_native_word(g,next_pc,width);
#endif
    cpu->pipeline[1] = gba_native_word(g,next_pc+width,width);
    if (cpu->pipeline_valid) {
        cpu->r[15] = next_pc;
        if (!resident_rom)
            cpu->fetch_sequential = !(result & 0x00800000u) && !((result >> 16) && !(gba_io16(g, 0x204) & 0x4000));
    }
    if (g->machine.dma_bus_valid && (count > 1 || pc != g->machine.dma_pc))
        g->machine.dma_bus_valid = 0;
    g->machine.last_fetch = next_pc+width;
    g->machine.open_bus = width == 2 ? cpu->pipeline[1] * 0x10001u : cpu->pipeline[1];
    g->access_cycles = resident_rom ? g->access_cycles : fetch_cost + (result >> 24);
    uint32_t cycles = resident_rom ? actual_fetch + internal : count * fetch_cost + internal;
    gba_cpu_charge(g, cycles);
    return cycles;
}
int gba_dynarec_bind(gba_t *g, const gba_dynarec_memory_t *m) {
    if (!g || !m)
        return GBA_ERROR_MEMORY;
    if (g->running || g->stepping)
        return GBA_ERROR_BUSY;
    gba_dynarec_memory_t supplied = *m;
    m = &supplied;
    if (!m->code || !m->data || ((uintptr_t)m->code & 31) || ((uintptr_t)m->data & 31) ||
        m->code_size < 2*GBA_JIT_RESERVE || m->code_size > UINT32_MAX ||
        m->data_size < sizeof(gba_native_cache_t) + 64 * sizeof(gba_native_block_t)
#if GBA_DYNAREC_PERSISTENT_ENGINE
            + 16 * sizeof(gba_persistent_block_t) + 31u
#endif
        )
        return GBA_ERROR_MEMORY;
    uintptr_t code = (uintptr_t)m->code, data = (uintptr_t)m->data;
    if (code > UINTPTR_MAX - m->code_size || data > UINTPTR_MAX - m->data_size ||
        (code < data + m->data_size && data < code + m->code_size))
        return GBA_ERROR_MEMORY;
    const void *regions[] = {g, g->memory.ewram, g->memory.iwram, g->memory.vram, g->memory.palette, g->memory.oam,
        g->memory.save, g->memory.framebuffer, g->memory.rom_cache, g->rom, g->bios};
    const size_t lengths[] = {sizeof(*g), 0x40000, 0x8000, 0x18000, 0x400, 0x400, GBA_SAVE_CAPACITY,
        GBA_DRAW_SIZE * sizeof(gba_color_t), GBA_ROM_STREAM ? GBA_ROM_CACHE_SLOTS * GBA_ROM_PAGE_SIZE : 0, g->rom_size,
        g->bios ? 0x4000 : 0};
    for (unsigned i = 0; i < sizeof(regions) / sizeof(*regions); i++) {
        uintptr_t start = (uintptr_t)regions[i];
        if (!regions[i] || !lengths[i])
            continue;
        if (start > UINTPTR_MAX - lengths[i] || (code < start + lengths[i] && start < code + m->code_size) ||
            (data < start + lengths[i] && start < data + m->data_size))
            return GBA_ERROR_MEMORY;
    }
    if (gba_code_begin_write(m->code, m->code_size) || gba_code_publish(m->code, m->code_size))
        return GBA_ERROR_UNSUPPORTED;
    g->dynarec_memory = *m;
    gba_native_cache_t *c = gba_native_cache(g);
    gba_memset(m->data, 0, m->data_size);
    c->epoch = 1;
    c->code_base=GBA_JIT_RESERVE;
#if GBA_DYNAREC_CHAIN
    if(m->code_size>=4*GBA_JIT_RESERVE)c->code_base+=GBA_DYNAREC_COARSE_CHAIN?4096:2048;
#endif
    c->used = c->code_base;
    c->waitcnt = UINT32_MAX;
    size_t metadata_end=m->data_size;
#if GBA_DYNAREC_PAGE_INDEX
    size_t index_bytes=(m->data_size/8u)&~31u;
    metadata_end=(m->data_size-index_bytes)&~31u;
    if(metadata_end<sizeof(*c)+64*sizeof(gba_native_block_t)+16*sizeof(gba_persistent_block_t)) {
        gba_memset(&g->dynarec_memory,0,sizeof(g->dynarec_memory));return GBA_ERROR_MEMORY;
    }
    c->page_index_offset=(uint32_t)metadata_end;
    c->page_index_capacity=(uint32_t)(index_bytes/sizeof(gba_page_member_t));
#endif
    size_t available = (metadata_end - sizeof(*c)) / sizeof(gba_native_block_t);
#if GBA_DYNAREC_PERSISTENT_ENGINE
    available/=2;
#endif
    c->slots = 64;
    while (c->slots < 2*GBA_JIT_MAX_SETS && c->slots * 2 <= available)
        c->slots *= 2;
#if GBA_DYNAREC_PERSISTENT_ENGINE
    c->persistent_offset=(uint32_t)((sizeof(*c)+c->slots*sizeof(gba_native_block_t)+31u)&~31u);
    c->persistent_slots=16;
    size_t p_available=(metadata_end-c->persistent_offset)/sizeof(gba_persistent_block_t);
    if(p_available<16) {gba_memset(&g->dynarec_memory,0,sizeof(g->dynarec_memory));return GBA_ERROR_MEMORY;}
    while(c->persistent_slots<16384 && c->persistent_slots*2<=p_available)c->persistent_slots*=2;
    c->persistent_reset=1;
#endif
    return GBA_OK;
}
#if GBA_DYNAREC_PAGE_INDEX && GBA_DYNAREC_STATS
void gba_dynarec_same_value(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    if(c)GBA_NATIVE_COUNT(c,invalidate_same_value,1);
}
#endif
void gba_dynarec_invalidate(gba_t *g, uint32_t address, size_t size) {
    gba_native_cache_t *c = g ? gba_native_cache(g) : NULL;
    if (!c || !size)
        return;
#if GBA_DYNAREC_PERSISTENT_ENGINE
    gba_persistent_invalidate(g,address,size);
#endif
    unsigned first = gba_native_page(address);
    if (size == SIZE_MAX || first == GBA_JIT_PAGES || size > 0x40000) {
#if GBA_DYNAREC_HARD_CACHE
        c->hard_valid=0;
#endif
        gba_memset(c->code_page,0,sizeof(c->code_page));
#if GBA_DYNAREC_PAGE_INDEX
        gba_memset(c->legacy_code_page,0,sizeof(c->legacy_code_page));
#endif
        if (!++c->epoch) {
            gba_memset(gba_native_blocks(c), 0, c->slots * sizeof(gba_native_block_t));
            c->epoch = 1;
        }
        return;
    }
    /* Physical page addressing also covers writes crossing a RAM mirror boundary. */
    uint32_t mask = (address >> 24) == 2 ? 0x3ffff : 0x7fff;
    unsigned base = (address >> 24) == 2 ? 0 : 1024;
    size_t remaining = size;
    while (remaining) {
        unsigned page = base + ((address & mask) >> 8);
        size_t step = 256 - (address & 255);
        if (step > remaining)
            step = remaining;
#if GBA_DYNAREC_LINE_INVALIDATE
        /* A write to data beside translated code cannot alter that code or
         * its two pipeline lookahead words. Preserve the hot generation when
         * none of the actually protected 32-byte lines is touched. */
        unsigned first_line=(address&255)>>5,last_line=((address&255)+(unsigned)step-1)>>5;
        unsigned touched=(255u<<first_line)&(255u>>(7-last_line));
        if(c->code_page[page]&touched) {
#endif
        if (!++c->generation[page])
            gba_dynarec_invalidate(g, 0, SIZE_MAX);
#if GBA_DYNAREC_LINE_INVALIDATE
        }
#endif
        remaining -= step;
        address += (uint32_t)step;
    }
}
int gba_dynarec_stats(const gba_t *g, gba_dynarec_stats_t *stats) {
    if (!g || !stats || !gba_native_cache(g))
        return GBA_ERROR_MEMORY;
#if GBA_DYNAREC_STATS
    *stats = gba_native_cache(g)->stats;
#else
    gba_memset(stats, 0, sizeof(*stats));
#endif
    stats->code_bytes = gba_native_cache(g)->used;
    stats->direct_links = gba_native_cache(g)->direct_links;
    stats->coarse_rom_links = gba_native_cache(g)->coarse_rom_links;
#if GBA_DYNAREC_INTERIOR
    stats->interior_hits=gba_native_cache(g)->interior_entries;
#endif
    return GBA_OK;
}

#else

int gba_dynarec_bind(gba_t *g, const gba_dynarec_memory_t *memory) {
    (void)g;
    (void)memory;
    return GBA_ERROR_UNSUPPORTED;
}
void gba_dynarec_invalidate(gba_t *g, uint32_t address, size_t size) {
    (void)g;
    (void)address;
    (void)size;
}
int gba_dynarec_stats(const gba_t *g, gba_dynarec_stats_t *stats) {
    (void)g;
    (void)stats;
    return GBA_ERROR_UNSUPPORTED;
}

#endif
