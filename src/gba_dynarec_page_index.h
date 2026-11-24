/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
/* Physical-page reverse membership. Included outside native execution. */
#pragma once
#if GBA_DYNAREC_PAGE_INDEX
typedef struct {
    uint32_t page_next, block_next, block;
    uint16_t page;
    uint8_t lines, reserved;
} gba_page_member_t;
_Static_assert(sizeof(gba_page_member_t)==16,"page-index arena sizing");
static gba_page_member_t *gba_page_members(gba_native_cache_t *c) {
    return (gba_page_member_t *)((uint8_t *)c+c->page_index_offset);
}
static void gba_page_index_reset(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_page_member_t *members=gba_page_members(c);
    gba_memset(c->page_index_head,0,sizeof(c->page_index_head));
    for(unsigned n=0;n<c->page_index_capacity;n++)members[n].page_next=n+2;
    if(c->page_index_capacity)members[c->page_index_capacity-1].page_next=0;
    c->page_index_free=c->page_index_capacity?1:0;
    c->page_index_available=c->page_index_capacity;
    gba_memcpy(c->code_page,c->legacy_code_page,sizeof(c->code_page));
}
static void gba_page_index_remove(gba_t *g,gba_persistent_block_t *block) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_page_member_t *members=gba_page_members(c);
    uint32_t id=block->page_members;block->page_members=0;
    while(id) {
        gba_page_member_t *member=members+id-1;
        uint32_t next=member->block_next;
        unsigned page=member->page;
        uint32_t *cursor=c->page_index_head+page;
        while(*cursor && *cursor!=id)cursor=&members[*cursor-1].page_next;
        if(*cursor)*cursor=member->page_next;
        uint8_t lines=c->legacy_code_page[page];
        for(uint32_t entry=c->page_index_head[page];entry;entry=members[entry-1].page_next)
            lines|=members[entry-1].lines;
        c->code_page[page]=lines;
        member->page_next=c->page_index_free;c->page_index_free=id;c->page_index_available++;
        id=next;
    }
}
typedef struct {
    unsigned count;
    struct {uint16_t page;uint8_t lines;} entry[GBA_DYNAREC_PERSISTENT_BLOCK_INSNS*2+2];
} gba_page_coverage_t;
static void gba_page_index_cover(gba_page_coverage_t *coverage,uint32_t pc,unsigned width) {
    unsigned region=pc>>24;if(region!=2 && region!=3)return;
    unsigned mask=region==3?0x7fff:0x3ffff;
    uint32_t end=pc+3*width;
    for(uint32_t at=pc;at<end;) {
        uint32_t stop=(at&~255u)+256u;if(stop>end)stop=end;
        unsigned page=(region==3?1024:0)+((at&mask)>>8);
        unsigned first=(at&255)>>5,last=((stop-1)&255)>>5;
        unsigned lines=(255u<<first)&(255u>>(7-last)),n=0;
        for(;n<coverage->count;n++)if(coverage->entry[n].page==page)break;
        if(n==coverage->count) {coverage->entry[n].page=(uint16_t)page;coverage->entry[n].lines=0;coverage->count++;}
        coverage->entry[n].lines|=(uint8_t)lines;at=stop;
    }
}
static int gba_page_index_register(gba_t *g,gba_persistent_block_t *block,unsigned slot,
    const gba_page_coverage_t *actual) {
    unsigned region=block->pc>>24;
    if(region!=2 && region!=3)return 1;
    gba_native_cache_t *c=gba_native_cache(g);
    gba_page_member_t *members=gba_page_members(c);
    uint32_t end=block->end+2*(block->thumb?2u:4u);
    unsigned required=actual?actual->count:((end-1)>>8)-(block->pc>>8)+1;
    if(required>c->page_index_available) {
        c->persistent_reset=1;GBA_NATIVE_COUNT(c,invalidate_recycles,1);return 0;
    }
    uint32_t mask=region==3?0x7fffu:0x3ffffu,at=block->pc;
    for(unsigned n=0;n<required;n++) {
        uint32_t stop=(at&~255u)+256u;if(stop>end)stop=end;
        unsigned page=(region==3?1024:0)+((at&mask)>>8);
        unsigned first=(at&255)>>5,last=((stop-1)&255)>>5;
        unsigned lines=(255u<<first)&(255u>>(7-last));
        if(actual) {page=actual->entry[n].page;lines=actual->entry[n].lines;}
        uint32_t id=c->page_index_free;
        gba_page_member_t *member=members+id-1;
        c->page_index_free=member->page_next;c->page_index_available--;
        member->page=(uint16_t)page;member->lines=(uint8_t)lines;member->block=slot;
        member->page_next=c->page_index_head[page];c->page_index_head[page]=id;
        member->block_next=block->page_members;block->page_members=id;
        c->code_page[page]|=member->lines;at=stop;
    }
    return 1;
}
static void gba_page_index_invalidate(gba_t *g,uint32_t address,size_t size) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_page_member_t *members=gba_page_members(c);
    gba_persistent_block_t *table=gba_persistent_table(g);
    unsigned region=address>>24,mask=region==3?0x7fff:0x3ffff;
    GBA_NATIVE_COUNT(c,invalidate_requests,1);
    while(size) {
        unsigned offset=address&mask,page=(region==3?1024:0)+(offset>>8);
        size_t step=256-(address&255);if(step>size)step=size;
        unsigned first=(address&255)>>5,last=((address&255)+(unsigned)step-1)>>5;
        unsigned touched=(255u<<first)&(255u>>(7-last));
        GBA_NATIVE_COUNT(c,invalidate_pages,1);
        uint32_t id=c->page_index_head[page];
        while(id) {
            gba_page_member_t *member=members+id-1;
            uint32_t next=member->page_next;
            GBA_NATIVE_COUNT(c,invalidate_checks,1);
            if(member->lines&touched) {
                gba_persistent_block_t *block=table+member->block;
                if(gba_persistent_unlink(g,block->pc,block->thumb)) {
                    gba_fail(g,GBA_ERROR_IO);return;
                }
                gba_persistent_forget_edges(g,block);
                gba_page_index_remove(g,block);block->valid=0;
                GBA_NATIVE_COUNT(c,invalidate_blocks,1);
            }
            /* Removing a block can also remove a second membership on this
             * physical page (mirror wrap); never follow a freed member. */
            id=(member->lines&touched)?c->page_index_head[page]:next;
        }
        if((c->code_page[page]&touched) && !c->page_index_head[page])
            GBA_NATIVE_COUNT(c,invalidate_empty,1);
        address+=(uint32_t)step;size-=step;
    }
}

#ifdef GBA_TEST_PAGE_INDEX
unsigned gba_test_page_index(gba_t *g) {
    gba_native_cache_t *c=gba_native_cache(g);
    gba_persistent_block_t *table=gba_persistent_table(g);
    gba_memset(table,0,c->persistent_slots*sizeof(*table));
    gba_page_index_reset(g);c->persistent_reset=0;
    unsigned capacity=c->page_index_available;
    /* Unrelated physical pages do not increase local candidate visits. */
    for(unsigned n=0;n<64;n++) {
        table[n]=(gba_persistent_block_t){.pc=0x03000000+n*256,.end=0x03000004+n*256,.valid=1};
        if(!gba_page_index_register(g,table+n,n,NULL))return 1;
    }
#if GBA_DYNAREC_STATS
    uint32_t before=c->stats.invalidate_checks;
#endif
    gba_page_index_invalidate(g,0x03008000+256*7,1);
    if(table[7].valid || !table[6].valid || !table[8].valid)return 2;
#if GBA_DYNAREC_STATS
    if(c->stats.invalidate_checks-before!=1)return 3;
#endif
    if(c->code_page[1024+7])return 4;
    /* Same-page blocks protect independent lines, including rejected code. */
    table[7]=(gba_persistent_block_t){.pc=0x03000880,.end=0x03000882,.thumb=1,.valid=1};
    if(!gba_page_index_register(g,table+7,7,NULL))return 5;
    gba_page_index_invalidate(g,0x03000808,1); /* ARM pipeline lookahead. */
    if(table[8].valid || !table[7].valid || c->code_page[1032]!=16)return 6;
    gba_page_index_invalidate(g,0x03008884,1); /* Thumb negative-cache lookahead through mirror. */
    if(table[7].valid || c->code_page[1032])return 7;
    for(unsigned n=0;n<64;n++)if(table[n].valid) {
        gba_page_index_remove(g,table+n);table[n].valid=0;
    }
    if(c->page_index_available!=capacity)return 8;
    /* Mirror wrap protects both physical end and beginning pages. */
    table[0]=(gba_persistent_block_t){.pc=0x03007ffc,.end=0x03008004,.valid=1};
    if(!gba_page_index_register(g,table,0,NULL) || !c->code_page[1151] || !c->code_page[1024])return 9;
    gba_page_index_invalidate(g,0x03000008,1);
    if(table[0].valid || c->code_page[1151] || c->code_page[1024] || c->page_index_available!=capacity)return 10;
    gba_page_coverage_t coverage={0};
    gba_page_index_cover(&coverage,0x03000000,4);gba_page_index_cover(&coverage,0x03001000,4);
    table[0]=(gba_persistent_block_t){.pc=0x03000000,.end=0x03001004,.valid=1};
    if(!gba_page_index_register(g,table,0,&coverage))return 14;
    gba_page_index_invalidate(g,0x03000800,4);
    if(!table[0].valid || c->code_page[1032])return 15;
    gba_page_index_invalidate(g,0x03001008,1);
    if(table[0].valid || c->page_index_available!=capacity)return 16;
    /* Persistent retirement cannot remove protection owned by legacy code. */
    c->legacy_code_page[1024]=128;
    table[0]=(gba_persistent_block_t){.pc=0x03000000,.end=0x03000004,.valid=1};
    if(!gba_page_index_register(g,table,0,NULL))return 11;
    gba_page_index_remove(g,table);table[0].valid=0;
    if(c->code_page[1024]!=128)return 12;
    /* Exhaustion never publishes partial memberships or consumes the free list. */
    unsigned free=c->page_index_free;c->page_index_available=0;
    if(gba_page_index_register(g,table,0,NULL) || !c->persistent_reset ||
       table[0].page_members || c->page_index_free!=free)return 13;
    c->legacy_code_page[1024]=0;
    gba_memset(table,0,c->persistent_slots*sizeof(*table));gba_page_index_reset(g);
    c->persistent_reset=1;
    return 0;
}
#endif
#endif
