/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#pragma once
#if GBA_DYNAREC_COARSE_CHAIN
/* A32 continuation for the explicitly approximate timing mode. No device
 * callback runs with guest registers resident in r8-r11. ROM is immutable
 * inside a CPU call; epoch, mode and budget guards all precede commit.
 * Pipeline contents/open bus are materialized once on the C exit. The A32
 * memory helpers accept ordinary RAM/ROM and stable IO only; none consumes
 * these deferred fields or calls a device callback. */
static void gba_a32_direct_rom(gba_t *g,gba_a32_t *e) {
    gba_native_cache_t *c=gba_native_cache(g);
    uint32_t *fail[32];unsigned failures=0;
#define REJECT(cond) do{fail[failures++]=e->next;gba_a32_emit(e,(cond)|0x0a000000u);}while(0)
#define LOADB(rd,rn,off) gba_a32_emit(e,0xe5d00000u|((rn)<<16)|((rd)<<12)|(off))
#define LOADH(rd,rn,off) gba_a32_emit(e,0xe1d000b0u|((rn)<<16)|((rd)<<12)|(((off)&0xf0)<<4)|((off)&15))
#define SAVE(rd,off) do {if((off)<=4095)gba_a32_store(e,rd,4,off);else {gba_a32_constant(e,14,off);gba_a32_emit(e,0xe784000eu|((rd)<<12));}}while(0)
    gba_a32_emit(e,0xe3170102u);REJECT(0x10000000u); /* partial/loop exit */
    gba_a32_emit(e,0xe59d001cu); /* source block */
    gba_a32_load(e,3,0,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe1a03c23u);
    gba_a32_emit(e,0xe3530003u);uint32_t *source_ram=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3530008u);REJECT(0x30000000u);
    gba_a32_emit(e,0xe353000cu);REJECT(0x80000000u);
    *source_ram|=(uint32_t)(e->next-source_ram-2)&0xffffff;
    /* A taken conditional branch may leave a valid prefix of a longer block.
     * It is an ordinary edge, not a memory guard failure. Charge its actual
     * executed instruction count, including earlier complete loop rounds. */
    gba_a32_emit(e,0xe20730ffu);gba_a32_emit(e,0xe3530000u);REJECT(0x00000000u);
    gba_a32_load(e,12,0,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe1a0cc2cu);gba_a32_emit(e,0xe35c0003u);
    LOADB(12,0,offsetof(gba_native_block_t,prefix_count));
    gba_a32_emit(e,0x03a0c001u);
    gba_a32_emit(e,0xe00c0c93u); /* MUL ip,r3,ip */
    gba_a32_emit(e,0xe085c00cu);gba_a32_emit(e,0xe58dc008u); /* consumed, before refill */
    LOADB(12,0,offsetof(gba_native_block_t,flags));gba_a32_emit(e,0xe31c0020u);
    /* A loop falling through after repeated rounds ends at its static tail. */
    gba_a32_emit(e,0x15d03000u|offsetof(gba_native_block_t,count));
    gba_a32_emit(e,0xe31c0001u);
    gba_a32_emit(e,0xe3a0c004u);gba_a32_emit(e,0x13a0c002u);
    gba_a32_emit(e,0xe003039cu); /* source count * width */
    gba_a32_load(e,2,0,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe0822003u);
    gba_a32_load(e,3,4,offsetof(gba_cpu_t,pipeline_valid));gba_a32_emit(e,0xe58d3004u);
    gba_a32_emit(e,0xe3530000u);gba_a32_emit(e,0x0594203cu);
    gba_a32_load(e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(e,0xe31c0020u);
    gba_a32_emit(e,0xe3a03004u);gba_a32_emit(e,0x13a03002u);gba_a32_emit(e,0xe58d300cu);
    gba_a32_emit(e,0xe243c001u);gba_a32_emit(e,0xe112000cu);REJECT(0x10000000u);
    gba_a32_emit(e,0xe1a03c22u);gba_a32_emit(e,0xe3530003u);
    gba_a32_emit(e,0x03a0c001u);uint32_t *target_ram=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3530008u);REJECT(0x30000000u);
    gba_a32_emit(e,0xe353000cu);REJECT(0x80000000u);
    gba_a32_emit(e,0xe3a0c000u);*target_ram|=(uint32_t)(e->next-target_ram-2)&0xffffff;
    gba_a32_emit(e,0xe58dc000u); /* target is IWRAM, otherwise resident ROM */
    gba_a32_emit(e,0xe1a030a2u);gba_a32_emit(e,0xe0233422u);gba_a32_emit(e,0xe203303fu);
#if GBA_DYNAREC_LINK_CACHE
    gba_a32_emit(e,0xe59d001cu);gba_a32_load(e,1,0,offsetof(gba_native_block_t,prefix_offset));
    gba_a32_emit(e,0xe3510000u);uint32_t *no_link=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_load(e,12,1,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe15c0002u);
    uint32_t *linked=e->next;gba_a32_emit(e,0x0a000000u);
    *no_link|=(uint32_t)(e->next-no_link-2)&0xffffff;
#endif
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)c->recent);gba_a32_emit(e,0xe79c1103u);
    gba_a32_emit(e,0xe3510000u);uint32_t *alternate=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_load(e,12,1,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe15c0002u);
    uint32_t *matched=e->next;gba_a32_emit(e,0x0a000000u);
    *alternate|=(uint32_t)(e->next-alternate-2)&0xffffff;
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)c->recent_alt);gba_a32_emit(e,0xe79c1103u);
    gba_a32_emit(e,0xe3510000u);REJECT(0x00000000u);
    gba_a32_load(e,12,1,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe15c0002u);REJECT(0x10000000u);
    *matched|=(uint32_t)(e->next-matched-2)&0xffffff;
#if GBA_DYNAREC_LINK_CACHE
    *linked|=(uint32_t)(e->next-linked-2)&0xffffff;
#endif
    LOADH(12,1,offsetof(gba_native_block_t,cache_key));gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);
    gba_a32_emit(e,GBA_DYNAREC_ROM_TRACE?0xe3a030f0u:GBA_DYNAREC_WIDE_ROM?0xe3a03c01u:0xe3a03040u);
    gba_a32_emit(e,0xe59d0000u);gba_a32_emit(e,0xe3500000u);gba_a32_emit(e,0x13a03080u);
    /* Recover the width comparison after selecting the target region key. */
    gba_a32_emit(e,0xe59d000cu);gba_a32_emit(e,0xe3500002u);
    gba_a32_emit(e,0x03833001u);gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    LOADB(12,1,offsetof(gba_native_block_t,flags));gba_a32_emit(e,0xe20cc003u);
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);
    gba_a32_emit(e,0xe3a03002u);gba_a32_emit(e,0x03a03003u);gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    gba_a32_load(e,3,1,offsetof(gba_native_block_t,epoch));
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->epoch);gba_a32_emit(e,0xe59cc000u);
    gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    LOADB(3,1,offsetof(gba_native_block_t,count));gba_a32_emit(e,GBA_DYNAREC_TINY_BLOCKS?0xe3530001u:0xe3530002u);REJECT(0x30000000u);
    gba_a32_emit(e,0xe59d0000u);gba_a32_emit(e,0xe3500000u);
    uint32_t *rom_target=e->next;gba_a32_emit(e,0x0a000000u);
    /* RAM targets retain generation and live pipeline validation. No device
     * callback or compiler can run inside this native continuation. */
    gba_a32_emit(e,0xe1a03422u);gba_a32_emit(e,0xe203307fu);gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->generation[1024]);
    gba_a32_emit(e,0xe79cc103u);gba_a32_load(e,3,1,offsetof(gba_native_block_t,page_generation));gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    LOADH(3,1,offsetof(gba_native_block_t,end_page));gba_a32_constant(e,12,(uint32_t)(uintptr_t)c->generation);
    gba_a32_emit(e,0xe79cc103u);gba_a32_load(e,3,1,offsetof(gba_native_block_t,end_generation));gba_a32_emit(e,0xe15c0003u);REJECT(0x10000000u);
    gba_a32_emit(e,0xe1a0c882u);gba_a32_emit(e,0xe1a0c8acu);
    gba_a32_load(e,3,4,offsetof(gba_t,memory.iwram));gba_a32_emit(e,0xe083c00cu);
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);gba_a32_emit(e,0x01dc30b0u);gba_a32_emit(e,0x159c3000u);
    gba_a32_load(e,0,1,offsetof(gba_native_block_t,head));gba_a32_emit(e,0xe1530000u);REJECT(0x10000000u);
    gba_a32_emit(e,0xe59d300cu);gba_a32_emit(e,0xe3530002u);gba_a32_emit(e,0x01dc30b2u);gba_a32_emit(e,0x159c3004u);
    gba_a32_load(e,0,1,offsetof(gba_native_block_t,head)+4);gba_a32_emit(e,0xe1530000u);REJECT(0x10000000u);
    *rom_target|=(uint32_t)(e->next-rom_target-2)&0xffffff;
    /* Reserve both target fetches on a taken edge and the next full block. */
#if GBA_DYNAREC_REFILL_MATCH
    LOADH(3,1,offsetof(gba_native_block_t,loop_cycles));gba_a32_emit(e,0xe1a03423u);
    gba_a32_emit(e,0xe59dc000u);gba_a32_emit(e,0xe35c0000u);gba_a32_emit(e,0x13a03002u);
#else
    LOADB(3,1,offsetof(gba_native_block_t,prefix_count));
    gba_a32_emit(e,0xe59dc000u);gba_a32_emit(e,0xe35c0000u);gba_a32_emit(e,0x13a03001u);
#endif
    gba_a32_emit(e,0xe59dc004u);gba_a32_emit(e,0xe35c0000u);
    gba_a32_emit(e,0x13a03000u);
#if !GBA_DYNAREC_REFILL_MATCH
    gba_a32_emit(e,0x01a03083u);
#endif
    gba_a32_emit(e,0xe59dc008u);gba_a32_emit(e,0xe083300cu);gba_a32_emit(e,0xe58d3008u);
    LOADH(12,1,offsetof(gba_native_block_t,fetch_bound_seq));gba_a32_emit(e,0xe083300cu);
    gba_a32_emit(e,0xe59dc000u);gba_a32_emit(e,0xe35c0000u);
    LOADB(12,1,offsetof(gba_native_block_t,count));gba_a32_emit(e,0x1083300cu);
    /* IWRAM blocks reserve the additional possible ROM-data wait in a
     * separate field. The native memory helper is allowed to load ROM here. */
    gba_a32_load(e,12,1,offsetof(gba_native_block_t,memory_mask));gba_a32_emit(e,0x1083300cu);
    LOADH(12,1,offsetof(gba_native_block_t,extra_bound));gba_a32_emit(e,0xe083300cu);
    gba_a32_emit(e,0xe59dc010u);gba_a32_emit(e,0xe15c0003u);REJECT(0x90000000u);
    /* Commit only now. Pending devices wait until the exit stub spills. */
#if GBA_DYNAREC_LINK_CACHE
    /* ROM_PREFIX is prohibited in this mode. Reuse its private pointer slot
     * for the last validated successor; all normal epoch/mode/RAM guards run
     * again on every hit, even if a metadata slot has since been reused. */
    gba_a32_emit(e,0xe59d001cu);gba_a32_store(e,1,0,offsetof(gba_native_block_t,prefix_offset));
#endif
    gba_a32_emit(e,0xe59d0008u);gba_a32_emit(e,0xe59d3010u);gba_a32_emit(e,0xe0433000u);gba_a32_emit(e,0xe58d3010u);
    gba_a32_emit(e,0xe59d3018u);gba_a32_emit(e,0xe0833000u);gba_a32_emit(e,0xe58d3018u);
    gba_a32_load(e,3,4,offsetof(gba_t,cpu_pending));gba_a32_emit(e,0xe0833000u);SAVE(3,offsetof(gba_t,cpu_pending));
    gba_a32_emit(e,0xe3a00000u);SAVE(0,offsetof(gba_t,machine.prefetch_count));SAVE(0,offsetof(gba_t,machine.prefetch_phase));
    gba_a32_emit(e,0xe3a00001u);SAVE(0,offsetof(gba_cpu_t,pipeline_valid));
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    gba_a32_emit(e,0xe3170801u);uint32_t *no_write=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3a00000u);SAVE(0,offsetof(gba_t,wait.valid));
    *no_write|=(uint32_t)(e->next-no_write-2)&0xffffff;
#endif
    gba_a32_emit(e,0xe59d001cu);gba_a32_load(e,0,0,offsetof(gba_native_block_t,pc));gba_a32_emit(e,0xe1a00c20u);gba_a32_emit(e,0xe3500003u);
    uint32_t *rom_link=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe59d0000u);gba_a32_emit(e,0xe3500001u);
    uint32_t *ram_link=e->next;gba_a32_emit(e,0x0a000000u);
    *rom_link|=(uint32_t)(e->next-rom_link-2)&0xffffff;
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->coarse_rom_links);uint32_t *counter=e->next;gba_a32_emit(e,0xea000000u);
    *ram_link|=(uint32_t)(e->next-ram_link-2)&0xffffff;
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->direct_links);
    *counter|=(uint32_t)(e->next-counter-2)&0xffffff;
    gba_a32_emit(e,0xe59c0000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe58c0000u);
    gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->rom_data_seen);gba_a32_emit(e,0xe3a00002u);gba_a32_emit(e,0xe58c0000u);
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
    gba_a32_emit(e,0xe3a00001u);gba_a32_emit(e,0xe58d0000u);
    gba_a32_emit(e,0xe3a05000u);gba_a32_emit(e,0xe3a06000u);gba_a32_emit(e,0xe3a07000u);gba_a32_emit(e,0xe58d7004u);
    gba_a32_load(e,12,4,offsetof(gba_cpu_t,cpsr));gba_a32_emit(e,0xe128f00cu);gba_a32_emit(e,0xe12fff1eu);
#ifdef GBA_TEST_CHAIN_REJECTS
    uint32_t *leave[32];
    for(unsigned n=0;n<failures;n++) {
        *fail[n]|=(uint32_t)(e->next-fail[n]-2)&0xffffff;
        gba_a32_constant(e,12,(uint32_t)(uintptr_t)&c->coarse_reject[n]);
        gba_a32_emit(e,0xe59c0000u);gba_a32_emit(e,0xe2800001u);gba_a32_emit(e,0xe58c0000u);
        leave[n]=e->next;gba_a32_emit(e,0xea000000u);
    }
    for(unsigned n=0;n<failures;n++)*leave[n]|=(uint32_t)(e->next-leave[n]-2)&0xffffff;
#else
    for(unsigned n=0;n<failures;n++)*fail[n]|=(uint32_t)(e->next-fail[n]-2)&0xffffff;
#endif
#undef REJECT
#undef LOADB
#undef LOADH
#undef SAVE
}
#endif
