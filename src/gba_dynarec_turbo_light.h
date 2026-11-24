/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
/* LIGHT phase B1: compiler-owned bounded literal pool, no guest timing change. */
#pragma once
#if GBA_DYNAREC_TURBO_LIGHT
static unsigned gba_turbo_light_long(uint32_t raw,unsigned width) {
    if(width!=4 || (raw&0x0f8000f0u)!=0x00800090u)return 0;
    unsigned low=(raw>>12)&15,high=(raw>>16)&15;
    return low!=15 && high!=15 && low!=high && (raw&15)!=15 && ((raw>>8)&15)!=15;
}
static void gba_turbo_light_flush(gba_a32_t *e) {
    gba_turbo_literal_pool_t *pool=e->literal_pool;
    if(!pool || !pool->count || e->failed)return;
    uint32_t *skip=e->next;gba_a32_emit(e,0xea000000u);
    uint32_t *values[128];unsigned unique=0;
    for(unsigned n=0;n<pool->count;n++) {
        unsigned same=0;
        for(;same<n;same++)if(pool->value[same]==pool->value[n])break;
        if(same<n)values[n]=values[same];
        else {values[n]=e->next;gba_a32_emit(e,pool->value[n]);unique++;}
        uintptr_t delta=(uintptr_t)values[n]-(uintptr_t)(pool->site[n]+2);
        if(delta>4095 || (delta&3) || e->failed) {e->failed=1;return;}
        *pool->site[n]|=(uint32_t)delta;
    }
    *skip|=(uint32_t)(e->next-skip-2)&0xffffff;
    pool->count=0;
    (void)unique;
}
static void gba_turbo_light_literal(gba_a32_t *e,unsigned rd,uint32_t value) {
    gba_turbo_literal_pool_t *pool=e->literal_pool;
    /* Flush only at explicit literal boundaries, never inside emit(): callers
     * often take e->next immediately before emitting a branch patch site. */
    if(pool->count && (pool->count==128 || e->next-pool->site[0]>512))gba_turbo_light_flush(e);
    if(e->failed)return;
    unsigned n=pool->count++;
    pool->site[n]=e->next;pool->value[n]=value;
    gba_a32_emit(e,0xe59f0000u|(rd<<12));
}
#endif
