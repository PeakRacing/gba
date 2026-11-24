/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
/* Private A32 executor. Included after the shared decoder/emission primitives.
 * r4=context, r5-r10=fixed guest registers, r11=remaining guest cycles.
 * The real thread stack and AAPCS boundary remain intact. No code is patched
 * while generated code runs. All addresses below are arena-relative offsets. */
#pragma once
#if GBA_DYNAREC_PERSISTENT_ENGINE
#ifdef GBA_TEST_BRANCH_SUPERBLOCK_PROOF
unsigned gba_test_branch_inline;
#endif
#ifdef GBA_TEST_TURBO_TRACE_DIAG
volatile uint32_t gba_test_turbo_branch_src, gba_test_turbo_branch_target;
volatile uint32_t gba_test_turbo_entry_pc, gba_test_turbo_exit_pc, gba_test_turbo_exit_meta;
volatile uint32_t gba_test_turbo_reject_pc[8], gba_test_turbo_reject_count[8], gba_test_turbo_reject_total;
volatile uint32_t gba_test_turbo_reject_reason[15];
volatile uint32_t gba_test_turbo_reject_reason_pc[15];
#endif
typedef struct {
    uint32_t pc, epoch, offset, refill, end;
#if GBA_DYNAREC_PAGE_INDEX
    uint32_t page_members;
#endif
    uint32_t target[6], patch[6], cold[6];
    uint32_t next[6];
    uint16_t count,bound,edge_count;
    uint8_t thumb, valid, taken;
} gba_persistent_block_t;
enum { GBA_PERSISTENT_EDGES = 6 };
_Static_assert(sizeof(gba_persistent_block_t)<=160,"persistent metadata capacity and link-index bound");

static gba_persistent_block_t *gba_persistent_table(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    return (gba_persistent_block_t *)((uint8_t *)c+c->persistent_offset);
}
static unsigned gba_persistent_index(const gba_native_cache_t *c,uint32_t pc,unsigned thumb) {
    return (((pc>>1)^(pc>>11)^(thumb*173u))&(c->persistent_slots/4-1))*4;
}
static gba_persistent_block_t *gba_persistent_lookup(gba_t *g,uint32_t pc,unsigned thumb) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_persistent_block_t *set=gba_persistent_table(g)+gba_persistent_index(c,pc,thumb);
    for(unsigned way=0;way<4;way++)if(set[way].valid && set[way].epoch==c->epoch &&
        set[way].pc==pc && set[way].thumb==thumb)return set+way;
    return NULL;
}
static unsigned gba_persistent_incoming_index(uint32_t pc) {return ((pc>>1)^(pc>>9))&255;}
static unsigned gba_persistent_indirect_index(uint32_t pc) {
    return ((pc>>1)^(pc>>11))&((1u<<GBA_DYNAREC_PERSISTENT_INDIRECT_BITS)-1);
}
static void gba_persistent_map(gba_a32_t *e,unsigned thumb) {
    static const unsigned arm[6]={0,1,6,9,12,14};
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_REGISTER_MAP == 1
    static const unsigned thumb_map[6]={0,1,2,3,6,7};
#elif GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_REGISTER_MAP == 2
    static const unsigned thumb_map[6]={0,1,4,5,6,7};
#elif GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_REGISTER_MAP == 3
    static const unsigned thumb_map[6]={0,1,2,4,6,7};
#else
    static const unsigned thumb_map[6]={0,1,2,3,4,5};
#endif
    for(unsigned i=0;i<6;i++)e->map[thumb?thumb_map[i]:arm[i]]=5+i;
}
static uint32_t gba_persistent_word(gba_t *g,uint32_t pc,unsigned width) {
    unsigned region=pc>>24;
    const uint8_t *p=region==3?g->memory.iwram+(pc&0x7fff):
        region==2?g->memory.ewram+(pc&0x3ffff):g->rom+(pc&0x1ffffff);
    return width==2?gba_le16(p):gba_le32(p);
}
static unsigned gba_persistent_region(gba_t *g,uint32_t pc,unsigned width) {
    unsigned region=pc>>24;
    if(pc&(width-1))return 0;
    if(region==3)return (pc&0x7fff)<=0x8000-3*width;
    if(region==2)return (pc&0x3ffff)<=0x40000-3*width;
    /* Exact ROM FIFO replay is deliberately left with the existing backend.
     * The persistent ROM route uses the SAME explicit FAST_BUS contract. */
    if(GBA_DYNAREC_FAST_BUS && region>=8 && region<=12 && g->rom) {
        uint32_t offset=pc&0x1ffffff;
        /* The cartridge header/GPIO window ends at 0x00cc. Game code commonly
         * starts immediately after it; the header below that window remains
         * scalar so GPIO/header reads cannot become native data accesses. */
        return offset>=0xcc && offset<g->rom_size && 3*width<=g->rom_size-offset;
    }
    return 0;
}
static unsigned gba_persistent_fetch(gba_t *g,uint32_t pc,unsigned width) {
    if((pc>>24)==3)return 1;
    if((pc>>24)==2)return width==4?6:3;
#if GBA_DYNAREC_FAST_BUS
    return gba_native_fetch_floor(gba_io16(g,0x204),width,pc>>24);
#else
    (void)g;(void)width;return 0;
#endif
}
static unsigned gba_persistent_refill(gba_t *g,uint32_t pc,unsigned width) {
#if GBA_DYNAREC_FAST_BUS
    if((pc>>24)>=8)return gba_native_refill_cost(gba_io16(g,0x204),width,pc>>24);
#else
    (void)g;
#endif
    if((pc>>24)==2)return width==4?12:6;
    return 2;
}
static void gba_persistent_fetch_commit(gba_a32_t *e,unsigned *pending) {
    if(!*pending)return;
    uint32_t immediate=gba_a32_immediate(*pending);
    if(immediate!=UINT32_MAX) {
        gba_a32_emit(e,0xe24bb000u|immediate);
    } else {
        gba_a32_constant(e,3,*pending);
        gba_a32_emit(e,0xe04bb003u);
    }
    *pending=0;
}
static void gba_persistent_jump(gba_a32_t *e,uintptr_t target) {
    gba_a32_literal(e,3,(uint32_t)target);gba_a32_emit(e,0xe12fff13u);
}
static void gba_persistent_exit_to(gba_t *g,gba_a32_t *e,unsigned thumb,
    uint32_t pc,unsigned meta,uint32_t last_fetch) {
    gba_a32_constant(e,0,pc);gba_a32_constant(e,1,meta|thumb|(thumb<<3));
    gba_a32_constant(e,2,last_fetch);
    gba_persistent_jump(e,(uintptr_t)g->dynarec_memory.code+gba_native_cache(g)->persistent_exit[thumb]);
}
static int gba_persistent_stubs(gba_t *g,unsigned thumb) {
    gba_native_cache_t *c=gba_native_cache(g);
    if(c->persistent_entry[thumb])return 0;
    uint32_t start=c->used;
    if(g->dynarec_memory.code_size-start<1024)return -1;
    uint32_t *code=(uint32_t *)((uint8_t *)g->dynarec_memory.code+start);
    if(gba_code_begin_write(code,1024))return -1;
    gba_a32_t e={.begin=code,.next=code,.end=code+256};
    gba_persistent_map(&e,thumb);
    /* Entry receives (g,budget,body). Preserve r4-r11/LR once, not per block. */
    gba_a32_emit(&e,0xe92d4ff0u);gba_a32_emit(&e,0xe24dd02cu);
    gba_a32_emit(&e,0xe58d2000u);gba_a32_emit(&e,0xe58d1004u);
    gba_a32_emit(&e,0xe1a04000u);gba_a32_emit(&e,0xe1a0b001u);
    gba_a32_load(&e,3,4,offsetof(gba_t,machine.last_fetch));
    if(thumb)gba_a32_emit(&e,0xe3833001u);
    gba_a32_emit(&e,0xe58d3008u);
    gba_a32_emit(&e,0xe3a03000u);
#if GBA_DYNAREC_STATS
    gba_a32_emit(&e,0xe58d300cu);gba_a32_emit(&e,0xe58d3010u);
    gba_a32_emit(&e,0xe58d3020u);gba_a32_emit(&e,0xe58d3024u);
#endif
    gba_a32_emit(&e,0xe58d3014u);
    gba_a32_emit(&e,0xe58d3018u);gba_a32_emit(&e,0xe58d301cu);
    for(unsigned r=0;r<15;r++)if(e.map[r])gba_a32_load(&e,e.map[r],4,r*4);
    gba_a32_load(&e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(&e,0xe128f00cu);
    gba_a32_emit(&e,0xe59d2000u);
#if GBA_DYNAREC_STATS
    gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3000u);
#endif
    gba_a32_emit(&e,0xe12fff12u);
    uint32_t exit=start+(uint32_t)(e.next-code)*4;
    /* Exit receives next PC, pipeline flags and final fetch address. */
    gba_a32_emit(&e,0xe10fc000u);
    gba_a32_store(&e,0,4,60);
    gba_a32_store(&e,2,4,offsetof(gba_t,machine.last_fetch));
    gba_a32_load(&e,3,4,offsetof(gba_t,dynarec_memory.data));
    gba_a32_constant(&e,2,offsetof(gba_native_cache_t,persistent_meta));
    gba_a32_emit(&e,0xe0833002u);gba_a32_store(&e,1,3,0);
#if GBA_DYNAREC_STATS
    gba_a32_emit(&e,0xe59d200cu);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_guest_count)-offsetof(gba_native_cache_t,persistent_meta));
    gba_a32_emit(&e,0xe59d2010u);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_block_count)-offsetof(gba_native_cache_t,persistent_meta));
    gba_a32_emit(&e,0xe59d2020u);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_memory_count)-offsetof(gba_native_cache_t,persistent_meta));
    gba_a32_emit(&e,0xe59d2024u);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_indirect_count)-offsetof(gba_native_cache_t,persistent_meta));
    gba_a32_emit(&e,0xe59d2000u);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_multi_count)-offsetof(gba_native_cache_t,persistent_meta));
#endif
    gba_a32_emit(&e,0xe59d2014u);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_internal)-offsetof(gba_native_cache_t,persistent_meta));
    gba_a32_emit(&e,0xe59d2018u);gba_a32_store(&e,2,3,
        offsetof(gba_native_cache_t,persistent_data_cost)-offsetof(gba_native_cache_t,persistent_meta));
    for(unsigned r=0;r<15;r++)if(e.map[r])gba_a32_store(&e,e.map[r],4,r*4);
    gba_a32_load(&e,0,4,offsetof(gba_cpu_t,cpsr));
    gba_a32_emit(&e,0xe3c0020fu);gba_a32_emit(&e,0xe20cc20fu);gba_a32_emit(&e,0xe180000cu);
    gba_a32_store(&e,0,4,offsetof(gba_cpu_t,cpsr));
    gba_a32_emit(&e,0xe59d0004u);gba_a32_emit(&e,0xe040000bu);
    gba_a32_emit(&e,0xe28dd02cu);gba_a32_emit(&e,0xe8bd8ff0u);
    uint32_t bridge=start+(uint32_t)(e.next-code)*4;
    /* Store every old mapping before loading any overlapping new mapping. */
    for(unsigned r=0;r<15;r++)if(e.map[r])gba_a32_store(&e,e.map[r],4,r*4);
    gba_memset(e.map,0,sizeof(e.map));gba_persistent_map(&e,thumb^1u);
    for(unsigned r=0;r<15;r++)if(e.map[r])gba_a32_load(&e,e.map[r],4,r*4);
    gba_a32_load(&e,2,4,offsetof(gba_cpu_t,cpsr));
    gba_a32_emit(&e,thumb?0xe3c22020u:0xe3822020u);
    gba_a32_store(&e,2,4,offsetof(gba_cpu_t,cpsr));
    gba_a32_emit(&e,0xe128f00cu);gba_a32_emit(&e,0xe12fff13u);
    if(e.failed || gba_code_publish(code,(size_t)(e.next-code)*4))return -1;
    c->persistent_entry[thumb]=start;c->persistent_exit[thumb]=exit;
    c->persistent_bridge[thumb]=bridge;
    c->used=(start+(uint32_t)(e.next-code)*4+31u)&~31u;
    return 0;
}
static int gba_persistent_patch(gba_t *g,uint32_t offset,uint32_t target) {
    uint32_t *p=(uint32_t *)((uint8_t *)g->dynarec_memory.code+offset);
    if(gba_code_begin_write(p,4))return -1;
    *p=(uint32_t)(uintptr_t)g->dynarec_memory.code+target;
    return gba_code_publish(p,4);
}
#if GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH
static int gba_persistent_patch_branch(gba_t *g,uint32_t offset,uint32_t target) {
    int32_t delta=(int32_t)target-(int32_t)offset-8;
    if(delta<-0x2000000 || delta>0x1fffffc || (delta&3))return -1;
    uint32_t *p=(uint32_t *)((uint8_t *)g->dynarec_memory.code+offset);
    if(gba_code_begin_write(p,4))return -1;
    *p=0xea000000u|((uint32_t)(delta/4)&0xffffffu);
    return gba_code_publish(p,4);
}
#endif
/* Revoke incoming links before a metadata slot or physical code page changes.
 * Cold-path table scans are bounded by caller-owned metadata, never allocations. */
static int gba_persistent_unlink(gba_t *g,uint32_t pc,unsigned thumb) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_persistent_block_t *table=gba_persistent_table(g);
    unsigned cache=gba_persistent_indirect_index(pc);
    if(c->persistent_indirect[cache].key==(pc|thumb)) {
        c->persistent_indirect[cache].key=0;c->persistent_indirect[cache].entry=0;
    }
    unsigned link=c->persistent_incoming[gba_persistent_incoming_index(pc)];
    while(link) {
        unsigned n=(link-1)/GBA_PERSISTENT_EDGES,edge=(link-1)%GBA_PERSISTENT_EDGES;
        if(table[n].valid && table[n].thumb==thumb && table[n].target[edge]==pc) {
#if GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH
            if(g->dynarec_memory.code_size<=0x2000000u) {
                if(gba_persistent_patch_branch(g,table[n].patch[edge],table[n].cold[edge]))return -1;
            } else
#endif
            if(gba_persistent_patch(g,table[n].patch[edge],table[n].cold[edge]))return -1;
        }
        link=table[n].next[edge];
    }
    return 0;
}
static void gba_persistent_forget_edges(gba_t *g,gba_persistent_block_t *block) {
    gba_native_cache_t *c=gba_native_cache(g);gba_persistent_block_t *table=gba_persistent_table(g);
    unsigned n=(unsigned)(block-table);
    for(unsigned edge=0;edge<block->edge_count;edge++)if(block->patch[edge]) {
        unsigned id=n*GBA_PERSISTENT_EDGES+edge+1;
        uint32_t *cursor=&c->persistent_incoming[gba_persistent_incoming_index(block->target[edge])];
        while(*cursor && *cursor!=id) {
            unsigned link=*cursor-1;cursor=&table[link/GBA_PERSISTENT_EDGES].next[link%GBA_PERSISTENT_EDGES];
        }
        if(*cursor)*cursor=block->next[edge];
        block->next[edge]=0;block->patch[edge]=0;
    }
}
#include "gba_dynarec_page_index.h"
static void gba_persistent_invalidate(gba_t *g,uint32_t address,size_t size) {
    gba_native_cache_t *c=gba_native_cache(g);
    if(!c->persistent_slots)return;
    if(size==SIZE_MAX || (address>>24)<2 || (address>>24)>3 || size>0x40000) {
        c->persistent_reset=1;return;
    }
    if(c->persistent_reset)return; /* Published offsets may already be reused. */
#if GBA_DYNAREC_PAGE_INDEX
    gba_page_index_invalidate(g,address,size);
#else
    gba_persistent_block_t *table=gba_persistent_table(g);
    unsigned region=address>>24,mask=region==3?0x7fff:0x3ffff;
    /* Ordinary data writes (including DMA bursts) must not scan the code
     * registry. The shared physical-line map includes persistent lookahead. */
    size_t remaining=size;uint32_t probe=address;unsigned protected=0;
    while(remaining) {
        unsigned offset=probe&mask,page=(region==3?1024:0)+(offset>>8);
        if(c->code_page[page]&(1u<<((offset>>5)&7))) {protected=1;break;}
        size_t step=32-(offset&31);if(step>remaining)step=remaining;
        remaining-=step;probe+=(uint32_t)step;
    }
    if(!protected)return;
    /* A page may have several mirrors. Include both lookahead instructions. */
    for(unsigned n=0;n<c->persistent_slots;n++)if(table[n].valid && (table[n].pc>>24)==region) {
        uint32_t first=table[n].pc&mask,last=(table[n].end&mask)+2*(table[n].thumb?2u:4u);
        uint32_t at=address&mask;
        unsigned overlap=size>mask+1-at ? at<last || size-(mask+1-at)>first : at<last && at+size>first;
        if(overlap) {
            if(gba_persistent_unlink(g,table[n].pc,table[n].thumb))gba_fail(g,GBA_ERROR_IO);
            gba_persistent_forget_edges(g,table+n);
            table[n].valid=0;
        }
    }
#endif
}
#if GBA_DYNAREC_PERSISTENT_PAGE_TABLE || GBA_DYNAREC_TURBO_TLB
static void gba_persistent_pages_rebuild(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
#if GBA_DYNAREC_PERSISTENT_PAGE_TABLE
    gba_memset(c->persistent_read_page,0,sizeof(c->persistent_read_page));
    gba_memset(c->persistent_write_page,0,sizeof(c->persistent_write_page));
    for(uint32_t address=0x02000000u;address<0x03000000u;address+=0x8000u) {
        unsigned index=address>>15;
        uint32_t base=(uint32_t)(uintptr_t)(g->memory.ewram+(address&0x3fffc000u));
        if(base&3)continue;
        c->persistent_read_page[index]=base|3;c->persistent_write_page[index]=base|3;
    }
    for(uint32_t address=0x03000000u;address<0x04000000u;address+=0x8000u) {
        unsigned index=address>>15;
        uint32_t base=(uint32_t)(uintptr_t)(g->memory.iwram+(address&0x7fffu));
        if(base&3)continue;
        c->persistent_read_page[index]=base|2;c->persistent_write_page[index]=base|2;
    }
    for(uint32_t address=0x08000000u;address<0x0e000000u && g->rom;address+=0x8000u) {
        uint32_t offset=address&0x1ffffffu;
        if(offset<0x8000u || offset>=g->rom_size || g->rom_size-offset<0x8000u)continue;
        unsigned index=address>>15;
        uint32_t base=(uint32_t)(uintptr_t)(g->rom+offset);
        if(!(base&3))c->persistent_read_page[index]=base|1;
    }
#endif
#if GBA_DYNAREC_TURBO_TLB
    gba_memset(c->turbo_read_tlb,0,sizeof(c->turbo_read_tlb));
    gba_memset(c->turbo_write_tlb,0,sizeof(c->turbo_write_tlb));
    gba_memset(c->turbo_read_cost,0,sizeof(c->turbo_read_cost));
    gba_memset(c->turbo_write_cost,0,sizeof(c->turbo_write_cost));
    for(uint32_t address=0x02000000u;address<0x02040000u;address+=0x8000u) {
        unsigned index=address>>15;
        uint32_t base=(uint32_t)(uintptr_t)(g->memory.ewram+(address&0x3ffffu));
        if(base&3)continue;
        c->turbo_read_tlb[index]=base|2;c->turbo_write_tlb[index]=base|2;
        c->turbo_read_cost[0][index]=4;c->turbo_read_cost[1][index]=4;c->turbo_read_cost[2][index]=7;
        c->turbo_write_cost[0][index]=3;c->turbo_write_cost[1][index]=3;c->turbo_write_cost[2][index]=6;
    }
    {
        unsigned index=0x03000000u>>15;
        uint32_t base=(uint32_t)(uintptr_t)g->memory.iwram;
        if(!(base&3)) {
            c->turbo_read_tlb[index]=base|2;c->turbo_write_tlb[index]=base|2;
            c->turbo_read_cost[0][index]=c->turbo_read_cost[1][index]=c->turbo_read_cost[2][index]=2;
            c->turbo_write_cost[0][index]=c->turbo_write_cost[1][index]=c->turbo_write_cost[2][index]=1;
        }
    }
    if(g->rom) {
        static const unsigned first[4]={4,3,2,8};
        uint16_t wait=gba_io16(g,0x204);
        for(uint32_t address=0x08000000u;address<0x0e000000u;address+=0x8000u) {
            uint32_t offset=address&0x1ffffffu;
            if(offset<0x1000u || offset>=g->rom_size || g->rom_size-offset<0x8000u)continue;
            unsigned index=address>>15,bank=((address>>24)-8)/2;
            uint32_t base=(uint32_t)(uintptr_t)(g->rom+offset);
            if(base&3)continue;
            c->turbo_read_tlb[index]=base|3;
            for(unsigned width=0;width<3;width++)
                c->turbo_read_cost[width][index]=(uint8_t)(first[(wait>>(2+bank*3))&3]+2+
                    (width==2?gba_gamepak_seq(wait,bank):0));
        }
    }
#endif
}
#endif
static void gba_persistent_reset(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_memset(gba_persistent_table(g),0,c->persistent_slots*sizeof(gba_persistent_block_t));
#if GBA_DYNAREC_PAGE_INDEX
    gba_page_index_reset(g);
#endif
    c->persistent_entry[0]=c->persistent_entry[1]=0;
    c->persistent_exit[0]=c->persistent_exit[1]=0;
    c->persistent_bridge[0]=c->persistent_bridge[1]=0;
#if GBA_DYNAREC_PERSISTENT_MAP_HELPER
    gba_memset(c->persistent_mapper,0,sizeof(c->persistent_mapper));
#endif
    gba_memset(c->persistent_indirect,0,sizeof(c->persistent_indirect));
    gba_memset(c->persistent_incoming,0,sizeof(c->persistent_incoming));
    c->persistent_reset=0;
#if GBA_DYNAREC_PERSISTENT_PAGE_TABLE || GBA_DYNAREC_TURBO_TLB
    gba_persistent_pages_rebuild(g);
#endif
}
#if GBA_DYNAREC_TURBO_LOCAL_ALU
static void gba_turbo_local_flush(gba_a32_t *e) {
    for(unsigned slot=0;slot<GBA_DYNAREC_TURBO_LOCAL_REGS;slot++) {
        if(!e->local_guest[slot])continue;
        unsigned guest=e->local_guest[slot]-1,host=slot?12:14;
        gba_a32_store(e,host,4,guest*4);
#ifdef GBA_TEST_LOCAL_ALU_COUNT
        extern unsigned gba_test_local_alu_count;
        gba_a32_literal(e,3,(uint32_t)(uintptr_t)&gba_test_local_alu_count);
        gba_a32_emit(e,0xe5930000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe5830000u);
#endif
        e->map[guest]=0;e->local_guest[slot]=0;
    }
}
static void gba_turbo_local_admit(gba_a32_t *e,uint32_t op) {
    unsigned kind=(op>>21)&15,rd=(op>>12)&15;
    if((kind>=8 && kind<=11) || rd==15 || e->map[rd])return;
    unsigned slot=0;for(;slot<GBA_DYNAREC_TURBO_LOCAL_REGS;slot++)if(!e->local_guest[slot])break;
    if(slot==GBA_DYNAREC_TURBO_LOCAL_REGS)return;
    unsigned host=slot?12:14;
    /* Full overwrites need no old value. Read/modify/write aliases must be
     * initialized before changing the map; no guest flags change. */
    unsigned ignore=kind==13 || kind==15,immediate=op&0x02000000u;
    unsigned reads=(!ignore && ((op>>16)&15)==rd) ||
        (!immediate && ((op&15)==rd || ((op&16) && ((op>>8)&15)==rd)));
    if(reads)gba_a32_load(e,host,4,rd*4);
    e->map[rd]=host;e->local_guest[slot]=rd+1;
}
#endif
static void gba_persistent_alu(gba_a32_t *e,uint32_t raw,uint32_t op,uint32_t pc,unsigned width) {
    if(width==2 && (raw&0xf800)==0xf000) {
        gba_a32_constant(e,0,pc+4+(uint32_t)(gba_sign(raw&0x7ff,11)*4096));
        gba_a32_guest_write(e,14,0);return;
    }
    unsigned rn=(op>>16)&15,rd=(op>>12)&15,kind=(op>>21)&15;
    unsigned test=kind>=8 && kind<=11,ignore=kind==13 || kind==15;
    unsigned immediate=op&0x02000000,shifted=!immediate && (op&16);
    unsigned advance=width==2?4:shifted?12:8;
    uint32_t source_pc=pc+advance;
    if(width==2 && (raw&0xf800)==0xa000)source_pc&=~3u;
    unsigned hrn=ignore?0:e->map[rn]?e->map[rn]:0;
    unsigned hrd=test?0:e->map[rd]?e->map[rd]:2;
    if(!ignore && !e->map[rn])gba_a32_guest_read(e,0,rn,source_pc);
    uint32_t operand=op&0xfff;
    if(!immediate) {
        unsigned rm=op&15,rs=(op>>8)&15,hrm=e->map[rm]?e->map[rm]:1;
        if(!e->map[rm])gba_a32_guest_read(e,1,rm,pc+advance);
        operand=(operand&~15u)|hrm;
        if(shifted) {
            unsigned hrs=e->map[rs]?e->map[rs]:3;
            if(!e->map[rs])gba_a32_guest_read(e,3,rs,pc+(width==2?4:8));
            operand=(operand&~0xf00u)|(hrs<<8);
            gba_a32_emit(e,0xe24bb001u);
        }
    }
    gba_a32_emit(e,0xe0000000u|(op&0x03f00000u)|operand|(hrn<<16)|(hrd<<12));
    if(!test && !e->map[rd])gba_a32_guest_write(e,rd,2);
}
static void gba_persistent_multiply(gba_a32_t *e,uint32_t op,uint32_t pc) {
    unsigned rm=op&15,rs=(op>>8)&15,rd=(op>>16)&15;
    unsigned hrm=e->map[rm]?e->map[rm]:0,hrs=e->map[rs]?e->map[rs]:1;
    if(!e->map[rm])gba_a32_guest_read(e,0,rm,pc+8);
    if(!e->map[rs])gba_a32_guest_read(e,1,rs,pc+8);
    gba_a32_emit(e,0xe0202fc0u|(hrs<<16)|hrs);gba_a32_emit(e,0xe3822080u);
    gba_a32_emit(e,0xe16f2f12u);gba_a32_emit(e,0xe1a021a2u);gba_a32_emit(e,0xe2622004u);
    if(op&0x200000) {
        unsigned rn=(op>>12)&15;
        gba_a32_guest_read(e,3,rn,pc+8);
        gba_a32_emit(e,0xe0233090u|(hrs<<8)|hrm);gba_a32_emit(e,0xe2822001u);
    } else gba_a32_emit(e,0xe0030090u|(hrs<<8)|hrm);
    if(op&0x100000)gba_a32_emit(e,0xe1130003u);
    gba_a32_guest_write(e,rd,3);gba_a32_emit(e,0xe04bb002u);gba_a32_emit(e,0xe58d2014u);
    gba_a32_emit(e,0xe3a03000u);gba_a32_emit(e,0xe58d3018u);
}
#if GBA_DYNAREC_TURBO_LIGHT
static void gba_turbo_light_multiply_long(gba_t *g,gba_a32_t *e,uint32_t op,uint32_t pc) {
    (void)g;
    unsigned low=(op>>12)&15,high=(op>>16)&15;
    gba_a32_guest_read(e,2,op&15,pc+8);gba_a32_guest_read(e,3,(op>>8)&15,pc+8);
    /* ARM7 termination count, including signed sign-extension and accumulate. */
    if(op&0x400000)gba_a32_emit(e,0xe023efc3u); /* EOR lr,r3,r3,ASR #31 */
    else gba_a32_emit(e,0xe1a0e003u);
    gba_a32_emit(e,0xe38ee080u);gba_a32_emit(e,0xe16fef1eu);
    gba_a32_emit(e,0xe1a0e1aeu);gba_a32_emit(e,0xe26ee004u);
    gba_a32_emit(e,0xe28ee000u|((op&0x200000)?2u:1u));
    if(op&0x200000) {gba_a32_guest_read(e,0,low,pc+8);gba_a32_guest_read(e,1,high,pc+8);}
    gba_a32_emit(e,0xe0810392u|(op&0x600000)); /* [S/U]M[U/A]LL r0,r1,r2,r3, no S */
    gba_a32_guest_write(e,low,0);gba_a32_guest_write(e,high,1);
#if GBA_DYNAREC_STATS
    gba_a32_literal(e,3,(uint32_t)(uintptr_t)&gba_native_cache(g)->stats.turbo_long_multiply);
    gba_a32_emit(e,0xe593c000u);gba_a32_emit(e,0xe28cc001u);gba_a32_emit(e,0xe583c000u);
#endif
    if(op&0x100000) {
        /* Preserve guest C/V explicitly; ARM7 long multiply updates N/Z only. */
        gba_a32_emit(e,0xe10fc000u);gba_a32_emit(e,0xe3ccc103u);
        gba_a32_emit(e,0xe2013102u);gba_a32_emit(e,0xe18cc003u);
        gba_a32_emit(e,0xe1903001u);gba_a32_emit(e,0x038cc101u);
        gba_a32_emit(e,0xe128f00cu);
    }
    gba_a32_emit(e,0xe04bb00eu);gba_a32_emit(e,0xe58de014u);
    gba_a32_emit(e,0xe3a03000u);gba_a32_emit(e,0xe58d3018u);
}
#endif
typedef struct {
    unsigned rn,rd,bytes,load,sign,reg_offset,rm,shift,up,wb,post;
    uint32_t offset;
} gba_persistent_memory_t;
static int gba_persistent_memory_decode(uint32_t raw,unsigned width,gba_persistent_memory_t *m) {
    *m=(gba_persistent_memory_t){.bytes=4,.up=1};
    if(width==4) {
        m->rn=(raw>>16)&15;m->rd=(raw>>12)&15;m->load=!!(raw&0x100000);
        m->up=!!(raw&0x800000);m->post=!(raw&0x1000000);m->wb=m->post || (raw&0x200000);
        if(m->rn==15 && m->wb)return 0;
        m->rm=raw&15;
        if((raw&0x0c000000)==0x04000000) {
            if((raw&0x02000010)==0x02000010)return 0;
            m->bytes=(raw&0x400000)?1:4;
            m->reg_offset=!!(raw&0x2000000);m->shift=raw&0xff0;
            m->offset=raw&0xfff;return 1;
        }
        if((raw&0x0e000090)==0x00000090 && (raw&0x60)) {
            unsigned type=(raw>>5)&3;
            if(!m->load && type!=1)return 0;
            m->bytes=type==2?1:2;m->sign=type>1;
            m->reg_offset=!(raw&0x400000);m->offset=((raw>>4)&0xf0)|(raw&15);return 1;
        }
        return 0;
    }
    m->rd=raw&7;m->rn=(raw>>3)&7;
    if((raw&0xf800)==0x4800) {
        m->rn=15;m->rd=(raw>>8)&7;m->load=1;m->offset=(raw&255)*4;
    } else if((raw&0xf000)==0x5000) {
        unsigned kind=(raw>>9)&7;
        m->reg_offset=1;m->rm=(raw>>6)&7;m->load=kind>=3;
        m->bytes=kind==1 || kind==5 || kind==7?2:kind==2 || kind==3 || kind==6?1:4;
        m->sign=kind==3 || kind==7;
    } else if((raw&0xe000)==0x6000) {
        m->bytes=(raw&0x1000)?1:4;m->load=!!(raw&0x800);m->offset=((raw>>6)&31)*m->bytes;
    } else if((raw&0xf000)==0x8000) {
        m->bytes=2;m->load=!!(raw&0x800);m->offset=((raw>>6)&31)*2;
    } else if((raw&0xf000)==0x9000) {
        m->rn=13;m->rd=(raw>>8)&7;m->load=!!(raw&0x800);m->offset=(raw&255)*4;
    } else return 0;
    return 1;
}
typedef struct {
    uint32_t *site[32];
    unsigned prefix[32],count;
} gba_persistent_reservations_t;
typedef struct {
    uint32_t *site[16];
    uint32_t pc[16];
    unsigned prefix[16],count;
} gba_persistent_side_exits_t;
static void gba_persistent_local_reject(gba_t *g,gba_a32_t *e,uint32_t pc,unsigned thumb,unsigned prefix,unsigned fetch) {
    if(!prefix) {
        gba_a32_emit(e,0xe59d301cu);gba_a32_emit(e,0xe3530000u);
        uint32_t *normal=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_emit(e,0xe08bb003u);gba_a32_emit(e,0xe28bb000u|fetch);
        gba_a32_emit(e,0xe128f00cu);gba_a32_constant(e,0,pc);gba_a32_constant(e,1,thumb);
        gba_a32_emit(e,0xe59d2008u);gba_a32_emit(e,0xe2023001u);gba_a32_emit(e,0xe1811183u);gba_a32_emit(e,0xe3c22001u);
        gba_persistent_jump(e,(uintptr_t)g->dynarec_memory.code+gba_native_cache(g)->persistent_exit[thumb]);
        *normal|=(uint32_t)(e->next-normal-2)&0xffffff;
    }
    gba_a32_emit(e,0xe128f00cu);gba_a32_emit(e,0xe28bb000u|fetch);
#if GBA_DYNAREC_STATS
    if(prefix) {gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe2833000u|prefix);gba_a32_emit(e,0xe58d300cu);}
#endif
    gba_persistent_exit_to(g,e,thumb,pc,6,pc+(thumb?2:4));
}
static void gba_persistent_reject_site(gba_a32_t *e,gba_persistent_side_exits_t *side,
    uint32_t pc,unsigned prefix) {
    if(side->count==16){e->failed=1;return;}
    side->pc[side->count]=pc;
    side->prefix[side->count]=prefix;
    gba_a32_constant(e,0,pc);
    gba_a32_constant(e,2,prefix);
    side->site[side->count++]=e->next;gba_a32_emit(e,0xea000000u);
}
static void gba_persistent_shared_reject(gba_t *g,gba_a32_t *e,gba_persistent_side_exits_t *side,
    uint32_t pc,unsigned thumb,unsigned fetch) {
    for(unsigned n=0;n<side->count;n++)*side->site[n]|=(uint32_t)(e->next-side->site[n]-2)&0xffffff;
    if(side->count==1) {
        (void)pc;
        gba_persistent_local_reject(g,e,side->pc[0],thumb,side->prefix[0],fetch);return;
    }
    /* r0 carries the exact instruction PC and r2 its ordinal in this trace.
     * Fused traces may jump between instruction PCs, so a loop index cannot
     * be used to reconstruct either the exit PC or the fetch position. */
    gba_a32_emit(e,0xe3520000u);uint32_t *prefix=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe59d301cu);gba_a32_emit(e,0xe3530000u);
    uint32_t *normal=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe08bb003u);gba_a32_emit(e,0xe28bb000u|fetch);
    gba_a32_emit(e,0xe128f00cu);gba_a32_constant(e,1,thumb);
    gba_a32_emit(e,0xe59d2008u);gba_a32_emit(e,0xe2023001u);
    gba_a32_emit(e,0xe1811183u);gba_a32_emit(e,0xe3c22001u);
    gba_persistent_jump(e,(uintptr_t)g->dynarec_memory.code+gba_native_cache(g)->persistent_exit[thumb]);
    *prefix|=(uint32_t)(e->next-prefix-2)&0xffffff;*normal|=(uint32_t)(e->next-normal-2)&0xffffff;
#if GBA_DYNAREC_STATS
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe0833002u);gba_a32_emit(e,0xe58d300cu);
#endif
    gba_a32_emit(e,0xe28bb000u|fetch);
    gba_a32_constant(e,1,6|thumb|(thumb<<3));
    /* r0 is the first unexecuted instruction. The shared reject must refill
     * from that instruction's next fetch, not from the entry PC or from the
     * metadata word held in r1. */
    gba_a32_emit(e,0xe2802000u|(thumb?2u:4u));
    gba_a32_emit(e,0xe128f00cu);
    gba_persistent_jump(e,(uintptr_t)g->dynarec_memory.code+gba_native_cache(g)->persistent_exit[thumb]);
}
static void gba_persistent_reserve(gba_a32_t *e,gba_persistent_reservations_t *r,
    unsigned prefix,unsigned fetch) {
    if(GBA_DYNAREC_PERSISTENT_TAIL) {
        if(r->count==32){e->failed=1;return;}
        r->site[r->count]=e->next;r->prefix[r->count++]=prefix;
    }
    gba_a32_emit(e,0xe2823000u|gba_a32_immediate(fetch*16));
}
#include "gba_dynarec_persistent_mapper.h"
static void gba_persistent_prefetch_clear(gba_t *g,gba_a32_t *e) {
#if GBA_DYNAREC_TURBO_LIGHT
    /* r4 is the invariant instance base throughout persistent execution.
     * Avoid a fresh absolute pointer/literal for two adjacent state stores. */
    if(offsetof(gba_t,machine.prefetch_phase)<=4095) {
        gba_a32_emit(e,0xe3a00000u);
        gba_a32_store(e,0,4,offsetof(gba_t,machine.prefetch_count));
        gba_a32_store(e,0,4,offsetof(gba_t,machine.prefetch_phase));
        return;
    }
#endif
    gba_a32_literal(e,3,(uint32_t)(uintptr_t)&g->machine.prefetch_count);
    gba_a32_emit(e,0xe3a00000u);gba_a32_emit(e,0xe5830000u);gba_a32_emit(e,0xe5830004u);
}
static void gba_persistent_memory(gba_t *g,gba_a32_t *e,const gba_persistent_memory_t *m,
    uint32_t pc,unsigned thumb,unsigned prefix,unsigned fetch,
    gba_persistent_reservations_t *reservations,unsigned fixed_cost,gba_persistent_side_exits_t *side) {
    uint32_t *reject[8];unsigned rejected=0,literal=0;
    uint32_t address=0,offset=0;
#if GBA_DYNAREC_TURBO_TLB
    uint32_t *tlb_slow[8],*tlb_fast=NULL;
    unsigned tlb_slow_count=0;
#endif
    /* PC-relative ROM literals have a translation-time address. They still
     * load live ROM data and charge the identical wait cost; only redundant
     * dynamic region/alignment/range guards are removed. GPIO stays slow. */
    if(GBA_DYNAREC_PERSISTENT_LITERALS && m->load && m->rn==15 && !m->reg_offset && !m->wb && g->rom) {
        address=thumb?((pc+4)&~3u):pc+8;
        address=m->up?address+m->offset:address-m->offset;offset=address&0x1ffffff;
        literal=(address>>24)>=8 && (address>>24)<=12 && !(address&(m->bytes-1)) &&
            offset<g->rom_size && m->bytes<=g->rom_size-offset &&
            (offset+m->bytes<=0xc0 || offset>=0xcc);
#ifdef GBA_TEST_LITERAL_COMPARE
        extern int gba_test_literal_enabled(const gba_t *);
        literal=literal && gba_test_literal_enabled(g);
#endif
    }
    uint32_t *memory_begin=e->next,*literal_budget=NULL;
    unsigned leaf=GBA_DYNAREC_PERSISTENT_LITERAL_LEAF && literal;
    if(leaf) {
        /* A PC-relative read has an immutable address and wait bank for this
         * translation epoch, but its ROM data stays live. WAITCNT/unload/reset
         * revoke the epoch. Retain the exact budget and prefetch side effects. */
        static const unsigned first[4]={4,3,2,8};
        uint16_t wait=gba_io16(g,0x204);unsigned bank=((address>>24)-8)/2;
        gba_a32_emit(e,0xe10fc000u);
        gba_a32_constant(e,1,(uint32_t)(uintptr_t)(g->rom+offset));
        gba_a32_constant(e,2,first[(wait>>(2+bank*3))&3]+2+
            (m->bytes==4?gba_gamepak_seq(wait,bank):0));
        gba_persistent_reserve(e,reservations,fixed_cost,fetch);gba_a32_emit(e,0xe15b0003u);
        reject[rejected++]=e->next;gba_a32_emit(e,0x9a000000u);
        gba_persistent_prefetch_clear(g,e);
        GBA_NATIVE_COUNT(gba_native_cache(g),persistent_literal_sites,1);
    } else {
    gba_a32_guest_read(e,0,m->rn,thumb?((pc+4)&~3u):pc+8);
    if(m->reg_offset) {
        gba_a32_guest_read(e,1,m->rm,pc+(thumb?4:8));
        if(m->shift)gba_a32_emit(e,0xe1a01001u|m->shift);
    } else gba_a32_constant(e,1,m->offset);
    if(m->wb) {
        gba_a32_emit(e,m->up?0xe0802001u:0xe0402001u);gba_a32_emit(e,0xe58d2028u);
        if(!m->post)gba_a32_emit(e,0xe1a00002u);
    } else gba_a32_emit(e,m->up?0xe0800001u:0xe0400001u);
    gba_a32_emit(e,0xe10fc000u); /* Preserve guest NZCV over address guards. */
#if GBA_DYNAREC_PERSISTENT_MAP_HELPER
    unsigned helper=m->bytes==4?2:m->bytes==2?1:0;helper+=m->load*3;
#if GBA_DYNAREC_TURBO_TLB
    if(!literal) {
        uint32_t *tlb_table=m->load?gba_native_cache(g)->turbo_read_tlb:
            gba_native_cache(g)->turbo_write_tlb;
        uint8_t *tlb_cost=(m->load?gba_native_cache(g)->turbo_read_cost:
            gba_native_cache(g)->turbo_write_cost)[m->bytes==4?2:m->bytes==2?1:0];
        /* One 32 KiB-page lookup covers every ordinary RAM and ROM mapping.
         * Entries hold host_base | 2 for RAM and host_base | 3 for ROM. */
        gba_a32_emit(e,0xe3100000u|(m->bytes-1));
        tlb_slow[tlb_slow_count++]=e->next;gba_a32_emit(e,0x1a000000u);
        gba_a32_emit(e,0xe1a03880u);gba_a32_emit(e,0xe1a038a3u); /* r3 = address & 0x7fff */
        gba_a32_constant(e,1,0x8000u-m->bytes);gba_a32_emit(e,0xe1530001u);
        tlb_slow[tlb_slow_count++]=e->next;gba_a32_emit(e,0x8a000000u);
        gba_a32_emit(e,0xe1a037a0u); /* r3 = address >> 15 */
        gba_a32_literal(e,1,(uint32_t)(uintptr_t)tlb_table);
        gba_a32_emit(e,0xe7911103u); /* LDR r1,[r1,r3,LSL #2] */
        gba_a32_emit(e,0xe3110002u);
        tlb_slow[tlb_slow_count++]=e->next;gba_a32_emit(e,0x0a000000u);
        gba_a32_emit(e,0xe1a037a0u);
        gba_a32_literal(e,2,(uint32_t)(uintptr_t)tlb_cost);
        gba_a32_emit(e,0xe7d22003u); /* LDRB r2,[r2,r3] */
        gba_a32_emit(e,0xe1a03880u);gba_a32_emit(e,0xe1a038a3u); /* page offset */
        gba_a32_emit(e,0xe2010001u);gba_a32_emit(e,0xe3c11003u);gba_a32_emit(e,0xe0811003u);
        gba_persistent_reserve(e,reservations,fixed_cost,fetch);gba_a32_emit(e,0xe15b0003u);
        tlb_slow[tlb_slow_count++]=e->next;gba_a32_emit(e,0x9a000000u);
        gba_a32_emit(e,0xe3500000u);uint32_t *tlb_not_rom=e->next;gba_a32_emit(e,0x0a000000u);
        gba_persistent_prefetch_clear(g,e);
        *tlb_not_rom|=(uint32_t)(e->next-tlb_not_rom-2)&0xffffff;
#if GBA_DYNAREC_STATS
        gba_a32_literal(e,14,(uint32_t)(uintptr_t)&gba_native_cache(g)->stats.turbo_tlb_hits);
        gba_a32_emit(e,0xe59e3000u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58e3000u);
#endif
        tlb_fast=e->next;gba_a32_emit(e,0xea000000u);
        for(unsigned n=0;n<tlb_slow_count;n++)
            *tlb_slow[n]|=(uint32_t)(e->next-tlb_slow[n]-2)&0xffffff;
#if GBA_DYNAREC_STATS
        gba_a32_literal(e,14,(uint32_t)(uintptr_t)&gba_native_cache(g)->stats.turbo_tlb_misses);
        gba_a32_emit(e,0xe59e3000u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58e3000u);
#endif
    }
#endif
    uintptr_t target=(uintptr_t)g->dynarec_memory.code+gba_native_cache(g)->persistent_mapper[helper];
    gba_persistent_mapper_call(e,target);
    gba_a32_emit(e,0xe3510000u);reject[rejected++]=e->next;gba_a32_emit(e,0x0a000000u);
    gba_persistent_reserve(e,reservations,fixed_cost,fetch);gba_a32_emit(e,0xe15b0003u);
    reject[rejected++]=e->next;gba_a32_emit(e,0x9a000000u);
    /* Mapping is side-effect free. Only a fully accepted access discards ROM
     * prefetch; insufficient budget must leave that state untouched. */
    gba_a32_emit(e,0xe3500000u);uint32_t *not_rom=e->next;gba_a32_emit(e,0x0a000000u);
    gba_persistent_prefetch_clear(g,e);
    *not_rom|=(uint32_t)(e->next-not_rom-2)&0xffffff;
#else
    if(m->bytes>1) {gba_a32_emit(e,0xe3100000u|(m->bytes-1));reject[rejected++]=e->next;gba_a32_emit(e,0x1a000000u);}
    gba_a32_emit(e,0xe1a03c20u); /* region = address >> 24 */
    gba_a32_emit(e,0xe3530003u);uint32_t *iwram=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3530002u);uint32_t *ewram=e->next;gba_a32_emit(e,0x0a000000u);
    if(m->load && g->rom) {
        gba_a32_emit(e,0xe3530008u);reject[rejected++]=e->next;gba_a32_emit(e,0x3a000000u);
        gba_a32_emit(e,0xe353000cu);reject[rejected++]=e->next;gba_a32_emit(e,0x8a000000u);
        gba_a32_emit(e,0xe1a02380u);gba_a32_emit(e,0xe1a023a2u); /* low 25 bits */
        gba_a32_constant(e,1,(uint32_t)g->rom_size-m->bytes);gba_a32_emit(e,0xe1520001u);
        reject[rejected++]=e->next;gba_a32_emit(e,0x8a000000u);
        gba_a32_emit(e,0xe35200c0u);uint32_t *not_gpio=e->next;gba_a32_emit(e,0x3a000000u);
        gba_a32_emit(e,0xe35200ccu);reject[rejected++]=e->next;gba_a32_emit(e,0x3a000000u);
        *not_gpio|=(uint32_t)(e->next-not_gpio-2)&0xffffff;
        gba_a32_constant(e,1,(uint32_t)(uintptr_t)g->rom);gba_a32_emit(e,0xe0811002u);
        static const unsigned first[4]={4,3,2,8};
        uint16_t wait=gba_io16(g,0x204);unsigned costs[3];
        for(unsigned a=0;a<3;a++)costs[a]=first[(wait>>(2+a*3))&3]+1+
            (m->bytes==4?gba_gamepak_seq(wait,a):0)+1;
        gba_a32_constant(e,2,costs[0]);gba_a32_emit(e,0xe353000au);
        gba_a32_emit(e,0x23a02000u|costs[1]);gba_a32_emit(e,0xe353000cu);
        gba_a32_emit(e,0x23a02000u|costs[2]);
        literal_budget=e->next;
        gba_persistent_reserve(e,reservations,fixed_cost,fetch);gba_a32_emit(e,0xe15b0003u);
        reject[rejected++]=e->next;gba_a32_emit(e,0x9a000000u);
        gba_persistent_prefetch_clear(g,e);
    } else {reject[rejected++]=e->next;gba_a32_emit(e,0xea000000u);}
    uint32_t *rom_ready=e->next;gba_a32_emit(e,0xea000000u);
    uint32_t *ram_ready[2];
    for(unsigned region=2;region<=3;region++) {
        uint32_t *target=region==3?iwram:ewram;
        *target|=(uint32_t)(e->next-target-2)&0xffffff;
        unsigned shift=region==3?17:14;
        gba_a32_emit(e,0xe1a02000u|(shift<<7));gba_a32_emit(e,0xe1a02022u|(shift<<7));
        if(!m->load) {
            gba_a32_literal(e,3,(uint32_t)(uintptr_t)(gba_native_cache(g)->code_page+(region==3?1024:0)));
            gba_a32_emit(e,0xe7d33422u); /* LDRB r3,[r3,r2,LSR #8] */
            gba_a32_emit(e,0xe1a0e2a2u);gba_a32_emit(e,0xe20ee007u);gba_a32_emit(e,0xe1a03e33u);
            gba_a32_emit(e,0xe3130001u);reject[rejected++]=e->next;gba_a32_emit(e,0x1a000000u);
        }
        gba_a32_constant(e,1,(uint32_t)(uintptr_t)(region==3?g->memory.iwram:g->memory.ewram));
        gba_a32_emit(e,0xe0811002u);
        gba_a32_constant(e,2,(region==3?1:m->bytes==4?6:3)+m->load);
        ram_ready[region-2]=e->next;gba_a32_emit(e,0xea000000u);
    }
    *rom_ready|=(uint32_t)(e->next-rom_ready-2)&0xffffff;
    for(unsigned i=0;i<2;i++)*ram_ready[i]|=(uint32_t)(e->next-ram_ready[i]-2)&0xffffff;
    gba_persistent_reserve(e,reservations,fixed_cost,fetch);gba_a32_emit(e,0xe15b0003u);
    reject[rejected++]=e->next;gba_a32_emit(e,0x9a000000u);
#endif
#if GBA_DYNAREC_TURBO_TLB
    if(tlb_fast) *tlb_fast|=(uint32_t)(e->next-tlb_fast-2)&0xffffff;
#endif
    }
    if(m->load) {
        uint32_t instruction=m->bytes==4?0xe5913000u:m->bytes==1?
            (m->sign?0xe1d130d0u:0xe5d13000u):(m->sign?0xe1d130f0u:0xe1d130b0u);
        gba_a32_emit(e,instruction);gba_a32_guest_write(e,m->rd,3);
#if GBA_DYNAREC_STATS
        if(leaf) {
            gba_a32_literal(e,3,(uint32_t)(uintptr_t)&gba_native_cache(g)->stats.persistent_literal_leaf_reads);
            gba_a32_emit(e,0xe5930000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe5830000u);
        }
#endif
    } else {
        gba_a32_guest_read(e,3,m->rd,pc+(thumb?4:12));
        gba_a32_emit(e,m->bytes==4?0xe5813000u:m->bytes==1?0xe5c13000u:0xe1c130b0u);
    }
    if(m->wb && (!m->load || m->rn!=m->rd)) {
        gba_a32_emit(e,0xe59d3028u);gba_a32_guest_write(e,m->rn,3);
    }
#if GBA_DYNAREC_STATS
    gba_a32_emit(e,0xe59d3020u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58d3020u);
#endif
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    /* Aligned ordinary RAM/resident ROM reads have no observable side effect.
     * Writes still revoke the proof. Device/GPIO/open-bus reads never reach
     * this path; the existing loop proof also requires complete CPU/bus
     * equality and stops strictly before the next event/control budget. */
    if(!GBA_DYNAREC_PERSISTENT_READ_WAIT || !m->load) {
        gba_a32_literal(e,3,(uint32_t)(uintptr_t)&g->wait.valid);
        gba_a32_emit(e,0xe3a01000u);gba_a32_emit(e,0xe5831000u);
    }
#endif
    gba_a32_emit(e,0xe58d2018u);
    gba_a32_emit(e,0xe04bb002u); /* Commit only the real data/internal cost. */
    gba_a32_constant(e,3,m->load);gba_a32_emit(e,0xe58d3014u);
    gba_a32_emit(e,0xe128f00cu);uint32_t *done=e->next;gba_a32_emit(e,0xea000000u);
    for(unsigned i=0;i<rejected;i++)*reject[i]|=(uint32_t)(e->next-reject[i]-2)&0xffffff;
    if(GBA_DYNAREC_PERSISTENT_SHARED_EXITS)gba_persistent_reject_site(e,side,pc,prefix);
    else gba_persistent_local_reject(g,e,pc,thumb,prefix,fetch);
    *done|=(uint32_t)(e->next-done-2)&0xffffff;
    if(literal && literal_budget) {
        /* Keep the original allocation and every budget/prefetch side effect.
         * Only bypass the guards whose predicates are known at translation.
         * Stable layout also isolates this speed change from cache recycling. */
        gba_a32_t fast={.begin=memory_begin,.next=memory_begin,.end=literal_budget};
        static const unsigned first[4]={4,3,2,8};
        uint16_t wait=gba_io16(g,0x204);unsigned bank=((address>>24)-8)/2;
        unsigned cost=first[(wait>>(2+bank*3))&3]+1+
            (m->bytes==4?gba_gamepak_seq(wait,bank):0)+1;
        gba_a32_emit(&fast,0xe10fc000u);
        gba_a32_constant(&fast,1,(uint32_t)(uintptr_t)(g->rom+offset));
        gba_a32_constant(&fast,2,cost);
        uint32_t *jump=fast.next;gba_a32_emit(&fast,0xea000000u|((uint32_t)(literal_budget-jump-2)&0xffffff));
        if(fast.failed)e->failed=1;
        GBA_NATIVE_COUNT(gba_native_cache(g),persistent_literal_sites,1);
    }
}
static void gba_persistent_edge_account(gba_t *g,gba_a32_t *e,unsigned branch,
    uint32_t last_fetch,unsigned thumb,unsigned count) {
#if !GBA_DYNAREC_STATS
    (void)g;(void)e;(void)branch;(void)last_fetch;(void)thumb;(void)count;
#else
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe2833000u|count);gba_a32_emit(e,0xe58d300cu);
    gba_a32_emit(e,0xe59d3010u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58d3010u);
#if GBA_DYNAREC_TURBO
    gba_a32_literal(e,3,(uint32_t)(uintptr_t)&gba_native_cache(g)->stats.turbo_blocks);
    gba_a32_emit(e,0xe5930000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe5830000u);
#else
    (void)g;
#endif
    if(branch) {gba_a32_constant(e,3,last_fetch|thumb);gba_a32_emit(e,0xe58d3008u);}
#endif
}
static void gba_persistent_edge(gba_t *g,gba_a32_t *e,gba_persistent_block_t *b,
    unsigned edge,uint32_t pc,unsigned branch,uint32_t last_fetch,unsigned count) {
    if(edge>=GBA_PERSISTENT_EDGES) {e->failed=1;return;}
    uint32_t base=(uint32_t)((uint8_t *)e->begin-(uint8_t *)g->dynarec_memory.code);
    b->target[edge]=pc;
    if(b->edge_count<=edge)b->edge_count=(uint16_t)(edge+1);
    if(branch)b->taken|=(uint8_t)(1u<<edge);
    gba_persistent_edge_account(g,e,branch,last_fetch,b->thumb,count);
#if GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH
    if(g->dynarec_memory.code_size<=0x2000000u) {
        b->patch[edge]=base+(uint32_t)(e->next-e->begin)*4;
        gba_a32_emit(e,0xeaffffffu); /* Initially branches to the adjacent cold exit. */
        b->cold[edge]=base+(uint32_t)(e->next-e->begin)*4;
    } else
#endif
    {
        /* LDR pc,[pc,#-4], absolute body address; one atomic literal to republish. */
        gba_a32_emit(e,0xe51ff004u);b->patch[edge]=base+(uint32_t)(e->next-e->begin)*4;
        gba_a32_emit(e,0);
        b->cold[edge]=base+(uint32_t)(e->next-e->begin)*4;
    }
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_STATS
    gba_a32_literal(e,3,(uint32_t)(uintptr_t)&gba_native_cache(g)->stats.turbo_fallbacks);
    gba_a32_emit(e,0xe5930000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe5830000u);
#endif
    gba_persistent_exit_to(g,e,b->thumb,pc,branch?0:6,last_fetch);
#if GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH
    if(g->dynarec_memory.code_size>0x2000000u)
#endif
    {
        uint32_t *literal=(uint32_t *)((uint8_t *)g->dynarec_memory.code+b->patch[edge]);
        *literal=(uint32_t)(uintptr_t)g->dynarec_memory.code+b->cold[edge];
    }
}
/* Target r0 and desired mode r1. Cache entries are revoked before their code
 * is recycled; no generation or host-registry walk occurs on a warm hit. */
static void gba_persistent_indirect(gba_t *g,gba_a32_t *e,gba_persistent_block_t *b,uint32_t pc,unsigned memory_tail) {
    gba_native_cache_t *c=gba_native_cache(g);unsigned width=b->thumb?2:4;
    gba_a32_emit(e,0xe10fc000u);
#if GBA_DYNAREC_STATS
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe2833000u|b->count);gba_a32_emit(e,0xe58d300cu);
    gba_a32_emit(e,0xe59d3010u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58d3010u);
#endif
    gba_a32_constant(e,3,(pc+2*width)|b->thumb);gba_a32_emit(e,0xe58d3008u);
    if(!memory_tail) {
        gba_a32_emit(e,0xe3a03000u);gba_a32_emit(e,0xe58d3014u);gba_a32_emit(e,0xe58d3018u);
    }
    gba_a32_emit(e,0xe3c00001u);gba_a32_emit(e,0xe3510000u);gba_a32_emit(e,0x03c00003u);
    gba_a32_emit(e,0xe1800001u); /* aligned PC | Thumb tag */
    gba_a32_emit(e,0xe0202520u);gba_a32_emit(e,0xe1a020a2u);
    if(GBA_DYNAREC_PERSISTENT_INDIRECT_BITS<=8)
        gba_a32_emit(e,0xe2022000u|((1u<<GBA_DYNAREC_PERSISTENT_INDIRECT_BITS)-1));
    else {
        unsigned shift=32-GBA_DYNAREC_PERSISTENT_INDIRECT_BITS;
        gba_a32_emit(e,0xe1a02002u|(shift<<7));gba_a32_emit(e,0xe1a02022u|(shift<<7));
    }
    gba_a32_literal(e,3,(uint32_t)(uintptr_t)c->persistent_indirect);
    gba_a32_emit(e,0xe0833182u);gba_a32_emit(e,0xe5932000u);gba_a32_emit(e,0xe1520000u);
    uint32_t *miss=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe5933004u);
#if GBA_DYNAREC_STATS
    gba_a32_emit(e,0xe59d2024u);gba_a32_emit(e,0xe2822001u);gba_a32_emit(e,0xe58d2024u);
#endif
    gba_a32_emit(e,0xe3510000u|b->thumb);
    uint32_t *same=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe128f00cu);
    /* Preserve the resolved destination over the bridge-address literal. */
    gba_a32_emit(e,0xe1a02003u);
    gba_a32_literal(e,0,(uint32_t)(uintptr_t)g->dynarec_memory.code+c->persistent_bridge[b->thumb]);
    gba_a32_emit(e,0xe1a03002u);gba_a32_emit(e,0xe12fff10u);
    *same|=(uint32_t)(e->next-same-2)&0xffffff;
    gba_a32_emit(e,0xe128f00cu);gba_a32_emit(e,0xe12fff13u);
    *miss|=(uint32_t)(e->next-miss-2)&0xffffff;
    gba_a32_emit(e,0xe3c00001u); /* physical guest PC no longer needs its tag */
    if(b->thumb)gba_a32_emit(e,0xe3811008u);
#if GBA_DYNAREC_STATS
    gba_a32_emit(e,0xe3811040u);
#endif
    gba_a32_constant(e,2,pc+2*width);gba_a32_emit(e,0xe128f00cu);
    gba_persistent_jump(e,(uintptr_t)g->dynarec_memory.code+c->persistent_exit[b->thumb]);
}
#include "gba_dynarec_persistent_multi.h"
static int __attribute__((unused)) gba_persistent_can_translate(gba_t *g,uint32_t at,unsigned width) {
    if(!gba_persistent_region(g,at,width))return 0;
    uint32_t raw=gba_persistent_word(g,at,width),op=0;
    unsigned kind=gba_native_decode(raw,width,&op);
#if GBA_DYNAREC_TURBO_LIGHT
        if(gba_turbo_light_long(raw,width))kind=GBA_JIT_MUL;
#endif
    unsigned bl_prefix=width==2 && (raw&0xf800)==0xf000;
    if(bl_prefix)kind=GBA_JIT_ALU;
    unsigned bx=width==2?(raw&0xff87)==0x4700:(raw&0x0ffffff0)==0x012fff10;
    unsigned bl_tail=width==2 && (raw&0xf800)==0xf800;
    unsigned pc_alu=kind==GBA_JIT_BRANCH && !bx && !bl_tail &&
        (width==2?(raw&0xfc00)==0x4400:!(raw&0x0c000000) && !(raw&0x00100000) &&
            !(!(raw&0x02000000) && (raw&16)));
    gba_persistent_memory_t memory;
    unsigned mem=gba_persistent_memory_decode(raw,width,&memory);
    unsigned multiple=gba_persistent_multiple_decode(raw,width);
    unsigned direct=kind==GBA_JIT_BRANCH && (width==2?((raw&0xf800)==0xe000 ||
        ((raw&0xf000)==0xd000 && ((raw>>8)&15)<14)):(raw&0x0e000000)==0x0a000000);
    if(kind!=GBA_JIT_ALU && kind!=GBA_JIT_MUL && !direct && !mem && !multiple && !bx && !bl_tail && !pc_alu)return 0;
    return (width==2 || (raw>>28)!=15);
}
static gba_persistent_block_t *gba_persistent_compile(gba_t *g,uint32_t pc,unsigned thumb) {
    gba_native_cache_t *c=gba_native_cache(g);
    unsigned width=thumb?2:4,index=gba_persistent_index(c,pc,thumb);
    unsigned compile_words=2048u+GBA_DYNAREC_PERSISTENT_BLOCK_INSNS*128u;
    size_t compile_bytes=(size_t)compile_words*4u;
    gba_persistent_block_t *set=gba_persistent_table(g)+index,*slot=NULL;
    for(unsigned way=0;way<4;way++)if(!set[way].valid) {slot=set+way;break;}
    if(!slot)slot=set+(c->persistent_victim++&3);
    if(slot->valid && gba_persistent_unlink(g,slot->pc,slot->thumb)) {gba_fail(g,GBA_ERROR_IO);return NULL;}
    gba_persistent_forget_edges(g,slot);
#if GBA_DYNAREC_PAGE_INDEX
    gba_page_index_remove(g,slot);
#endif
    slot->valid=0;
    if(g->dynarec_memory.code_size-c->used<compile_bytes+(GBA_DYNAREC_PERSISTENT_MAP_HELPER?2048u:0)) {
        gba_dynarec_invalidate(g,0,SIZE_MAX);c->used=c->code_base;
        c->hot=NULL;gba_memset(gba_native_blocks(c),0,c->slots*sizeof(gba_native_block_t));
        gba_persistent_reset(g);GBA_NATIVE_COUNT(c,flushes,1);
    }
    if(gba_persistent_stubs(g,thumb)) {gba_fail(g,GBA_ERROR_IO);return NULL;}
#if GBA_DYNAREC_PERSISTENT_MAP_HELPER
    if(gba_persistent_mappers(g)) {gba_fail(g,GBA_ERROR_IO);return NULL;}
#endif
    if(g->dynarec_memory.code_size-c->used<compile_bytes)return NULL;
    uint32_t start=c->used;
    uint32_t *code=(uint32_t *)((uint8_t *)g->dynarec_memory.code+start);
    if(gba_code_begin_write(code,compile_bytes)) {gba_fail(g,GBA_ERROR_IO);return NULL;}
    gba_a32_t e={.begin=code,.next=code,.end=code+compile_words};gba_persistent_map(&e,thumb);
#if GBA_DYNAREC_TURBO_LIGHT
    gba_turbo_literal_pool_t literal_pool={0};e.literal_pool=&literal_pool;
#ifdef GBA_TEST_LIGHT_POOL_COMPARE
    extern int gba_test_light_pool_enabled(const gba_t *g);
    if(!gba_test_light_pool_enabled(g))e.literal_pool=NULL;
#endif
#endif
    gba_persistent_block_t b={.pc=pc,.epoch=c->epoch,.refill=start,.thumb=(uint8_t)thumb};
#if GBA_DYNAREC_PAGE_INDEX
    gba_page_coverage_t coverage={0};
#endif
    /* Entry guard reserves the maximum block cost. At a short boundary the
     * existing bounded executor handles the prefix, without partial commits. */
    unsigned refill=gba_persistent_refill(g,pc,width);
#if GBA_DYNAREC_PERSISTENT_INLINE_REFILL
    b.refill=start;
    gba_a32_emit(&e,0xe10fc000u);uint32_t *refill_load=e.next;
    gba_a32_emit(&e,0xe59f3000u);gba_a32_emit(&e,0xea000000u);
    uint32_t *refill_bound=e.next;gba_a32_emit(&e,0);
    gba_a32_emit(&e,0xe15b0003u);
    uint32_t *refill_fail=e.next;gba_a32_emit(&e,0x9a000000u);
    gba_a32_emit(&e,0xe128f00cu);
    gba_a32_emit(&e,0xe24bb000u|refill);
    uint32_t *refill_hot=e.next;gba_a32_emit(&e,0xea000000u);
    b.offset=start+(uint32_t)(e.next-code)*4;
    gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d301cu);
    gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3018u);
    gba_a32_emit(&e,0xe10fc000u);uint32_t *bound_load=e.next;
    gba_a32_emit(&e,0xe59f3000u);gba_a32_emit(&e,0xea000000u);
    uint32_t *bound=e.next;gba_a32_emit(&e,0);
    gba_a32_emit(&e,0xe15b0003u);
    uint32_t *enough=e.next;gba_a32_emit(&e,0x2a000000u);
    gba_a32_emit(&e,0xe128f00cu);gba_persistent_exit_to(g,&e,thumb,pc,6,pc+width);
    *enough|=(uint32_t)(e.next-enough-2)&0xffffff;
    gba_a32_emit(&e,0xe128f00cu);
    *refill_hot|=(uint32_t)(e.next-refill_hot-2)&0xffffff;
#else
    gba_a32_emit(&e,0xe10fc000u);uint32_t *refill_load=e.next;
    gba_a32_emit(&e,0xe59f3000u);gba_a32_emit(&e,0xea000000u);
    uint32_t *refill_bound=e.next;gba_a32_emit(&e,0);
    gba_a32_emit(&e,0xe15b0003u);
    uint32_t *refill_enough=e.next;gba_a32_emit(&e,0x2a000000u);
    gba_a32_emit(&e,0xe128f00cu);gba_a32_constant(&e,0,pc);gba_a32_constant(&e,1,thumb);
    gba_a32_emit(&e,0xe59d2008u);
    gba_a32_emit(&e,0xe2023001u);gba_a32_emit(&e,0xe1811183u);gba_a32_emit(&e,0xe3c22001u);
    gba_persistent_jump(&e,(uintptr_t)g->dynarec_memory.code+c->persistent_exit[thumb]);
    *refill_enough|=(uint32_t)(e.next-refill_enough-2)&0xffffff;
    gba_a32_emit(&e,0xe128f00cu);gba_a32_emit(&e,0xe24bb000u|refill);
    gba_a32_constant(&e,3,refill);gba_a32_emit(&e,0xe58d301cu);
    uint32_t *refill_body=e.next;gba_a32_emit(&e,0xea000000u);
    b.offset=start+(uint32_t)(e.next-code)*4;
    gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d301cu);
    gba_a32_emit(&e,0xe10fc000u);uint32_t *bound_load=e.next;
    gba_a32_emit(&e,0xe59f3000u);gba_a32_emit(&e,0xea000000u);
    uint32_t *bound=e.next;gba_a32_emit(&e,0);
    gba_a32_emit(&e,0xe15b0003u);
    uint32_t *enough=e.next;gba_a32_emit(&e,0x2a000000u);
    gba_a32_emit(&e,0xe128f00cu);gba_persistent_exit_to(g,&e,thumb,pc,6,pc+width);
    *enough|=(uint32_t)(e.next-enough-2)&0xffffff;
    gba_a32_emit(&e,0xe128f00cu);
    *refill_body|=(uint32_t)(e.next-refill_body-2)&0xffffff;
#endif
#ifdef GBA_TEST_HOT_BLOCKS
    gba_a32_literal(&e,3,(uint32_t)(uintptr_t)&gba_test_hot_count[pc&1023]);
    gba_a32_emit(&e,0xe5930000u);gba_a32_emit(&e,0xe2800001u);gba_a32_emit(&e,0xe5830000u);
#endif
    unsigned cost=0,fetch=gba_persistent_fetch(g,pc,width),pending_fetch=0,terminal=0,clean_tail=0;
    gba_persistent_reservations_t reservations={0};
    gba_persistent_side_exits_t side={0};
    unsigned used_edges=0;
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
    uint32_t cursor=pc,trace_high=pc;
    unsigned previous_prefix_halfword=0;
#endif
    for(unsigned n=0;n<GBA_DYNAREC_PERSISTENT_BLOCK_INSNS;n++) {
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
        uint32_t at=cursor,raw=gba_persistent_word(g,at,width),op=0;
#else
        uint32_t at=pc+n*width,raw=gba_persistent_word(g,at,width),op=0;
#endif
        if(!gba_persistent_region(g,at,width) || (at>>24)!=(pc>>24))break;
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
        unsigned instruction_fetch=gba_persistent_fetch(g,at,width);
#else
        unsigned instruction_fetch=fetch;
#endif
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
#endif
        unsigned kind=gba_native_decode(raw,width,&op);
#if GBA_DYNAREC_TURBO_LIGHT
        if(gba_turbo_light_long(raw,width))kind=GBA_JIT_MUL;
#endif
        unsigned bl_prefix=thumb && (raw&0xf800)==0xf000;
        if(bl_prefix)kind=GBA_JIT_ALU;
        unsigned bx=thumb?(raw&0xff87)==0x4700:(raw&0x0ffffff0)==0x012fff10;
        unsigned bl_tail=thumb && (raw&0xf800)==0xf800;
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
        unsigned known_bl=GBA_DYNAREC_PERSISTENT_BL_DIRECT && bl_tail && n &&
            previous_prefix_halfword;
#else
        unsigned known_bl=GBA_DYNAREC_PERSISTENT_BL_DIRECT && bl_tail && n &&
            (gba_persistent_word(g,at-2,2)&0xf800)==0xf000;
#endif
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
        previous_prefix_halfword=bl_prefix;
#endif
        unsigned pc_alu=kind==GBA_JIT_BRANCH && !bx && !bl_tail &&
            (thumb?(raw&0xfc00)==0x4400:!(raw&0x0c000000) && !(raw&0x00100000) &&
                !(!(raw&0x02000000) && (raw&16)));
        gba_persistent_memory_t memory;
        unsigned mem=gba_persistent_memory_decode(raw,width,&memory);
        unsigned multiple=gba_persistent_multiple_decode(raw,width);
        unsigned direct=kind==GBA_JIT_BRANCH && (thumb?((raw&0xf800)==0xe000 ||
            ((raw&0xf000)==0xd000 && ((raw>>8)&15)<14)):(raw&0x0e000000)==0x0a000000);
        unsigned conditional_direct=direct && (thumb?(raw&0xf000)==0xd000:((raw>>28)<14));
        if(GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK && b.edge_count &&
            (bx || bl_tail || pc_alu))break;
        if(GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK && direct &&
            b.edge_count+(conditional_direct?2u:1u)>GBA_PERSISTENT_EDGES)break;
        if(kind!=GBA_JIT_ALU && kind!=GBA_JIT_MUL && !direct && !mem && !multiple && !bx && !bl_tail && !pc_alu)break;
        unsigned condition=thumb?14:raw>>28;
        if(condition==15)break;
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
        cursor=at+width;
        if(cursor>trace_high)trace_high=cursor;
#endif
        unsigned boundary=mem||multiple||direct||bx||bl_tail||pc_alu||known_bl;
        if(boundary) {
            gba_persistent_fetch_commit(&e,&pending_fetch);
            gba_a32_emit(&e,0xe24bb000u|instruction_fetch);
        } else pending_fetch+=instruction_fetch;
        cost+=instruction_fetch;b.count++;
#if GBA_DYNAREC_PAGE_INDEX
        gba_page_index_cover(&coverage,at,width);
#endif
#if GBA_DYNAREC_TURBO_LOCAL_ALU
        if(condition<14 || boundary || kind!=GBA_JIT_ALU || bl_prefix)gba_turbo_local_flush(&e);
        else gba_turbo_local_admit(&e,op);
#endif
        uint32_t *skip=NULL;
        if(condition<14) {skip=e.next;gba_a32_emit(&e,((condition^1u)<<28)|0x0a000000u);}
        if(known_bl) {
            /* Both halfwords executed unconditionally in this same block.
             * Preserve the prefix LR effect and the tail's real return PC;
             * a budget/interrupt entry at the tail alone stays indirect. */
            uint32_t prefix=gba_persistent_word(g,at-2,2);
            uint32_t target=at+2+(uint32_t)(gba_sign(prefix&2047,11)*4096)+(raw&2047)*2;
            gba_a32_constant(&e,1,(at+2)|1);gba_a32_guest_write(&e,14,1);
            gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3014u);gba_a32_emit(&e,0xe58d3018u);
            unsigned edge=GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK?b.edge_count:
                ((used_edges&1)?1:0);
            gba_persistent_edge(g,&e,&b,edge,target,1,at+2*width,b.count);used_edges|=1u<<edge;
            terminal=1;break;
        }
        if(bx || bl_tail || pc_alu) {
            if(bx) {
                gba_a32_guest_read(&e,0,thumb?(raw>>3)&15:raw&15,at+(thumb?4:8));
                gba_a32_emit(&e,0xe2001001u);
            } else if(bl_tail) {
                gba_a32_guest_read(&e,0,14,0);gba_a32_constant(&e,1,(raw&2047)*2);gba_a32_emit(&e,0xe0800001u);
                gba_a32_constant(&e,1,(at+2)|1);gba_a32_guest_write(&e,14,1);gba_a32_constant(&e,1,1);
            } else {
                gba_persistent_alu(&e,raw,op,at,width);gba_a32_load(&e,0,4,60);gba_a32_constant(&e,1,thumb);
            }
            gba_persistent_indirect(g,&e,&b,at,0);
            if(skip) {*skip|=(uint32_t)(e.next-skip-2)&0xffffff;
                gba_persistent_edge(g,&e,&b,1,at+width,0,at+2*width,b.count);used_edges=2;}
            terminal=1;break;
        }
        if(direct) {
            gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3014u);gba_a32_emit(&e,0xe58d3018u);
            if(!thumb && (raw&0x1000000)) {gba_a32_constant(&e,0,at+4);gba_a32_guest_write(&e,14,0);}
            uint32_t target=thumb?at+4+(uint32_t)(gba_sign(raw&((raw&0xf800)==0xe000?2047:255),
                (raw&0xf800)==0xe000?11:8)*2):at+8+(uint32_t)(gba_sign(raw&0xffffff,24)*4);
            unsigned thumb_cond=thumb && (raw&0xf000)==0xd000 && ((raw>>8)&15)<14;
            unsigned arm_cond=!thumb && condition<14;
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
            unsigned turbo_direct=!conditional_direct && target>at &&
                (target>>24)==(pc>>24) && target-at<0x10000u &&
                (thumb || !(raw&0x1000000)) &&
                gba_persistent_can_translate(g,target,width);
            if(turbo_direct) {
#ifdef GBA_TEST_TURBO_TRACE_DIAG
                gba_a32_literal(&e,3,(uint32_t)(uintptr_t)&gba_test_turbo_branch_src);
                gba_a32_constant(&e,2,at);gba_a32_emit(&e,0xe5832000u);
                gba_a32_literal(&e,3,(uint32_t)(uintptr_t)&gba_test_turbo_branch_target);
                gba_a32_constant(&e,2,target);gba_a32_emit(&e,0xe5832000u);
#endif
                unsigned rrefill=gba_persistent_refill(g,target,width);
                /* A forward unconditional direct edge is part of this trace.
                 * Emit its accounting and continue with the destination in
                 * the same native block. The old cold exit stored an incoming
                 * edge that could never reach this inline continuation, which
                 * caused one C round trip per branch and defeated trace scans. */
                gba_persistent_edge_account(g,&e,1,at+2*width,thumb,b.count);
                if(rrefill)gba_a32_emit(&e,0xe24bb000u|rrefill);
                cost+=rrefill;cursor=target;previous_prefix_halfword=0;
                if(target+width>trace_high)trace_high=target+width;
                continue;
            }
#endif
            unsigned inline_branch=GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK &&
                (thumb_cond || arm_cond);
            if(inline_branch) {
#ifdef GBA_TEST_BRANCH_SUPERBLOCK_PROOF
                gba_test_branch_inline++;
#endif
                uint32_t *not_taken=skip;
                if(thumb_cond) {
                    not_taken=e.next;gba_a32_emit(&e,((((raw>>8)&15)^1u)<<28)|0x0a000000u);
                }
                unsigned edge=b.edge_count;
                gba_persistent_edge(g,&e,&b,edge,target,1,at+2*width,b.count);used_edges|=1u<<edge;
                if(not_taken)*not_taken|=(uint32_t)(e.next-not_taken-2)&0xffffff;
                skip=NULL;continue;
            }
            if(thumb_cond) {
                uint32_t *not_taken=e.next;gba_a32_emit(&e,((((raw>>8)&15)^1u)<<28)|0x0a000000u);
                gba_persistent_edge(g,&e,&b,0,target,1,at+2*width,b.count);used_edges=1;
                *not_taken|=(uint32_t)(e.next-not_taken-2)&0xffffff;
                gba_persistent_edge(g,&e,&b,1,at+width,0,at+2*width,b.count);used_edges=2;
            } else {
                unsigned edge=GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK?b.edge_count:
                    ((used_edges&1)?1:0);
                gba_persistent_edge(g,&e,&b,edge,target,1,at+2*width,b.count);used_edges|=1u<<edge;
                if(skip) {*skip|=(uint32_t)(e.next-skip-2)&0xffffff;skip=NULL;
                    gba_persistent_edge(g,&e,&b,GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK?
                        b.edge_count:1,at+width,0,at+2*width,b.count);used_edges|=2;}
            }
            terminal=1;break;
        }
        if(mem || multiple) {
            clean_tail=0;
            /* Dynamic data cost is checked after mapping, before any effect;
             * the entry reserves only the bounded fetch-only tail. */
            if(multiple)gba_persistent_multiple(g,&e,multiple,at,thumb,n,instruction_fetch,&reservations,cost,&side);
            else gba_persistent_memory(g,&e,&memory,at,thumb,n,instruction_fetch,&reservations,cost,&side);
            if(multiple?((multiple&0x100000)&&(multiple&0x8000)):(memory.load && memory.rd==15)) {
                gba_a32_load(&e,0,4,60);gba_a32_constant(&e,1,thumb);
                gba_persistent_indirect(g,&e,&b,at,1);
                if(skip) {*skip|=(uint32_t)(e.next-skip-2)&0xffffff;
                    gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3014u);gba_a32_emit(&e,0xe58d3018u);
                    gba_persistent_edge(g,&e,&b,GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK?
                        b.edge_count:1,at+width,0,at+2*width,b.count);used_edges|=2;}
                terminal=1;break;
            }
            if(skip) {
                uint32_t *done=e.next;gba_a32_emit(&e,0xea000000u);
                *skip|=(uint32_t)(e.next-skip-2)&0xffffff;skip=NULL;
                gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3014u);gba_a32_emit(&e,0xe58d3018u);
                *done|=(uint32_t)(e.next-done-2)&0xffffff;
            }
            continue;
        }
        if(kind==GBA_JIT_MUL) {
            clean_tail=0;
#if GBA_DYNAREC_TURBO_LIGHT
            if(gba_turbo_light_long(raw,width)) {
                cost+=(op&0x200000)?6:5;gba_turbo_light_multiply_long(g,&e,op,at);
            } else
#endif
            {cost+=(op&0x200000)?5:4;gba_persistent_multiply(&e,op,at);}
        } else {
            gba_persistent_alu(&e,raw,op,at,width);
            unsigned internal=(unsigned)(!bl_prefix && !(op&0x02000000) && (op&16));
            if(internal)cost++;
            /* Consecutive unconditional plain ALUs leave the same final
             * access metadata (zero internal/data cost). Keep it resident
             * instead of issuing two stack stores per guest instruction.
             * Memory, multiply and conditional paths break this invariant;
             * guards and cycle deductions remain at their original sites. */
            if(!clean_tail || internal || condition<14) {
                gba_a32_constant(&e,3,internal);gba_a32_emit(&e,0xe58d3014u);
                gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3018u);
            }
            clean_tail=!internal && condition==14;
        }
        if(skip) {
            uint32_t *done=e.next;gba_a32_emit(&e,0xea000000u);
            *skip|=(uint32_t)(e.next-skip-2)&0xffffff;
            gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3014u);gba_a32_emit(&e,0xe58d3018u);
            *done|=(uint32_t)(e.next-done-2)&0xffffff;
        }
    }
    gba_persistent_fetch_commit(&e,&pending_fetch);
#if GBA_DYNAREC_TURBO_LOCAL_ALU
    gba_turbo_local_flush(&e);
#endif
    if(!b.count) {
        /* Cache only a decoder rejection. Dynamic memory/budget failures keep
         * the compiled block and may succeed on its very next invocation. */
        b.valid=1;b.end=pc+width;
#if GBA_DYNAREC_PAGE_INDEX
        if(!gba_page_index_register(g,&b,(unsigned)(slot-gba_persistent_table(g)),NULL))return NULL;
#endif
        *slot=b;
        unsigned region=pc>>24;
        if(region==2 || region==3)for(uint32_t at=pc;at<pc+3*width;at=(at&~31u)+32) {
            uint32_t mask=region==3?0x7fffu:0x3ffffu,offset=at&mask;
            c->code_page[(region==3?1024:0)+(offset>>8)]|=(uint8_t)(1u<<((offset>>5)&7));
        }
        return NULL;
    }
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
    b.end=trace_high;
#else
    b.end=pc+b.count*width;
#endif
    if(!terminal) {
        unsigned edge=GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK?b.edge_count:
            ((used_edges&1)?1:0);
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION
        gba_persistent_edge(g,&e,&b,edge,cursor,0,cursor+width,b.count);used_edges|=1u<<edge;
#else
        gba_persistent_edge(g,&e,&b,edge,b.end,0,b.end+width,b.count);used_edges|=1u<<edge;
#endif
    }
    if(side.count)gba_persistent_shared_reject(g,&e,&side,pc,thumb,fetch);
#if GBA_DYNAREC_PERSISTENT_INLINE_REFILL
    uint32_t *refill_slow=e.next;
    *refill_fail|=(uint32_t)(refill_slow-refill_fail-2)&0xffffff;
    gba_a32_emit(&e,0xe128f00cu);
    gba_a32_constant(&e,3,refill);gba_a32_emit(&e,0xe58d301cu);
    gba_a32_emit(&e,0xe3a03000u);gba_a32_emit(&e,0xe58d3018u);
    gba_a32_constant(&e,0,pc);gba_a32_constant(&e,1,thumb);
    gba_a32_emit(&e,0xe59d2008u);
    gba_persistent_jump(&e,(uintptr_t)g->dynarec_memory.code+c->persistent_exit[thumb]);
#endif
    /* Stay strictly before an observable fetch boundary. DMA captures the
     * pre-instruction PC there, not the state materialized at a block exit. */
    for(unsigned n=0;n<reservations.count;n++) {
        uint32_t tail=gba_a32_immediate(cost-reservations.prefix[n]);
        if(tail==UINT32_MAX) {e.failed=1;break;}
        *reservations.site[n]=0xe2823000u|tail;
    }
    /* Emission failure is an internal capacity/rejection condition; no code
     * has been published, so the caller can execute the scalar fallback. */
#if GBA_DYNAREC_TURBO_LIGHT
    gba_turbo_light_flush(&e);
#endif
    if(e.failed)return NULL;
    uint32_t guard_cost=cost+1,guard_refill=cost+refill+1;
    uint32_t guard_imm=gba_a32_immediate(guard_cost);
    if(guard_imm!=UINT32_MAX) {
        bound_load[0]=0xe35b0000u|guard_imm;
        bound_load[1]=0x2a000000u|((uint32_t)(enough-bound_load-1-2)&0xffffff);
        bound_load[2]=0xea000000u; /* Skip the superseded CMP on the reject path. */
    } else *bound=guard_cost;
#if GBA_DYNAREC_PERSISTENT_INLINE_REFILL
    guard_imm=gba_a32_immediate(guard_refill);
    if(guard_imm!=UINT32_MAX) {
        refill_load[0]=0xe35b0000u|guard_imm;
        refill_load[1]=0x9a000000u|((uint32_t)(refill_slow-refill_load-1-2)&0xffffff);
        refill_load[2]=0xea000000u;
    } else *refill_bound=guard_refill;
#else
    guard_imm=gba_a32_immediate(guard_refill);
    if(guard_imm!=UINT32_MAX) {
        refill_load[0]=0xe35b0000u|guard_imm;
        refill_load[1]=0x2a000000u|((uint32_t)(refill_enough-refill_load-1-2)&0xffffff);
        refill_load[2]=0xea000000u;
    } else *refill_bound=guard_refill;
#endif
    b.bound=(uint16_t)guard_cost;
#if GBA_DYNAREC_PAGE_INDEX
    if(!gba_page_index_register(g,&b,(unsigned)(slot-gba_persistent_table(g)),&coverage))return NULL;
#endif
    if(gba_code_publish(code,(size_t)(e.next-code)*4)) {
#if GBA_DYNAREC_PAGE_INDEX
        gba_page_index_remove(g,&b);
#endif
        gba_fail(g,GBA_ERROR_IO);return NULL;
    }
    c->used=(start+(uint32_t)(e.next-code)*4+31u)&~31u;b.valid=1;*slot=b;
    unsigned slot_index=(unsigned)(slot-gba_persistent_table(g));
    for(unsigned edge=0;edge<slot->edge_count;edge++)if(slot->patch[edge]) {
        uint32_t *head=&c->persistent_incoming[gba_persistent_incoming_index(slot->target[edge])];
        slot->next[edge]=*head;*head=slot_index*GBA_PERSISTENT_EDGES+edge+1;
    }
#if !GBA_DYNAREC_PAGE_INDEX
    unsigned code_region=pc>>24;
    if(code_region==2 || code_region==3) {
        for(uint32_t at=pc;at<b.end+2*width;at=(at&~31u)+32) {
            uint32_t mask=code_region==3?0x7fffu:0x3ffffu,offset=at&mask;
            c->code_page[(code_region==3?1024:0)+(offset>>8)]|=(uint8_t)(1u<<((offset>>5)&7));
        }
#if GBA_DYNAREC_PERSISTENT_PAGE_TABLE
#endif
    }
#endif
    GBA_NATIVE_COUNT(c,compiled_blocks,1);
#if GBA_DYNAREC_TURBO
    GBA_NATIVE_COUNT(c,turbo_traces,1);
    GBA_NATIVE_COUNT(c,turbo_instructions,b.count);
    GBA_NATIVE_COUNT(c,turbo_cycle_commits,cost);
    GBA_NATIVE_COUNT(c,turbo_generated_words,(unsigned)(e.next-code));
#endif
#ifdef GBA_TEST_HOT_BLOCKS
    {
        unsigned hot=pc&1023;
        gba_test_hot_pc[hot]=pc;gba_test_hot_words[hot]=(unsigned)(e.next-code);
    }
#endif
    return slot;
}
static int gba_persistent_resolve_edge(gba_t *g,gba_persistent_block_t *source,unsigned edge) {
    if(!source->valid || !source->patch[edge])return 0;
    gba_persistent_block_t *b=gba_persistent_lookup(g,source->target[edge],source->thumb);
    if(b && b->count) {
        uint32_t offset=(source->taken&(1u<<edge))?b->refill:b->offset;
        uint32_t target=(uint32_t)(uintptr_t)g->dynarec_memory.code+offset;
        (void)target;
#if GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH
        if(g->dynarec_memory.code_size<=0x2000000u) {
            int32_t delta=(int32_t)offset-(int32_t)source->patch[edge]-8;
            uint32_t expected=0xea000000u|((uint32_t)(delta/4)&0xffffffu);
            uint32_t *site=(uint32_t *)((uint8_t *)g->dynarec_memory.code+source->patch[edge]);
            if(*site!=expected) {
                if(gba_persistent_patch_branch(g,source->patch[edge],offset))return -1;
                GBA_NATIVE_COUNT(gba_native_cache(g),persistent_links,1);
#if GBA_DYNAREC_TURBO
                GBA_NATIVE_COUNT(gba_native_cache(g),turbo_links,1);
#endif
            }
        } else
#endif
        {
            uint32_t *literal=(uint32_t *)((uint8_t *)g->dynarec_memory.code+source->patch[edge]);
            if(*literal!=target) {
                if(gba_persistent_patch(g,source->patch[edge],offset))return -1;
                GBA_NATIVE_COUNT(gba_native_cache(g),persistent_links,1);
#if GBA_DYNAREC_TURBO
                GBA_NATIVE_COUNT(gba_native_cache(g),turbo_links,1);
#endif
            }
        }
    }
    return 0;
}
static int gba_persistent_link(gba_t *g,gba_persistent_block_t *target) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_persistent_block_t *table=gba_persistent_table(g);
    for(unsigned edge=0;edge<target->edge_count;edge++)if(gba_persistent_resolve_edge(g,target,edge))return -1;
    unsigned link=c->persistent_incoming[gba_persistent_incoming_index(target->pc)];
    while(link) {
        unsigned n=(link-1)/GBA_PERSISTENT_EDGES,edge=(link-1)%GBA_PERSISTENT_EDGES;
        if(table[n].target[edge]==target->pc && table[n].thumb==target->thumb &&
            gba_persistent_resolve_edge(g,table+n,edge))return -1;
        link=table[n].next[edge];
    }
    return 0;
}
uint32_t gba_persistent_run(gba_t *g,uint32_t budget) {
#ifdef GBA_TEST_TURBO_TRACE_DIAG
#define GBA_TURBO_REJECT(reason) do {gba_test_turbo_reject_reason[(reason)]++;gba_test_turbo_reject_reason_pc[(reason)]=pc;return 0;} while(0)
#else
#define GBA_TURBO_REJECT(reason) return 0
#endif
    gba_native_cache_t *c=gba_native_cache(g);gba_cpu_t *cpu=&g->machine.cpu;
    unsigned thumb=!!(cpu->cpsr&GBA_T),width=thumb?2:4;
    uint32_t pc=cpu->r[15];
    if(GBA_TEST_MODE)GBA_TURBO_REJECT(0); /* Instruction hooks require scalar boundaries. */
    if(!c || !g->stepping)GBA_TURBO_REJECT(1);
    if(!cpu->pipeline_valid)GBA_TURBO_REJECT(2);
    if(cpu->irq_delay || (cpu->wait_mask &&
        !(GBA_DYNAREC_PERSISTENT_HLE_IRQ && cpu->hle_irq)) || gba_dma_ready_mask(g) ||
        (cpu->irq_line && gba_io16(g,0x208) && !(cpu->cpsr&0x80)))GBA_TURBO_REJECT(3);
    if((g->machine.last_fetch>>24)!=(pc>>24))GBA_TURBO_REJECT(13);
    if(!gba_persistent_region(g,pc,width))GBA_TURBO_REJECT(14);
    if(c->waitcnt!=gba_io16(g,0x204))GBA_TURBO_REJECT(4); /* Legacy entry updates WAITCNT first. */
    if(c->persistent_reset)gba_persistent_reset(g);
    uint32_t distance=g->event_cycles>g->cpu_pending?g->event_cycles-g->cpu_pending:0;
    if(budget>distance)budget=distance;
#if GBA_DYNAREC_EVENT_SLIP
    if(budget<GBA_DYNAREC_EVENT_SLIP) {
        uint32_t minimum=GBA_DYNAREC_EVENT_SLIP,hard=gba_native_hard_distance(g);
        if(minimum>hard)minimum=hard;
        if(budget<minimum)budget=minimum;
    }
#endif
    budget=gba_native_control_budget(g,budget);
    if(budget<2)GBA_TURBO_REJECT(5);
    /* Never replace prefetched stale instructions after a self-modifying write. */
    if(cpu->pipeline[0]!=gba_persistent_word(g,pc,width) ||
       cpu->pipeline[1]!=gba_persistent_word(g,pc+width,width))GBA_TURBO_REJECT(6);
    gba_persistent_block_t *b=gba_persistent_lookup(g,pc,thumb);
    if(!b) {
        b=gba_persistent_compile(g,pc,thumb);if(!b)GBA_TURBO_REJECT(7);
        if(gba_persistent_link(g,b)) {gba_fail(g,GBA_ERROR_IO);GBA_TURBO_REJECT(8);}
#if GBA_DYNAREC_TURBO_PRELINK
        /* A trace that reaches a cold direct edge should not pay a second C
         * entry merely to compile a statically known destination. Prepare a
         * bounded frontier now; deeper blocks are prepared on first reach. */
        unsigned frontier=GBA_DYNAREC_TURBO_PRELINK;
        for(unsigned edge=0;edge<b->edge_count && frontier;edge++) {
            uint32_t target=b->target[edge];
            if(!target || gba_persistent_lookup(g,target,thumb) ||
                !gba_persistent_region(g,target,width))continue;
            gba_persistent_block_t *next=gba_persistent_compile(g,target,thumb);
            if(!next)continue;
            frontier--;
            if(next->count && gba_persistent_link(g,next)) {gba_fail(g,GBA_ERROR_IO);return 0;}
            /* A capacity recycle can invalidate the source pointer. */
            b=gba_persistent_lookup(g,pc,thumb);
            if(!b)GBA_TURBO_REJECT(9);
            if(gba_persistent_link(g,b)) {gba_fail(g,GBA_ERROR_IO);GBA_TURBO_REJECT(10);}
        }
#endif
    }
    if(!b->count)GBA_TURBO_REJECT(11);
    if(budget<b->bound)GBA_TURBO_REJECT(12);
    /* A direct-map collision can evict a still-translated indirect target.
     * Refill when that target is actually dispatched, including cache hits;
     * publishing only during compilation leaves warm misses permanent. */
    unsigned indirect=gba_persistent_indirect_index(pc);
    c->persistent_indirect[indirect].entry=(uint32_t)(uintptr_t)g->dynarec_memory.code+b->refill;
    c->persistent_indirect[indirect].key=pc|thumb;
    uint32_t (*entry)(gba_t *,uint32_t,uintptr_t)=(uint32_t (*)(gba_t *,uint32_t,uintptr_t))
        ((uint8_t *)g->dynarec_memory.code+c->persistent_entry[thumb]);
    uint32_t used=entry(g,budget,(uintptr_t)g->dynarec_memory.code+b->offset);
#ifdef GBA_TEST_TURBO_TRACE_DIAG
    gba_test_turbo_entry_pc=pc;
    gba_test_turbo_exit_pc=cpu->r[15];
    gba_test_turbo_exit_meta=c->persistent_meta;
#endif
    GBA_NATIVE_COUNT(c,persistent_entries,1);GBA_NATIVE_COUNT(c,native_entries,1);
    GBA_NATIVE_COUNT(c,zero_entries,!used);
    if(!used)return 0;
#ifdef GBA_TEST_HLE_IRQ_ADMISSION_COUNT
    if(cpu->hle_irq && cpu->wait_mask) {
        extern unsigned gba_test_hle_irq_entries;
        gba_test_hle_irq_entries++;
    }
#endif
    unsigned meta=c->persistent_meta;
    unsigned fetch_width=(meta&8)?2:4;
    width=(meta&1)?2:4;cpu->cpsr=(cpu->cpsr&~GBA_T)|((meta&1)?GBA_T:0);
    cpu->pipeline_valid=!!(meta&2);cpu->fetch_sequential=!!(meta&4);
    if(c->persistent_internal && !(gba_io16(g,0x204)&0x4000))cpu->fetch_sequential=0;
    if(cpu->pipeline_valid) {
        cpu->pipeline[0]=gba_persistent_word(g,cpu->r[15],width);
        cpu->pipeline[1]=gba_persistent_word(g,cpu->r[15]+width,width);
    } else {
        cpu->pipeline[0]=gba_persistent_word(g,g->machine.last_fetch-fetch_width,fetch_width);
        cpu->pipeline[1]=gba_persistent_word(g,g->machine.last_fetch,fetch_width);
    }
    g->machine.open_bus=fetch_width==2?cpu->pipeline[1]*0x10001u:cpu->pipeline[1];
    g->machine.dma_bus_valid=0;
    if((pc>>24)>=8)g->machine.prefetch_count=g->machine.prefetch_phase=0;
    g->access_cycles=gba_persistent_fetch(g,g->machine.last_fetch,fetch_width)+c->persistent_data_cost-c->persistent_internal;
    GBA_NATIVE_COUNT(c,persistent_blocks,c->persistent_block_count);
    GBA_NATIVE_COUNT(c,persistent_instructions,c->persistent_guest_count);
    GBA_NATIVE_COUNT(c,persistent_indirect_hits,c->persistent_indirect_count);
    GBA_NATIVE_COUNT(c,persistent_multi_instructions,c->persistent_multi_count);
    GBA_NATIVE_COUNT(c,persistent_misses,!!(meta&64));
    GBA_NATIVE_COUNT(c,native_instructions,c->persistent_guest_count-c->persistent_memory_count);
    GBA_NATIVE_COUNT(c,memory_instructions,c->persistent_memory_count);
    gba_cpu_charge(g,used);return used;
#undef GBA_TURBO_REJECT
}
#endif
