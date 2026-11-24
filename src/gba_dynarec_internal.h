/* Private native cache ABI; included after gba_internal.h. */
#pragma once
#if GBA_DYNAREC
#define GBA_DYNAREC_ATTACHED_PREFIX 1
enum { GBA_JIT_PAGES = 1152, GBA_JIT_RESERVE = 8192, GBA_JIT_MAX_SETS=GBA_DYNAREC_DATA_SIZE>=1024u*1024u?8192:4096 };
enum { GBA_JIT_ALU = 1, GBA_JIT_MUL, GBA_JIT_BRANCH, GBA_JIT_ARM_MEMORY, GBA_JIT_THUMB_MEMORY, GBA_JIT_RAM_READ, GBA_JIT_MULTIPLE };
typedef struct {
    uint32_t pc, head[2], page_generation, epoch, offset;
    uint32_t memory_mask, multiple_mask, end_generation, prefix_offset;
    uint16_t raw_words, extra_bound, loop_cycles, cache_key, end_page;
    uint16_t fetch_bound_seq, fetch_bound_nonseq, regmap;
    uint8_t count, flags, side_exits, prefix_count;
} gba_native_block_t;
/* All bounds fit 16 bits at the 64-instruction RAM / 32-instruction ROM
 * limits. Keep 4096 entries available in a 256 KiB cache: growing this to
 * 64 bytes silently halves the power-of-two slot count to 2048. */
_Static_assert(sizeof(gba_native_block_t)<=60,"native metadata cache capacity");
typedef struct {
    uint32_t generation[GBA_JIT_PAGES];
    uint32_t epoch, used, slots, waitcnt, rom_data_seen, chain_stub, code_base, direct_links, coarse_rom_links;
#if GBA_DYNAREC_PERSISTENT_ENGINE
    uint32_t persistent_offset, persistent_slots, persistent_entry[2], persistent_exit[2];
#if GBA_DYNAREC_PAGE_INDEX
    uint32_t page_index_offset, page_index_capacity, page_index_free, page_index_available;
    uint32_t page_index_head[GBA_JIT_PAGES];
    uint8_t legacy_code_page[GBA_JIT_PAGES];
#endif
#if GBA_DYNAREC_PERSISTENT_MAP_HELPER
    uint32_t persistent_mapper[6];
#endif
#if GBA_DYNAREC_PERSISTENT_PAGE_TABLE
    uint32_t persistent_read_page[8192];
    uint32_t persistent_write_page[8192];
#endif
#if GBA_DYNAREC_TURBO_TLB
#define GBA_TURBO_TLB_ENTRIES 8192u
    uint32_t turbo_read_tlb[GBA_TURBO_TLB_ENTRIES];
    uint32_t turbo_write_tlb[GBA_TURBO_TLB_ENTRIES];
    uint8_t turbo_read_cost[3][GBA_TURBO_TLB_ENTRIES];
    uint8_t turbo_write_cost[3][GBA_TURBO_TLB_ENTRIES];
#endif
    uint32_t persistent_meta, persistent_reset;
    uint32_t persistent_guest_count, persistent_block_count;
    uint32_t persistent_internal;
    uint32_t persistent_data_cost;
    uint32_t persistent_memory_count, persistent_indirect_count;
#if GBA_DYNAREC_STATS
    uint32_t persistent_multi_count;
#endif
    uint32_t persistent_bridge[2];
    uint32_t persistent_victim;
    uint32_t persistent_incoming[256];
    struct { uint32_t key,entry; } persistent_indirect[1u<<GBA_DYNAREC_PERSISTENT_INDIRECT_BITS];
#endif
#if GBA_DYNAREC_HARD_CACHE
    uint32_t hard_valid,hard_cycles,hard_clock;
#endif
#if GBA_DYNAREC_BULK_PREFILL
    uint32_t prefill_cycles;
#endif
#ifdef GBA_TEST_CHAIN_REJECTS
    uint32_t coarse_reject[32];
#endif
#if GBA_DYNAREC_ROM_CHAIN
    uint32_t chain_rom_count,chain_rom_phase,chain_rom_sequential;
#endif
    gba_native_block_t prefix_active;
#if GBA_DYNAREC_INTERIOR
    gba_native_block_t interior_active;
    uint32_t resume_ip,resume_seed,resume_failure,interior_entries;
#endif
    gba_native_block_t *hot;
    gba_native_block_t *recent[1u<<GBA_DYNAREC_RECENT_BITS];
#if GBA_DYNAREC_COARSE_CHAIN
    gba_native_block_t *recent_alt[1u<<GBA_DYNAREC_RECENT_BITS];
#endif
    uint32_t stub[12];
    uint8_t victim[GBA_JIT_MAX_SETS];
    uint8_t code_page[GBA_JIT_PAGES]; /* Eight protected 32-byte lines per page. */
    uint8_t schedule[32];
    uint16_t multiple_schedule[32];
#if GBA_DYNAREC_STATS
    gba_dynarec_stats_t stats;
#endif
} gba_native_cache_t;
#if GBA_DYNAREC_STATS
#define GBA_NATIVE_COUNT(cache, member, amount) ((cache)->stats.member += (amount))
#else
#define GBA_NATIVE_COUNT(cache, member, amount) ((void)0)
#endif

GBA_INLINE gba_native_cache_t *gba_native_cache(const gba_t *g) {
    return (gba_native_cache_t *)g->dynarec_memory.data;
}
GBA_INLINE uint32_t gba_native_control_budget(const gba_t *g,uint32_t budget) {
    if(g->native_control_budget && budget>=g->native_control_budget)budget=g->native_control_budget-1;
    return budget;
}
/* DMA must become visible before subsequent CPU loads or code fetches. A
 * delayed audio sample is a timing approximation; executing through a pending
 * memory copy can consume a not-yet-written function pointer. Distances below
 * are from the committed device snapshot, minus already pending CPU work. */
GBA_INLINE uint32_t gba_native_hard_snapshot(const gba_t *g) {
    uint32_t distance=g->machine.cpu.irq_delay?g->machine.cpu.irq_delay:UINT32_MAX;
    const gba_ppu_t *p=&g->machine.ppu;
    for(unsigned ch=0;ch<4;ch++) {
        const gba_dma_t *d=&g->machine.dma[ch];
        if(d->startup && d->startup<distance)distance=d->startup;
        if(!(d->control&0x8000) || d->pending)continue;
        unsigned timing=(d->control>>12)&3;
        uint32_t edge=UINT32_MAX;
        if(timing==1)edge=(p->line<160?160-p->line:228-p->line+160)*GBA_CYCLES_PER_LINE-p->phase;
        if(timing==2) {
            if(p->line<160 && p->phase<GBA_HBLANK_START)edge=GBA_HBLANK_START-p->phase;
            else if(p->line<159)edge=GBA_CYCLES_PER_LINE-p->phase+GBA_HBLANK_START;
            else edge=(228-p->line)*GBA_CYCLES_PER_LINE-p->phase+GBA_HBLANK_START;
        }
        if(edge<distance)distance=edge;
    }
    return distance;
}
GBA_INLINE uint32_t gba_native_hard_distance(const gba_t *g) {
    uint32_t distance;
#if GBA_DYNAREC_HARD_CACHE
#ifdef GBA_TEST_HARD_CACHE_COMPARE
    extern int gba_test_deadline_cache(const gba_t *);
    if(!gba_test_deadline_cache(g)) {
        distance=gba_native_hard_snapshot(g);
        return distance>g->cpu_pending?distance-g->cpu_pending:0;
    }
#endif
    gba_native_cache_t *cache=gba_native_cache(g);
    if(!cache->hard_valid || cache->hard_clock!=g->machine.cycles) {
        cache->hard_cycles=gba_native_hard_snapshot(g);cache->hard_clock=g->machine.cycles;cache->hard_valid=1;
    }
    distance=cache->hard_cycles;
#else
    distance=gba_native_hard_snapshot(g);
#endif
    return distance>g->cpu_pending?distance-g->cpu_pending:0;
}
#if GBA_DYNAREC_FASTCALL
/* ARM-only internal call boundary. GCC preserves the enclosing C function's
 * AAPCS registers once, instead of the generated block saving them again.
 * The native frame uses the real thread stack and restores its own LR. */
GBA_INLINE uint64_t gba_native_invoke(gba_t *g,unsigned argument,uintptr_t address) {
    register uintptr_t a0 __asm__("r0")=(uintptr_t)g;
    register uint32_t a1 __asm__("r1")=argument;
    register uintptr_t entry __asm__("r12")=address;
    __asm__ volatile("blx r12" : "+r"(a0),"+r"(a1),"+r"(entry) : :
        "r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","lr","cc","memory");
    return (uint64_t)(uint32_t)a0|((uint64_t)a1<<32);
}
#endif
GBA_INLINE gba_native_block_t *gba_native_blocks(gba_native_cache_t *cache) {
    return (gba_native_block_t *)(cache + 1);
}
#if GBA_DYNAREC_TRACE_LINKS
GBA_INLINE uint32_t gba_native_trace_pc(const gba_t *g,const gba_native_block_t *b,unsigned count) {
    const uint8_t *p=(const uint8_t *)g->dynarec_memory.code+b->offset+(b->raw_words&2047u)*4;
    return gba_le32(p+b->count*((b->flags&1)?2:4)+count*4);
}
#endif
#if GBA_DYNAREC_TRACE_CONTEXT
GBA_INLINE unsigned gba_native_context_register(uint32_t pc,uint32_t op,unsigned width) {
    if((pc>>24)>=8 && (pc>>24)<=12) {
        if(width==2 && (op&0xff87u)==0x4700u)return (op>>3)&15;
        if(width==4 && (op&0xfffffff0u)==0xe12fff10u)return op&15;
    }
    return 15;
}
#endif
GBA_INLINE unsigned gba_native_recent_index(uint32_t pc) {
    return ((pc>>1)^(pc>>8))&((1u<<GBA_DYNAREC_RECENT_BITS)-1);
}
GBA_INLINE void gba_native_recent_publish(gba_native_cache_t *c,uint32_t pc,gba_native_block_t *b) {
    unsigned index=gba_native_recent_index(pc);
#if GBA_DYNAREC_COARSE_CHAIN
    if(c->recent[index]!=b)c->recent_alt[index]=c->recent[index];
#endif
    c->recent[index]=b;
}
GBA_INLINE unsigned gba_native_page(uint32_t address) {
    if ((address >> 24) == 2)
        return (address & 0x3ffff) >> 8;
    if ((address >> 24) == 3)
        return 1024 + ((address & 0x7fff) >> 8);
    return GBA_JIT_PAGES;
}
#if GBA_DYNAREC_CHAIN
uint64_t gba_native_chain(gba_t *g,uint32_t result,uint32_t executed,uint32_t *frame);
#endif
uint32_t gba_native_fetch_span(gba_t *g, unsigned n, unsigned width, unsigned region);
typedef struct { uint32_t count,phase,sequential; } gba_native_rom_state_t;
uint32_t gba_native_replay_rom(gba_t *g,const gba_native_block_t *b,unsigned count,const gba_native_rom_state_t *saved);
#if GBA_DYNAREC_PERSISTENT_ENGINE
uint32_t gba_persistent_run(gba_t *g, uint32_t budget);
#endif
#endif
