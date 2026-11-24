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
#include "gba_internal.h"
#include "gba_dynarec_internal.h"
#include "gba_dynarec_hot.h"
#include "gba_arm926_cache.h"
#if defined(GBA_WAIT_DIAG_ACTIVE)
unsigned gba_wait_diag[32];
#endif

static gba_t machine[2];
#ifdef GBA_TEST_LOCAL_ALU_COUNT
unsigned gba_test_local_alu_count;
#endif
#ifdef GBA_TEST_LIGHT_POOL_COMPARE
int gba_test_light_pool_enabled(const gba_t *g){return g==&machine[1];}
#endif
#ifdef GBA_TEST_LITERAL_COMPARE
static int literal_compare_active;
int gba_test_literal_enabled(const gba_t *g){return literal_compare_active && g==&machine[1];}
#endif
#ifdef GBA_TEST_HARD_CACHE_COMPARE
int gba_test_deadline_cache(const gba_t *g){return g==&machine[1];}
#endif
GBA_ALIGN(32) static uint8_t hardware[2][800000];
GBA_ALIGN(32) static uint8_t native_code[GBA_DYNAREC_CODE_SIZE];
GBA_ALIGN(32) static uint8_t native_data[GBA_DYNAREC_DATA_SIZE];
#ifdef GBA_TEST_REPLICA_VALIDATION
GBA_ALIGN(32) static uint8_t other_code[GBA_DYNAREC_CODE_SIZE],other_data[GBA_DYNAREC_DATA_SIZE];
#else
GBA_ALIGN(32) static uint8_t other_code[65536], other_data[GBA_DYNAREC_PAGE_INDEX?65536:GBA_DYNAREC_PERSISTENT?32768:GBA_DYNAREC_DATA_SIZE>=1024u*1024u?65536:16384];
#endif
static uint8_t rom[65536];
static unsigned lane, assertions, failures, quick_compare;
static uint32_t last_pc, last_op;
#if GBA_TEST_RENDER_REFERENCE
int gba_test_render_reference(gba_t *g){return g==&machine[0];}
#endif
static unsigned publish_failure;
static uint32_t audio[2], seed = 0x98273421;

void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *out = d;
    const uint8_t *in = s;
    typedef uint32_t alias_word __attribute__((may_alias));
    if (!(((uintptr_t)out | (uintptr_t)in) & 3)) {
        while (n >= 4) {
            *(alias_word *)out = *(const alias_word *)in;
            out += 4;
            in += 4;
            n -= 4;
        }
    }
    while (n--)
        *out++ = *in++;
    return d;
}
void *memset(void *d, int v, size_t n) {
    typedef uint32_t alias_word __attribute__((may_alias));
    uint8_t *out = d;
    uint32_t word = (uint8_t)v * 0x01010101u;
    if (!((uintptr_t)out & 3))
        while (n >= 4) {
            *(alias_word *)out = word;
            out += 4;
            n -= 4;
        }
    while (n--)
        *out++ = (uint8_t)v;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    typedef uint32_t alias_word __attribute__((may_alias));
    if (!(((uintptr_t)x | (uintptr_t)y) & 3)) {
        while (n >= 4 && *(const alias_word *)x == *(const alias_word *)y) {
            x += 4;
            y += 4;
            n -= 4;
        }
    }
    while (n--) {
        if (*x != *y)
            return *x - *y;
        x++;
        y++;
    }
    return 0;
}
void *gba_memcpy(void *d, const void *s, size_t n) {
    return memcpy(d, s, n);
}
void *gba_memset(void *d, int v, size_t n) {
    return memset(d, v, n);
}
void *gba_malloc(size_t n) {
    (void)n;
    return NULL;
}
void gba_free(void *p) {
    (void)p;
}
int gba_log_printf(const char *format, ...) {
    (void)format;
    return 0;
}
uint32_t gba_get_ticks(void) {
    return 0;
}
int gba_initex(gba_t *g) {
    (void)g;
    return 0;
}
int gba_deinitex(gba_t *g) {
    (void)g;
    return 0;
}
static void puts_uart(const char *p);
static void hex(uint32_t value);
void gba_frame(gba_t *g) {
#ifdef GBA_QEMU_PROGRESS
    if(!(g->machine.frames%60)) {
        puts_uart("REPLAY frame=");hex(g->machine.frames);
        puts_uart(" pc=");hex(g->machine.cpu.r[15]);puts_uart("\n");
    }
#else
    (void)g;
#endif
}
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *p) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
    return 0;
}
int gba_sound_output(uint8_t *p, size_t n) {
    while (n--)
        audio[lane] = (audio[lane] ^ *p++) * 16777619u;
    return 0;
}
static void puts_uart(const char *p);
static void hex(uint32_t value);
int gba_code_begin_write(void *address, size_t bytes) {
    (void)address;
    (void)bytes;
    return 0;
}
int gba_code_publish(void *address, size_t bytes) {
    if (publish_failure) {
        publish_failure = 0;
        return -1;
    }
    /* Execute the same line-scoped publication hook used by the F1C port. */
    return gba_arm926_publish(address,bytes);
}
static void puts_uart(const char *p) {
    volatile uint32_t *uart = (volatile uint32_t *)0x101f1000;
    while (*p) {
        while (uart[6] & 32) {
        }
        *uart = (uint32_t)*p++;
    }
}
static void hex(uint32_t value) {
    char out[9];
    for (unsigned i = 0; i < 8; i++)
        out[i] = "0123456789abcdef"[(value >> (28 - i * 4)) & 15];
    out[8] = 0;
    puts_uart(out);
}
static void finish(unsigned code) {
    uint32_t block[2] = {0x20026, code};
    register uint32_t r0 __asm__("r0") = 0x20;
    register uint32_t r1 __asm__("r1") = (uint32_t)(uintptr_t)block;
    __asm__ volatile("swi 0x123456" : "+r"(r0), "+r"(r1)::"memory");
    for (;;) {
    }
}
void trap(uint32_t pc) {
    puts_uart("TRAP pc=");
    hex(pc);
    puts_uart(" op=");
    hex(*(uint32_t *)(uintptr_t)pc);
    puts_uart("\n");
    finish(4);
}
static uint32_t random32(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
#ifdef GBA_TEST_CHAIN_TRACE
static uint32_t chain_trace[32][10],chain_trace_count;
void gba_test_chain_trace(gba_t *g,uint32_t pc,uint32_t count,uint32_t fetch,uint32_t result) {
    uint32_t *r=chain_trace[chain_trace_count++&31];
    r[0]=pc;r[1]=count;r[2]=fetch;r[3]=result;r[4]=g->machine.cycles+g->cpu_pending;
    r[5]=g->machine.prefetch_count;r[6]=g->machine.prefetch_phase;r[7]=g->machine.cpu.fetch_sequential;
    r[8]=g->machine.cpu.r[15];r[9]=gba_native_cache(g)->chain_rom_sequential;
}
#endif
static void compare(unsigned scene) {
#ifdef GBA_TEST_CANDIDATE_ONLY
    (void)scene;
    return;
#endif
    assertions++;
    gba_t *a = &machine[0], *b = &machine[1];
    if (memcmp(&a->machine, &b->machine, sizeof(a->machine)) || memcmp(a->io, b->io, sizeof(a->io)) ||
        (!quick_compare && memcmp(hardware[0], hardware[1], gba_memory_size())) || a->fault != b->fault ||
        audio[0] != audio[1]) {
        failures++;
        puts_uart("last_pc=");
        hex(last_pc);
        puts_uart(" opcode=");
        hex(last_op);
        puts_uart(" ");
        puts_uart("FAIL scene=");
        hex(scene);
        puts_uart(" pc=");
        hex(a->machine.cpu.r[15]);
        puts_uart("/");
        hex(b->machine.cpu.r[15]);
        for (unsigned i = 0; i < sizeof(a->machine) / 4; i++) {
            uint32_t x = ((uint32_t *)&a->machine)[i], y = ((uint32_t *)&b->machine)[i];
            if (x != y) {
                puts_uart(" word=");
                hex(i);
                puts_uart(" value=");
                hex(x);
                puts_uart("/");
                hex(y);
                break;
            }
        }
        puts_uart("\n");
        puts_uart(" audio=");hex(audio[0]);puts_uart("/");hex(audio[1]);
        puts_uart(" fault=");hex(a->fault);puts_uart("/");hex(b->fault);puts_uart("\n");
        puts_uart(" cycles=");hex(a->machine.cycles);puts_uart("/");hex(b->machine.cycles);
        puts_uart(" fifo=");hex(a->machine.prefetch_count);puts_uart("/");hex(b->machine.prefetch_count);
        puts_uart(" phase=");hex(a->machine.prefetch_phase);puts_uart("/");hex(b->machine.prefetch_phase);
        puts_uart(" access=");hex(a->access_cycles);puts_uart("/");hex(b->access_cycles);puts_uart("\n");
        for (unsigned i = 0; i < sizeof(a->io); i++)
            if (a->io[i] != b->io[i]) { puts_uart(" io_byte=");hex(i);puts_uart("\n");break; }
        for (unsigned i = 0; i < gba_memory_size(); i++)
            if (hardware[0][i] != hardware[1][i]) {
                puts_uart(" memory_byte="); hex(i);
                puts_uart(" value="); hex(hardware[0][i]); puts_uart("/"); hex(hardware[1][i]);
                puts_uart("\n"); break;
            }
#ifdef GBA_TEST_CHAIN_TRACE
        for(unsigned n=0;n<32;n++) {
            uint32_t *r=chain_trace[(chain_trace_count+n)&31];puts_uart("CHAIN ");
            for(unsigned v=0;v<10;v++){hex(r[v]);puts_uart(" ");}puts_uart("\n");
        }
#endif
#ifdef GBA_TEST_TURBO_TRACE_DIAG
        extern volatile uint32_t gba_test_turbo_branch_src,gba_test_turbo_branch_target;
        puts_uart(" turbo_branch=");hex(gba_test_turbo_branch_src);puts_uart("/");
        hex(gba_test_turbo_branch_target);puts_uart("\n");
        for(unsigned n=0;n<8;n++) {puts_uart(" turbo_word=");hex((uint32_t)gba_read16(b,gba_test_turbo_branch_target+n*2));puts_uart("\n");}
#endif
        finish(1);
    }
}
#ifdef GBA_TEST_INLINE_PC_PROOF
uint32_t gba_test_inline_pc_entries;
#endif
#ifdef GBA_TEST_BULK_PREFILL_PROOF
unsigned gba_test_bulk_prefill_entries;
#endif
#ifdef GBA_TEST_VIDEO_STORE_PROOF
unsigned gba_test_video_store_entries;
#endif
static void setup(unsigned thumb, uint32_t op, uint32_t flags) {
    for (unsigned i = 0; i < 2; i++) {
        gba_t *g = &machine[i];
        gba_reset(g);
        g->gba_quit = 0;
        g->fault = 0;
        g->machine.cpu.cpsr = 0x1f | (thumb ? GBA_T : 0) | flags;
        g->machine.cpu.r[15] = 0x03000000;
        for (unsigned r = 0; r < 15; r++)
            g->machine.cpu.r[r] = 0x1f234567u * r + op;
        for (unsigned pos = 0; pos < 256; pos += thumb ? 2 : 4) {
            if (thumb)
                gba_put16(g->memory.iwram + pos, (uint16_t)op);
            else
                gba_put32(g->memory.iwram + pos, op);
        }
        audio[i] = 0;
    }
}
static void run(unsigned scene, uint32_t budget) {
#if defined(GBA_TEST_PERSISTENT_DIAG)
    puts_uart("RUN ");hex(scene);puts_uart(" budget=");hex(budget);puts_uart("\n");
#endif
    last_pc = machine[1].machine.cpu.r[15];
    last_op = machine[1].machine.cpu.pipeline[0];
    lane = 1;
    gba_run_cycles(&machine[1], budget);
    lane = 0;
    uint32_t target = machine[1].machine.cycles;
    unsigned steps = 0;
    while (machine[0].machine.cycles < target && steps++ < 100000)
        gba_step(&machine[0]);
    compare(scene);
}

extern unsigned gba_short_write, gba_replace_failure;
extern int gba_ram_file_flip(const char *path, size_t offset);
extern uint32_t gba_ram_file_hash(const char *path);
static void require(int success, unsigned scene) {
    assertions++;
    if (!success) {
        puts_uart("API FAIL ");
        hex(scene);
        puts_uart("\n");
        finish(9);
    }
}
#if GBA_DYNAREC_TURBO_LIGHT && !defined(GBA_TEST_CANDIDATE_ONLY)
static void light_long_multiply_proof(void) {
    static const uint32_t values[]={0,1,255,256,65535,65536,0xffffff,0x1000000,0x7fffffff,0x80000000,0xffffffff};
    unsigned native_seen=0;
    for(unsigned variant=0;variant<128;variant++)for(unsigned n=0;n<sizeof(values)/sizeof(*values);n++) {
        unsigned low=variant%15,high=(variant/3)%15;if(high==low)high=(high+1)%15;
        unsigned rs=(variant/5)%15,rm=(variant/7)%15;
        static const unsigned conditions[]={14,0,1,12};
        uint32_t op=(conditions[(variant>>3)&3]<<28)|0x00800090u|(high<<16)|(low<<12)|(rs<<8)|rm|
            ((variant&1)?0x100000u:0)|((variant&2)?0x200000u:0)|((variant&4)?0x400000u:0);
        setup(0,op,((variant>>3)&15)<<28);
        for(unsigned i=0;i<2;i++)machine[i].machine.cpu.r[rs]=values[n];
        run(0xfb000+variant*16+n,1);run(0xfb000+variant*16+n,7);
        run(0xfb000+variant*16+n,511);run(0xfb000+variant*16+n,29);
#if GBA_DYNAREC_STATS
        native_seen+=gba_native_cache(&machine[1])->stats.turbo_long_multiply!=0;
#endif
    }
    require(native_seen || !GBA_DYNAREC_STATS,0xfbfff);
    puts_uart("LIGHT long multiply: signed/unsigned, accumulate, N/Z/C/V, aliases, conditions and deadlines PASS\n");
}
#endif
#if GBA_DYNAREC_EVENT_SLIP && !defined(GBA_TEST_CANDIDATE_ONLY)
static void light_control_proof(void) {
    for(unsigned thumb=0;thumb<2;thumb++)for(unsigned variant=0;variant<16;variant++) {
        setup(thumb,thumb?0x3001:0xe2800001,GBA_C);
        unsigned width=thumb?2:4;
        for(unsigned n=0;n<128;n++) {
            unsigned op=n==127?(thumb?0xe77f:0xeaffff7f):(thumb?0x3001:0xe2800001);
            if(thumb)gba_put16(rom+0x1000+n*width,(uint16_t)op);else gba_put32(rom+0x1000+n*width,op);
        }
        gba_t *g=&machine[1];g->machine.cpu.r[15]=0x08001000;g->machine.cpu.pipeline_valid=0;
        gba_io_write(g,0x204,variant*0x111,2);
        unsigned done=gba_run_cycles(g,4096);
        /* Single scalar instructions may finish across the API budget. This
         * fixture has only ADD/B and at most 36 cycles for a ROM refill. */
        puts_uart("Control slice cycles=");hex(done);puts_uart("\n");
        require(done<=4096+36,0xfa100+thumb*16+variant);
    }
    puts_uart("Native control ceiling: 4096 cycles plus one bounded scalar instruction PASS\n");
}
#endif
static void instances(void) {
    gba_dynarec_memory_t other = {other_code, sizeof(other_code), other_data, sizeof(other_data)};
    require(!gba_dynarec_bind(&machine[0], &other), 21);
    setup(0, 0xe2800001, 0);
    for (unsigned i = 0; i < 2; i++) {
        machine[i].machine.cpu.r[0] = 0;
        for (unsigned at = 0; at < 256; at += 4)
            gba_put32(machine[i].memory.iwram + at, 0xe2800001 + i);
        gba_put32(machine[i].memory.iwram + 256, 0xeaffffbe);
    }
    for (unsigned n = 0; n < 40; n++) {
        lane = 0;
        gba_run_cycles(&machine[0], 97);
        lane = 1;
        gba_run_cycles(&machine[1], 97);
        require(machine[1].machine.cpu.r[0] == machine[0].machine.cpu.r[0] * 2 &&
                    machine[1].machine.cycles == machine[0].machine.cycles && !machine[0].fault && !machine[1].fault,
            22);
    }
    puts_uart("Two independently compiled instances PASS\n");
}
static void states(void) {
    setup(0, 0xe2800001, 0);
    for (unsigned i = 0; i < 2; i++)
        gba_put32(machine[i].memory.iwram + 256, 0xeaffffbe);
    for (unsigned n = 0; n < 20; n++)
        run(0x90000, 93);
    require(!gba_state_save_file(&machine[0], "ref.state"), 1);
    require(!gba_state_save_file(&machine[1], "jit.state"), 2);
    require(gba_ram_file_hash("ref.state") == gba_ram_file_hash("jit.state"), 3);
    for (unsigned n = 0; n < 20; n++)
        run(0x90001, 97);
    require(!gba_state_load_file(&machine[0], "ref.state"), 4);
    require(!gba_state_load_file(&machine[1], "jit.state"), 5);
    for (unsigned n = 0; n < 20; n++)
        run(0x90002, 97);
    uint32_t hash = gba_ram_file_hash("jit.state");
    gba_short_write = 1;
    require(gba_state_save_file(&machine[1], "jit.state") != 0, 6);
    require(gba_ram_file_hash("jit.state") == hash && !machine[1].fault, 7);
    gba_replace_failure = 1;
    require(gba_state_save_file(&machine[1], "jit.state") != 0, 8);
    require(gba_ram_file_hash("jit.state") == hash && !machine[1].fault, 9);
    require(!gba_state_save_file(&machine[1], "jit.state"), 10);
    for (unsigned n = 0; n < 32; n++) {
        gba_machine_t before = machine[1].machine;
        size_t position = 64 + n * 101;
        require(!gba_ram_file_flip("jit.state", position), 11);
        require(gba_state_load_file(&machine[1], "jit.state") != 0, 12);
        require(!memcmp(&before, &machine[1].machine, sizeof(before)), 13);
        require(!gba_ram_file_flip("jit.state", position), 14);
    }
    gba_dynarec_memory_t invalid = machine[1].dynarec_memory;
    invalid.code = (uint8_t *)invalid.code + 1;
    require(gba_dynarec_bind(&machine[1], &invalid) == GBA_ERROR_MEMORY, 15);
    invalid = machine[1].dynarec_memory;
    invalid.code = hardware[1];
    require(gba_dynarec_bind(&machine[1], &invalid) == GBA_ERROR_MEMORY, 16);
    invalid = machine[1].dynarec_memory;
    invalid.data_size = 1024;
    require(gba_dynarec_bind(&machine[1], &invalid) == GBA_ERROR_MEMORY, 17);
    invalid = machine[1].dynarec_memory;
    machine[1].running = 1;
    require(gba_dynarec_bind(&machine[1], &invalid) == GBA_ERROR_BUSY, 18);
    machine[1].running = 0;
    publish_failure = 1;
    require(gba_dynarec_bind(&machine[1], &invalid) == GBA_ERROR_UNSUPPORTED, 19);
    setup(0, 0xe2800001, 0);
    /* Warm the pipeline through the scalar fallback, then fail publication of
     * the first multi-instruction native span before any of its effects. */
    lane = 1;
    gba_run_cycles(&machine[1], 1);
    publish_failure = 1;
    uint32_t before = machine[1].machine.cpu.r[0];
    gba_run_cycles(&machine[1], 512);
    require(machine[1].fault == GBA_ERROR_IO && machine[1].machine.cpu.r[0] == before, 20);
    puts_uart("STATE replay, short write, replace failure, 32 corruptions, binding failures PASS\n");
}

#if GBA_DYNAREC_PERSISTENT_ENGINE
#ifdef GBA_TEST_BRANCH_SUPERBLOCK_PROOF
static void branch_superblock_proof(void) {
    extern unsigned gba_test_branch_inline;
    gba_test_branch_inline=0;
    for(unsigned thumb=0;thumb<2;thumb++)for(unsigned zero=0;zero<2;zero++) {
        setup(thumb,thumb?0x2800:0xe3500000,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[0]=zero;g->machine.cpu.r[1]=g->machine.cpu.r[2]=0;
            g->machine.cpu.r[3]=g->machine.cpu.r[4]=0;g->machine.cpu.r[15]=0x03000000;
            if(thumb) {
                gba_put16(g->memory.iwram+0x00,0x2800);
                gba_put16(g->memory.iwram+0x02,0xd006);
                gba_put16(g->memory.iwram+0x04,0x3101);
                gba_put16(g->memory.iwram+0x06,0x3201);
                gba_put16(g->memory.iwram+0x08,0xe00a);
                gba_put16(g->memory.iwram+0x10,0x3901);
                gba_put16(g->memory.iwram+0x12,0x3301);
                gba_put16(g->memory.iwram+0x14,0xe004);
                gba_put16(g->memory.iwram+0x20,0x3401);
                gba_put16(g->memory.iwram+0x22,0xe7ed);
            } else {
                gba_put32(g->memory.iwram+0x00,0xe3500000);
                gba_put32(g->memory.iwram+0x04,0x0a000002);
                gba_put32(g->memory.iwram+0x08,0xe2811001);
                gba_put32(g->memory.iwram+0x0c,0xe2822001);
                gba_put32(g->memory.iwram+0x10,0xea000005);
                gba_put32(g->memory.iwram+0x18,0xe2411001);
                gba_put32(g->memory.iwram+0x1c,0xe2833001);
                gba_put32(g->memory.iwram+0x20,0xea000000);
                gba_put32(g->memory.iwram+0x24,0xe2844001);
                gba_put32(g->memory.iwram+0x28,0xeafffff4);
            }
        }
        static const uint32_t budgets[]={1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,97,127,257};
        for(unsigned n=0;n<sizeof(budgets)/sizeof(budgets[0]);n++)
            run(0xfa000+thumb*512+zero*256+n,budgets[n]);
    }
    puts_uart("Persistent conditional-branch superblocks: ARM/Thumb taken/fallthrough, joins, budgets PASS\n");
    puts_uart("Persistent branch inline compiles=");hex(gba_test_branch_inline);puts_uart("\n");
}
#endif
static void persistent_proof(void) {
#if GBA_DYNAREC_TURBO
    uint32_t saved_rom[16];
    for(unsigned n=0;n<16;n++)saved_rom[n]=gba_le32(rom+0xcc+n*4);
    for(unsigned region=1;region<=(GBA_DYNAREC_FAST_BUS?2u:1u);region++)for(unsigned thumb=0;thumb<2;thumb++) {
        setup(thumb,0,0);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            uint8_t *code=region==1?g->memory.ewram:rom+0xcc;
            uint32_t pc=region==1?0x02000000u:0x080000ccu;
            if(thumb) {
                gba_put16(code+0,0x2000);gba_put16(code+2,0x3001);gba_put16(code+4,0xe7fe);
            } else {
                gba_put32(code+0,0xe3a00000);gba_put32(code+4,0xe2800001);gba_put32(code+8,0xeafffffe);
            }
            g->machine.cpu.r[15]=pc;g->machine.cpu.pipeline_valid=0;
        }
#if GBA_DYNAREC_STATS
        uint64_t before=gba_native_cache(&machine[1])->stats.persistent_instructions;
#endif
        run(0xfb000+region*256+thumb*64,2048);
#if GBA_DYNAREC_STATS
        require(gba_native_cache(&machine[1])->stats.persistent_instructions>before,
            0xfb800+region*8+thumb);
#endif
        if(region==1)require(gba_native_cache(&machine[1])->code_page[0]&1,0xfb900+thumb);
    }
    for(unsigned n=0;n<16;n++)gba_put32(rom+0xcc+n*4,saved_rom[n]);
    puts_uart(GBA_DYNAREC_FAST_BUS?"Turbo EWRAM and post-header ROM code execution PASS\n":
        "Turbo EWRAM code execution PASS\n");
#endif
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_FUSION && GBA_DYNAREC_PERSISTENT_SHARED_EXITS
    for(unsigned thumb=0;thumb<2;thumb++)for(unsigned multi=0;multi<2;multi++) {
        setup(thumb,0,0);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            if(thumb) {
                gba_put16(g->memory.iwram+0x00,0x2000); /* movs r0,#0 */
                gba_put16(g->memory.iwram+0x02,0x2101); /* movs r1,#1 */
                gba_put16(g->memory.iwram+0x04,0xe001); /* forward b +2 */
                gba_put16(g->memory.iwram+0x06,0x2102); /* skipped */
                gba_put16(g->memory.iwram+0x08,0x4802); /* pc-relative address */
                gba_put16(g->memory.iwram+0x0a,multi?0xc806:0x6802);
                gba_put16(g->memory.iwram+0x0c,0x3201);
                gba_put16(g->memory.iwram+0x0e,0xe7fe); /* b self */
                gba_put16(g->memory.iwram+0x14,0);
                gba_put16(g->memory.iwram+0x16,0x0400);
            } else {
                gba_put32(g->memory.iwram+0x00,0xe3a00000); /* mov r0,#0 */
                gba_put32(g->memory.iwram+0x04,0xe3a01001); /* mov r1,#1 */
                gba_put32(g->memory.iwram+0x08,0xea000000); /* forward b +0 */
                gba_put32(g->memory.iwram+0x0c,0xe3a01002); /* skipped */
                gba_put32(g->memory.iwram+0x10,0xe3a00201); /* mov r0,#0x04000000 */
                gba_put32(g->memory.iwram+0x14,multi?0xe8900006:0xe5902000);
                gba_put32(g->memory.iwram+0x18,0xe2822001);
                gba_put32(g->memory.iwram+0x1c,0xeafffffe); /* b self */
            }
        }
        static const uint32_t budgets[]={64,127,4096};
        for(unsigned n=0;n<sizeof(budgets)/sizeof(budgets[0]);n++)
            run(0xfc000+thumb*256+multi*64+n,budgets[n]);
    }
    puts_uart("Turbo fused-branch shared MMIO/multi reject exits PASS\n");
#endif
    gba_native_cache_t *geometry=gba_native_cache(&machine[1]);
    puts_uart("Persistent cache geometry private=");hex(sizeof(*geometry));
    puts_uart(" legacy=");hex(geometry->slots);puts_uart(" native=");hex(geometry->persistent_slots);
    puts_uart(" indirect=");hex(1u<<GBA_DYNAREC_PERSISTENT_INDIRECT_BITS);puts_uart("\n");
#if GBA_DYNAREC_DATA_SIZE == 512u*1024u && GBA_DYNAREC_PERSISTENT_INDIRECT_BITS <= 9
    require(geometry->slots==4096 && geometry->persistent_slots==4096,0xfd180);
#endif
#if GBA_DYNAREC_PERSISTENT_ROM_WINDOW
    /* Exercise the live native ROM mapping, including both endpoints and all
     * rejected/ordinary fallback classes. Count actual helper selections,
     * rather than translation sites; a selection can precede a budget exit. */
    static const uint32_t addresses[]={0x08001000,0x0800fffc,0x08000ffc,0x080000c4,
        0x08010000,0x0a001000,0x0c001000,0x08001001};
    uint32_t saved_first=gba_le32(rom+0x1000),saved_last=gba_le32(rom+0xfffc);
    for(unsigned thumb=0;thumb<2;thumb++)for(unsigned width=0;width<3;width++)
    for(unsigned at=0;at<8;at++)for(unsigned prefetch=0;prefetch<2;prefetch++) {
        uint32_t op=thumb?(width==0?0x7820:width==1?0x8820:0x6820):
            (width==0?0xe5d40000:width==1?0xe1d400b0:0xe5940000);
        setup(thumb,op,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[4]=addresses[at];
            gba_write16(g,0x04000204,prefetch?0x4000:0);
            if(thumb)gba_put16(g->memory.iwram+2,0xe7fd);
            else gba_put32(g->memory.iwram+4,0xeafffffd);
        }
        gba_dynarec_stats_t rb,ra;gba_dynarec_stats(&machine[1],&rb);
        for(unsigned n=0;n<8;n++)run(0xff000+thumb*512+width*128+at*16+prefetch*8+n,127+n*19);
        /* Borrowed ROM data remains live, including after code publication. */
        gba_put32(rom+0x1000,0xa1234567u^at);gba_put32(rom+0xfffc,0x89abcdefu^width);
        for(unsigned n=0;n<4;n++)run(0xff800+thumb*512+width*128+at*16+prefetch*4+n,63+n*23);
        gba_dynarec_stats(&machine[1],&ra);
#if GBA_DYNAREC_STATS
        unsigned eligible=at<2 || (at==7 && width==0);
#if GBA_DYNAREC_TURBO_TLB
        unsigned tlb_page=at==1;
        uint32_t hits_before=tlb_page ? ra.turbo_tlb_hits>rb.turbo_tlb_hits :
            ra.persistent_rom_window_hits>rb.persistent_rom_window_hits;
        uint32_t hits_after=tlb_page ? ra.turbo_tlb_hits==rb.turbo_tlb_hits :
            ra.persistent_rom_window_hits==rb.persistent_rom_window_hits;
#else
        uint32_t hits_before=ra.persistent_rom_window_hits>rb.persistent_rom_window_hits;
        uint32_t hits_after=ra.persistent_rom_window_hits==rb.persistent_rom_window_hits;
#endif
        if(eligible && !hits_before) {
            puts_uart("ROM window counters thumb=");hex(thumb);puts_uart(" width=");hex(width);
            puts_uart(" tlb=");hex(ra.turbo_tlb_hits-rb.turbo_tlb_hits);
            puts_uart(" mapper=");hex(ra.persistent_rom_window_hits-rb.persistent_rom_window_hits);puts_uart("\n");
        }
        require(eligible?(hits_before && ra.memory_instructions>rb.memory_instructions):hits_after,
            0xfff00+thumb*32+width*8+at);
#endif
    }
    gba_put32(rom+0x1000,saved_first);gba_put32(rom+0xfffc,saved_last);
    puts_uart("Persistent live primary ROM window: widths, endpoints, GPIO, OOB, mirrors, unaligned, prefetch and mutation PASS\n");
#endif
#if GBA_DYNAREC_PERSISTENT_BL_DIRECT
    for(unsigned variant=0;variant<128;variant++) {
        unsigned prefix_index=variant&15,base=0xfe0+((variant&32)?2:0);
        uint32_t address=0x03000000u|((variant&64)?0x8000:0)|base;
        uint32_t target=(variant&16)?address-0x100:address+0x100;
        int delta=(int)target-(int)(address+prefix_index*2+4);
        unsigned high=0xf000|(((unsigned)delta>>12)&2047),low=0xf800|(((unsigned)delta>>1)&2047);
        setup(1,0x3401,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[15]=address;g->machine.cpu.r[2]=0;
            for(unsigned n=0;n<prefix_index;n++)gba_put16(g->memory.iwram+base+n*2,0x3401);
            gba_put16(g->memory.iwram+base+prefix_index*2,high);
            gba_put16(g->memory.iwram+base+prefix_index*2+2,low);
            gba_put16(g->memory.iwram+base+prefix_index*2+4,0xe000|((-(int)(prefix_index+4))&2047));
            for(unsigned offset=base-0x100;offset<=base+0x100;offset+=0x200) {
                gba_put16(g->memory.iwram+offset,offset<base?0x3203:0x3201);
                gba_put16(g->memory.iwram+offset+2,0x4770);
            }
        }
        static const unsigned budgets[]={1,2,3,4,5,15,16,17,31,64,127,263};
        for(unsigned n=0;n<12;n++)run(0xfe000+variant*32+n,budgets[n]);
        target=(variant&16)?address+0x100:address-0x100;
        delta=(int)target-(int)(address+prefix_index*2+4);
        for(unsigned i=0;i<2;i++) {
            gba_write16(&machine[i],address+prefix_index*2,0xf000|(((unsigned)delta>>12)&2047));
            gba_write16(&machine[i],address+prefix_index*2+2,0xf800|(((unsigned)delta>>1)&2047));
        }
        for(unsigned n=0;n<8;n++)run(0xfe010+variant*32+n,17+n*31);
    }
    puts_uart("Persistent direct BL: signed targets, halfword alignment, all block positions, budgets, page/mirror and code mutation PASS\n");
#endif
#ifdef GBA_TEST_DIRECT_BL_PROOF
    setup(1,0xf000,GBA_C|GBA_V);
    for(unsigned i=0;i<2;i++) {
        gba_t *g=&machine[i];g->machine.cpu.r[0]=0;g->machine.cpu.r[15]=0x03001000;
        gba_put16(g->memory.iwram+0x1000,0xf000);gba_put16(g->memory.iwram+0x1002,0xf840);
        gba_put16(g->memory.iwram+0x1004,0xe7fc);
        gba_put16(g->memory.iwram+0x1084,0x3001);gba_put16(g->memory.iwram+0x1086,0x4770);
    }
    gba_dynarec_stats_t bl_before,bl_after;gba_dynarec_stats(&machine[1],&bl_before);
    for(unsigned n=0;n<32;n++)run(0xfd300+n,397+n*7);
    gba_dynarec_stats(&machine[1],&bl_after);
    puts_uart("Persistent Thumb direct BL calls=");hex(machine[1].machine.cpu.r[0]);
    puts_uart(" entries=");hex(bl_after.persistent_entries-bl_before.persistent_entries);puts_uart("\n");
#if GBA_DYNAREC_STATS
    require(machine[1].machine.cpu.r[0]>(bl_after.persistent_entries-bl_before.persistent_entries)*4,0xfd380);
#endif
    puts_uart("Persistent known Thumb BL and colliding BX return continue natively PASS\n");
#endif
#ifdef GBA_TEST_INDIRECT_CAPACITY_PROOF
    for(unsigned thumb=0;thumb<2;thumb++) {
        setup(thumb,thumb?0x3001:0xe2800001,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[0]=g->machine.cpu.r[2]=0;
            g->machine.cpu.r[1]=0x03001080|thumb;g->machine.cpu.r[3]=0x03001000|thumb;
            g->machine.cpu.r[15]=0x03001000;
            if(thumb) {
                gba_put16(g->memory.iwram+0x1000,0x3001);gba_put16(g->memory.iwram+0x1002,0x4708);
                gba_put16(g->memory.iwram+0x1080,0x3201);gba_put16(g->memory.iwram+0x1082,0x4718);
            } else {
                gba_put32(g->memory.iwram+0x1000,0xe2800001);gba_put32(g->memory.iwram+0x1004,0xe12fff11);
                gba_put32(g->memory.iwram+0x1080,0xe2822001);gba_put32(g->memory.iwram+0x1084,0xe12fff13);
            }
        }
        gba_dynarec_stats_t ic_before,ic_after;gba_dynarec_stats(&machine[1],&ic_before);
        for(unsigned n=0;n<32;n++)run(0xfd200+thumb*64+n,397+n*7);
        gba_dynarec_stats(&machine[1],&ic_after);
        puts_uart("Persistent indirect collision thumb=");hex(thumb);
        puts_uart(" hits=");hex(ic_after.persistent_indirect_hits-ic_before.persistent_indirect_hits);
        puts_uart(" entries=");hex(ic_after.persistent_entries-ic_before.persistent_entries);puts_uart("\n");
#if GBA_DYNAREC_STATS
        require(ic_after.persistent_indirect_hits-ic_before.persistent_indirect_hits>
            (ic_after.persistent_entries-ic_before.persistent_entries)*4,0xfd280+thumb);
#endif
    }
    puts_uart("Persistent colliding indirect targets retain native continuation PASS\n");
#endif
#if defined(GBA_TEST_READ_WAIT_PROOF) && GBA_FAST_WAIT
    setup(1,0x4903,GBA_C|GBA_V);
    for(unsigned i=0;i<2;i++) {
        gba_t *g=&machine[i];
        gba_put16(g->memory.iwram+2,0x8808); /* LDRH r0,[r1] */
        gba_put16(g->memory.iwram+4,0x2800); /* CMP r0,#0 */
        gba_put16(g->memory.iwram+6,0xd0fb); /* BEQ literal-load loop */
        gba_put16(g->memory.iwram+8,0xe7fa);
        gba_put32(g->memory.iwram+16,0x04000006);
    }
    for(unsigned n=0;n<16;n++)run(0xfd000+n,127+n*17);
    puts_uart("Persistent read-only poll skipped cycles=");hex((uint32_t)machine[1].wait.skipped_cycles);puts_uart("\n");
    /* Most of this bounded fixture is a stable poll. A few incidental scalar
     * skips near entry budgets do not prove preservation across native reads. */
    require(machine[1].wait.skipped_cycles>1024,0xfd100);
    puts_uart("Persistent read-only poll retains bounded wait proof PASS\n");
#endif
#ifdef GBA_TEST_PERSISTENT_TAIL_PROOF
    for(unsigned budget=16;budget<160;budget++) {
        setup(0,0xe1a05005,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[1]=0x12345678;g->machine.cpu.r[2]=0x02002000;
            g->machine.cpu.r[3]=0x23456789;g->machine.cpu.r[4]=0x81234567;
            gba_put32(g->memory.iwram+4,0xe5920000);
            for(unsigned at=8;at<68;at+=4)gba_put32(g->memory.iwram+at,0xe0244391);
            gba_put32(g->memory.iwram+68,0xeaffffee);
            gba_put32(g->memory.ewram+0x2000,0x76543210);
        }
        run(0xfc000+budget*2,1);run(0xfc001+budget*2,budget);
    }
    puts_uart("Persistent load/MLA tail deadline boundary comparison PASS\n");
#endif
    setup(0,0xe2800001,GBA_C|GBA_V);
    for(unsigned i=0;i<2;i++) {
        for(unsigned at=0;at<128;at+=4)gba_put32(machine[i].memory.iwram+at,0xe2800001);
        gba_put32(machine[i].memory.iwram+128,0xeaffffde);
    }
    gba_dynarec_stats_t before,after;
    gba_dynarec_stats(&machine[1],&before);
    for(unsigned n=0;n<16;n++)run(0xf1000+n,4096);
    gba_dynarec_stats(&machine[1],&after);
#if GBA_DYNAREC_STATS
    require(after.persistent_entries>before.persistent_entries,0xf1020);
    require(after.persistent_links>before.persistent_links,0xf1021);
    require(after.persistent_instructions-before.persistent_instructions>
        (uint64_t)(after.persistent_entries-before.persistent_entries)*16,0xf1022);
    puts_uart("Persistent guest instructions=");hex((uint32_t)(after.persistent_instructions-before.persistent_instructions));
    puts_uart(" entries=");hex(after.persistent_entries-before.persistent_entries);
    puts_uart(" blocks=");hex(after.persistent_blocks-before.persistent_blocks);
    puts_uart(" links=");hex(after.persistent_links-before.persistent_links);puts_uart("\n");
#endif
    puts_uart("Persistent multi-block native execution and exact boundary comparison PASS\n");
    for(unsigned variant=0;variant<8;variant++) {
        setup(0,0xe2800001,(variant&15)<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[12]=0x03000205;g->machine.cpu.r[7]=0x03000000;
            for(unsigned n=0;n<12;n++) {
                unsigned rd=n%6;
                gba_put32(g->memory.iwram+n*4,0xe2900001u|(rd<<16)|(rd<<12));
                gba_put16(g->memory.iwram+0x204+n*2,(uint16_t)(0x3001|(rd<<8)));
            }
            gba_put32(g->memory.iwram+48,0xe12fff1c);gba_put16(g->memory.iwram+0x21c,0x4738);
        }
        gba_dynarec_stats(&machine[1],&before);
        for(unsigned n=0;n<24;n++)run(0xf1800+variant*32+n,13+(n*71)%2000);
        gba_dynarec_stats(&machine[1],&after);
#if GBA_DYNAREC_STATS
        require(after.persistent_indirect_hits>before.persistent_indirect_hits,0xf1900+variant);
#endif
        if(!variant) {
            for(unsigned n=0;n<256;n++)run(0xf1a00+n,4096);
            gba_dynarec_stats(&machine[1],&before);
            gba_memset(gba_native_cache(&machine[1])->persistent_indirect,0,
                sizeof(gba_native_cache(&machine[1])->persistent_indirect));
            for(unsigned n=0;n<64;n++)run(0xf1b00+n,4096);
            gba_dynarec_stats(&machine[1],&after);
#if GBA_DYNAREC_STATS
            require(after.compiled_blocks==before.compiled_blocks,0xf1b80);
            require(after.persistent_indirect_hits>before.persistent_indirect_hits,0xf1b81);
#endif
            puts_uart("Persistent evicted indirect targets recover without recompilation PASS\n");
        }
    }
    puts_uart("Persistent ARM/Thumb fixed-map bridges and real indirect-cache hits PASS\n");
    for(unsigned variant=0;variant<80;variant++) {
        unsigned thumb=variant&1,load=!(variant&2),bytes=(variant>>2)%5;
        unsigned region=(variant&16)?3:2;
        static const uint32_t arm_load[]={0xe5940000,0xe5d40000,0xe1d400b0};
        static const uint32_t arm_store[]={0xe5840000,0xe5c40000,0xe1c400b0};
        static const uint16_t thumb_load[]={0x6820,0x7820,0x8820,0x5660,0x5e60};
        static const uint16_t thumb_store[]={0x6020,0x7020,0x8020};
        unsigned plain=bytes>2?2:bytes;
        uint32_t op=thumb?(load?thumb_load[bytes]:thumb_store[plain]):(load?arm_load[plain]:arm_store[plain]);
        setup(thumb,op,(variant&15)<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[0]=0x12349876;g->machine.cpu.r[1]=0;
            g->machine.cpu.r[4]=(region<<24)|0x2000|((variant&32)?(region==3?0x8000:0x40000):0);
            for(unsigned at=0;at<0x100;at+=4)gba_put32((region==3?g->memory.iwram:g->memory.ewram)+0x2000+at,0x3d128900+at);
            if(thumb) {gba_put16(g->memory.iwram+2,0x3001);gba_put16(g->memory.iwram+4,0x3404);gba_put16(g->memory.iwram+6,0xe7fb);}
            else {gba_put32(g->memory.iwram+4,0xe2800001);gba_put32(g->memory.iwram+8,0xe2844004);gba_put32(g->memory.iwram+12,0xeafffffbu);}
        }
        for(unsigned n=0;n<8;n++)run(0xf2000+variant*16+n,379);
    }
    puts_uart("Persistent RAM loads/stores, widths, flags, mirrors and linked refill comparison PASS\n");
    for(unsigned variant=0;variant<48;variant++) {
        setup(0,0xe2800001,(variant&15)<<28);
        static const uint32_t operations[]={0xe5b40004,0xe4940004,0xe5340004,0xe4140004,
            0xe5a40004,0xe4840004,0xe5240004,0xe4040004,0xe1d400b0,0xe1d400d0,0xe1d400f0,0xe1c400b0};
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[4]=0x02002080;g->machine.cpu.r[0]=0x765489ab;
            gba_put32(g->memory.iwram+4,operations[variant%12]);gba_put32(g->memory.iwram+8,0xeafffffcu);
            for(unsigned at=0;at<256;at+=4)gba_put32(g->memory.ewram+0x2000+at,0x817f5689u+at);
        }
        for(unsigned n=0;n<8;n++)run(0xf4000+variant*16+n,13+n*19);
    }
    puts_uart("Persistent ARM pre/post writeback and signed halfword comparison PASS\n");
    for(unsigned thumb=0;thumb<2;thumb++)for(unsigned lookahead=0;lookahead<2;lookahead++) {
        setup(thumb,thumb?0x3001:0xe2800001,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            if(thumb)gba_put16(machine[i].memory.iwram+64,0xe7de);
            else gba_put32(machine[i].memory.iwram+128,0xeaffffde);
        }
        for(unsigned n=0;n<8;n++)run(0xf5000+n,257);
        for(unsigned i=0;i<2;i++) {
            uint32_t at=0x03ff8000u+(lookahead?(thumb?34:68):0);
            if(thumb)gba_write16(&machine[i],at,0x3007);else gba_write32(&machine[i],at,0xe2800007);
        }
        for(unsigned n=0;n<8;n++)run(0xf5100+n,257);
    }
    puts_uart("Persistent incoming-link revocation through physical RAM mirrors PASS\n");
    /* Deliberately overfill one four-way set. Every evicted target has an
     * incoming direct edge, and execution must return through its cold path
     * instead of following a recycled metadata slot or stale code address. */
    setup(0,0xe2800001,GBA_C|GBA_V);
    uint32_t colliding[8];unsigned collisions=0;
    unsigned mask=gba_native_cache(&machine[1])->persistent_slots/4-1;
    for(uint32_t pc=0x03000000;pc<0x03007ff0 && collisions<8;pc+=4)
        if(!(((pc>>1)^(pc>>11))&mask))colliding[collisions++]=pc;
    require(collisions==8,0xf6000);
    if(collisions==8) {
        for(unsigned i=0;i<2;i++)for(unsigned n=0;n<8;n++) {
            uint32_t at=colliding[n]&0x7fff,target=colliding[(n+1)&7];
            gba_put32(machine[i].memory.iwram+at,0xe2800001);
            gba_put32(machine[i].memory.iwram+at+4,0xea000000u|(((target-colliding[n]-12)>>2)&0xffffff));
        }
        for(unsigned n=0;n<32;n++)run(0xf6100+n,97+n%13);
        for(unsigned i=0;i<2;i++)gba_write32(&machine[i],colliding[3]+0x8000,0xe2800007);
        for(unsigned n=0;n<32;n++)run(0xf6200+n,97+n%13);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_put32(g->memory.ewram+0x2000,0xe2800009);
            gba_io_write(g,0xb0,0x02002000,4);
            gba_io_write(g,0xb4,colliding[3]+0x8000,4);
            gba_io_write(g,0xb8,1,2);gba_io_write(g,0xba,0x8400,2);
        }
        for(unsigned n=0;n<32;n++)run(0xf6300+n,97+n%13);
        require(gba_le32(machine[1].memory.iwram+(colliding[3]&0x7fff))==0xe2800009,0xf6330);
        gba_native_cache(&machine[1])->epoch=UINT32_MAX;
        gba_dynarec_invalidate(&machine[1],0,SIZE_MAX);
        require(gba_native_cache(&machine[1])->epoch==1,0xf6400);
        for(unsigned n=0;n<32;n++)run(0xf6401+n,97+n%13);
        for(unsigned i=0;i<2;i++)gba_io_write(&machine[i],0x204,0x4317,2);
        for(unsigned n=0;n<32;n++)run(0xf6500+n,97+n%13);
    }
    puts_uart("Persistent set eviction, incoming-link reuse, DMA code mutation, epoch wrap and WAITCNT PASS\n");
#if GBA_DYNAREC_PERSISTENT_MULTI
    for(unsigned thumb=0;thumb<2;thumb++) {
        setup(thumb,thumb?0xb53f:0xe92d403f,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            machine[i].machine.cpu.r[13]=0x03006000;machine[i].machine.cpu.r[14]=0x03000000|thumb;
            if(thumb) {gba_put16(machine[i].memory.iwram+2,0x3001);gba_put16(machine[i].memory.iwram+4,0xbd3f);}
            else {gba_put32(machine[i].memory.iwram+4,0xe2800001);gba_put32(machine[i].memory.iwram+8,0xe8bd803f);}
        }
        gba_dynarec_stats(&machine[1],&before);
        for(unsigned n=0;n<32;n++)run(0xfa000+thumb*64+n,397+n*7);
        gba_dynarec_stats(&machine[1],&after);
#if GBA_DYNAREC_STATS
        require(after.persistent_multi_sites>before.persistent_multi_sites,0xfa080+thumb);
        require(after.persistent_multi_instructions>before.persistent_multi_instructions,0xfa086+thumb);
        puts_uart("Persistent multi thumb=");hex(thumb);puts_uart(" instructions=");hex((uint32_t)(after.persistent_instructions-before.persistent_instructions));
        puts_uart(" entries=");hex(after.persistent_entries-before.persistent_entries);puts_uart(" indirect=");hex(after.persistent_indirect_hits-before.persistent_indirect_hits);
        puts_uart(" misses=");hex(after.persistent_misses-before.persistent_misses);puts_uart("\n");
        require(after.memory_instructions-before.memory_instructions>
            (uint64_t)(after.persistent_entries-before.persistent_entries)*2,0xfa082+thumb);
        require(after.persistent_indirect_hits-before.persistent_indirect_hits>
            after.persistent_entries-before.persistent_entries,0xfa084+thumb);
#endif
    }
    puts_uart("Persistent RAM push/pop and PC-return chains actually execute across blocks PASS\n");
    for(unsigned variant=0;variant<128;variant++) {
        unsigned rn=(variant*7)%15,load=variant&1,region=variant&64?2:3;
        unsigned list=variant&16?0x7fff:0x4055;
        uint32_t op=0xe8000000u|(rn<<16)|list|(load<<20)|((variant&2)?0x800000:0)|
            ((variant&4)?0x1000000:0)|((variant&8)?0x200000:0);
        if(variant&32)op=(op&0x0fffffff)|0x10000000; /* conditional NE */
        setup(0,op,(variant&15)<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[rn]=(region<<24)|0x4000|
                (variant&32?(region==3?0x8000:0x40000):0);
            gba_put32(g->memory.iwram+4,0xeafffffe);
            uint8_t *p=region==3?g->memory.iwram:g->memory.ewram;
            for(unsigned at=0x3f00;at<0x4100;at+=4)gba_put32(p+at,0x81234000+at);
        }
        run(0xfb000+variant*4,1);run(0xfb001+variant*4,17);run(0xfb002+variant*4,257);
    }
    puts_uart("Persistent multiple direction/pre-post/writeback/base-in-list/condition/mirror comparison PASS\n");
#endif
}
#ifdef GBA_TEST_LITERAL_COMPARE
static void literal_compare(void) {
    literal_compare_active=1;
    gba_dynarec_stats_t before,after;
    for(unsigned wait_case=0;wait_case<4;wait_case++)for(unsigned variant=0;variant<48;variant++) {
        unsigned thumb=variant<24?(variant&1):0;
        setup(thumb,thumb?0x481f:0xe59f0078,(variant&15)<<28);
        static const uint32_t loads[]={0xe59f0078,0xe5df0078,0xe1df07b8,0xe1df07d8,0xe1df07f8};
        unsigned start=variant==26?0xffc0:0x1000;
        uint32_t op=loads[(variant>>1)%5];
        if(variant==24)op=0xe51f0f48; /* GPIO must retain the slow path. */
        if(variant==25)op=0xe59f0079; /* Unaligned load must retain rotation. */
        if(variant==26)op=0xe59f0078; /* Past the resident ROM tail. */
        if(variant==27)op=0xe59ff078; /* PC destination: use existing branch bridge. */
        gba_put32(rom+0x1080,0x817f5689);
        if(variant==27)gba_put32(rom+0x1080,0x08001000);
        if(thumb) {
            gba_put16(rom+0x1000,0x481f);gba_put16(rom+0x1002,0xb401);gba_put16(rom+0x1004,0xe7fc);
        } else {
            gba_put32(rom+start,op);gba_put32(rom+start+4,0xe10f1000);gba_put32(rom+start+8,0xeafffffc);
        }
        for(unsigned i=0;i<2;i++) {
            gba_write16(&machine[i],0x04000204,(wait_case&1?0x4000:0)|(wait_case&2?0x7fc:0));
            machine[i].machine.cpu.r[15]=((variant>=28?8+((variant-28)%3)*2:8)<<24)|start;
            machine[i].machine.cpu.r[13]=0x03006000;
        }
        gba_dynarec_stats(&machine[1],&before);
        static const unsigned budgets[]={1,2,3,4,7,8,9,15,16,17,31,32,33,63,64,97,127,263,1024};
        for(unsigned n=0;n<sizeof(budgets)/sizeof(budgets[0]);n++) {
            if(n==4 && variant!=27)gba_put32(rom+0x1080,0xfedc8123);
            last_pc=machine[1].machine.cpu.r[15];last_op=machine[1].machine.cpu.pipeline[0];
            lane=0;gba_run_cycles(&machine[0],budgets[n]);
            lane=1;gba_run_cycles(&machine[1],budgets[n]);
            compare(0xf7000+wait_case*4096+variant*32+n);
        }
        gba_dynarec_stats(&machine[1],&after);
#if GBA_DYNAREC_STATS
        if(variant<24 || variant>=27) {
            require(after.persistent_literal_sites>before.persistent_literal_sites,0xf7800+variant);
            require(after.memory_instructions>before.memory_instructions,0xf7900+variant);
#if GBA_DYNAREC_PERSISTENT_LITERAL_LEAF
            require(after.persistent_literal_leaf_reads>before.persistent_literal_leaf_reads,0xf7b00+variant);
#endif
        } else require(after.persistent_literal_sites==before.persistent_literal_sites,0xf7a00+variant);
#endif
    }
    puts_uart("Persistent live ROM literal widths/signs, flags, budget and on/off native comparison PASS\n");
    literal_compare_active=0;
}
#endif
#endif

static uint8_t game_rom[0x2000000];
static int host_call(unsigned op, void *argument) {
    register unsigned r0 __asm__("r0") = op;
    register uintptr_t r1 __asm__("r1") = (uintptr_t)argument;
    __asm__ volatile("swi 0x123456" : "+r"(r0), "+r"(r1)::"memory");
    return (int)r0;
}
static int host_open(const char *path, unsigned length) {
    uintptr_t args[3] = {(uintptr_t)path, 1, length};
    return host_call(1, args);
}
static int host_read(int fd, void *data, size_t bytes) {
    uintptr_t args[3] = {(unsigned)fd, (uintptr_t)data, bytes};
    return host_call(6, args);
}
typedef struct {
    const uint8_t *bytes;
    size_t size, limit;
} test_stream_t;
static test_stream_t streams[2];
#if GBA_ROM_STREAM
static size_t stream_read(void *user, uint32_t offset, void *data, size_t bytes) {
    test_stream_t *stream = user;
    if (offset >= stream->limit)
        return 0;
    size_t available = stream->limit - offset;
    if (bytes > available)
        bytes = available;
    memcpy(data, stream->bytes + offset, bytes);
    return bytes;
}
#endif
static int load_image(unsigned index, const uint8_t *bytes, size_t size) {
    streams[index].bytes = bytes;
    streams[index].size = size;
    streams[index].limit = size;
#if GBA_ROM_STREAM
    return gba_load_stream(&machine[index], size, stream_read, &streams[index]);
#else
    return gba_load_rom(&machine[index], bytes, size);
#endif
}

#if GBA_ROM_STREAM
static void short_stream(void) {
    for (unsigned i = 0; i < 2; i++) {
        gba_unload_rom_discard(&machine[i]);
        for (unsigned at = 0; at < sizeof(rom); at += 4)
            gba_put32(rom + at, 0xe2800001);
        require(!load_image(i, rom, sizeof(rom)), 30);
        machine[i].machine.cpu.r[15] = 0x08001ff8;
        streams[i].limit = 8192;
    }
    run(0xa0000, 1);
    require(machine[0].fault == GBA_ERROR_IO && machine[1].fault == GBA_ERROR_IO, 31);
    for (unsigned i = 0; i < 2; i++) {
        gba_unload_rom_discard(&machine[i]);
        require(!load_image(i, rom, sizeof(rom)), 32);
        machine[i].machine.cpu.r[15] = 0x08001ff8;
    }
    for (unsigned n = 0; n < 20; n++)
        run(0xa0001, 93);
    puts_uart("STREAM short-read and reload PASS\n");
}
#endif

#ifdef GBA_QEMU_STATE
static uint8_t fixture_state[800000];
static void load_fixture_state(void) {
    const char path[]=GBA_QEMU_STATE;
    int fd=host_open(path,sizeof(path)-1);require(fd>=0,0xa0000);
    uintptr_t host_fd=(unsigned)fd;int size=host_call(12,&host_fd);
    require(size>0 && (unsigned)size<=sizeof(fixture_state),0xa0001);
    require(!host_read(fd,fixture_state,(unsigned)size),0xa0002);host_call(2,&host_fd);
    gba_file_t *f=gba_fopen("fixture.state","wb");require(f!=NULL,0xa0003);
    require(gba_fwrite(fixture_state,1,(unsigned)size,f)==(unsigned)size,0xa0004);require(!gba_fclose(f),0xa0005);
    for(unsigned i=0;i<2;i++)require(!gba_state_load_file(&machine[i],"fixture.state"),0xa0006+i);
    puts_uart("Fixture state loaded frame=");hex(machine[1].machine.frames);puts_uart("\n");
}
#endif
static uint32_t fixture_events[1024][2];
static void fixture_advance(gba_t *g) {
#ifdef GBA_QEMU_SLICE_CYCLES
    uint32_t first=g->machine.frames;
    do {if(!gba_run_cycles(g,GBA_QEMU_SLICE_CYCLES))break;}
    while(g->machine.frames==first && !g->fault && !g->gba_quit && !g->machine.cpu.stopped);
#else
    (void)gba_run_frame(g);
#endif
}
static void fixtures(void) {
#ifndef GBA_QEMU_MANIFEST
#define GBA_QEMU_MANIFEST "test/out/dynarec-fixtures.bin"
#endif
    const char file[] = GBA_QEMU_MANIFEST;
    int fd = host_open(file, sizeof(file) - 1);
    if (fd < 0)
        return;
    uint32_t count;
    if (host_read(fd, &count, 4))
        finish(5);
    for (unsigned scene = 0; scene < count; scene++) {
        uint32_t descriptor[3];
        char path[512];
        if (host_read(fd, descriptor, sizeof(descriptor)) || descriptor[0] >= sizeof(path) || descriptor[2] > 1024)
            finish(5);
        if (host_read(fd, path, descriptor[0]))
            finish(5);
        path[descriptor[0]] = 0;
        if (host_read(fd, fixture_events, descriptor[2] * 8))
            finish(5);
        int game = host_open(path, descriptor[0]);
        if (game < 0)
            finish(6);
        uintptr_t game_fd = (unsigned)game;
        int size = host_call(12, &game_fd);
        if (size < 192 || (size_t)size > sizeof(game_rom) || host_read(game, game_rom, (size_t)size))
            finish(6);
        host_call(2, &game_fd);
        puts_uart("ROM ");
        puts_uart(path);
        puts_uart(" frames=");
        hex(descriptor[1]);
        puts_uart("\n");
        for (unsigned i = 0; i < 2; i++) {
            gba_unload_rom_discard(&machine[i]);
            if (load_image(i, game_rom, (size_t)size))
                finish(7);
            audio[i] = 0;
        }
#ifdef GBA_QEMU_STATE
        load_fixture_state();
#endif
        unsigned base_frame=machine[1].machine.frames;
        (void)base_frame;
#if defined(GBA_WAIT_DIAG_ACTIVE)
        memset(gba_wait_diag,0,sizeof(gba_wait_diag));
#endif
        unsigned cursor = 0;
#if GBA_DYNAREC_STATS
        gba_dynarec_stats_t before_stats,after_stats;
        gba_dynarec_stats(&machine[1],&before_stats);
#endif
        uint32_t keys = 0;
#ifdef GBA_TEST_CHAIN_REJECTS
        memset(gba_native_cache(&machine[1])->coarse_reject,0,sizeof(gba_native_cache(&machine[1])->coarse_reject));
        gba_native_cache(&machine[1])->coarse_rom_links=0;
#endif
        for (unsigned frame = 0; frame < descriptor[1]; frame++) {
#ifdef GBA_QEMU_PERF_WARM
            if(frame==GBA_QEMU_PERF_WARM) {
                puts_uart("PERF BEGIN frames=");hex(machine[1].machine.frames);
                puts_uart(" cycles=");hex(machine[1].machine.cycles);puts_uart("\n");
            }
#endif
            while (cursor < descriptor[2] && fixture_events[cursor][0] <= frame)
                keys = fixture_events[cursor++][1];
#ifdef GBA_TEST_REPLICA_VALIDATION
            uint32_t replica_frame_before=machine[1].machine.frames;
#endif
            gba_set_keys(&machine[0], (uint16_t)keys);
            gba_set_keys(&machine[1], (uint16_t)keys);
#if defined(GBA_JIT_COMPARE_STEPS) && GBA_JIT_COMPARE_STEPS
            quick_compare = 1;
            while (machine[1].machine.frames <= base_frame+frame && !machine[1].fault)
                run(0x1000000 + scene * 0x10000 + frame, 1);
            quick_compare = 0;
#else
            lane = 1;
            fixture_advance(&machine[1]);
#endif
            lane = 0;
#if defined(GBA_TEST_REPLICA_VALIDATION) && !defined(GBA_TEST_CANDIDATE_ONLY)
            fixture_advance(&machine[0]);
            /* gba_run_frame may finish an atomic DMA spanning a vblank.
             * Metroid's initialization does so in the established exact tests
             * too. Count actual frames, not one frame per API invocation. */
#ifdef GBA_TEST_TURBO_TRACE_DIAG
            {
                extern volatile uint32_t gba_test_turbo_branch_src,gba_test_turbo_branch_target;
                extern volatile uint32_t gba_test_turbo_entry_pc,gba_test_turbo_exit_pc,gba_test_turbo_exit_meta;
                puts_uart("REPLAY frame=");hex(machine[1].machine.frames);
                puts_uart(" pc=");hex(machine[1].machine.cpu.r[15]);
                puts_uart(" cycles=");hex(machine[1].machine.cycles);
                puts_uart(" branch=");hex(gba_test_turbo_branch_src);puts_uart("/");hex(gba_test_turbo_branch_target);
                puts_uart(" entry_exit=");hex(gba_test_turbo_entry_pc);puts_uart("/");hex(gba_test_turbo_exit_pc);
                puts_uart(" meta=");hex(gba_test_turbo_exit_meta);puts_uart("\n");
            }
#endif
            require(machine[1].machine.frames>replica_frame_before ||
                (machine[1].machine.cpu.stopped && machine[1].machine.frames==replica_frame_before),0xb0000+frame);
            if(machine[1].machine.frames>replica_frame_before+1) {
                puts_uart("REPLICA atomic DMA frame advance=");hex(machine[1].machine.frames-replica_frame_before);puts_uart("\n");
            }
#elif !defined(GBA_TEST_CANDIDATE_ONLY)
            unsigned steps = 0;
            while ((int32_t)(machine[1].machine.cycles - machine[0].machine.cycles) > 0 && steps++ < 10000000) {
                if (!gba_step(&machine[0]))
                    break;
            }
#endif
            compare(0x1000000 + scene * 0x10000 + frame);
#if defined(GBA_TEST_REPLICA_VALIDATION) && !defined(GBA_TEST_CANDIDATE_ONLY)
            if(frame==descriptor[1]/2) {
                require(!gba_state_save_file(&machine[1],"replica.state"),0xb1000);
                for(unsigned i=0;i<2;i++)require(!gba_state_load_file(&machine[i],"replica.state"),0xb1001+i);
                compare(0xb1003);
            }
#endif
            if (machine[1].fault) {
                puts_uart("ROM fault=");
                hex((uint32_t)machine[1].fault);
                puts_uart("\n");
                finish(8);
            }
        }
#ifdef GBA_QEMU_PERF_WARM
        puts_uart("PERF END frames=");hex(machine[1].machine.frames);
        puts_uart(" cycles=");hex(machine[1].machine.cycles);puts_uart("\n");
#endif
        puts_uart("ROM PASS pc=");
        hex(machine[1].machine.cpu.r[15]);
        puts_uart(" cycles=");
        hex(machine[1].machine.cycles);
        puts_uart("\n");
#ifdef GBA_TEST_HOT_BLOCKS
        {
            extern uint32_t gba_test_hot_count[1024],gba_test_hot_pc[1024],gba_test_hot_words[1024];
            for(unsigned top=0;top<16;top++) {
                unsigned best=0;
                for(unsigned n=1;n<1024;n++)if(gba_test_hot_count[n]>gba_test_hot_count[best])best=n;
                if(!gba_test_hot_count[best])break;
                puts_uart("ROM hot pc=");hex(gba_test_hot_pc[best]);puts_uart(" count=");hex(gba_test_hot_count[best]);
                puts_uart(" words=");hex(gba_test_hot_words[best]);puts_uart("\n");
                gba_test_hot_count[best]=0;
            }
        }
#endif
#ifdef GBA_TEST_SCANLINE_COUNTERS
        {
            extern unsigned gba_test_scanline_entries, gba_test_scanline_reject[16];
            puts_uart("ROM scanline_fast=");hex(gba_test_scanline_entries);puts_uart(" rejects=");
            for(unsigned n=0;n<16;n++)if(gba_test_scanline_reject[n]) {
                hex(n);puts_uart(":");hex(gba_test_scanline_reject[n]);puts_uart(",");
            }
            puts_uart("\n");
        }
#endif
#ifdef GBA_TEST_TURBO_TRACE_DIAG
        {
            extern volatile uint32_t gba_test_turbo_branch_src,gba_test_turbo_branch_target;
            extern volatile uint32_t gba_test_turbo_entry_pc,gba_test_turbo_exit_pc,gba_test_turbo_exit_meta;
            puts_uart("ROM turbo_branch=");hex(gba_test_turbo_branch_src);puts_uart("/");
            hex(gba_test_turbo_branch_target);puts_uart("\n");
            puts_uart("ROM turbo_entry_exit=");hex(gba_test_turbo_entry_pc);puts_uart("/");
            hex(gba_test_turbo_exit_pc);puts_uart(" meta=");hex(gba_test_turbo_exit_meta);puts_uart("\n");
            extern volatile uint32_t gba_test_turbo_reject_pc[8],gba_test_turbo_reject_count[8],gba_test_turbo_reject_total;
            extern volatile uint32_t gba_test_turbo_reject_reason[15];
            extern volatile uint32_t gba_test_turbo_reject_reason_pc[15];
            puts_uart("ROM turbo_rejects=");hex(gba_test_turbo_reject_total);puts_uart("\n");
            for(unsigned n=0;n<15;n++)if(gba_test_turbo_reject_reason[n]) {
                puts_uart("ROM turbo_reject_reason=");hex(n);
                puts_uart(" count=");hex(gba_test_turbo_reject_reason[n]);
                puts_uart(" pc=");hex(gba_test_turbo_reject_reason_pc[n]);puts_uart("\n");
            }
            for(unsigned n=0;n<8;n++)if(gba_test_turbo_reject_count[n]) {
                puts_uart("ROM turbo_reject_pc=");hex(gba_test_turbo_reject_pc[n]);
                puts_uart(" count=");hex(gba_test_turbo_reject_count[n]);puts_uart("\n");
            }
        }
#endif
#ifdef GBA_TEST_CHAIN_REJECTS
        puts_uart("COARSE links=");hex(gba_native_cache(&machine[1])->coarse_rom_links);puts_uart("\n");
        for(unsigned n=0;n<32;n++)if(gba_native_cache(&machine[1])->coarse_reject[n]) {
            puts_uart("COARSE reject=");hex(n);puts_uart(" count=");hex(gba_native_cache(&machine[1])->coarse_reject[n]);puts_uart("\n");
        }
#endif
#if GBA_DYNAREC_STATS
        gba_dynarec_stats(&machine[1],&after_stats);
        puts_uart("ROM native=");hex((uint32_t)(after_stats.native_instructions-before_stats.native_instructions));
        puts_uart(" memory=");hex((uint32_t)(after_stats.memory_instructions-before_stats.memory_instructions));
        puts_uart(" fallback=");hex((uint32_t)(after_stats.fallback_instructions-before_stats.fallback_instructions));
        puts_uart(" entries=");hex(after_stats.native_entries-before_stats.native_entries);
        puts_uart(" zero=");hex(after_stats.zero_entries-before_stats.zero_entries);
        puts_uart(" budget=");hex(after_stats.budget_rejects-before_stats.budget_rejects);puts_uart("\n");
        puts_uart("ROM persistent_blocks=");hex(after_stats.persistent_blocks-before_stats.persistent_blocks);
        puts_uart(" entries=");hex(after_stats.persistent_entries-before_stats.persistent_entries);
        puts_uart(" instructions=");hex(after_stats.persistent_instructions-before_stats.persistent_instructions);
        puts_uart(" links=");hex(after_stats.persistent_links-before_stats.persistent_links);
        puts_uart(" misses=");hex(after_stats.persistent_misses-before_stats.persistent_misses);puts_uart("\n");
#if GBA_DYNAREC_TURBO
        puts_uart("ROM turbo traces=");hex(after_stats.turbo_traces-before_stats.turbo_traces);
        puts_uart(" blocks=");hex(after_stats.turbo_blocks-before_stats.turbo_blocks);
        puts_uart(" links=");hex(after_stats.turbo_links-before_stats.turbo_links);
        puts_uart(" fallbacks=");hex(after_stats.turbo_fallbacks-before_stats.turbo_fallbacks);puts_uart("\n");
        puts_uart("ROM turbo_tlb_hits=");hex(after_stats.turbo_tlb_hits-before_stats.turbo_tlb_hits);
        puts_uart(" misses=");hex(after_stats.turbo_tlb_misses-before_stats.turbo_tlb_misses);puts_uart("\n");
        puts_uart("ROM turbo_instructions=");hex((uint32_t)(after_stats.turbo_instructions-before_stats.turbo_instructions));
        puts_uart(" generated_words=");hex((uint32_t)(after_stats.turbo_generated_words-before_stats.turbo_generated_words));puts_uart("\n");
#endif
        for(unsigned n=0;n<8;n++) {
            puts_uart("ROM span_max=");hex(1u<<n);puts_uart(" entries=");
            hex(after_stats.entries_by_size[n]-before_stats.entries_by_size[n]);puts_uart("\n");
        }
#endif
#if defined(GBA_WAIT_DIAG_ACTIVE)
        puts_uart("wait_skipped=");hex(machine[1].wait.skipped_cycles);
        puts_uart(" reject=");hex(machine[1].wait.reject_pc);puts_uart("\n");
        for(unsigned i=0;i<32;i++)if(gba_wait_diag[i]) {
            puts_uart("wait_reason=");hex(i);puts_uart(" count=");hex(gba_wait_diag[i]);puts_uart("\n");
        }
        memset(gba_wait_diag,0,sizeof(gba_wait_diag));
#endif
    }
    uintptr_t manifest_fd = (unsigned)fd;
    host_call(2, &manifest_fd);
}

#ifdef GBA_TEST_FAST_LOOP_PROOF
/* This compares guest computation/storage to the scalar interpreter while
 * deliberately excluding the approximate bus clock and device state. No
 * hardware event is permitted until both finite loops complete. */
static void fast_loop_proof(void) {
    uint32_t generated_bytes=0,max_iterations=0;
    unsigned cases=GBA_DYNAREC_LONG_LOOPS?96:64;
    for(unsigned variant=0;variant<cases;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4,body=variant&2?(thumb?100:52):8;
        if(variant>=64)body=variant&2?3:2;
        uint32_t start=0x08002000,exit=start+body*width;
        setup(thumb,thumb?0x3400:0xe2844000,GBA_C|GBA_V);
        for(unsigned n=0;n<=body;n++) {
            uint32_t op=thumb?0x3201:0xe2822001;
            if(n==0)op=thumb?0x3400:0xe2844000;
            if(n==1)op=thumb?0x6821:0xe4941004;
            if(n==2)op=thumb?0x3404:0xe0822001;
            if(n==3)op=thumb?0x1852:0xe4852004;
            if(n==4)op=thumb?0x602a:0xe1a03002;
            if(n==5)op=thumb?0x3504:0xe2833001;
            if((variant&32) && n>=1 && n<=5)op=thumb?0x3201:0xe2822001;
            if(n==body-2)op=thumb?0x3801:0xe2500001;
            if(n==body-1)op=thumb?(0xd100u|((-(int)(body+1))&255)):(0x1a000000u|((-(int)(body+1))&0xffffff));
            if(n==body)op=thumb?0xe7fe:0xeafffffe;
            if(thumb)gba_put16(rom+0x2000+n*width,op);else gba_put32(rom+0x2000+n*width,op);
        }
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *c=&g->machine.cpu;
            c->r[0]=variant>=64?600+variant:7+(variant>>2)*3;c->r[1]=c->r[2]=c->r[3]=0;
            c->r[4]=variant&4?0x03001000:0x02001000;c->r[5]=variant&8?0x03002000:0x02002000;
            c->r[15]=start;c->pipeline_valid=0;
            for(unsigned n=0;n<256;n++){gba_put32(g->memory.ewram+0x1000+n*4,0x12345678+n);gba_put32(g->memory.iwram+0x1000+n*4,0x12345678+n);}
            gba_io_write(g,0x204,variant&16?0x4000:0,2);
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        /* Compile/warm once, then restore only guest state so the measured
         * finite computation exercises the cached hot entry as well. */
        lane=1;
        machine[1].machine.cpu.pipeline[0]=thumb?gba_le16(rom+0x2000):gba_le32(rom+0x2000);
        machine[1].machine.cpu.pipeline[1]=thumb?gba_le16(rom+0x2000+width):gba_le32(rom+0x2000+width);
        machine[1].machine.cpu.pipeline_valid=machine[1].machine.cpu.fetch_sequential=1;
        machine[1].machine.last_fetch=start+width;
        require(gba_dynarec_block(&machine[1],1024)!=0,0xc6000+variant);
        machine[1].machine=machine[0].machine;memcpy(machine[1].io,machine[0].io,sizeof(machine[0].io));
        memcpy(machine[1].memory.ewram,machine[0].memory.ewram,0x40000);
        memcpy(machine[1].memory.iwram,machine[0].memory.iwram,0x8000);
        machine[1].cpu_pending=0;machine[1].event_cycles=UINT32_MAX;
        lane=0;unsigned limit=100000;
        while((machine[0].machine.cpu.r[0] || machine[0].machine.cpu.r[15]!=exit) && limit--)gba_cpu_execute(&machine[0]);
        require(limit && machine[0].machine.cpu.r[15]==exit,0xc0000+variant);
        lane=1;limit=100000;unsigned repeated=0;
        while((machine[1].machine.cpu.r[0] || machine[1].machine.cpu.r[15]!=exit) && limit--) {
            gba_t *g=&machine[1];gba_cpu_t *c=&g->machine.cpu;
            if(!c->pipeline_valid) {
                const uint8_t *p=rom+(c->r[15]&0x1ffffff);
                c->pipeline[0]=thumb?gba_le16(p):gba_le32(p);
                c->pipeline[1]=thumb?gba_le16(p+width):gba_le32(p+width);
                c->pipeline_valid=c->fetch_sequential=1;g->machine.last_fetch=c->r[15]+width;
            }
            unsigned before=c->r[0],clock=g->machine.cycles+g->cpu_pending;
            uint32_t compiled=gba_dynarec_hot(g,1024);
            if(compiled==UINT32_MAX)compiled=0;
            else if(!compiled)compiled=gba_dynarec_block(g,1024);
            if(!compiled)gba_cpu_execute(g);
            if(compiled)require(g->machine.cycles+g->cpu_pending-clock<=1024,0xc9000+variant);
            if(before>c->r[0]+1) {
                repeated=1;
                if(before-c->r[0]>max_iterations)max_iterations=before-c->r[0];
                require(g->machine.cycles+g->cpu_pending-clock>=(before-c->r[0]-1)*body,0xc7000+variant);
            }
        }
        require(limit && !machine[1].fault,0xc1000+variant);
        for(unsigned r=0;r<15;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xc2000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xc3000+variant);
        require(!memcmp(machine[0].memory.ewram,machine[1].memory.ewram,0x40000) &&
            !memcmp(machine[0].memory.iwram,machine[1].memory.iwram,0x8000),0xc4000+variant);
        unsigned wide=0,loops=0;gba_native_cache_t *cache=gba_native_cache(&machine[1]);
        for(unsigned n=0;n<cache->slots;n++) {
            gba_native_block_t *b=gba_native_blocks(cache)+n;
            if(b->epoch==cache->epoch && b->count && b->pc==start){wide=b->count>32;loops=!!(b->flags&32);}
        }
        require(loops && repeated && (variant>=64 || !(variant&2)||wide),0xc5000+variant);
        generated_bytes+=cache->used-cache->code_base;
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("FAST LOOP generated code total bytes=");hex(generated_bytes);puts_uart("\n");
    if(GBA_DYNAREC_LONG_LOOPS)require(max_iterations>=85,0xc8000);
    puts_uart("FAST LOOP max native iterations=");hex(max_iterations);puts_uart(" cases=");hex(cases);puts_uart(" PASS; scalar computation and minimum charging, no clock/device equivalence claim\n");
}
#endif
#if GBA_DYNAREC_COARSE_CHAIN
/* Finite four-block ROM loop: scalar computation is the oracle even though
 * device/fetch timing is deliberately approximate. Distinct register maps,
 * RAM loads/stores, taken edges and the final not-taken edge all run. */
static void coarse_chain_proof(void) {
    uint32_t total_links=0;
    for(unsigned variant=0;variant<96;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4,body=12,code_ram=(variant&32)||variant>=64;
        uint32_t start=code_ram?0x03003000:0x08003000,exit=start+0x3c0+body*width;
        setup(thumb,thumb?0x3201:0xe2822001,GBA_C|GBA_V);
        for(unsigned block=0;block<4;block++) {
            unsigned at=0x3000+block*0x140;
            for(unsigned n=0;n<=body;n++) {
                unsigned rd=1+(n+block*3)%3;
                uint32_t op=thumb?(0x3001|(rd<<8)):(0xe2800001|(rd<<16)|(rd<<12));
                if((variant&2) && block==1 && n==0)op=thumb?0x6821:0xe5941000;
                if(n==2)op=thumb?0x6821:0xe5941000;
                if(n==5)op=thumb?0x602a:0xe5852000;
                if(block==3 && n==body-2)op=thumb?0x3801:0xe2500001;
                if(n==body-1) {
                    int target=block==3?0x3000:(int)(at+0x140);
                    int delta=target-(int)(at+n*width)-(thumb?4:8);
                    op=thumb?(block==3?(0xd100u|((delta/2)&255)):(0xe000u|((delta/2)&2047))):
                        ((block==3?0x1a000000u:0xea000000u)|((delta/4)&0xffffff));
                    /* Thumb conditional branches cannot span four pages.
                     * Use BNE to a local unconditional back edge instead. */
                    if(thumb && block==3)op=0xd100; /* BNE PC+4 -> local trampoline */
                }
                if(n==body)op=thumb?0xe7fe:0xeafffffe;
                if(thumb)gba_put16(rom+at+n*width,op);else gba_put32(rom+at+n*width,op);
            }
            if(thumb && block==3) {
                int delta=0x3000-(int)(at+(body+1)*2)-4;
                gba_put16(rom+at+(body+1)*2,0xe000|((delta/2)&2047));
                gba_put16(rom+at+(body+2)*2,0xe7fe);
            }
        }
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            if(code_ram)memcpy(g->memory.iwram+0x3000,rom+0x3000,0x400);
            cpu->r[0]=19+variant;cpu->r[1]=cpu->r[2]=cpu->r[3]=0;
            cpu->r[4]=variant&4?0x03001000:0x02001000;
            if(variant&2)cpu->r[4]=0x04000006; /* zero-prefix device guard, VCOUNT */
            if(variant>=64)cpu->r[4]=0x08001000; /* RAM native span loading slow ROM */
            cpu->r[5]=variant&8?0x03002000:0x02002000;
            gba_put32(g->memory.ewram+0x1000,0x27182818);gba_put32(g->memory.iwram+0x1000,0x27182818);
            cpu->r[15]=start;cpu->pipeline_valid=0;
            gba_io_write(g,0x204,variant>=64?0x033c:variant&16?0x4000:0,2);
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        lane=1;
        for(unsigned block=0;block<4;block++) {
            gba_t *g=&machine[1];gba_cpu_t *cpu=&g->machine.cpu;
            unsigned at=0x3000+block*0x140;
            cpu->r[4]=0x02001000; /* compile RAM guard; runtime may reject it */
            cpu->r[15]=start+block*0x140;
            const uint8_t *p=(code_ram?g->memory.iwram:rom)+at;
            cpu->pipeline[0]=thumb?gba_le16(p):gba_le32(p);
            cpu->pipeline[1]=thumb?gba_le16(p+width):gba_le32(p+width);
            cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+width;
            require(gba_dynarec_block(g,2048)!=0,0xd0000+variant*4+block);
        }
        machine[1].machine=machine[0].machine;memcpy(machine[1].io,machine[0].io,sizeof(machine[0].io));
        memcpy(machine[1].memory.ewram,machine[0].memory.ewram,0x40000);
        memcpy(machine[1].memory.iwram,machine[0].memory.iwram,0x8000);
        machine[1].cpu_pending=0;machine[1].event_cycles=UINT32_MAX;
        for(unsigned i=0;i<2;i++) {
            lane=i;unsigned limit=100000;
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            while((cpu->r[0] || cpu->r[15]!=exit) && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=code_ram?g->memory.iwram+(cpu->r[15]&0x7fff):rom+(cpu->r[15]&0x1ffffff);
                    cpu->pipeline[0]=thumb?gba_le16(p):gba_le32(p);
                    cpu->pipeline[1]=thumb?gba_le16(p+width):gba_le32(p+width);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+width;
                }
                unsigned before_cycles=g->machine.cycles+g->cpu_pending;
                unsigned budget=variant>=64?((variant&3)*32+97):1024;
                uint32_t done=gba_dynarec_hot(g,budget);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,budget);
                if(!done)gba_cpu_execute(g);
                if(done)require(g->machine.cycles+g->cpu_pending-before_cycles<=(budget<GBA_DYNAREC_EVENT_SLIP?GBA_DYNAREC_EVENT_SLIP:budget),0xd5100+variant);
            }
            require(limit && !g->fault,0xd1000+variant*2+i);
        }
        for(unsigned r=0;r<15;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xd2000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xd3000+variant);
        require(!memcmp(machine[0].memory.ewram,machine[1].memory.ewram,0x40000) &&
            !memcmp(machine[0].memory.iwram,machine[1].memory.iwram,0x8000),0xd4000+variant);
        uint32_t links=(start>>24)==3?gba_native_cache(&machine[1])->direct_links:gba_native_cache(&machine[1])->coarse_rom_links;
        require(links!=0,0xd5000+variant);total_links+=links;
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    for(unsigned variant=0;variant<32;variant++) {
        uint32_t start=0x08004000,thumb_pc=variant&16?0x03004140:start+0x140,exit=thumb_pc+8;
        setup(0,0xe2822001,GBA_C|GBA_V);
        const uint32_t arm[]={0xe2822001,0xe5941000,0xe5852000,0xe59f6000,0xe12fff16,0x08004141,0xeafffffe};
        const uint16_t thumb[]={0x3301,0x3801,0x4e03,0xd101,0xe7fe,0xe7fe,0x3701,0x4730,0xe7fe,0xe7fe};
        for(unsigned n=0;n<sizeof(arm)/sizeof(arm[0]);n++)gba_put32(rom+0x4000+n*4,arm[n]);
        gba_put32(rom+0x4014,thumb_pc|1);
        for(unsigned n=0;n<sizeof(thumb)/sizeof(thumb[0]);n++)gba_put16(rom+0x4140+n*2,thumb[n]);
        if(GBA_DYNAREC_TINY_BLOCKS)gba_put16(rom+0x4140+12,0x4730);
        gba_put32(rom+0x4140+20,start);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            if(variant&16)memcpy(g->memory.iwram+0x4140,rom+0x4140,24);
            cpu->r[0]=19+variant;cpu->r[1]=cpu->r[2]=cpu->r[3]=0;
            cpu->r[4]=variant&1?0x03001000:0x02001000;cpu->r[5]=variant&2?0x03002000:0x02002000;
            gba_put32(g->memory.ewram+0x1000,0x12345678);gba_put32(g->memory.iwram+0x1000,0x12345678);
            cpu->r[15]=start;cpu->pipeline_valid=0;
            gba_io_write(g,0x204,variant&4?0x4000:0,2);
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        lane=1;
        const uint32_t entries[]={start,thumb_pc,thumb_pc+12};
        for(unsigned n=0;n<3;n++) {
            gba_t *g=&machine[1];gba_cpu_t *cpu=&g->machine.cpu;unsigned width=n?2:4;
            cpu->cpsr=(cpu->cpsr&~GBA_T)|(n?GBA_T:0);cpu->r[15]=entries[n];
            const uint8_t *p=(entries[n]>>24)==3?g->memory.iwram+(entries[n]&0x7fff):rom+(entries[n]&0x1ffffff);
            cpu->pipeline[0]=width==2?gba_le16(p):gba_le32(p);cpu->pipeline[1]=width==2?gba_le16(p+width):gba_le32(p+width);
            cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=entries[n]+width;
            require(gba_dynarec_block(g,1024)!=0,0xd6000+variant*4+n);
        }
        machine[1].machine=machine[0].machine;memcpy(machine[1].io,machine[0].io,sizeof(machine[0].io));
        memcpy(machine[1].memory.ewram,machine[0].memory.ewram,0x40000);memcpy(machine[1].memory.iwram,machine[0].memory.iwram,0x8000);
        machine[1].cpu_pending=0;machine[1].event_cycles=UINT32_MAX;
        for(unsigned i=0;i<2;i++) {
            lane=i;unsigned limit=100000;gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            while((cpu->r[0] || cpu->r[15]!=exit) && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                unsigned width=(cpu->cpsr&GBA_T)?2:4;
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=(cpu->r[15]>>24)==3?g->memory.iwram+(cpu->r[15]&0x7fff):rom+(cpu->r[15]&0x1ffffff);
                    cpu->pipeline[0]=width==2?gba_le16(p):gba_le32(p);cpu->pipeline[1]=width==2?gba_le16(p+width):gba_le32(p+width);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+width;
                }
                uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(!done)gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xd7000+variant*2+i);
        }
        for(unsigned r=0;r<15;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xd8000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xd9000+variant);
        require(!memcmp(machine[0].memory.ewram,machine[1].memory.ewram,0x40000) &&
            !memcmp(machine[0].memory.iwram,machine[1].memory.iwram,0x8000),0xda000+variant);
        uint32_t links=gba_native_cache(&machine[1])->coarse_rom_links;
        require(links!=0,0xdb000+variant);total_links+=links;
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("COARSE native links=");hex(total_links);puts_uart("; 128 finite scalar cases, IWRAM/ROM data and ARM/Thumb exchange, PASS\n");
}
#endif
#if GBA_DYNAREC_BL_PREFIX
static void bl_pair_proof(void) {
    for(unsigned variant=0;variant<32;variant++) {
        unsigned region=variant&1?3:8;
        uint32_t start=(region<<24)+0x3000,callee=variant&2?start-0x100:start+0x100,exit=start+10;
        int delta=(int)callee-(int)(start+6);
        uint16_t high=0xf000|(((unsigned)delta>>12)&2047),low=0xf800|(((unsigned)delta>>1)&2047);
        setup(1,0x3201,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];uint8_t *p=region==3?g->memory.iwram:rom;
            unsigned at=start&0x7fff,to=callee&0x7fff;
            const uint16_t ops[]={0x3201,high,low,0x3801,0xd1fa,0xe7fe};
            for(unsigned n=0;n<6;n++)gba_put16(p+at+n*2,ops[n]);
            gba_put16(p+to,0x3301);gba_put16(p+to+2,0x4770);gba_put16(p+to+4,0xe7fe);
            g->machine.cpu.r[0]=19+variant;g->machine.cpu.r[2]=g->machine.cpu.r[3]=0;
            g->machine.cpu.r[15]=start;g->machine.cpu.pipeline_valid=0;
            gba_io_write(g,0x204,variant&4?0x4000:0,2);
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;unsigned limit=100000;gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            while((cpu->r[0] || cpu->r[15]!=exit) && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                if(GBA_DYNAREC_BULK_PREFILL){
                    unsigned before=g->machine.cycles+g->cpu_pending;
                    unsigned actual=gba_cpu_run(g,1+variant%23);
                    if(g->machine.cycles+g->cpu_pending-before>GBA_DYNAREC_EVENT_SLIP+1+variant%23) {
                        puts_uart("BULK bound cycles=");hex(g->machine.cycles+g->cpu_pending-before);
                        puts_uart(" returned=");hex(actual);puts_uart(" pc=");hex(cpu->r[15]);puts_uart("\n");
                    }
                    require(g->machine.cycles+g->cpu_pending-before<=GBA_DYNAREC_EVENT_SLIP+1+variant%23,0xdf200+variant);
                    continue;
                }
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=region==3?g->memory.iwram+(cpu->r[15]&0x7fff):rom+(cpu->r[15]&0x1ffffff);
                    cpu->pipeline[0]=gba_le16(p);cpu->pipeline[1]=gba_le16(p+2);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+2;
                }
                uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(!done)gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xdd000+variant*2+i);
        }
        for(unsigned r=0;r<15;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xde000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xdf000+variant);
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("Thumb BL positive/negative calls and BX LR returns: 32 finite scalar cases PASS\n");
#ifdef GBA_TEST_BULK_PREFILL_PROOF
    require(gba_test_bulk_prefill_entries!=0,0xdf100);
    puts_uart("Bulk pipeline prefill actual entries=");hex(gba_test_bulk_prefill_entries);puts_uart("\n");
#endif
}
#endif
#if GBA_DYNAREC_LINK_CACHE && GBA_DYNAREC_EVENT_SLIP<=256
static void link_slot_proof(void) {
    setup(0,0xe0000190,GBA_C|GBA_V);
    gba_t *g=&machine[1];gba_cpu_t *cpu=&g->machine.cpu;
    for(unsigned n=0;n<64;n++)gba_put32(g->memory.iwram+0x5000+n*4,n==63?0xeaffffbfu:0xe0000190u);
    cpu->r[0]=0x12345678;cpu->r[1]=0x87654321;cpu->r[15]=0x03005000;
    cpu->pipeline[0]=cpu->pipeline[1]=0xe0000190;
    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+4;
    g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
    require(gba_dynarec_block(g,100000)!=0,0xdf300);
    gba_native_block_t *b=gba_native_cache(g)->recent[gba_native_recent_index(0x03005000)];
    require(b && b->count+b->extra_bound>GBA_DYNAREC_EVENT_SLIP && !b->prefix_offset && !b->prefix_count,0xdf301);
    cpu->r[15]=0x03005000;cpu->pipeline[0]=cpu->pipeline[1]=0xe0000190;
    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+4;
    g->cpu_pending=0;g->event_cycles=UINT32_MAX;
    unsigned result=gba_dynarec_block(g,17);
    require(!result && !b->prefix_offset && !b->prefix_count,0xdf302);
    g->stepping=g->cpu_bulk=0;g->cpu_pending=g->event_cycles=0;
    puts_uart("Native successor slot never aliases a bounded RAM prefix PASS\n");
}
#endif
#if GBA_DYNAREC_LINE_INVALIDATE
static void line_generation_proof(void) {
    setup(0,0xe2800001,0);
    gba_t *g=&machine[1];gba_native_cache_t *cache=gba_native_cache(g);
    for(unsigned variant=0;variant<1024;variant++) {
        unsigned region=variant&1?3:2,mask=region==3?0x7fff:0x3ffff;
        unsigned address=(region<<24)|(random32()&0xffffff),size=1+variant%80;
        uint8_t touched[GBA_JIT_PAGES]={0};uint32_t before[GBA_JIT_PAGES];
        for(unsigned n=0;n<GBA_JIT_PAGES;n++) {
            cache->code_page[n]=(uint8_t)random32();before[n]=cache->generation[n];
        }
        for(unsigned n=0;n<size;n++) {
            unsigned a=address+n,page=(region==3?1024:0)+((a&mask)>>8);
            if(cache->code_page[page]&(1u<<((a>>5)&7)))touched[page]=1;
        }
        gba_dynarec_invalidate(g,address,size);
        for(unsigned n=0;n<GBA_JIT_PAGES;n++)require(cache->generation[n]==before[n]+touched[n],0xe0000+variant);
    }
    gba_dynarec_invalidate(g,0,SIZE_MAX);
    puts_uart("Code-line generation: 1024 dirty spans and all 1152 pages per span PASS\n");
}
#endif
#if GBA_DYNAREC_ROM_TRACE
static void rom_trace_proof(void) {
    for(unsigned variant=0;variant<16;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4;uint32_t start=0x080060f0,exit=start+97*width;
        setup(thumb,thumb?0x3201:0xe2822001,GBA_C|GBA_V);
        for(unsigned n=0;n<100;n++) {
            unsigned r=2+n%3;
            unsigned op=thumb?(0x3001|(r<<8)):(0xe2800001|(r<<16)|(r<<12));
            if(n==95)op=thumb?0x3801:0xe2500001;
            if(n==96)op=thumb?(0xd100|((-(int)98)&255)):(0x1a000000|((-(int)98)&0xffffff));
            if(n>=97)op=thumb?0xe7fe:0xeafffffe;
            if(thumb)gba_put16(rom+0x60f0+n*width,op);else gba_put32(rom+0x60f0+n*width,op);
        }
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[0]=3+variant;g->machine.cpu.r[2]=g->machine.cpu.r[3]=g->machine.cpu.r[4]=0;
            g->machine.cpu.r[15]=start;g->machine.cpu.pipeline_valid=0;
            gba_io_write(g,0x204,variant&2?0x4000:0,2);
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;unsigned limit=100000;
            while((cpu->r[0] || cpu->r[15]!=exit) && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=rom+(cpu->r[15]&0x1ffffff);
                    cpu->pipeline[0]=thumb?gba_le16(p):gba_le32(p);cpu->pipeline[1]=thumb?gba_le16(p+width):gba_le32(p+width);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+width;
                }
                unsigned before=g->machine.cycles+g->cpu_pending;uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(done)require(g->machine.cycles+g->cpu_pending-before<=1024,0xe1000+variant);
                else gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xe2000+variant*2+i);
        }
        for(unsigned r=0;r<15;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xe3000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xe4000+variant);
        gba_native_block_t *b=gba_native_cache(&machine[1])->recent[gba_native_recent_index(start)];
        require(b && b->pc==start && b->count>64 && (b->pc>>8)!=((b->pc+b->count*width)>>8),0xe5000+variant);
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("ROM multi-page native spans: 16 finite scalar computation and budget cases PASS\n");
}
#endif
#if GBA_DYNAREC_VIDEO_STORE
static void video_store_proof(void) {
    static const unsigned offsets[]={0,1,2,3,0x3fc,0x10000,0x14000,0x18000,0x1fffc,0xfffffc};
    for(unsigned variant=0;variant<1440;variant++) {
        unsigned n=variant,thumb=n%2;n/=2;unsigned bytes=1u<<(n%3);n/=3;
        unsigned region=5+n%3;n/=3;unsigned mode=n%2?3:0;n/=2;
        unsigned offset=offsets[n%10];n/=10;
        unsigned op=thumb?(bytes==1?0x7008:bytes==2?0x8008:0x6008):
            (bytes==1?0xe5c10000u:bytes==2?0xe1c100b0u:0xe5810000u);
        setup(thumb,op,GBA_N|GBA_C|GBA_V);
        uint32_t start=0x08006000,exit=0x08006020,value=n&1?0x6d6d6d6d:0x82f06e13;
        if(thumb) {gba_put16(rom+0x6000,op);gba_put16(rom+0x6002,0x3201);gba_put16(rom+0x6004,0x4718);}
        else {gba_put32(rom+0x6000,op);gba_put32(rom+0x6004,0xe2822001);gba_put32(rom+0x6008,0xe12fff13);}
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            gba_memset(g->memory.palette,0x6d,1024);gba_memset(g->memory.vram,0x6d,0x18000);gba_memset(g->memory.oam,0x6d,1024);
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
            gba_memset(&g->video_cache,0,sizeof(g->video_cache));
            for(unsigned b=0;b<sizeof(g->video_cache.dependency)/sizeof(g->video_cache.dependency[0]);b++)
                for(unsigned row=0;row<5;row++)g->video_cache.dependency[b][row]=0x01010101u<<row;
#endif
            gba_put16(g->io,(uint16_t)mode);
            g->machine.ppu.oam_latch_valid=n>>1;
            cpu->r[0]=value;cpu->r[1]=(region<<24)|offset;cpu->r[2]=0;cpu->r[3]=exit|(thumb?1:0);cpu->r[15]=start;
            cpu->pipeline[0]=op;cpu->pipeline[1]=thumb?0x3201:0xe2822001;
            cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=start+(thumb?2:4);
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;gba_t *g=&machine[i];unsigned limit=20;
            while(g->machine.cpu.r[15]!=exit && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                unsigned before=g->machine.cycles+g->cpu_pending;uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(done)require(g->machine.cycles+g->cpu_pending-before<=1024,0xe6000+variant);
                else gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xe7000+variant*2+i);
        }
        for(unsigned r=0;r<16;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xe8000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xeb000+variant);
        require(!memcmp(machine[0].memory.palette,machine[1].memory.palette,1024) &&
                !memcmp(machine[0].memory.vram,machine[1].memory.vram,0x18000) &&
                !memcmp(machine[0].memory.oam,machine[1].memory.oam,1024),0xec000+variant);
#if GBA_VIDEO_CACHE && !GBA_TEST_MODE && !GBA_RAM_LACK
        require(!memcmp(machine[0].video_cache.dirty,machine[1].video_cache.dirty,sizeof(machine[0].video_cache.dirty)) &&
                !memcmp(machine[0].video_cache.pending_oam,machine[1].video_cache.pending_oam,sizeof(machine[0].video_cache.pending_oam)),0xed000+variant);
#endif
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
#ifdef GBA_TEST_VIDEO_STORE_PROOF
    require(gba_test_video_store_entries>100,0xee000);
#endif
    puts_uart("Video stores: 1440 ARM/Thumb cases, byte quirks, mirrors, alignment and latched/live dirty dependencies PASS\n");
}
#endif
#ifdef GBA_TEST_DMA_HARD_PROOF
static void dma_hard_proof(void) {
    setup(0,0xe1a00000,0);
    gba_t *probe=&machine[1];
    gba_memset(probe->machine.dma,0,sizeof(probe->machine.dma));
    static const unsigned phases[]={0,GBA_HBLANK_START-1,GBA_HBLANK_START,GBA_CYCLES_PER_LINE-1};
    static const unsigned pending[]={0,1,1000,2048};
    for(unsigned line=0;line<228;line++)for(unsigned p=0;p<4;p++)for(unsigned timing=1;timing<=2;timing++) {
        probe->machine.ppu.line=line;probe->machine.ppu.phase=phases[p];
        uint32_t expected=UINT32_MAX;
        /* Independent event walk, including the inactive-line/frame wrap. */
        for(unsigned ahead=0;ahead<=228;ahead++) {
            unsigned future=(line+ahead)%228;
            int32_t at=(int32_t)(ahead*GBA_CYCLES_PER_LINE+(timing==2?GBA_HBLANK_START:0))-(int32_t)phases[p];
            if(at>0 && ((timing==1 && future==160)||(timing==2 && future<160))) {expected=(uint32_t)at;break;}
        }
        probe->machine.dma[3].control=0x8000|(timing<<12);
#if GBA_DYNAREC_HARD_CACHE
        gba_native_cache(probe)->hard_valid=0;
#endif
        for(unsigned n=0;n<4;n++) {
            probe->cpu_pending=pending[n];
            require(gba_native_hard_distance(probe)==(expected>pending[n]?expected-pending[n]:0),0xee100+line);
        }
    }
    probe->cpu_pending=0;
    puts_uart("DMA deadline distances: all 228 lines, edge phases and frame wrap PASS\n");
    for(unsigned variant=0;variant<96;variant++) {
        unsigned n=variant,thumb=n%2;n/=2;unsigned timing=n%3;n/=3;
        unsigned region=2+n%2;n/=2;unsigned count=1+n%8;
        uint32_t start=0x08006000,exit=0x08006020,destination=(region<<24)|0x6000;
        setup(thumb,thumb?0x6808:0xe5910000,GBA_C|GBA_V);
        /* Two ordinary instructions allow even a newly triggered blanking DMA
         * to finish startup before the load. The reference's first atomic
         * instruction is allowed to straddle the trigger itself. */
        if(thumb){gba_put16(rom+0x6000,0x0012);gba_put16(rom+0x6002,0x0012);gba_put16(rom+0x6004,0x6808);gba_put16(rom+0x6006,0x1c46);gba_put16(rom+0x6008,0x4738);gba_put16(rom+0x6020,0xe7fe);}
        else {gba_put32(rom+0x6000,0xe1a02002);gba_put32(rom+0x6004,0xe1a02002);gba_put32(rom+0x6008,0xe5910000);gba_put32(rom+0x600c,0xe2806001);gba_put32(rom+0x6010,0xe12fff17);gba_put32(rom+0x6020,0xeafffffe);}
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            for(unsigned j=0;j<8;j++)gba_put32(g->memory.iwram+0x5000+j*4,0x12345670u+variant+j);
            gba_memset((region==2?g->memory.ewram:g->memory.iwram)+0x6000,0xd5,32);
            g->machine.ppu.line=timing==1?159:0;
            g->machine.ppu.phase=(timing==1?GBA_CYCLES_PER_LINE:GBA_HBLANK_START)-2;
            gba_io_write(g,0xd4,0x03005000,4);gba_io_write(g,0xd8,destination,4);
            gba_io_write(g,0xdc,count,2);gba_io_write(g,0xde,0x8400|(timing<<12),2);
            cpu->r[0]=0;cpu->r[1]=destination;cpu->r[6]=0;cpu->r[7]=exit|(thumb?1:0);cpu->r[15]=start;
            cpu->pipeline[0]=cpu->pipeline[1]=thumb?0x0012:0xe1a02002;
            cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=start+(thumb?2:4);
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;gba_t *g=&machine[i];unsigned limit=100;
            while(g->machine.cpu.r[15]!=exit && limit--) {
                if(i)gba_run_cycles(g,1);else gba_step(g);
            }
            require(limit && !g->fault,0xef000+variant*2+i);
            if(g->machine.cpu.r[0]!=0x12345670u+variant || g->machine.cpu.r[6]!=0x12345671u+variant) {
                puts_uart("DMA case=");hex(variant);puts_uart(" lane=");hex(i);
                puts_uart(" r0=");hex(g->machine.cpu.r[0]);puts_uart(" r6=");hex(g->machine.cpu.r[6]);
                puts_uart(" control=");hex(g->machine.dma[3].control);puts_uart(" startup=");hex(g->machine.dma[3].startup);
                puts_uart(" remaining=");hex(g->machine.dma[3].remaining);puts_uart(" line=");hex(g->machine.ppu.line);
                puts_uart(" phase=");hex(g->machine.ppu.phase);puts_uart("\n");
            }
            require(g->machine.cpu.r[0]==0x12345670u+variant && g->machine.cpu.r[6]==0x12345671u+variant,0xef200+variant*2+i);
        }
        const uint8_t *a=(region==2?machine[0].memory.ewram:machine[0].memory.iwram)+0x6000;
        const uint8_t *b=(region==2?machine[1].memory.ewram:machine[1].memory.iwram)+0x6000;
        require(!memcmp(a,b,32),0xef400+variant);
    }
    puts_uart("DMA hard boundaries: 96 immediate/HBlank/VBlank memory-copy-before-load cases PASS\n");
}
#endif
#if GBA_DYNAREC_TRACE_LINKS
static uint32_t trace_branch(unsigned thumb,uint32_t at,uint32_t target,unsigned link,unsigned conditional) {
    if(thumb)return conditional?0xd000u|(((target-at-4)/2)&255u):0xe000u|(((target-at-4)/2)&2047u);
    return (conditional?0x0a000000u:link?0xeb000000u:0xea000000u)|(((target-at-8)/4)&0xffffffu);
}
static void linked_trace_proof(void) {
    for(unsigned variant=0;variant<64;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4;uint32_t start=0x08006000,exit=0x08006180;
        setup(thumb,thumb?0x3201:0xe2822001,GBA_C|GBA_V|((variant&2)?GBA_Z:0));
        uint32_t pcs[]={start,start+width,0x08006100,0x08006100+width,0x08006100+2*width,0x08006100+3*width,
            0x08006080,0x08006080+width,0x08006080+2*width,exit};
        uint32_t ops[]={thumb?0x3201:0xe2822001,trace_branch(thumb,start+width,0x08006100,variant&4,0),
            thumb?0xa404:0xe1a0400f,trace_branch(thumb,0x08006100+width,exit,0,1),thumb?0x3302:0xe2833002,
            trace_branch(thumb,0x08006100+3*width,0x08006080,0,0),thumb?0x600a:0xe5812000,
            thumb?0x3603:0xe2866003,trace_branch(thumb,0x08006080+2*width,exit,0,0),thumb?0xe7fe:0xeafffffe};
        for(unsigned n=0;n<sizeof(pcs)/sizeof(pcs[0]);n++) {
            if(thumb)gba_put16(rom+(pcs[n]&0x1ffffff),ops[n]);else gba_put32(rom+(pcs[n]&0x1ffffff),ops[n]);
        }
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            cpu->r[1]=0x02001000+((variant&8)?1:0);cpu->r[2]=thumb && (variant&2)?UINT32_MAX:0;
            cpu->r[3]=0;cpu->r[4]=0;cpu->r[6]=0;cpu->r[14]=0x12345678;
            cpu->r[15]=start;cpu->pipeline[0]=ops[0];cpu->pipeline[1]=ops[1];
            cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=start+width;
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
            gba_memset(g->memory.ewram+0x1000,0x5a,16);
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;gba_t *g=&machine[i];unsigned limit=1000;
            while(g->machine.cpu.r[15]!=exit && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                gba_cpu_t *cpu=&g->machine.cpu;
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=rom+(cpu->r[15]&0x1ffffff);
                    cpu->pipeline[0]=thumb?gba_le16(p):gba_le32(p);cpu->pipeline[1]=thumb?gba_le16(p+width):gba_le32(p+width);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+width;
                }
                unsigned before=g->machine.cycles+g->cpu_pending;uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(done)require(g->machine.cycles+g->cpu_pending-before<=1024,0xf0000+variant);
                else gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xf0100+variant*2+i);
        }
        for(unsigned r=0;r<16;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xf1000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xf2000+variant);
        require(!memcmp(machine[0].memory.ewram+0x1000,machine[1].memory.ewram+0x1000,16),0xf3000+variant);
        gba_native_cache_t *cache=gba_native_cache(&machine[1]);gba_native_block_t *b=NULL;
        for(unsigned n=0;n<cache->slots;n++) {
            gba_native_block_t *entry=gba_native_blocks(cache)+n;
            if(entry->epoch==cache->epoch && entry->pc==start && entry->count){b=entry;break;}
        }
        require(b && b->pc==start && b->count>4 && gba_native_trace_pc(&machine[1],b,2)==0x08006100,0xf4000+variant);
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("Linked ROM traces: 64 ARM/Thumb B/BL, PC reads, conditional exits, memory guards and budget cases PASS\n");
}
static void linked_thumb_call_proof(void) {
    for(unsigned variant=0;variant<32;variant++) {
        uint32_t start=variant&4?0x080064fe:0x08006400,target=variant&2?0x08006200:0x08006600,exit=0x08006700;
        int32_t delta=(int32_t)(target-start-4);
        unsigned high=0xf000u|(((uint32_t)delta>>12)&2047),low=0xf800u|(((uint32_t)delta>>1)&2047);
        setup(1,high,GBA_C|GBA_V);
        gba_put16(rom+(start&0x1ffffff),high);gba_put16(rom+((start+2)&0x1ffffff),low);
        gba_put16(rom+((start+4)&0x1ffffff),0x3301);gba_put16(rom+((start+6)&0x1ffffff),0x4720);
        gba_put16(rom+(target&0x1ffffff),0x3201);
        uint32_t tail=target+2;
        if(variant&16) {
            uint32_t call=target+4,sub=0x08006a00;int32_t offset=(int32_t)(sub-call-4);
            gba_put16(rom+((target+2)&0x1ffffff),0x4676); /* MOV r6,lr */
            gba_put16(rom+(call&0x1ffffff),0xf000u|(((uint32_t)offset>>12)&2047));
            gba_put16(rom+((call+2)&0x1ffffff),0xf800u|(((uint32_t)offset>>1)&2047));
            gba_put16(rom+((target+8)&0x1ffffff),0x46b6); /* MOV lr,r6 */
            gba_put16(rom+(sub&0x1ffffff),0x3301);gba_put16(rom+((sub+2)&0x1ffffff),0x4770);
            tail=target+10;
        }
        if(variant&8){gba_put16(rom+(tail&0x1ffffff),0x46ae);tail+=2;} /* MOV lr,r5: guard mismatch */
        gba_put16(rom+(tail&0x1ffffff),0x4770);gba_put16(rom+(exit&0x1ffffff),0xe7fe);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            cpu->r[2]=variant;cpu->r[3]=0;cpu->r[4]=exit|1;cpu->r[5]=exit|1;cpu->r[6]=0;cpu->r[14]=0xdeadbeef;cpu->r[15]=start;
            cpu->pipeline[0]=high;cpu->pipeline[1]=low;cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=start+2;
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;gba_t *g=&machine[i];unsigned limit=1000;
            while(g->machine.cpu.r[15]!=exit && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                gba_cpu_t *cpu=&g->machine.cpu;
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=rom+(cpu->r[15]&0x1ffffff);cpu->pipeline[0]=gba_le16(p);cpu->pipeline[1]=gba_le16(p+2);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+2;
                }
                unsigned before=g->machine.cycles+g->cpu_pending;uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(done)require(g->machine.cycles+g->cpu_pending-before<=1024,0xf5000+variant);
                else gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xf5100+variant*2+i);
        }
        for(unsigned r=0;r<16;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xf6000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xf7000+variant);
        gba_native_cache_t *cache=gba_native_cache(&machine[1]);gba_native_block_t *b=NULL;
        for(unsigned n=0;n<cache->slots;n++) {
            gba_native_block_t *entry=gba_native_blocks(cache)+n;
            if(entry->epoch==cache->epoch && entry->pc==start && entry->count){b=entry;break;}
        }
        require(b && b->count>=4 && gba_native_trace_pc(&machine[1],b,2)==target,0xf8000+variant);
        unsigned returned=0;
        for(unsigned n=0;b && n<=b->count;n++)returned|=gba_native_trace_pc(&machine[1],b,n)==start+4;
        require(returned,0xf8100+variant);
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("Linked Thumb calls/returns: 32 signed/page-boundary/nested/LR-mismatch cases PASS\n");
}
static void linked_arm_call_proof(void) {
    for(unsigned variant=0;variant<16;variant++) {
        uint32_t start=variant&1?0x080064fc:0x08006400,target=variant&2?0x08006200:0x08006600,exit=0x08006700;
        uint32_t call=trace_branch(0,start,target,1,0);
        setup(0,call,GBA_N|GBA_C|GBA_V);
        gba_put32(rom+(start&0x1ffffff),call);gba_put32(rom+((start+4)&0x1ffffff),0xe2833001);gba_put32(rom+((start+8)&0x1ffffff),0xe12fff14);
        gba_put32(rom+(target&0x1ffffff),0xe2822001);uint32_t tail=target+4;
        if(variant&8) {
            uint32_t sub=0x08006a00;
            gba_put32(rom+((target+4)&0x1ffffff),0xe1a0600e);
            gba_put32(rom+((target+8)&0x1ffffff),trace_branch(0,target+8,sub,1,0));
            gba_put32(rom+((target+12)&0x1ffffff),0xe1a0e006);
            gba_put32(rom+(sub&0x1ffffff),0xe2833001);gba_put32(rom+((sub+4)&0x1ffffff),0xe12fff1e);
            tail=target+16;
        }
        if(variant&4){gba_put32(rom+(tail&0x1ffffff),0xe1a0e005);tail+=4;}
        gba_put32(rom+(tail&0x1ffffff),0xe12fff1e);gba_put32(rom+(exit&0x1ffffff),0xeafffffe);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            cpu->r[2]=variant;cpu->r[3]=0;cpu->r[4]=exit;cpu->r[5]=exit;cpu->r[6]=0;cpu->r[14]=0xdeadbeef;cpu->r[15]=start;
            cpu->pipeline[0]=call;cpu->pipeline[1]=0xe2833001;cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=start+4;
            g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        }
        for(unsigned i=0;i<2;i++) {
            lane=i;gba_t *g=&machine[i];unsigned limit=1000;
            while(g->machine.cpu.r[15]!=exit && limit--) {
                if(!i){gba_cpu_execute(g);continue;}
                gba_cpu_t *cpu=&g->machine.cpu;
                if(!cpu->pipeline_valid) {
                    const uint8_t *p=rom+(cpu->r[15]&0x1ffffff);cpu->pipeline[0]=gba_le32(p);cpu->pipeline[1]=gba_le32(p+4);
                    cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+4;
                }
                unsigned before=g->machine.cycles+g->cpu_pending;uint32_t done=gba_dynarec_hot(g,1024);
                if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                if(done)require(g->machine.cycles+g->cpu_pending-before<=1024,0xf9000+variant);
                else gba_cpu_execute(g);
            }
            require(limit && !g->fault,0xf9100+variant*2+i);
        }
        for(unsigned r=0;r<16;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xfa000+variant*16+r);
        require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xfb000+variant);
        gba_native_cache_t *cache=gba_native_cache(&machine[1]);gba_native_block_t *b=NULL;
        for(unsigned n=0;n<cache->slots;n++) {
            gba_native_block_t *entry=gba_native_blocks(cache)+n;
            if(entry->epoch==cache->epoch && entry->pc==start && entry->count){b=entry;break;}
        }
        unsigned returned=0;
        for(unsigned n=0;b && n<=b->count;n++)returned|=gba_native_trace_pc(&machine[1],b,n)==start+4;
        require(b && b->count>3 && gba_native_trace_pc(&machine[1],b,1)==target && returned,0xfc000+variant);
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;}
    }
    puts_uart("Linked ARM calls/returns: 16 signed/page-boundary/nested/LR-mismatch cases PASS\n");
}
#endif
#ifdef GBA_TEST_ROM_EDGE_PROOF
static void rom_edge_proof(void) {
    static const unsigned regions[]={8,9,10,12};
    for(unsigned i=0;i<2;i++){gba_unload_rom_discard(&machine[i]);require(!load_image(i,game_rom,sizeof(game_rom)),0xfd000+i);}
    for(unsigned variant=0;variant<8;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4,region=regions[variant/2];
        uint32_t pc=(region<<24)|0xffffc0u,op=thumb?0x3001:0xe2800001;
        for(unsigned at=0;at<512;at+=width) {
            if(thumb){gba_put16(game_rom+at,op);gba_put16(game_rom+0xffff00+at,op);gba_put16(game_rom+0x1fffe00+at,op);}
            else {gba_put32(game_rom+at,op);gba_put32(game_rom+0xffff00+at,op);gba_put32(game_rom+0x1fffe00+at,op);}
        }
        setup(thumb,op,GBA_C|GBA_V);
        gba_t *g=&machine[1];gba_cpu_t *cpu=&g->machine.cpu;
        cpu->r[15]=pc;cpu->r[0]=0;cpu->pipeline[0]=cpu->pipeline[1]=op;cpu->pipeline_valid=cpu->fetch_sequential=1;
        g->machine.last_fetch=pc+width;g->stepping=g->cpu_bulk=1;g->event_cycles=UINT32_MAX;
        require(gba_dynarec_block(g,1024)!=0,0xfd010+variant);
        gba_native_block_t *b=gba_native_cache(g)->recent[gba_native_recent_index(pc)];
        require(b && b->pc==pc && b->count && (b->count+2)*width<=64,0xfd020+variant);
        g->stepping=g->cpu_bulk=0;g->cpu_pending=g->event_cycles=0;
    }
    for(unsigned i=0;i<2;i++){gba_unload_rom_discard(&machine[i]);require(!load_image(i,rom,sizeof(rom)),0xfd030+i);}
    puts_uart("Native ROM edges: 32MiB, wait-region and EEPROM boundaries retain two fetches PASS\n");
}
#endif
#if GBA_DYNAREC_TRACE_CONTEXT
static void trace_context_proof(void) {
    static const unsigned registers[]={3,7,14},targets[]={0x08006200,0x08006400,0x08006200,0x08006600,0x08006400,0x08006200};
    for(unsigned variant=0;variant<6;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4,reg=registers[variant/2];
        uint32_t pc=0x08006000,op=thumb?0x4700u|(reg<<3):0xe12fff10u|reg;
        setup(thumb,op,GBA_C|GBA_V);
        if(thumb)gba_put16(rom+0x6000,op);else gba_put32(rom+0x6000,op);
        for(unsigned n=0;n<3;n++) {
            uint32_t target=0x08006200+n*0x200,add=thumb?0x3201u+n:0xe2822001u+n;
            if(thumb){gba_put16(rom+(target&0x1ffffff),add);gba_put16(rom+((target+2)&0x1ffffff),reg==14?trace_branch(1,target+2,0x08006800,0,0):0x4770);}
            else {gba_put32(rom+(target&0x1ffffff),add);gba_put32(rom+((target+4)&0x1ffffff),reg==14?trace_branch(0,target+4,0x08006800,0,0):0xe12fff1e);}
        }
        if(thumb){gba_put16(rom+0x6800,0xe7fe);gba_put16(rom+0x6820,0xe7fe);}
        else {gba_put32(rom+0x6800,0xeafffffe);gba_put32(rom+0x6820,0xeafffffe);}
        for(unsigned iteration=0;iteration<6;iteration++) {
            uint32_t target=targets[iteration],exit=0x08006800+(reg!=14 && (iteration&1)?32:0);
            for(unsigned i=0;i<2;i++) {
                gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
                gba_memset(cpu,0,sizeof(*cpu));cpu->cpsr=0x1f|(thumb?GBA_T:0)|GBA_C|GBA_V;
                cpu->r[14]=exit|thumb;cpu->r[reg]=target|thumb;cpu->r[15]=pc;
                cpu->pipeline[0]=op;cpu->pipeline[1]=thumb?gba_le16(rom+0x6002):gba_le32(rom+0x6004);
                cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=pc+width;
                g->stepping=g->cpu_bulk=1;g->cpu_pending=0;g->event_cycles=UINT32_MAX;
            }
            for(unsigned i=0;i<2;i++) {
                lane=i;gba_t *g=&machine[i];unsigned limit=1000;
                while(g->machine.cpu.r[15]!=exit && limit--) {
                    if(!i){gba_cpu_execute(g);continue;}
                    gba_cpu_t *cpu=&g->machine.cpu;
                    if(!cpu->pipeline_valid) {
                        const uint8_t *p=rom+(cpu->r[15]&0x1ffffff);
                        cpu->pipeline[0]=thumb?gba_le16(p):gba_le32(p);cpu->pipeline[1]=thumb?gba_le16(p+width):gba_le32(p+width);
                        cpu->pipeline_valid=cpu->fetch_sequential=1;g->machine.last_fetch=cpu->r[15]+width;
                    }
                    unsigned before=g->machine.cycles+g->cpu_pending;uint32_t done=gba_dynarec_hot(g,1024);
                    if(done==UINT32_MAX)done=0;else if(!done)done=gba_dynarec_block(g,1024);
                    if(done)require(g->machine.cycles+g->cpu_pending-before<=1024,0xfe000+variant*8+iteration);
                    else gba_cpu_execute(g);
                }
                require(limit && !g->fault,0xfe100+variant*16+iteration*2+i);
            }
            for(unsigned r=0;r<16;r++)require(machine[0].machine.cpu.r[r]==machine[1].machine.cpu.r[r],0xfe200+variant*128+iteration*16+r);
            require(machine[0].machine.cpu.cpsr==machine[1].machine.cpu.cpsr,0xfe600+variant*8+iteration);
            gba_native_cache_t *cache=gba_native_cache(&machine[1]);gba_native_block_t *b=cache->recent[gba_native_recent_index(pc)];
            require(b && b->pc==pc && b->count>3 && b->prefix_offset==(target|thumb) && gba_native_trace_pc(&machine[1],b,1)==target,0xfe700+variant*8+iteration);
        }
        /* Bypass the C cache-context checks deliberately: the generated guard
         * must still commit an ordinary BX and execute no predicted target. */
        gba_t *g=&machine[1];gba_cpu_t *cpu=&g->machine.cpu;gba_native_cache_t *cache=gba_native_cache(g);
        gba_native_block_t *b=cache->recent[gba_native_recent_index(pc)];
        uint32_t actual=0x08006402|(thumb?0:1);
        cpu->r[2]=0;cpu->r[reg]=actual;cpu->r[15]=pc;cpu->pipeline_valid=1;cpu->cpsr=0x1f|(thumb?GBA_T:0)|GBA_C|GBA_V;
        uintptr_t entry=(uintptr_t)g->dynarec_memory.code+b->offset;uint64_t returned;
#if GBA_DYNAREC_FASTCALL
        returned=gba_native_invoke(g,b->count|(1u<<8),entry);
#else
        returned=((uint64_t (*)(gba_t *,unsigned))entry)(g,b->count|(1u<<8));
#endif
        require(((returned>>32)&255)==1 && !cpu->pipeline_valid && cpu->r[2]==0 &&
            cpu->r[15]==(actual&~((actual&1)?1u:3u)) && !!(cpu->cpsr&GBA_T)==!!(actual&1),0xfe800+variant);
        uint32_t expected=b->prefix_offset;cpu->r[reg]=expected;cpu->r[15]=pc;cpu->pipeline[0]=op;
        cpu->pipeline[1]=thumb?gba_le16(rom+0x6002):gba_le32(rom+0x6004);cpu->pipeline_valid=cpu->fetch_sequential=1;
        cpu->cpsr=0x1f|(thumb?GBA_T:0);g->machine.last_fetch=pc+width;
        for(unsigned budget=1;budget<=17;budget+=16) {
#if GBA_DYNAREC_HARD_CACHE
            gba_native_cache(g)->hard_valid=0;
#endif
            g->cpu_pending=0;g->event_cycles=budget+1;g->machine.dma[0].startup=budget+1;
            require(!gba_dynarec_block(g,budget) && b->prefix_offset==expected,0xfe900+variant);
        }
        for(unsigned i=0;i<2;i++){machine[i].stepping=machine[i].cpu_bulk=0;machine[i].cpu_pending=machine[i].event_cycles=0;machine[i].machine.dma[0].startup=0;}
    }
    puts_uart("BX entry contexts: alternating targets/LR, raw mode-mismatch guards and budget-field isolation PASS\n");
}
#endif
#if GBA_DYNAREC_HARD_CACHE
static void hard_cache_proof(void) {
    setup(0,0xe2800001,0);gba_t *g=&machine[1];gba_native_cache_t *cache=gba_native_cache(g);
    gba_memset(g->machine.dma,0,sizeof(g->machine.dma));g->machine.cpu.irq_delay=0;
    require(gba_native_hard_distance(g)==UINT32_MAX && cache->hard_valid,0xff000);
    gba_io_write(g,0xd4,0x03005000,4);gba_io_write(g,0xd8,0x02006000,4);gba_io_write(g,0xdc,1,2);
    gba_io_write(g,0xde,0x8400,2);
    require(!cache->hard_valid,0xff001);
    require(gba_native_hard_distance(g)==g->machine.dma[3].startup && cache->hard_valid,0xff002);
    gba_io_write(g,0xde,0,2);require(!cache->hard_valid && gba_native_hard_distance(g)==UINT32_MAX,0xff003);
    g->machine.cpu.irq_delay=7;gba_cpu_sync(g);
    require(!cache->hard_valid && gba_native_hard_distance(g)==7,0xff004);
    g->cpu_pending=3;require(gba_native_hard_distance(g)==4,0xff005);
    g->cpu_pending=9;require(gba_native_hard_distance(g)==0,0xff006);g->cpu_pending=0;
    g->machine.cycles++;g->machine.cpu.irq_delay=3;
    require(gba_native_hard_distance(g)==3,0xff007);
    gba_dynarec_invalidate(g,0,SIZE_MAX);require(!cache->hard_valid,0xff008);
    gba_native_hard_distance(g);require(cache->hard_valid,0xff009);
    gba_reset(g);require(!cache->hard_valid,0xff00a);
    puts_uart("DMA deadline cache: IO arm/disarm, zero-cycle sync, pending cycles, clock change, reset and epoch invalidation PASS\n");
}
#endif
#include "../optimization/hle_irq.h"
int main(void) {
    for (unsigned i = 0; i < 2; i++) {
        gba_memory_t m;
        if (gba_memory_bind(&m, hardware[i], sizeof(hardware[i])) || gba_init_static(&machine[i], &m) ||
            load_image(i, rom, sizeof(rom)))
            finish(2);
    }
    gba_dynarec_memory_t memory = {native_code, sizeof(native_code), native_data, sizeof(native_data)};
    if (gba_dynarec_bind(&machine[1], &memory))
        finish(3);
#if GBA_DYNAREC_TURBO
    puts_uart("TURBO cache native_slots=");hex(gba_native_cache(&machine[1])->slots);
    puts_uart(" persistent_slots=");hex(gba_native_cache(&machine[1])->persistent_slots);puts_uart("\n");
#endif
    if(!GBA_DYNAREC_PERSISTENT_ENGINE && GBA_DYNAREC_DATA_SIZE>=256u*1024u)
        require(gba_native_cache(&machine[1])->slots>=4096,0x00f0);
    if(!GBA_DYNAREC_PERSISTENT_ENGINE && GBA_DYNAREC_DATA_SIZE>=512u*1024u)
        require(gba_native_cache(&machine[1])->slots==(GBA_DYNAREC_DATA_SIZE>=1024u*1024u?16384u:8192u),0x00f1);
#ifdef GBA_TEST_REPLICA_VALIDATION
    gba_dynarec_memory_t replica={other_code,sizeof(other_code),other_data,sizeof(other_data)};
    require(!gba_dynarec_bind(&machine[0],&replica),0x00f2);
    puts_uart("REPLICA RUNTIME ONLY: no interpreter accuracy claim\n");
#endif
#ifdef GBA_TEST_PAGE_INDEX
    extern unsigned gba_test_page_index(gba_t *g);
    unsigned index_result=gba_test_page_index(&machine[1]);
    puts_uart("Page-index lifecycle result=");hex(index_result);puts_uart("\n");
    require(index_result==0,0xf1000+index_result);
#endif
    puts_uart("ARM926 native execution start\n");
#if GBA_DYNAREC_PERSISTENT_HLE_IRQ && !defined(GBA_TEST_CANDIDATE_ONLY)
    hle_irq_admission_proof();
#endif
#if GBA_DYNAREC_TURBO_LIGHT && !defined(GBA_TEST_CANDIDATE_ONLY)
    light_long_multiply_proof();
#endif
#if GBA_DYNAREC_EVENT_SLIP && !defined(GBA_TEST_CANDIDATE_ONLY)
    light_control_proof();
#endif
#if GBA_DYNAREC_HARD_CACHE
    hard_cache_proof();
#endif
#ifdef GBA_TEST_ROM_EDGE_PROOF
    rom_edge_proof();
#endif
#if GBA_DYNAREC_TRACE_LINKS
    linked_trace_proof();
    linked_thumb_call_proof();
    linked_arm_call_proof();
#endif
#if GBA_DYNAREC_TRACE_CONTEXT
    trace_context_proof();
#endif
#ifdef GBA_TEST_DMA_HARD_PROOF
    dma_hard_proof();
#endif
#if GBA_DYNAREC_VIDEO_STORE
    video_store_proof();
#endif
#if GBA_DYNAREC_ROM_TRACE
    rom_trace_proof();
#endif
#if GBA_DYNAREC_LINE_INVALIDATE
    line_generation_proof();
#endif
#ifdef GBA_TEST_FAST_LOOP_PROOF
    fast_loop_proof();
#endif
#if GBA_DYNAREC_COARSE_CHAIN
    coarse_chain_proof();
#endif
#if GBA_DYNAREC_BL_PREFIX
    bl_pair_proof();
#endif
#if GBA_DYNAREC_LINK_CACHE && GBA_DYNAREC_EVENT_SLIP<=256
    link_slot_proof();
#endif
#if defined(GBA_QEMU_FIXTURES_ONLY)
#ifdef GBA_TEST_BRANCH_SUPERBLOCK_PROOF
    branch_superblock_proof();
#endif
#ifdef GBA_TEST_LITERAL_COMPARE
    literal_compare();
#endif
#if defined(GBA_TEST_PERSISTENT_PROOF)
    persistent_proof();states();
#endif
    fixtures();goto fixture_report;
#endif
#if GBA_DYNAREC_BL_PREFIX
    for(unsigned variant=0;variant<128;variant++) {
        static const unsigned halves[]={0,1,255,1023,1024,1792,2046,2047};
        setup(1,0x3201,GBA_C|GBA_V);
        unsigned at=variant&8?0xfe:0x1000;
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];uint8_t *p=variant&16?rom:g->memory.iwram;
            g->machine.cpu.r[15]=(variant&16?0x08000000:0x03000000)+at;
            gba_put16(p+at,0x3201);gba_put16(p+at+2,0xf000|halves[variant&7]);
            gba_put16(p+at+4,0x3301);gba_put16(p+at+6,0xe7fe);
            gba_io_write(g,0x204,variant&32?0x4000:0,2);
        }
        run(0xdc000+variant,1);run(0xdc000+variant,17);run(0xdc000+variant,301);
    }
    puts_uart("Thumb BL prefix boundary/sign/state matrix PASS\n");
#endif
    /* A register-shifted write to PC still has one internal cycle before
     * its real target refill. Ensure it is the tail of a native span. */
    for(unsigned variant=0;variant<16;variant++) {
        setup(0,0xe2844001,(variant&15)<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[0]=0x03000100;g->machine.cpu.r[2]=0;
            gba_put32(g->memory.iwram+4,0xe1a0f210);
            gba_put32(g->memory.iwram+0x100,0xe2844001);
            gba_put32(g->memory.iwram+0x104,0xeaffffbdu);
        }
        for(unsigned n=0;n<8;n++)run(0x0100+variant*16+n,31+n*37);
    }
    for(unsigned region=0;region<2;region++)for(unsigned condition=0;condition<15;condition++)
    for(unsigned flags=0;flags<16;flags++) {
        setup(0,0xe2844001,flags<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];unsigned offset=region?0x1800:0;
            uint8_t *code=region?rom:g->memory.iwram;
            uint32_t base=region?0x08000000:0x03000000;
            g->machine.cpu.r[0]=base+offset+0x100;g->machine.cpu.r[2]=0;
            g->machine.cpu.r[15]=base+offset;
            for(unsigned n=0;n<0x108;n+=4)gba_put32(code+offset+n,0xe2844001);
            gba_put32(code+offset+4,(condition<<28)|0x01a0f210u);
            gba_put32(code+offset+0x104,0xeaffffbd);
            gba_io_write(g,0x204,flags&1?0x4000:0,2);
        }
        for(unsigned n=0;n<4;n++)run(0x1000+region*0x1000+condition*64+flags*4+n,31+n*101);
    }
    for (unsigned i = 0; i < 512; i++) {
        unsigned kind = i & 15, rd = (i >> 4) % 15, rn = (i >> 1) % 15, rm = (i >> 3) % 15;
        uint32_t op = 0xe0000000u | (kind << 21) | ((i & 16) ? 0x100000 : 0) | (rn << 16) | (rd << 12) | rm;
        if (i & 32)
            op |= 0x02000000u | (random32() & 0xfff);
        else if (i & 64)
            op |= ((i >> 4) & 3) << 5 | ((i >> 2) & 31) << 7;
        else if (i & 128)
            op |= 16 | ((i >> 3) % 15) << 8 | ((i >> 4) & 3) << 5;
        if (kind >= 8 && kind <= 11)
            op |= 0x100000;
        setup(0, op, random32() & 0xf0000000u);
        run(i, 1);
        run(i, 100);
        run(i, 512);
    }
    for (unsigned op = 0; op < 0x4800; op += 13) {
        if ((op & 0xfc00) == 0x4400 && ((op >> 8) & 3) == 3)
            continue;
        setup(1, op, random32() & 0xf0000000u);
        run(0x10000 + op, 1);
        run(0x10000 + op, 100);
        run(0x10000 + op, 512);
    }
    for (unsigned i = 0; i < 128; i++) {
        uint32_t op = 0xe0000091u | ((i % 14) << 16) | (((i + 3) % 14) << 8) | (((i + 5) % 14) << 12) |
                      ((i & 1) ? 0x100000 : 0) | ((i & 2) ? 0x200000 : 0);
        setup(0, op, random32() & 0xf0000000u);
        run(0x20000 + i, 1);
        run(0x20000 + i, 100);
        run(0x20000 + i, 512);
    }
    const uint32_t mul_values[]={0,1,255,256,65535,65536,0xffffff,0x1000000,0x7fffffff,0x80000000,0xffffffff};
    for(unsigned variant=0;variant<256;variant++)for(unsigned value=0;value<sizeof(mul_values)/sizeof(*mul_values);value++) {
        unsigned rd=variant%15,rs=(variant/5)%15,rm=(variant/11)%15,rn=(variant/7)%15;
        uint32_t op=0xe0000090u|(rd<<16)|(rs<<8)|rm|(rn<<12)|((variant&1)?0x100000:0)|((variant&2)?0x200000:0);
        setup(0,op,(variant&15)<<28);
        for(unsigned i=0;i<2;i++)machine[i].machine.cpu.r[rs]=mul_values[value];
        run(0x24000+variant*16+value,511);
        run(0x24000+variant*16+value,29);
    }
    /* Live resident ROM fetch, prefetch FIFO and mirrored page boundaries. */
    for (unsigned variant = 0; variant < 32; variant++) {
        setup(variant & 1, (variant & 1) ? 0x3001 : 0xe2900001, GBA_C);
        unsigned width = (variant & 1) ? 2 : 4;
        for (unsigned at = 0; at < sizeof(rom); at += width) {
            if (width == 2)
                gba_put16(rom + at, 0x3001);
            else
                gba_put32(rom + at, 0xe2900001);
        }
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            g->machine.cpu.r[15] = 0x08001000 + (variant & 4 ? 0xf0 : 0);
            gba_io_write(g, 0x204, (variant & 2 ? 0x4000 : 0) | ((variant >> 3) * 0x111), 2);
        }
        for (unsigned n = 0; n < 20; n++)
            run(0x28000 + variant, 24 + n * 17);
    }
    /* ROM internal cycles fill FIFO or break the sequential stream exactly
     * where MUL/register-shift executes, including failed ARM conditions. */
    for(unsigned variant=0;variant<128;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4,area=(variant>>1)%3;
        setup(thumb,thumb?0x4348:0xe0000190,(variant>>3)<<28);
        for(unsigned n=0;n<32;n++) {
            uint32_t op=thumb?((n&1)?0x4088:0x4348):((n&1)?0xe1b00210:0xe0000190);
            if(!thumb && (variant&16))op=(op&0x0fffffff)|((variant&15)<<28);
            if(n==31)op=thumb?0xe7df:0xeaffffdf;
            if(thumb)gba_put16(rom+0x1800+n*width,(uint16_t)op);
            else gba_put32(rom+0x1800+n*width,op);
        }
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[15]=0x08001800+area*0x02000000;
            g->machine.cpu.r[0]=0x81234567;g->machine.cpu.r[1]=variant&32?0xffffff80:0x12345;
            g->machine.cpu.r[2]=(variant&63)+1;
            gba_io_write(g,0x204,(variant&8?0x4000:0)|((variant>>3)*0x111),2);
        }
        for(unsigned n=0;n<32;n++)run(0x2c000+variant*32+n,17+(n*101)%857);
    }
    /* Conditional execution, including failed flag-setting instructions. */
    for (unsigned condition = 0; condition < 16; condition++)
        for (unsigned flags = 0; flags < 16; flags++) {
            uint32_t op = (condition << 28) | 0x02901001u;
            setup(0, op, flags << 28);
            run(0x30000 + condition * 16 + flags, 1);
            run(0x30000 + condition * 16 + flags, 300);
        }
    /* A native span may end in a conditional/control transfer. Exercise its
     * instruction PC, cached LR, failed condition and refill state explicitly. */
    for (unsigned variant = 0; variant < 64; variant++) {
        unsigned thumb = variant & 1;
        setup(thumb, thumb ? 0x3001 : 0xe2900001, (variant >> 2) << 28);
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            g->machine.cpu.r[1] = 0x03000000 | thumb;
            if (thumb)
                gba_put16(g->memory.iwram + 2, variant & 2 ? 0xe7fd : 0xd1fd);
            else
                gba_put32(g->memory.iwram + 4, variant & 2 ? 0xe12fff11 : 0x1afffffd);
        }
        for (unsigned n = 0; n < 8; n++) run(0x38000 + variant, 511 + n);
    }
    /* ROM back edges stay native across iterations. Exercise refill costs,
     * prefetch enable/phase, all wait areas, conditional exit and near events. */
    for (unsigned variant=0;variant<256;variant++) {
        unsigned thumb=variant&1, area=(variant>>1)%3;
        setup(thumb,thumb?0x3801:0xe2500001,GBA_C|GBA_V);
        if(thumb) {
            gba_put16(rom+0x1800,0x3801);gba_put16(rom+0x1802,0xd1fd);
            gba_put16(rom+0x1804,0xe7fe);gba_put16(rom+0x1806,0x46c0);
        } else {
            gba_put32(rom+0x1800,0xe2500001);gba_put32(rom+0x1804,0x1afffffd);
            gba_put32(rom+0x1808,0xeafffffe);gba_put32(rom+0x180c,0xe1a00000);
        }
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[15]=0x08001800+area*0x02000000;
            g->machine.cpu.r[0]=17+(variant&31);
            gba_io_write(g,0x204,(variant&8?0x4000:0)|((variant>>2)*0x111),2);
            if(variant&128) {gba_io_write(g,0x100,0xff80,2);gba_io_write(g,0x102,0x80,2);}
        }
        for(unsigned n=0;n<16;n++) run(0x3a000+variant,17+(n*37)%529);
    }
    const uint32_t memory_ops[] = {0xe5810000, 0xe5910000, 0xe5c10001, 0xe5d10001, 0xe4810004, 0xe4910004, 0xe7810102,
        0xe7910102, 0xe1c100b0, 0xe1d100b0, 0xe1d100d1, 0xe1d100f1};
    for (unsigned op = 0; op < sizeof(memory_ops) / sizeof(*memory_ops); op++) {
        setup(0, memory_ops[op], GBA_C | GBA_V);
        for (unsigned i = 0; i < 2; i++) {
            machine[i].machine.cpu.r[1] = 0x02002001;
            machine[i].machine.cpu.r[2] = 4;
            gba_put32(machine[i].memory.ewram + 0x2000, 0x981234ab);
        }
        run(0x40000 + op, 1);
        run(0x40000 + op, 200);
    }
    const uint32_t conditional_memory[]={0x01d400d1,0x01d400f0,0x017400d1,0x017400f2,
        0x00f400d2,0x00f400f2,0x01b400d5,0x019400f5,0x05940000,0x05840000};
    for(unsigned condition=0;condition<15;condition++)for(unsigned flags=0;flags<16;flags++)
    for(unsigned pattern=0;pattern<sizeof(conditional_memory)/sizeof(*conditional_memory);pattern++) {
        uint32_t op=(condition<<28)|conditional_memory[pattern];
        setup(0,op,flags<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[4]=(flags&1)?0x03001000:0x02001000;
            g->machine.cpu.r[5]=flags&2?1:4;
            for(unsigned n=0;n<1024;n++) {
                g->memory.ewram[0x800+n]=(uint8_t)(n*137);
                g->memory.iwram[0x800+n]=(uint8_t)(n*137);
            }
        }
        run(0x48000+condition*256+flags*16+pattern,511);
        run(0x48000+condition*256+flags*16+pattern,17);
        run(0x48000+condition*256+flags*16+pattern,259);
    }
    for(unsigned variant=0;variant<64;variant++) {
        unsigned thumb=variant&1, width=thumb?2:4;
        setup(thumb,thumb?0x3001:0xe2800001,GBA_C);
        unsigned start=(variant&2)?0x7f80:0x80;
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[15]=0x03000000+start;
            g->machine.cpu.r[4]=0x02001000;
            for(unsigned n=0;n<48;n++) {
                uint32_t op=n%5==0?(thumb?0x6821:0xe5941000):(thumb?0x3001:0xe2800001);
                if(n==47)op=thumb?0xd1cf:0x1affffcf;
                unsigned offset=(start+n*width)&0x7fff;
                if(thumb)gba_put16(g->memory.iwram+offset,(uint16_t)op);
                else gba_put32(g->memory.iwram+offset,op);
            }
        }
        for(unsigned n=0;n<12;n++) {
            run(0x49a00+variant,499+n*19);
            if(n==4)for(unsigned i=0;i<2;i++) {
                unsigned offset=(start+35*width)&0x7fff;
                if(thumb)gba_write16(&machine[i],0x03800000+offset,0x3002);
                else gba_write32(&machine[i],0x03800000+offset,0xe2800002);
            }
        }
    }
    const uint32_t stable_io_addresses[]={0x04000004,0x04000005,0x04000006,0x04000007,
        0x04000130,0x04000131,0x04000132,0x04000133,0x04000000,0x04000100,0x04000134};
    const uint32_t stable_io_ops[]={0xe5d40000,0xe1d400b0,0xe5940000,0xe1d400d0,0xe1d400f0};
    for(unsigned address=0;address<sizeof(stable_io_addresses)/sizeof(*stable_io_addresses);address++)
    for(unsigned operation=0;operation<sizeof(stable_io_ops)/sizeof(*stable_io_ops);operation++)
    for(unsigned code_region=0;code_region<2;code_region++) {
        uint32_t op=stable_io_ops[operation];setup(0,op,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[4]=stable_io_addresses[address];
            gba_put32(g->io+4,0xabcdff33);gba_put32(g->io+0x130,0xfcfffcff);
            if(code_region)g->machine.cpu.r[15]=0x08001000;
        }
        if(code_region)for(unsigned n=0;n<256;n+=4)gba_put32(rom+0x1000+n,op);
        for(unsigned n=0;n<8;n++)run(0x4a000+address*32+operation*2+code_region,17+n*53);
    }
    for (unsigned op = 0x4800; op < 0xa000; op += 71) {
        setup(1, op, GBA_C);
        for (unsigned i = 0; i < 2; i++) {
            for (unsigned r = 0; r < 8; r++)
                machine[i].machine.cpu.r[r] = 0x02000000u + r * 4;
            machine[i].machine.cpu.r[13] = 0x02001001;
        }
        run(0x50000 + op, 1);
        run(0x50000 + op, 80);
    }
    for (unsigned variant = 0; variant < 32; variant++) {
        unsigned thumb = variant & 1;
        setup(thumb, thumb ? 0x6820 : 0xe5940000, (variant & 3) << 28);
        const uint32_t arm_mix[] = {0xe5940000,0xe2800001,0xe5d42003,0xe0823000,0xe5965000,0xeafffff9};
        const uint16_t thumb_mix[] = {0x6820,0x3001,0x78e2,0x1813,0x6835,0xe7f9};
        const uint32_t targets[] = {0x02001000,0x03001000,0x04000100,0x08000100,0x06000000,0x02001001,0x02801000,0x03801000};
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            for (unsigned n = 0; n < 6; n++) {
                if (thumb) gba_put16(g->memory.iwram + n*2, thumb_mix[n]);
                else gba_put32(g->memory.iwram + n*4, arm_mix[n]);
            }
            g->machine.cpu.r[4] = variant & 2 ? 0x03001000 : 0x02001000;
            g->machine.cpu.r[6] = targets[(variant >> 2) & 7];
            gba_put32(g->memory.ewram + 0x1000, 0x76543210);
            gba_put32(g->memory.iwram + 0x1000, 0xfedcba98);
        }
        for (unsigned n = 0; n < 12; n++) run(0x58000 + variant, 499 + n * 3);
    }
    for (unsigned variant = 0; variant < 32; variant++) {
        unsigned thumb = variant & 1;
        setup(thumb, thumb ? 0x6020 : 0xe5840000, 0);
        const uint32_t arm_mix[] = {0xe5840000,0xe2800001,0xe5c42003,0xe5943000,0xe5865000,0xeafffff9};
        const uint16_t thumb_mix[] = {0x6020,0x3001,0x70e2,0x6823,0x6035,0xe7f9};
        const uint32_t targets[] = {0x02001000,0x03001000,0x03000008,0x04000100,0x06000000,0x02001001,0x02801000,0x03801000};
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            for (unsigned n = 0; n < 6; n++) {
                if (thumb) gba_put16(g->memory.iwram + n*2, thumb_mix[n]);
                else gba_put32(g->memory.iwram + n*4, arm_mix[n]);
            }
            g->machine.cpu.r[4] = variant & 2 ? 0x03001000 : 0x02001000;
            g->machine.cpu.r[6] = targets[(variant >> 2) & 7];
            g->machine.cpu.r[5] = thumb ? 0xe7fe : 0xeafffffe;
        }
        for (unsigned n = 0; n < 12; n++) run(0x59000 + variant, 499 + n * 3);
    }
    const uint32_t branches[] = {0xeafffffe, 0xebfffffe, 0xe12fff10, 0xe1a0f000, 0xe280f000};
    const unsigned thumb_lists[]={1,3,0x13,0xff,0};
    for(unsigned variant=0;variant<128;variant++) {
        unsigned list=thumb_lists[(variant>>4)%5];
        unsigned op=(variant&1?0xbc00:0xb400)|(variant&2?0x100:0)|list;
        setup(1,op,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[13]=(variant&4?0x02004000:0x03004000)+(variant&8?1:0);
            g->machine.cpu.r[14]=0x03000001;
            for(unsigned n=0x1000;n<0x7000;n+=4){gba_put32(g->memory.ewram+n,0x03000001);gba_put32(g->memory.iwram+n,0x03000001);}
            gba_put16(g->memory.iwram+256,0xe77e);
        }
        for(unsigned n=0;n<6;n++)run(0x61000+variant,499+n*7);
    }
    for(unsigned variant=0;variant<96;variant++) {
        unsigned rn=(variant>>1)&7,list=thumb_lists[(variant>>4)%5];
        setup(1,0xc000|(variant&1?0x800:0)|(rn<<8)|list,GBA_C);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            for(unsigned r=0;r<8;r++)g->machine.cpu.r[r]=0x02001000+r*128;
            for(unsigned n=0x1000;n<0x7000;n+=4)gba_put32(g->memory.ewram+n,0x02001000);
            gba_put16(g->memory.iwram+256,0xe77e);
        }
        for(unsigned n=0;n<6;n++)run(0x62000+variant,499+n*7);
    }
    for(unsigned variant=0;variant<24;variant++) {
        unsigned thumb=variant&1,area=(variant>>1)%3;
        setup(thumb,thumb?0x6820:0xe5940000,GBA_C);
        const uint32_t arm_copy[]={0xe5940000,0xe2844004,0xe5860000,0xe1540005,0x1afffffa,0xeafffffe};
        const uint16_t thumb_copy[]={0x6820,0x3404,0x6030,0x42ac,0xd1fa,0xe7fe};
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            for(unsigned n=0;n<6;n++) {
                if(thumb)gba_put16(g->memory.iwram+n*2,thumb_copy[n]);
                else gba_put32(g->memory.iwram+n*4,arm_copy[n]);
            }
            g->machine.cpu.r[4]=0x08000000+area*0x2000000+(variant&8?0xfffc:0x1000);
            g->machine.cpu.r[5]=g->machine.cpu.r[4]+64;
            g->machine.cpu.r[6]=0x02001000;
            gba_io_write(g,0x204,variant&16?0x4000:0,2);
            g->machine.prefetch_count=5;g->machine.prefetch_phase=1;
            g->machine.last_fetch=0x03000000;
        }
        for(unsigned n=0;n<8;n++)run(0x60000+variant,511+n*3);
    }
    for(unsigned variant=0;variant<96;variant++) {
        unsigned thumb=variant&1, cascade=variant&16;
        setup(thumb,thumb?0x8820:0xe1d400b0,GBA_C);
        const uint32_t arm_poll[]={0xe1d400b0,0xe20000ff,0xe1500005,0x1afffffb,0xe5860000,0xeafffff9};
        const uint16_t thumb_poll[]={0x8820,0x0600,0x0e00,0x42a8,0xd1fa,0x6030,0xe7f8};
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            for(unsigned n=0;n<(thumb?7u:6u);n++) {
                if(thumb)gba_put16(g->memory.iwram+n*2,thumb_poll[n]);
                else gba_put32(g->memory.iwram+n*4,arm_poll[n]);
            }
            if(!thumb && (variant&64))gba_put32(g->memory.iwram,0xe5940000);
            g->machine.cpu.r[4]=cascade?0x04000104:0x04000100;
            g->machine.cpu.r[5]=0x40;g->machine.cpu.r[6]=0x02001000;
            gba_io_write(g,0x100,cascade?0xfff8:0xff80,2);
            gba_io_write(g,0x102,0x80|((variant>>1)&3),2);
            if(cascade) {gba_io_write(g,0x104,0xff80,2);gba_io_write(g,0x106,0x84,2);}
            if(variant&32) {
                gba_put32(g->memory.iwram+0x7ffc,0x03000200);
                gba_put32(g->memory.iwram+0x200,0xe12fff1e);
                gba_io_write(g,0x200,8,2);gba_io_write(g,0x208,1,2);
                gba_io_write(g,0x102,0xc0|((variant>>1)&3),2);
            }
        }
        for(unsigned n=0;n<24;n++)run(0x5f000+variant,37+n*19);
    }
    const uint32_t register_lists[]={1,3,0x13,0xff,0x1fff,0x7fff};
    for(unsigned variant=0;variant<192;variant++) {
        unsigned load=variant&1, writeback=variant&2, up=variant&4, pre=variant&8;
        unsigned rn=variant&16?4:14;
        uint32_t op=0xe8000000u|(rn<<16)|(load?0x100000:0)|(writeback?0x200000:0)|
            (up?0x800000:0)|(pre?0x1000000:0)|register_lists[(variant>>5)%6];
        setup(0,op,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[rn]=variant&32?0x03002000:0x02002000;
            for(unsigned n=0;n<128;n++) {gba_put32(g->memory.ewram+0x1f00+n*4,0x12345600+n);
                gba_put32(g->memory.iwram+0x1f00+n*4,0x12345600+n);}
            gba_put32(g->memory.iwram+256,0xeaffffbe);
        }
        for(unsigned n=0;n<4;n++)run(0x5e000+variant,499+n*7);
    }
#if GBA_DYNAREC_MULTI_INLINE
    /* Entire-span validation, mirrored RAM, base-in-list and ROM timing replay
     * for inline LDM. Misaligned/device/wrapping accesses must reach C intact. */
    const uint32_t ldm_targets[]={0x03001000,0x02001000,0x03801000,0x02801000,
        0x03007ff8,0x0203fff8,0x03001001,0x04000100};
    for(unsigned variant=0;variant<128;variant++) {
        unsigned rn=variant&8?14:4,code_rom=variant&16,up=variant&32,pre=variant&64;
        unsigned list=(variant&8)?0x7fff:0x13;
        uint32_t op=0xe8300000u|(rn<<16)|(up?0x800000:0)|(pre?0x1000000:0)|list;
        setup(0,op,GBA_C|GBA_V);
        for(unsigned n=0;n<7;n++)gba_put32(rom+0x1000+n*4,n==0?op:n==6?0xeafffffe:0xe1a00000);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            for(unsigned n=0;n<7;n++)gba_put32(g->memory.iwram+n*4,n==0?op:n==6?0xeafffffe:0xe1a00000);
            for(unsigned n=0;n<64;n++) {
                gba_put32(g->memory.iwram+0xf80+n*4,0x12340000+n);
                gba_put32(g->memory.ewram+0xf80+n*4,0x12340000+n);
            }
            g->machine.cpu.r[rn]=ldm_targets[variant&7];
            if(code_rom)g->machine.cpu.r[15]=0x08001000;
            gba_io_write(g,0x204,(variant&1)?0x4000:0,2);
        }
        run(0x8d000+variant,37);run(0x8d000+variant,503);run(0x8d000+variant,701);
    }
#endif
#if GBA_DYNAREC_MULTI_PC
    /* Native PC loads preserve ARMv4T mode, exact span atomicity and refill
     * timing, including mirror wrap, misalignment and base-in-list fallback. */
    for(unsigned variant=0;variant<128;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4,rn=thumb?13:4;
        unsigned list=thumb?0x800f:((variant&4)?0x8013:0x8003);
        unsigned count=(unsigned)__builtin_popcount(list),up=thumb||!!(variant&32),pre=!thumb&&!!(variant&64);
        uint32_t op=thumb?0xbd0f:(0xe8300000u|(rn<<16)|list|(up?0x800000:0)|(pre?0x1000000:0));
        setup(thumb,thumb?0x3701:0xe2877001,GBA_C|GBA_V);
        uint32_t base=variant&2?0x03001000:0x02001000;
        if(variant&8)base+=0x00800000;
        if((variant&12)==12)base=variant&2?0x03007ffc:0x0203fffc;
        if((variant&28)==28)base|=1;
        unsigned at=up?base+(pre?4:0):base-count*4+(pre?0:4);
        uint32_t start=variant&16?0x08001000:0x03000000,target=variant&16?0x08001200:0x03000200;
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[15]=start;g->machine.cpu.r[rn]=base;
            unsigned mask=(base>>24)==2?0x3ffff:0x7fff;
            uint8_t *ram=(base>>24)==2?g->memory.ewram:g->memory.iwram;
            for(unsigned n=0;n<count;n++)gba_put32(ram+(((at&~3u)+n*4)&mask),n==count-1?target|(variant&3):0x12340000+n);
            uint8_t *code=variant&16?rom+0x1000:g->memory.iwram;
            uint8_t *to=variant&16?rom+0x1200:g->memory.iwram+0x200;
            if(thumb){gba_put16(code,0x3701);gba_put16(code+width,op);gba_put16(to,0xe7fe);gba_put16(to+2,0xe7fe);}
            else {gba_put32(code,0xe2877001);gba_put32(code+width,op);gba_put32(to,0xeafffffe);gba_put32(to+4,0xeafffffe);}
            gba_io_write(g,0x204,variant&1?0x4000:0,2);
        }
        run(0x8f000+variant,37);run(0x8f000+variant,503);
    }
#ifdef GBA_TEST_INLINE_PC_PROOF
    require(gba_test_inline_pc_entries!=0,0x8f100);
    puts_uart("Native inline LDM/POP PC entries=");hex(gba_test_inline_pc_entries);puts_uart("\n");
#endif
#endif
#if GBA_DYNAREC_STORE_INLINE
    /* Stores into code, a neighbouring code line, mirrors, and physical RAM
     * boundaries must fall back atomically before a single register changes. */
    const uint32_t stm_targets[]={0x03000004,0x03000020,0x03800004,0x03007ffc,
        0x0203fffc,0x02001000,0x03001001,0x04000000};
    for(unsigned variant=0;variant<64;variant++) {
        setup(0,0xe8a40003,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[4]=stm_targets[variant&7];
            g->machine.cpu.r[0]=0xe1a00000;g->machine.cpu.r[1]=0xeafffffe;
            if(variant&8)gba_put32(g->memory.iwram,0xe9240003);
            if(variant&16)gba_put32(g->memory.iwram,0xe8a40013); /* base in list */
            if(variant&32)gba_put32(g->memory.iwram,0xe8a44003); /* cached LR */
            gba_put32(g->memory.iwram+4,0xe1a00000);
            gba_put32(g->memory.iwram+8,0xeafffffe);
        }
        run(0x8e000+variant,31);run(0x8e000+variant,503);run(0x8e000+variant,701);
    }
#endif
    const uint32_t address_modes[] = {0xe4810004,0xe4910004,0xe5a10004,0xe5b10004,
        0xe7810102,0xe7910102,0xe6110062,0xe7110062,0xe6a10002,0xe4b10004,
        0xe0d100b2,0xe0c100b2,0xe1f100b2,0xe1e100b2,0xe19100b2,0xe18100b2};
    for (unsigned variant=0;variant<128;variant++) {
        uint32_t op=address_modes[variant&15];
        if (variant&16) op=(op&~0xf000u)|0x1000;
        setup(0,op,variant&32?GBA_C:0);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[1]=variant&64?0x03801000:0x02801000;
            g->machine.cpu.r[2]=4;
            gba_put32(g->memory.iwram+256,0xeaffffbe);
        }
        for(unsigned n=0;n<4;n++) run(0x5c000+variant,511+n);
    }
    for(unsigned variant=0;variant<32;variant++) {
        unsigned kind=variant&7;
        setup(1,0x5160|(kind<<9),variant&8?GBA_C:0);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[4]=variant&16?0x03001000:0x02001000;
            g->machine.cpu.r[5]=variant&8?1:4;
            gba_put32(g->memory.ewram+0x1000,0x89abcdef);
            gba_put32(g->memory.ewram+0x1004,0x9812f0e1);
            gba_put32(g->memory.iwram+0x1000,0x89abcdef);
            gba_put32(g->memory.iwram+0x1004,0x9812f0e1);
            gba_put16(g->memory.iwram+256,0xe77e);
        }
        for(unsigned n=0;n<4;n++) run(0x5d000+variant,511+n);
    }
    /* Execute mixed blocks from actual resident ROM, with both FIFO settings,
     * every Game Pak wait area and an unaligned/device read prefix exit. */
    for (unsigned variant = 0; variant < 64; variant++) {
        unsigned thumb = variant & 1, area = (variant >> 2) % 3;
        setup(thumb, thumb ? 0x6020 : 0xe5840000, GBA_C | GBA_V);
        const uint32_t arm_mix[] = {0xe5840000,0xe2800001,0xe5c42003,0xe5943000,0xe5965000,0xeafffff9};
        const uint16_t thumb_mix[] = {0x6020,0x3001,0x70e2,0x6823,0x6835,0xe7f9};
        for (unsigned n = 0; n < 6; n++) {
            if (thumb) gba_put16(rom + 0x1000 + n*2, thumb_mix[n]);
            else gba_put32(rom + 0x1000 + n*4, arm_mix[n]);
        }
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            g->machine.cpu.r[15] = 0x08001000 + area * 0x02000000;
            g->machine.cpu.r[4] = variant & 2 ? 0x03001000 : 0x02001000;
            g->machine.cpu.r[6] = variant & 16 ? 0x02001001 : variant & 32 ? 0x04000100 : 0x02001000;
            gba_io_write(g, 0x204, (variant & 8 ? 0x4000 : 0) | ((variant >> 3) * 0x111), 2);
        }
        for (unsigned n = 0; n < 12; n++) run(0x5a000 + variant, 499 + n * 3);
    }
    for (unsigned variant = 0; variant < 16; variant++) {
        unsigned thumb = variant & 1;
        setup(thumb, thumb ? 0x6820 : 0xe5940000, 0);
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            g->machine.cpu.r[4] = variant & 4 ? 0x03fffffc : 0x02001000;
            if (thumb) {
                gba_put16(g->memory.iwram + 2, variant & 2 ? 0x3404 : 0x3401);
                gba_put16(g->memory.iwram + 4, 0xd1fc);
            } else {
                gba_put32(g->memory.iwram + 4, variant & 2 ? 0xe2844004 : 0xe2844001);
                gba_put32(g->memory.iwram + 8, 0xeafffffc);
            }
        }
        for (unsigned n = 0; n < 12; n++) run(0x5b000 + variant, 499 + n * 3);
    }
    for (unsigned op = 0; op < sizeof(branches) / sizeof(*branches); op++) {
        setup(0, branches[op], GBA_V);
        for (unsigned i = 0; i < 2; i++)
            machine[i].machine.cpu.r[0] = 0x03000000;
        run(0x60000 + op, 1);
        run(0x60000 + op, 300);
    }
    for (unsigned condition = 0; condition < 14; condition++) {
        setup(1, 0xd0feu | (condition << 8), random32() & 0xf0000000u);
        run(0x70000 + condition, 1);
        run(0x70000 + condition, 300);
    }
    for (unsigned variant = 0; variant < 8; variant++) {
        setup(0, 0xe2811001, 0);
        for (unsigned i = 0; i < 2; i++) {
            gba_t *g = &machine[i];
            if (variant & 1) {
                gba_put32(g->memory.iwram, 0xe5810000);
                g->machine.cpu.r[0] = 0xe2800001;
                g->machine.cpu.r[1] = 0x03800008;
            }
            if (variant & 2) {
                gba_put32(g->memory.ewram, 0xe2800002);
                gba_io_write(g, 0xd4, 0x02000000, 4);
                gba_io_write(g, 0xd8, 0x03000008, 4);
                gba_io_write(g, 0xdc, 0x80000001, 4);
            }
            if (variant & 4) {
                gba_put32(g->memory.iwram + 0x7ffc, 0x03000100);
                gba_put32(g->memory.iwram + 0x100, 0xe12fff1e);
                gba_io_write(g, 0x200, 8, 2);
                gba_io_write(g, 0x208, 1, 2);
                gba_io_write(g, 0x100, 0xfff0, 2);
                gba_io_write(g, 0x102, 0xc0, 2);
            }
        }
        for (unsigned n = 0; n < 40; n++)
            run(0x80000 + variant, 13 + n % 17);
    }
    /* Decoder refusals are keyed by both live pipeline words. Changing an
     * unsupported head or its successor must recover native compilation. */
    for(unsigned variant=0;variant<4;variant++) {
        unsigned thumb=variant&1,next=variant&2;
        setup(thumb,thumb?0xe7fe:0xe10f0000,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            if(thumb) {
                gba_put16(g->memory.iwram,0xe7fe);gba_put16(g->memory.iwram+2,0xe7fd);
            } else {
                gba_put32(g->memory.iwram,next?0xe2800001:0xe10f0000);
                gba_put32(g->memory.iwram+4,next?0xe10f1000:0xeafffffd);
                gba_put32(g->memory.iwram+8,0xeafffffc);
            }
        }
        for(unsigned n=0;n<4;n++)run(0x81000+variant*32+n,127+n*101);
        for(unsigned i=0;i<2;i++) {
            if(thumb)gba_write16(&machine[i],0x03000000,0x3001);
            else gba_write32(&machine[i],0x03000000+(next?4:0),next?0xe2811001:0xe2800001);
        }
#if GBA_DYNAREC_STATS
        uint32_t before_native=gba_native_cache(&machine[1])->stats.native_instructions;
#endif
        for(unsigned n=0;n<8;n++)run(0x81100+variant*32+n,257+n*101);
#if GBA_DYNAREC_STATS
        require(gba_native_cache(&machine[1])->stats.native_instructions>before_native,0x81200+variant);
#endif
        for(unsigned i=0;i<2;i++) {
            if(thumb)gba_write16(&machine[i],0x03000000,0xe7fe);
            else gba_write32(&machine[i],0x03000000+(next?4:0),next?0xe10f1000:0xe10f0000);
        }
        for(unsigned n=0;n<4;n++)run(0x81300+variant*32+n,127+n*101);
    }
    /* Cross audio-only timer overflows while preserving the exact first
     * FIFO-DMA, IRQ/cascade edge and projected byte/half/word counter reads. */
    for(unsigned variant=0;variant<128;variant++) {
        static const uint32_t reads[]={0xe1d400b0,0xe5940000,0xe5d40000,0xe5d40001};
        setup(0,reads[variant&3],GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];unsigned timer=(variant>>2)&3, fifo=(variant>>6)&1;
            g->machine.cpu.r[4]=0x04000100+timer*4;
            gba_put32(g->memory.iwram+4,0xe2811001);
            gba_put32(g->memory.iwram+8,0xeafffffc);
            gba_io_write(g,0x82,(timer&1)<<(10+fifo*4),2);
            for(unsigned n=0;n<8;n++)gba_io_write(g,0xa0+fifo*4,0x1289abcd+n,4);
            if(variant&32)g->machine.apu.fifo_count[fifo]=17;
            if(variant&16) {
                for(unsigned n=0;n<256;n++)gba_put32(g->memory.ewram+0x2000+n*4,0x34891200+n);
                gba_io_write(g,0xbc,0x02002000,4);gba_io_write(g,0xc0,0x040000a0+fifo*4,4);
                gba_io_write(g,0xc4,0xb6400004u,4);
            }
            gba_io_write(g,0x100+timer*4,variant&64?0xfffe:0xffe1,2);
            gba_io_write(g,0x102+timer*4,0x80|(variant&2?0x40:0),2);
            if((variant&32) && timer<3) {
                gba_io_write(g,0x104+timer*4,0xfffd,2);
                gba_io_write(g,0x106+timer*4,0x84|(variant&64?0x40:0),2);
            }
        }
        for(unsigned n=0;n<16;n++)run(0x82000+variant*32+n,17+(n*101)%1231);
    }
    /* Stores share a generation page with code while staying outside its
     * protected cache lines. Exact code and lookahead writes must still exit. */
    for(unsigned variant=0;variant<64;variant++) {
        unsigned thumb=variant&1;
        setup(thumb,thumb?0x6028:0xe5850000,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[0]=thumb?0x30013001:0xe2800001;
            g->machine.cpu.r[5]=0x03000000+((variant>>1)*4);
            if(thumb) {
                gba_put16(g->memory.iwram+2,0x3101);gba_put16(g->memory.iwram+4,0xe7fc);
            } else {
                gba_put32(g->memory.iwram+4,0xe2811001);gba_put32(g->memory.iwram+8,0xeafffffc);
            }
        }
        for(unsigned n=0;n<8;n++)run(0x84000+variant*16+n,17+n*61);
    }
    /* Change all four resident guest-register mappings across taken edges.
     * Values stay dirty until an engine exit; SP/LR are ordinary guest data. */
    for(unsigned variant=0;variant<8;variant++) {
        setup(0,0xe2800001,(variant&15)<<28);
        for(unsigned i=0;i<2;i++)for(unsigned block=0;block<4;block++)for(unsigned n=0;n<16;n++) {
            unsigned rd=(block*4+n%4)%15,rn=(block*4+(n+1)%4)%15;
            uint32_t op=0xe2800000u|(rn<<16)|(rd<<12)|(n+1);
            if(n&1)op|=0x100000;
            if(n==15)op=block==3?0xeaffffbf:0xeaffffff;
            gba_put32(machine[i].memory.iwram+block*64+n*4,op);
        }
        for(unsigned n=0;n<48;n++)run(0x86000+variant*64+n,13+(n*71)%2000);
    }
    for(unsigned variant=0;variant<8;variant++) {
        setup(0,0xe2800001,(variant&15)<<28);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[12]=0x03000201;g->machine.cpu.r[7]=0x03000000;
            for(unsigned n=0;n<12;n++) {
                unsigned rd=n%6;
                gba_put32(g->memory.iwram+n*4,0xe2900001u|(rd<<16)|(rd<<12));
                gba_put16(g->memory.iwram+0x200+n*2,(uint16_t)(0x3001|(rd<<8)));
            }
            gba_put32(g->memory.iwram+48,0xe12fff1c);
            gba_put16(g->memory.iwram+0x218,0x4738);
        }
        for(unsigned n=0;n<48;n++)run(0x87000+variant*64+n,13+(n*71)%2000);
    }
    /* RAM mixing loops may read ROM in every native repetition. Check
     * each Game Pak wait area, prefetch setting, and an exact taken back edge. */
    for(unsigned variant=0;variant<48;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4;
        setup(thumb,thumb?0x6820:0xe5940000,GBA_C|GBA_V);
        const uint32_t arm_ops[]={0xe5940000,0xe2822001,0xe2511001,0x1afffffb};
        const uint16_t thumb_ops[]={0x6820,0x3201,0x3901,0xd1fb};
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];g->machine.cpu.r[1]=4000;
            g->machine.cpu.r[4]=0x08004000+((variant>>1)%3)*0x02000000;
            for(unsigned n=0;n<4;n++) {
                if(thumb)gba_put16(g->memory.iwram+n*width,thumb_ops[n]);
                else gba_put32(g->memory.iwram+n*width,arm_ops[n]);
            }
            gba_io_write(g,0x204,(variant&8?0x4000:0)|((variant>>2)*0x111),2);
        }
        for(unsigned n=0;n<48;n++)run(0x88000+variant*64+n,49+(n*101)%857);
    }
    /* Event-limited native entries must commit exact prefixes, including
     * conditional flags, RAM/ROM data wait and a branch/refill boundary. */
    for(unsigned variant=0;variant<32;variant++) {
        unsigned thumb=variant&1,width=thumb?2:4;
        setup(thumb,thumb?0x3001:0xe2800001,(variant>>1)<<28);
#if GBA_DYNAREC_PERSISTENT_ENGINE && GBA_DYNAREC_STATS
        uint64_t persistent_before=gba_native_cache(&machine[1])->stats.persistent_instructions;
#endif
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];
            g->machine.cpu.r[4]=(variant&4)?0x08004000:(variant&2)?0x03001000:0x02001000;
            g->machine.cpu.r[5]=0x02004000;
            g->machine.cpu.r[1]=3;
            for(unsigned n=0;n<48;n++) {
                uint32_t op=thumb?0x3001:0xe2800001;
                if(n%4==0)op=thumb?0x6820:0xe5940000;
                if(n%4==2)op=thumb?0x4348:0xe0000190;
                if(n%4==3)op=thumb?0x6028:0xe5850000;
                if(n==47)op=thumb?0xe7cf:0xeaffffcf;
                if(thumb)gba_put16(g->memory.iwram+n*width,(uint16_t)op);
                else gba_put32(g->memory.iwram+n*width,op);
            }
            if(variant&8){gba_io_write(g,0x100,0xfff1,2);gba_io_write(g,0x102,0x80,2);}
        }
        for(unsigned n=0;n<96;n++)run(0x90000+variant*128+n,12+(n*37)%247);
        gba_native_cache_t *cache=gba_native_cache(&machine[1]);unsigned observed=0;
        for(unsigned n=0;n<cache->slots;n++) {
            gba_native_block_t *b=gba_native_blocks(cache)+n;
            if(b->epoch==cache->epoch && b->count &&
#if defined(GBA_DYNAREC_ATTACHED_PREFIX)
                b->prefix_offset
#else
                (b->cache_key&512)
#endif
            )observed++;
        }
        if(GBA_DYNAREC_CODE_SIZE>=256u*1024u) {
#if GBA_DYNAREC_PERSISTENT_ENGINE
            /* A persistent basic block can end at this budget without ever
             * creating the legacy backend's attached-prefix representation.
             * The complete workload above is still interpreter-compared. */
            unsigned persistent_observed=0;
#if GBA_DYNAREC_STATS
            persistent_observed=cache->stats.persistent_instructions>persistent_before;
#endif
            require(observed || persistent_observed || !GBA_DYNAREC_STATS,0x92000+variant);
#else
            require(observed!=0,0x92000+variant);
#endif
        }
    }
    puts_uart("Event-limited native prefixes PASS\n");
#if GBA_DYNAREC_CHAIN && GBA_DYNAREC_STATS
    if(GBA_DYNAREC_CODE_SIZE>=256u*1024u)require(gba_native_cache(&machine[1])->stats.chain_links!=0,0x93000);
    puts_uart("Native cross-block continuations=");hex(gba_native_cache(&machine[1])->stats.chain_links);puts_uart("\n");
#endif
#if GBA_DYNAREC_DIRECT && !GBA_DYNAREC_STATS
    if(GBA_DYNAREC_CODE_SIZE>=256u*1024u)require(gba_native_cache(&machine[1])->direct_links!=0,0x93001);
    puts_uart("Native direct fallthroughs=");hex(gba_native_cache(&machine[1])->direct_links);puts_uart("\n");
#endif
#if GBA_DYNAREC_INTERIOR
    require(gba_native_cache(&machine[1])->interior_entries!=0,0x93002);
    puts_uart("Native interior entries=");hex(gba_native_cache(&machine[1])->interior_entries);puts_uart("\n");
#endif
#if GBA_DYNAREC_STATS
    require(gba_native_cache(&machine[1])->stats.guard_alignment!=0,0x93003);
    puts_uart("Native guard reasons align/code/other=");hex(gba_native_cache(&machine[1])->stats.guard_alignment);
    puts_uart("/");hex(gba_native_cache(&machine[1])->stats.guard_code_write);puts_uart("/");hex(gba_native_cache(&machine[1])->stats.guard_other);puts_uart("\n");
#endif
#if GBA_DYNAREC_NEGATIVE && GBA_DYNAREC_STATS
    require(gba_native_cache(&machine[1])->stats.negative_hits!=0,0x93004);
    puts_uart("Native decoder rejection cache hits=");hex(gba_native_cache(&machine[1])->stats.negative_hits);puts_uart("\n");
#endif
#if GBA_DYNAREC_PERSISTENT_ENGINE
    persistent_proof();
#endif
    instances();
    states();
#if GBA_ROM_STREAM
    short_stream();
#endif
    fixtures();
#if defined(GBA_QEMU_FIXTURES_ONLY)
fixture_report:;
#endif
    gba_dynarec_stats_t stats;
    gba_dynarec_stats(&machine[1], &stats);
#ifdef GBA_TEST_LOCAL_ALU_COUNT
    require(gba_test_local_alu_count>0,0xfad00);
    puts_uart("Local ALU executions=");hex(gba_test_local_alu_count);puts_uart("\n");
#endif
    puts_uart("PASS assertions=");
    hex(assertions);
    puts_uart(" failures=");
    hex(failures);
    puts_uart(" native=");
    hex((uint32_t)stats.native_instructions);
    puts_uart(" fallback=");
    hex((uint32_t)stats.fallback_instructions);
    puts_uart(" compiled=");
    hex(stats.compiled_blocks);
    puts_uart(" flushes=");
    hex(stats.flushes);
    puts_uart(" helper=");
    hex((uint32_t)stats.memory_instructions);
    puts_uart(" hits=");
    hex(stats.cache_hits);
    puts_uart(" code_bytes=");
    hex(stats.code_bytes);
    puts_uart("\n");
    finish(0);
    return 0;
}
