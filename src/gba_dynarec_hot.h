/* Cached pure blocks need neither decoding nor a memory-access replay. */
#pragma once
#include "gba_dynarec_internal.h"
#if GBA_DYNAREC
GBA_INLINE uint32_t gba_dynarec_hot(gba_t *g, uint32_t budget) {
#if GBA_TEST_MODE
    (void)g; (void)budget;
    return 0;
#else
    gba_native_cache_t *cache=gba_native_cache(g);
    gba_cpu_t *cpu=&g->machine.cpu;
    uint32_t pc=cpu->r[15];
    gba_native_block_t *b=cache?cache->recent[gba_native_recent_index(pc)]:NULL;
    unsigned width=(cpu->cpsr&GBA_T)?2:4, region=pc>>24;
#if GBA_DYNAREC_TRACE_CONTEXT
    unsigned context_reg=gba_native_context_register(pc,cpu->pipeline[0],width);
    uint32_t context=context_reg<15?cpu->r[context_reg]:0;
#endif
#if GBA_DYNAREC_NEGATIVE
    if(b && !b->count && b->pc==pc && b->epoch==cache->epoch &&
#if GBA_DYNAREC_TRACE_CONTEXT
        context_reg==15 &&
#endif
        b->cache_key==((region>=8?(GBA_DYNAREC_ROM_TRACE?240u:GBA_DYNAREC_WIDE_ROM?256u:64u):128u)|(width==2)) &&
        b->head[0]==cpu->pipeline[0] && b->head[1]==cpu->pipeline[1]) {
        GBA_NATIVE_COUNT(cache,negative_hits,1);
        return UINT32_MAX; /* Decoder rejection, not a dynamic bus/budget failure. */
    }
#endif
    if (!b || b->count<(GBA_DYNAREC_TINY_BLOCKS?1:2) || b->pc!=pc ||
        b->epoch!=cache->epoch || b->cache_key!=((region==3?128u:GBA_DYNAREC_ROM_TRACE?240u:GBA_DYNAREC_WIDE_ROM?256u:64u)|(width==2?1u:0u)) ||
#if GBA_DYNAREC_TRACE_CONTEXT
        (context_reg<15 && b->prefix_offset!=context) ||
#endif
        (b->flags&3)!=(width==2?3u:2u) ||
        b->head[0]!=cpu->pipeline[0] || b->head[1]!=cpu->pipeline[1] ||
        !g->stepping || !cpu->pipeline_valid || (pc&(width-1)) ||
        (region!=3 && !(region>=8 && region<=12 && g->rom)) ||
        (g->machine.last_fetch>>24)!=region || gba_dma_ready_mask(g) ||
        (cpu->irq_line && !cpu->irq_delay && gba_io16(g,0x208) && !(cpu->cpsr&0x80)) ||
        (cpu->wait_mask && !cpu->hle_irq) || cache->waitcnt!=gba_io16(g,0x204)) return 0;
    if (region==3 && (b->page_generation!=cache->generation[1024+((pc&0x7fff)>>8)] ||
        (b->end_page<GBA_JIT_PAGES && b->end_generation!=cache->generation[b->end_page]))) return 0;
    if (!g->event_cycles || g->event_cycles<=g->cpu_pending) return 0;
    uint32_t distance=g->event_cycles-g->cpu_pending;
    if (budget>distance) budget=distance;
#if GBA_DYNAREC_EVENT_SLIP
    /* The generated block commits guest registers before devices advance.
     * Never add idle guest cycles; only relax the CPU/event interleave. */
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
    unsigned repetitions=1, count=b->count;
    uint32_t fetch=0;
    unsigned replay=region!=3 && ((b->flags&24)||b->side_exits);
    gba_native_rom_state_t saved={g->machine.prefetch_count,g->machine.prefetch_phase,cpu->fetch_sequential};
    if (region==3) {
        if (count+b->extra_bound>=budget) return 0;
        if (b->flags&32) {
            repetitions=(budget-1)/(count+b->extra_bound+b->memory_mask+2);
            if (!GBA_DYNAREC_LONG_LOOPS && repetitions>7) repetitions=7;
            if(repetitions>255/count)repetitions=255/count;
            if (!repetitions) repetitions=1;
        }
    } else if(replay) {
        uint32_t bound=cpu->fetch_sequential?b->fetch_bound_seq:b->fetch_bound_nonseq;
        if(bound+b->extra_bound>=budget)return 0;
#if GBA_DYNAREC_WIDE_ROM
        if(b->flags&32) {
            repetitions=(budget-1)/((count+2)*(b->fetch_bound_seq/count)+b->extra_bound);
            if(!GBA_DYNAREC_LONG_LOOPS && repetitions>7)repetitions=7;
            if(repetitions>255/count)repetitions=255/count;
            if(!repetitions)repetitions=1;
        }
#endif
    } else {
        /* ROM pure blocks contain no variable internal cycles. Compute the
         * exact fetch cost before entering code, rolling back on a near event. */
        if (b->flags&8) return 0;
        uint32_t saved_count=g->machine.prefetch_count, saved_phase=g->machine.prefetch_phase;
        unsigned saved_seq=cpu->fetch_sequential;
        uint32_t saved_access=g->access_cycles;
#if GBA_DYNAREC_FAST_BUS
        fetch=gba_native_replay_rom(g,b,count,&saved);
#else
        fetch=gba_native_fetch_span(g,count,width,region);
#endif
        if (fetch>=budget) {
            g->machine.prefetch_count=saved_count; g->machine.prefetch_phase=saved_phase;
            cpu->fetch_sequential=saved_seq; g->access_cycles=saved_access;
            return 0;
        }
        if (b->flags&32) {
#if GBA_DYNAREC_WIDE_ROM
            unsigned extra=(budget-fetch-1)/((count+2)*(b->fetch_bound_seq/count)+b->extra_bound);
#else
            unsigned extra=(budget-fetch-1)/b->loop_cycles;
#endif
            if (!GBA_DYNAREC_LONG_LOOPS && extra>6) extra=6;
            repetitions+=extra;
            if(repetitions>255/count)repetitions=255/count;
        }
    }
    if(region==3) {
        /* Include every possible ROM-data wait in every repeated iteration.
         * This permits sample-data loops to remain native instead of failing
         * their first ROM load solely because repetitions exceeded one. */
        cache->rom_data_seen=repetitions*(count+b->extra_bound+b->memory_mask+2)-2<budget?2:0;
    }
    if(region!=3)cache->rom_data_seen=2;
    uint64_t (*entry)(gba_t *,unsigned)=(uint64_t (*)(gba_t *,unsigned))
        ((uint8_t *)g->dynarec_memory.code+b->offset);
    uint64_t returned;
#if GBA_DYNAREC_CHAIN
    if((region==3 || (GBA_DYNAREC_ROM_CHAIN && (GBA_DYNAREC_COARSE_CHAIN || (replay && !(b->flags&32))))) && cache->chain_stub) {
#if GBA_DYNAREC_ROM_CHAIN
        cache->chain_rom_count=saved.count;cache->chain_rom_phase=saved.phase;cache->chain_rom_sequential=saved.sequential;
#endif
        uint64_t (*chain_entry)(gba_t *,unsigned,uint32_t,gba_native_block_t *)=
            (uint64_t (*)(gba_t *,unsigned,uint32_t,gba_native_block_t *))((uint8_t *)g->dynarec_memory.code+b->offset);
        returned=chain_entry(g,count|(repetitions<<8)|0x80000000u,budget,b);
        if(returned&(UINT64_C(1)<<62))return (uint32_t)returned;
    } else
#endif
        {
#if GBA_DYNAREC_FASTCALL
        returned=gba_native_invoke(g,count|(repetitions<<8),(uintptr_t)entry);
#else
        returned=entry(g,count|(repetitions<<8));
#endif
        }
    unsigned executed=(unsigned)(returned>>32)&255;
    GBA_NATIVE_COUNT(cache,native_entries,1);
    GBA_NATIVE_COUNT(cache,zero_entries,!executed);
    GBA_NATIVE_COUNT(cache,guard_returns,(unsigned)(returned>>63));
#if GBA_DYNAREC_STATS
    unsigned bucket=0;while(bucket<7 && executed>(1u<<bucket))bucket++;
    cache->stats.entries_by_size[bucket]++;
#endif
    if(!executed)return 0;
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    if((returned>>48)&1) {
        if(g->wait.valid)g->wait.reject_pc=g->wait.pc;
        gba_wait_invalidate(g);
    }
#endif
    unsigned memory=(unsigned)(returned>>40)&255;
    GBA_NATIVE_COUNT(cache,cache_hits,1);
    GBA_NATIVE_COUNT(cache,native_instructions,executed-memory);
    GBA_NATIVE_COUNT(cache,memory_instructions,memory);
    (void)memory;
    uint32_t result=(uint32_t)returned;
#if GBA_DYNAREC_FAST_BUS
    /* Repeated pure loops also executed every fetch. Charge the actual prefix
     * length, not just the first iteration examined by the entry guard. */
    if(region!=3) {
        fetch=gba_native_replay_rom(g,b,executed,&saved);
        g->access_cycles+=result>>24;
    }
#else
    if(replay)fetch=gba_native_replay_rom(g,b,executed,&saved);
#endif
    if(region==3 && (cache->rom_data_seen&1))g->machine.prefetch_count=g->machine.prefetch_phase=0;
    if((b->flags&32) && (returned>>63) && !(executed%count)) {
        cpu->pipeline_valid=cpu->fetch_sequential=0;cpu->r[15]=pc;
        g->machine.prefetch_count=g->machine.prefetch_phase=0;
    }
    if (region!=3 && executed>count) {
        /* Every taken back edge refills from an empty FIFO. Native execution
         * charged the intervening complete iterations in loop_cycles. */
        g->machine.prefetch_count=g->machine.prefetch_phase=0;
        if (cpu->pipeline_valid) cpu->fetch_sequential=1;
    }
    unsigned final_count=(b->flags&32)?((executed-1)%count)+1:executed;
    uint32_t next_pc=pc+final_count*width;
#if GBA_DYNAREC_TRACE_LINKS
    if(region!=3)next_pc=gba_native_trace_pc(g,b,final_count);
#endif
    const uint8_t *source=region==3?g->memory.iwram+(pc&0x7fff):g->rom+(pc&0x1ffffff);
#if GBA_DYNAREC_TRACE_LINKS
    if(region!=3) {
        source=g->rom+(next_pc&0x1ffffff);
        cpu->pipeline[0]=width==2?gba_le16(source):gba_le32(source);
        cpu->pipeline[1]=width==2?gba_le16(source+width):gba_le32(source+width);
    } else
#endif
    {
        cpu->pipeline[0]=width==2?gba_le16(source+final_count*width):gba_le32(source+final_count*width);
        cpu->pipeline[1]=width==2?gba_le16(source+(final_count+1)*width):gba_le32(source+(final_count+1)*width);
    }
    if (cpu->pipeline_valid) {
        cpu->r[15]=next_pc;
        if (region==3) cpu->fetch_sequential=!(result&0x00800000u) &&
            !((result>>16) && !(gba_io16(g,0x204)&0x4000));
    }
    if (g->machine.dma_bus_valid && (executed>1 || pc!=g->machine.dma_pc)) g->machine.dma_bus_valid=0;
    g->machine.last_fetch=next_pc+width;
    g->machine.open_bus=width==2?cpu->pipeline[1]*0x10001u:cpu->pipeline[1];
    if (region==3) g->access_cycles=1+(result>>24);
    uint32_t cycles=(region==3?executed:fetch)+(result&0xffff);
    gba_cpu_charge(g,cycles);
    return cycles;
#endif
}
#endif
