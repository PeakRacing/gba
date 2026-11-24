/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#pragma once
#if GBA_DYNAREC_PERSISTENT_MAP_HELPER
static void gba_persistent_mapper_call(gba_a32_t *e,uintptr_t target) {
    int64_t distance=(int64_t)(uint32_t)target-(int64_t)(uint32_t)(uintptr_t)e->next-8;
    if(distance>=-0x2000000 && distance<=0x1fffffc
#ifdef GBA_TEST_MAP_FAR_CALL
       && 0
#endif
       )gba_a32_emit(e,0xeb000000u|((uint32_t)(distance/4)&0xffffff));
    else {gba_a32_literal(e,3,(uint32_t)target);gba_a32_emit(e,0xe12fff33u);}
}
/* Shared native mapping bodies keep repeated guards out of every translated
 * memory instruction. They never call C, alter guest memory, or consume cycles.
 * r0=guest address -> r1=host pointer (zero on rejection), r2=data cost,
 * r0=ROM flag. r4-r12 and the real stack are untouched; r14 is the BL return. */
static int gba_persistent_mappers(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    for(unsigned index=0;index<6;index++) {
        if(c->persistent_mapper[index])continue;
        if(g->dynarec_memory.code_size-c->used<1024)return -1;
        unsigned bytes=1u<<(index%3),load=index/3;
        uint32_t start=c->used,*code=(uint32_t *)((uint8_t *)g->dynarec_memory.code+start);
        if(gba_code_begin_write(code,1024))return -1;
        gba_a32_t e={.begin=code,.next=code,.end=code+256};
#if GBA_DYNAREC_PERSISTENT_PAGE_TABLE
        uint32_t *reject[8];unsigned rejected=0;
        const uint32_t *page_base=load?c->persistent_read_page:c->persistent_write_page;
        if(bytes>1) {
            gba_a32_emit(&e,0xe3100000u|(bytes-1));
            reject[rejected++]=e.next;gba_a32_emit(&e,0x1a000000u);
        }
        gba_a32_emit(&e,0xe1a027a0u); /* index = address >> 15 */
        gba_a32_literal(&e,3,(uint32_t)(uintptr_t)page_base);
        gba_a32_emit(&e,0xe7931102u); /* host page base and type */
        gba_a32_emit(&e,0xe2013003u); /* type = entry & 3 */
        gba_a32_emit(&e,0xe3c11003u); /* page base = entry & ~3 */
        gba_a32_emit(&e,0xe3530000u);
        reject[rejected++]=e.next;gba_a32_emit(&e,0x0a000000u);
        gba_a32_emit(&e,0xe1a02880u);gba_a32_emit(&e,0xe1a028a2u); /* page offset */
        gba_a32_emit(&e,0xe0811002u);
        gba_a32_emit(&e,0xe3530002u);
        uint32_t *not_iwram=e.next;gba_a32_emit(&e,0x1a000000u);
        if(!load) {
            gba_a32_literal(&e,1,(uint32_t)(uintptr_t)(c->code_page+1024));
            gba_a32_emit(&e,0xe1a02880u);gba_a32_emit(&e,0xe1a028a2u);
            gba_a32_emit(&e,0xe7d11422u);gba_a32_emit(&e,0xe1a002a2u);
            gba_a32_emit(&e,0xe2000007u);gba_a32_emit(&e,0xe1a01031u);
            gba_a32_emit(&e,0xe3110001u);reject[rejected++]=e.next;gba_a32_emit(&e,0x1a000000u);
        }
        gba_a32_constant(&e,2,1u+load);
        gba_a32_emit(&e,0xe3a00000u);gba_a32_emit(&e,0xe12fff1eu);
        *not_iwram|=(uint32_t)(e.next-not_iwram-2)&0xffffff;
        gba_a32_emit(&e,0xe3530003u);
        uint32_t *not_ewram=e.next;gba_a32_emit(&e,0x1a000000u);
        if(!load) {
            gba_a32_literal(&e,1,(uint32_t)(uintptr_t)c->code_page);
            gba_a32_emit(&e,0xe1a02700u);gba_a32_emit(&e,0xe1a02722u);
            gba_a32_emit(&e,0xe7d11422u);gba_a32_emit(&e,0xe1a002a2u);
            gba_a32_emit(&e,0xe2000007u);gba_a32_emit(&e,0xe1a01031u);
            gba_a32_emit(&e,0xe3110001u);reject[rejected++]=e.next;gba_a32_emit(&e,0x1a000000u);
        }
        gba_a32_constant(&e,2,(bytes==4?6u:3u)+load);
        gba_a32_emit(&e,0xe3a00000u);gba_a32_emit(&e,0xe12fff1eu);
        *not_ewram|=(uint32_t)(e.next-not_ewram-2)&0xffffff;
        if(load) {
            gba_a32_emit(&e,0xe3530001u);
            reject[rejected++]=e.next;gba_a32_emit(&e,0x1a000000u);
            gba_a32_emit(&e,0xe1a03c20u); /* ROM wait bank from region */
            static const unsigned first[4]={4,3,2,8};
            uint16_t wait=gba_io16(g,0x204);unsigned costs[3];
            for(unsigned bank=0;bank<3;bank++)costs[bank]=first[(wait>>(2+bank*3))&3]+2+
                (bytes==4?gba_gamepak_seq(wait,bank):0);
            gba_a32_constant(&e,2,costs[0]);
            gba_a32_emit(&e,0xe353000au);uint32_t *bank_done=e.next;gba_a32_emit(&e,0x3a000000u);
            gba_a32_constant(&e,2,costs[1]);
            gba_a32_emit(&e,0xe353000cu);uint32_t *bank_one=e.next;gba_a32_emit(&e,0x3a000000u);
            gba_a32_constant(&e,2,costs[2]);
            *bank_done|=(uint32_t)(e.next-bank_done-2)&0xffffff;
            *bank_one|=(uint32_t)(e.next-bank_one-2)&0xffffff;
            gba_a32_emit(&e,0xe3a00001u);gba_a32_emit(&e,0xe12fff1eu);
        } else {
            reject[rejected++]=e.next;gba_a32_emit(&e,0xea000000u);
        }
        for(unsigned n=0;n<rejected;n++)*reject[n]|=(uint32_t)(e.next-reject[n]-2)&0xffffff;
        gba_a32_emit(&e,0xe3a01000u);gba_a32_emit(&e,0xe12fff1eu);
#else
        uint32_t *reject[8];unsigned rejected=0;
        if(bytes>1) {gba_a32_emit(&e,0xe3100000u|(bytes-1));reject[rejected++]=e.next;gba_a32_emit(&e,0x1a000000u);}
        gba_a32_emit(&e,0xe1a03c20u);gba_a32_emit(&e,0xe3530003u);
        uint32_t *ram[2];ram[1]=e.next;gba_a32_emit(&e,0x0a000000u);
        gba_a32_emit(&e,0xe3530002u);ram[0]=e.next;gba_a32_emit(&e,0x0a000000u);
        if(load && g->rom && g->rom_size>=bytes) {
            if(GBA_DYNAREC_PERSISTENT_ROM_WINDOW && g->rom_size>0x1000 && g->rom_size-0x1000>=bytes) {
                /* The primary GamePak window (08/09) has one wait-state
                 * bank. One unsigned range check also excludes GPIO/header,
                 * mirrors, MMIO and out-of-range reads. The fallback below
                 * retains their original guards. ROM data is still live. */
                gba_a32_emit(&e,0xe2402000u|gba_a32_immediate(0x08000000u));
                gba_a32_emit(&e,0xe2422000u|gba_a32_immediate(0x1000));
                gba_a32_constant(&e,1,(uint32_t)(g->rom_size-0x1000-bytes));gba_a32_emit(&e,0xe1520001u);
                uint32_t *ordinary=e.next;gba_a32_emit(&e,0x8a000000u);
                gba_a32_constant(&e,1,(uint32_t)(uintptr_t)(g->rom+0x1000));gba_a32_emit(&e,0xe0811002u);
                static const unsigned primary_first[4]={4,3,2,8};
                uint16_t primary_wait=gba_io16(g,0x204);
                gba_a32_constant(&e,2,primary_first[(primary_wait>>2)&3]+2+
                    (bytes==4?gba_gamepak_seq(primary_wait,0):0));
#if GBA_DYNAREC_STATS
                gba_a32_literal(&e,3,(uint32_t)(uintptr_t)&c->stats.persistent_rom_window_hits);
                gba_a32_emit(&e,0xe5930000u);gba_a32_emit(&e,0xe2800001u);gba_a32_emit(&e,0xe5830000u);
#endif
                gba_a32_emit(&e,0xe3a00001u);gba_a32_emit(&e,0xe12fff1eu);
                *ordinary|=(uint32_t)(e.next-ordinary-2)&0xffffff;
            }
            gba_a32_emit(&e,0xe3530008u);reject[rejected++]=e.next;gba_a32_emit(&e,0x3a000000u);
            gba_a32_emit(&e,0xe353000cu);reject[rejected++]=e.next;gba_a32_emit(&e,0x8a000000u);
            gba_a32_emit(&e,0xe1a02380u);gba_a32_emit(&e,0xe1a023a2u);
            gba_a32_constant(&e,1,(uint32_t)g->rom_size-bytes);gba_a32_emit(&e,0xe1520001u);
            reject[rejected++]=e.next;gba_a32_emit(&e,0x8a000000u);
            gba_a32_emit(&e,0xe35200c0u);uint32_t *not_gpio=e.next;gba_a32_emit(&e,0x3a000000u);
            gba_a32_emit(&e,0xe35200ccu);reject[rejected++]=e.next;gba_a32_emit(&e,0x3a000000u);
            *not_gpio|=(uint32_t)(e.next-not_gpio-2)&0xffffff;
            gba_a32_constant(&e,1,(uint32_t)(uintptr_t)g->rom);gba_a32_emit(&e,0xe0811002u);
            static const unsigned first[4]={4,3,2,8};
            uint16_t wait=gba_io16(g,0x204);unsigned costs[3];
            for(unsigned bank=0;bank<3;bank++)costs[bank]=first[(wait>>(2+bank*3))&3]+1+
                (bytes==4?gba_gamepak_seq(wait,bank):0)+1;
            gba_a32_constant(&e,2,costs[0]);gba_a32_emit(&e,0xe353000au);
            gba_a32_emit(&e,0x23a02000u|costs[1]);gba_a32_emit(&e,0xe353000cu);
            gba_a32_emit(&e,0x23a02000u|costs[2]);
            gba_a32_emit(&e,0xe3a00001u);gba_a32_emit(&e,0xe12fff1eu);
        } else {reject[rejected++]=e.next;gba_a32_emit(&e,0xea000000u);}
        for(unsigned region=2;region<=3;region++) {
            *ram[region-2]|=(uint32_t)(e.next-ram[region-2]-2)&0xffffff;
            unsigned shift=region==3?17:14;
            gba_a32_emit(&e,0xe1a02000u|(shift<<7));gba_a32_emit(&e,0xe1a02022u|(shift<<7));
            if(!load) {
                gba_a32_literal(&e,1,(uint32_t)(uintptr_t)(c->code_page+(region==3?1024:0)));
                gba_a32_emit(&e,0xe7d11422u);gba_a32_emit(&e,0xe1a002a2u);gba_a32_emit(&e,0xe2000007u);
                gba_a32_emit(&e,0xe1a01031u);gba_a32_emit(&e,0xe3110001u);
                reject[rejected++]=e.next;gba_a32_emit(&e,0x1a000000u);
            }
            gba_a32_constant(&e,1,(uint32_t)(uintptr_t)(region==3?g->memory.iwram:g->memory.ewram));
            gba_a32_emit(&e,0xe0811002u);gba_a32_constant(&e,2,(region==3?1:bytes==4?6:3)+load);
            gba_a32_emit(&e,0xe3a00000u);gba_a32_emit(&e,0xe12fff1eu);
        }
        for(unsigned n=0;n<rejected;n++)*reject[n]|=(uint32_t)(e.next-reject[n]-2)&0xffffff;
        gba_a32_emit(&e,0xe3a01000u);gba_a32_emit(&e,0xe12fff1eu);
#endif
        if(e.failed || gba_code_publish(code,(size_t)(e.next-code)*4))return -1;
        c->persistent_mapper[index]=start;c->used=(start+(uint32_t)(e.next-code)*4+31)&~31u;
    }
    return 0;
}
#endif
