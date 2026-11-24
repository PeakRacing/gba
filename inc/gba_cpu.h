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
#define GBA_IRQ_DELAY 7u
#define GBA_T         0x20u
#define GBA_V         0x10000000u
#define GBA_C         0x20000000u
#define GBA_Z         0x40000000u
#define GBA_N         0x80000000u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t r[16], bank[6][7], spsr[6], cpsr;
    uint32_t pipeline[2], pipeline_valid, fetch_sequential;
    uint32_t halted, stopped, wait_mask, wait_discard, hle_irq, hle_lr;
    uint32_t irq_line, irq_delay;
} gba_cpu_t;

#if GBA_FAST_BLOCKS && !GBA_TEST_MODE
typedef struct {
    uint32_t immediate;
    uint8_t kind, condition, rn, rd, rm, rs, shift, amount, flags;
} gba_register_instruction_t;
#endif

#if GBA_FAST_BLOCKS && !GBA_TEST_MODE
typedef struct {
    uint32_t pc, head[2], count;
    uint8_t bytes[64];
    gba_register_instruction_t instruction[16];
} gba_register_block_t;
#endif

#if GBA_DYNAREC
uint32_t gba_cpu_arm_memory(gba_t *g, uint32_t op, uint32_t pc);
uint32_t gba_cpu_thumb_memory(gba_t *g, uint32_t op, uint32_t pc);
#endif
uint32_t gba_cpu_execute(gba_t *g);
void gba_cpu_mode(gba_t *g, uint32_t cpsr);
void gba_cpu_exception(gba_t *g, unsigned mode, uint32_t vector, uint32_t lr);

#ifdef __cplusplus
}
#endif
