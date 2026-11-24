/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#pragma once
static unsigned gba_persistent_multiple_decode(uint32_t raw,unsigned width) {
    if(!GBA_DYNAREC_PERSISTENT_MULTI)return 0;
    if(width==4) {
        uint32_t op=(raw&0x0fffffffu)|0xe0000000u;
        if((op&0xfe400000u)!=0xe8000000u || !(op&0xffff) ||
            ((op&0x8000)&&!(op&0x100000)) || ((op>>16)&15)==15)return 0;
        return op;
    }
    if((raw&0xf600)==0xb400) {
        unsigned load=!!(raw&0x800),list=raw&255;
        if(raw&0x100)list|=load?0x8000:0x4000;
        return list?((load?0xe8bd0000u:0xe92d0000u)|list):0;
    }
    if((raw&0xf000)==0xc000 && (raw&255))
        return ((raw&0x800)?0xe8b00000u:0xe8a00000u)|(((raw>>8)&7)<<16)|(raw&255);
    return 0;
}
static void gba_persistent_multiple(gba_t *g,gba_a32_t *e,uint32_t op,uint32_t pc,
    unsigned thumb,unsigned prefix,unsigned fetch,
    gba_persistent_reservations_t *reservations,unsigned fixed_cost,gba_persistent_side_exits_t *side) {
    unsigned rn=(op>>16)&15,list=op&0xffff,count=(unsigned)__builtin_popcount(list),load=!!(op&0x100000);
    gba_a32_guest_read(e,14,rn,0);
    int delta=(op&0x800000)?((op&0x1000000)?4:0):-(int)(count*4)+((op&0x1000000)?0:4);
    if(delta)gba_a32_emit(e,(delta>0?0xe28e0000u:0xe24e0000u)|(unsigned)(delta>0?delta:-delta));
    else gba_a32_emit(e,0xe1a0000eu);
    gba_a32_emit(e,0xe10fc000u);
    uint32_t *reject[10];unsigned rejected=0;
    gba_a32_emit(e,0xe3100003u);reject[rejected++]=e->next;gba_a32_emit(e,0x1a000000u);
    gba_a32_emit(e,0xe1a02c20u);gba_a32_emit(e,0xe3520002u);
    uint32_t *ewram=e->next;gba_a32_emit(e,0x0a000000u);
    gba_a32_emit(e,0xe3520003u);reject[rejected++]=e->next;gba_a32_emit(e,0x1a000000u);
    uint32_t *mapped=NULL;
    for(unsigned path=0;path<2;path++) {
        if(path)*ewram|=(uint32_t)(e->next-ewram-2)&0xffffff;
        unsigned shift=path?14:17,size=path?0x40000:0x8000;
        gba_a32_emit(e,0xe1a00000u|(shift<<7));gba_a32_emit(e,0xe1a00020u|(shift<<7));
        gba_a32_constant(e,3,size-count*4);gba_a32_emit(e,0xe1500003u);
        reject[rejected++]=e->next;gba_a32_emit(e,0x8a000000u);
        if(!load) {
            /* The entire range is checked before the first write. Tagged
             * pages fall back to scalar code before any guest effect. */
            gba_a32_literal(e,1,(uint32_t)(uintptr_t)(gba_native_cache(g)->code_page+(path?0:1024)));
            gba_a32_emit(e,0xe1a02420u);gba_a32_emit(e,0xe7d13002u);gba_a32_emit(e,0xe3530000u);
            reject[rejected++]=e->next;gba_a32_emit(e,0x1a000000u);
            gba_a32_emit(e,0xe2802000u|((count-1)*4));gba_a32_emit(e,0xe1a02422u);
            gba_a32_emit(e,0xe7d13002u);gba_a32_emit(e,0xe3530000u);
            reject[rejected++]=e->next;gba_a32_emit(e,0x1a000000u);
        }
        gba_a32_constant(e,1,(uint32_t)(uintptr_t)(path?g->memory.ewram:g->memory.iwram));
        gba_a32_emit(e,0xe0811000u);gba_a32_constant(e,2,count*(path?6:1)+load);
        if(!path){mapped=e->next;gba_a32_emit(e,0xea000000u);}
    }
    *mapped|=(uint32_t)(e->next-mapped-2)&0xffffff;
    gba_persistent_reserve(e,reservations,fixed_cost,fetch);gba_a32_emit(e,0xe15b0003u);
    reject[rejected++]=e->next;gba_a32_emit(e,0x9a000000u);
    unsigned offset=0;
    for(unsigned r=0;r<16;r++)if(list&(1u<<r)) {
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
        gba_a32_emit(e,(op&0x800000?0xe28e0000u:0xe24e0000u)|(count*4));gba_a32_guest_write(e,rn,0);
    }
#if GBA_DYNAREC_STATS
    gba_a32_emit(e,0xe59d3020u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58d3020u);
    gba_a32_emit(e,0xe59d3000u);gba_a32_emit(e,0xe2833001u);gba_a32_emit(e,0xe58d3000u);
    GBA_NATIVE_COUNT(gba_native_cache(g),persistent_multi_sites,1);
#endif
#if GBA_FAST_WAIT && !GBA_TEST_MODE
    if(!GBA_DYNAREC_PERSISTENT_READ_WAIT || !load) {
        gba_a32_literal(e,3,(uint32_t)(uintptr_t)&g->wait.valid);
        gba_a32_emit(e,0xe3a01000u);gba_a32_emit(e,0xe5831000u);
    }
#endif
    gba_a32_emit(e,0xe58d2018u);gba_a32_emit(e,0xe04bb002u);
    gba_a32_constant(e,3,load);gba_a32_emit(e,0xe58d3014u);gba_a32_emit(e,0xe128f00cu);
    uint32_t *done=e->next;gba_a32_emit(e,0xea000000u);
    for(unsigned i=0;i<rejected;i++)*reject[i]|=(uint32_t)(e->next-reject[i]-2)&0xffffff;
    if(GBA_DYNAREC_PERSISTENT_SHARED_EXITS)gba_persistent_reject_site(e,side,pc,prefix);
    else gba_persistent_local_reject(g,e,pc,thumb,prefix,fetch);
    *done|=(uint32_t)(e->next-done-2)&0xffffff;
}
