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

/* Executable storage is borrowed; the port owns permissions and cache maintenance. */
typedef struct {
    void *code;
    size_t code_size;
    void *data;
    size_t data_size;
} gba_dynarec_memory_t;

typedef struct {
    uint32_t compiled_blocks, cache_hits, flushes, code_bytes;
    uint64_t native_instructions, fallback_instructions, memory_instructions;
    uint32_t native_entries, zero_entries, guard_returns, budget_rejects, chain_links, direct_links, interior_hits, coarse_rom_links;
    uint32_t guard_alignment,guard_code_write,guard_other,negative_hits;
    uint32_t entries_by_size[8];
    uint32_t persistent_entries, persistent_blocks, persistent_links, persistent_misses;
    uint32_t persistent_indirect_hits, persistent_literal_sites, persistent_multi_sites;
    uint64_t persistent_instructions, persistent_multi_instructions;
    uint32_t persistent_rom_window_hits;
    uint32_t persistent_literal_leaf_reads;
    uint32_t turbo_traces, turbo_blocks, turbo_links, turbo_tlb_hits, turbo_tlb_misses;
    uint32_t turbo_cycle_commits, turbo_fallbacks;
    uint64_t turbo_instructions;
    uint64_t turbo_generated_words;
    uint32_t invalidate_requests, invalidate_same_value, invalidate_pages;
    uint32_t invalidate_checks, invalidate_blocks, invalidate_empty, invalidate_recycles;
    uint32_t turbo_long_multiply;
} gba_dynarec_stats_t;

int gba_dynarec_bind(gba_t *gba, const gba_dynarec_memory_t *memory);
void gba_dynarec_invalidate(gba_t *gba, uint32_t address, size_t size);
int gba_dynarec_stats(const gba_t *gba, gba_dynarec_stats_t *stats);
#if GBA_DYNAREC
/* Called only outside generated code. Failure leaves the block unpublished. */
int gba_code_begin_write(void *address, size_t bytes);
int gba_code_publish(void *address, size_t bytes);
#if GBA_DYNAREC_STATS
void gba_dynarec_fallback(gba_t *gba, unsigned count);
#endif
uint32_t gba_dynarec_block(gba_t *gba, uint32_t budget);
#endif

#ifdef __cplusplus
}
#endif
