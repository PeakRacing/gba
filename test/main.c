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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static gba_t context;
static _Alignas(4) uint8_t memory[850000];
static uint8_t rom[32768];
static unsigned assertions, failures;
extern uint32_t gba_test_ticks, gba_test_image_hash, gba_test_audio_hash, gba_test_draws, gba_test_audio_bytes,
    gba_test_frame_limit;
extern uint32_t gba_test_validate_pc;
extern uint32_t gba_test_tone, gba_test_tone_samples, gba_test_tone_edges;
extern uint32_t gba_test_log_pc, gba_test_info_address;
extern unsigned gba_test_allocations, gba_test_frees;
#if GBA_USE_FS
extern int gba_test_fs_fail, gba_test_fs_apply;
#endif
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        assertions++;                                                                                                  \
        if (!(condition)) {                                                                                            \
            failures++;                                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                \
        }                                                                                                              \
    } while (0)
static gba_t *setup(void) {
    gba_memory_t descriptor;
    gba_memory_bind(&descriptor, memory, sizeof(memory));
    CHECK(gba_init_static(&context, &descriptor) == 0);
    for (unsigned i = 0; i < sizeof(rom); i += 4) {
        gba_put32(rom + i, 0xeafffffe);
    }
#if !GBA_BIOS_HLE
    static uint8_t bios[16384];
    gba_set_bios(&context, bios, sizeof(bios));
#endif
    CHECK(gba_load_rom(&context, rom, sizeof(rom)) == 0);
    return &context;
}
static void arm(gba_t *g, uint32_t op) {
    gba_put32(g->memory.iwram, op);
    gba_put32(g->memory.iwram + 4, 0xeafffffe);
    gba_put32(g->memory.iwram + 8, 0xe1a00000);
    g->machine.cpu.r[15] = 0x03000000;
    g->machine.cpu.pipeline_valid = 0;
    g->machine.cpu.cpsr &= ~GBA_T;
    gba_step(g);
}
static void thumb(gba_t *g, uint16_t op) {
    gba_put16(g->memory.iwram, op);
    gba_put16(g->memory.iwram + 2, 0xe7fe);
    gba_put16(g->memory.iwram + 4, 0x46c0);
    g->machine.cpu.r[15] = 0x03000000;
    g->machine.cpu.pipeline_valid = 0;
    g->machine.cpu.cpsr |= GBA_T;
    gba_step(g);
}
static void cpu_tests(void) {
    gba_t *g = setup();
    gba_cpu_t *c = &g->machine.cpu;
    c->r[0] = 0x7fffffff;
    arm(g, 0xe2901001);
    CHECK(c->r[1] == 0x80000000 && (c->cpsr & (GBA_N | GBA_V)) == (GBA_N | GBA_V));
    c->r[0] = 0xffffffff;
    arm(g, 0xe2901001);
    CHECK(c->r[1] == 0 && (c->cpsr & (GBA_Z | GBA_C)) == (GBA_Z | GBA_C));
    c->r[0] = 0;
    arm(g, 0xe2501001);
    CHECK(c->r[1] == 0xffffffff && !(c->cpsr & GBA_C));
    c->r[0] = 0x80000001;
    arm(g, 0xe1b01020);
    CHECK(c->r[1] == 0 && (c->cpsr & GBA_C));
    c->r[0] = 0x80000001;
    arm(g, 0xe1b01040);
    CHECK(c->r[1] == 0xffffffff && (c->cpsr & GBA_C));
    c->r[0] = 2;
    c->cpsr |= GBA_C;
    arm(g, 0xe1b01060);
    CHECK(c->r[1] == 0x80000001 && !(c->cpsr & GBA_C));
    for (unsigned amount = 0; amount < 256; amount++) {
        c->r[0] = 0x81234567;
        c->r[2] = amount;
        arm(g, 0xe1b01210);
        CHECK(c->r[1] == (amount < 32 ? 0x81234567u << amount : 0));
    }
    c->r[0] = 0x03000100;
    c->r[1] = 0x12345678;
    arm(g, 0xe5801000);
    CHECK(gba_le32(g->memory.iwram + 0x100) == 0x12345678);
    arm(g, 0xe5902001);
    CHECK(c->r[2] == 0x78123456);
    c->r[0] = 0x03000100;
    c->r[1] = 5;
    c->r[2] = 7;
    arm(g, 0xe8a00006);
    CHECK(c->r[0] == 0x03000108 && gba_le32(g->memory.iwram + 0x104) == 7);
    c->r[3] = 0x02000001;
    arm(g, 0xe12fff13);
    CHECK(c->r[15] == 0x02000000 && (c->cpsr & GBA_T));
    thumb(g, 0x2042);
    CHECK(c->r[0] == 0x42);
    thumb(g, 0x3001);
    CHECK(c->r[0] == 0x43);
    c->r[0] = 0;
    thumb(g, 0x3801);
    CHECK(c->r[0] == 0xffffffff && !(c->cpsr & GBA_C));
    c->r[0] = 5;
    c->r[1] = 7;
    thumb(g, 0x4348);
    CHECK(c->r[0] == 35);
    c->r[13] = 0x03002000;
    c->r[0] = 0x11223344;
    c->r[14] = 0x02000001;
    thumb(g, 0xb501);
    CHECK(c->r[13] == 0x03001ff8 && gba_le32(g->memory.iwram + 0x1ff8) == 0x11223344);
    c->r[0] = 0;
    thumb(g, 0xbd01);
    CHECK(c->r[0] == 0x11223344 && c->r[13] == 0x03002000 && c->r[15] == 0x02000000 && (c->cpsr & GBA_T));
    gba_cpu_mode(g, 0x1f);
    c->r[13] = 0x03001000;
    c->r[8] = 42;
    gba_cpu_exception(g, 0x11, 0x1c, 0x02000004);
    CHECK(c->r[8] == 0 && c->bank[0][0] == 42 && c->spsr[1] == 0x1f);
    gba_cpu_mode(g, 0x1f);
    CHECK(c->r[8] == 42 && c->r[13] == 0x03001000);
}
static void cpu_timing_tests(void) {
    /* Hardware calibration and isolated instruction timings from the fixed
     * mGBA timing suite. Measure with the emulated timer, not a host tick. */
    static const uint8_t calibration[2][8] = {{7, 4, 6, 4, 6, 2, 5, 2}, {4, 1, 3, 1, 4, 0, 3, 0}};
    static const uint8_t load[2][8] = {{10, 6, 9, 6, 9, 4, 8, 4}, {7, 3, 6, 3, 7, 3, 6, 3}};
    for (unsigned thumb_state = 0; thumb_state < 2; thumb_state++) {
        for (unsigned setting = 0; setting < 8; setting++) {
            unsigned wait = ((setting & 1) ? 0x4000 : 0) | ((setting & 2) ? 4 : 0) | ((setting & 4) ? 0x10 : 0);
            for (unsigned instruction = 0; instruction < 3; instruction++) {
                gba_t *g = setup();
                gba_cpu_t *c = &g->machine.cpu;
                unsigned width = thumb_state ? 2 : 4, count = instruction ? 1 : 0;
                if (thumb_state) {
                    gba_put16(rom + 0x100, 0x6001);                             /* str r1, [r0] */
                    gba_put16(rom + 0x102, instruction == 2 ? 0x881a : 0x46c0); /* ldrh r2, [r3] / nop */
                    gba_put16(rom + 0x100 + width * (count + 1), 0x8802);       /* ldrh r2, [r0] */
                    gba_put16(rom + 0x100 + width * (count + 2), 0x46c0);
                    gba_put16(rom + 0x100 + width * (count + 3), 0x46c0);
                } else {
                    gba_put32(rom + 0x100, 0xe5801000);
                    gba_put32(rom + 0x104, instruction == 2 ? 0xe1d320b0 : 0xe1a00000);
                    gba_put32(rom + 0x100 + width * (count + 1), 0xe1d020b0);
                    gba_put32(rom + 0x100 + width * (count + 2), 0xe1a00000);
                    gba_put32(rom + 0x100 + width * (count + 3), 0xe1a00000);
                }
                gba_write16(g, 0x04000204, (uint16_t)wait);
                c->cpsr = 0x1f | (thumb_state ? GBA_T : 0);
                c->r[15] = 0x08000100;
                c->r[0] = 0x04000100;
                c->r[1] = 0x00800000;
                c->r[3] = 0x03001000;
                for (unsigned i = 0; i < count + 2; i++) {
                    gba_step(g);
                }
                unsigned extra = instruction == 2   ? load[thumb_state][setting]
                                 : instruction == 1 ? gba_gamepak_seq((uint16_t)wait, 0) * (thumb_state ? 1 : 2)
                                                    : 0;
                CHECK(c->r[2] == calibration[thumb_state][setting] + extra && !g->fault);
            }
        }
    }
}
static void bus_tests(void) {
    gba_t *g = setup();
    gba_write32(g, 0x02000000, 0x11223344);
    CHECK(gba_read32(g, 0x02040000) == 0x11223344);
    CHECK(gba_read32(g, 0x02000001) == 0x44112233);
    g->machine.open_bus = 0xaabbccdd;
    CHECK(gba_read8(g, 0x01000000) == 0xdd && gba_read16(g, 0x01000000) == 0xccdd);
    gba_write8(g, 0x05000000, 0x12);
    CHECK(gba_read16(g, 0x05000000) == 0x1212);
    gba_write8(g, 0x07000000, 0x34);
    CHECK(gba_read16(g, 0x07000000) == 0);
    gba_write16(g, 0x06010000, 0xabcd);
    CHECK(gba_read16(g, 0x06018000) == 0xabcd);
    g->access_cycles = 0;
    gba_read32(g, 0x02000000);
    CHECK(g->access_cycles == 6);
    g->access_cycles = 0;
    gba_read32(g, 0x03000000);
    CHECK(g->access_cycles == 1);
    for (unsigned area = 0; area < 3; area++) {
        g->access_cycles = 0;
        gba_bus_read(g, 0x08000002 + area * 0x02000000, 2, 1, 0);
        CHECK(g->access_cycles == (2u << area) + 1);
        gba_write16(g, 0x04000204, 1u << (4 + area * 3));
        g->access_cycles = 0;
        gba_bus_read(g, 0x08000002 + area * 0x02000000, 2, 1, 0);
        CHECK(g->access_cycles == 2);
        gba_write16(g, 0x04000204, 0);
    }
    gba_irq(g, 3);
    gba_write8(g, 0x04000202, 1);
    CHECK(gba_io16(g, 0x202) == 2);
    gba_set_keys(g, GBA_KEY_A);
    CHECK(gba_io16(g, 0x130) == 0x3fe);
    gba_write16(g, 0x04000132, 0x4001);
    CHECK(gba_io16(g, 0x202) & 0x1000);
    static uint8_t bios[16384];
    gba_put32(bios, 0x12345678);
    g->bios = bios;
    g->machine.cpu.r[15] = 0;
    CHECK(gba_read32(g, 0) == 0x12345678);
    g->machine.cpu.r[15] = 0x08000000;
    gba_put32(bios, 0);
    CHECK(gba_read32(g, 0) == 0x12345678);
    g->bios = NULL;
#if GBA_ENABLE_SAVE
    CHECK(gba_set_save_type(g, GBA_SAVE_SRAM) == 0);
    gba_memcpy(g->memory.save, "Game", 4);
    CHECK(gba_read32(g, 0x0e000001) == 0x61616161 && gba_read16(g, 0x0e000001) == 0x61000061);
#endif
}
static void irq_tests(void) {
    gba_t *g = setup();
    gba_write16(g, 0x04000200, 1);
    gba_irq(g, 1);
    CHECK(g->machine.cpu.irq_line && g->machine.cpu.irq_delay == 7);
    gba_advance(g, 3);
    gba_irq(g, 1);
    CHECK(g->machine.cpu.irq_delay == 4);
    gba_write16(g, 0x04000202, 1);
    CHECK(!g->machine.cpu.irq_line && !g->machine.cpu.irq_delay);
    gba_irq(g, 1);
    g->machine.cpu.halted = 1;
    CHECK(gba_step(g) == 7 && g->machine.cpu.halted && !g->machine.cpu.irq_delay);
    gba_step(g);
    CHECK(!g->machine.cpu.halted && !g->machine.cpu.hle_irq);
#if GBA_BIOS_HLE
    uint32_t hle_cycles[2] = {0};
    for (unsigned external = 0; external < 2; external++) {
        for (unsigned thumb_state = 0; thumb_state < 2; thumb_state++) {
            g = setup();
            if (external) {
                static uint8_t bios[16384];
                gba_memset(bios, 0, sizeof(bios));
                const uint32_t dispatch[] = {0xe92d500f, 0xe3a00301, 0xe28fe000, 0xe510f004, 0xe8bd500f, 0xe25ef004};
                gba_put32(bios + 0x18, 0xea000042);
                for (unsigned i = 0; i < sizeof(dispatch) / sizeof(*dispatch); i++) {
                    gba_put32(bios + 0x128 + i * 4, dispatch[i]);
                }
                CHECK(gba_unload_rom_discard(g) == 0 && gba_set_bios(g, bios, sizeof(bios)) == 0 &&
                      gba_load_rom(g, rom, sizeof(rom)) == 0);
            }
            gba_cpu_t *c = &g->machine.cpu;
            gba_cpu_mode(g, 0x1f | (thumb_state ? GBA_T : 0));
            c->r[13] = 0x03007f00;
            c->bank[2][5] = 0x03007fa0;
            c->r[15] = 0x08000000;
            c->r[0] = 0x12345678;
            c->r[1] = 0x11223344;
            c->r[4] = 0x02000100;
            c->r[12] = 0xfeed1234;
            c->r[14] = 0x87654321;
            uint32_t sp = c->r[13], flags = c->cpsr;
            const uint32_t handler[] = {0xe5840000, 0xe584e004, 0xe3a01000, 0xe5801200, 0xe1a0f00e};
            for (unsigned i = 0; i < sizeof(handler) / sizeof(*handler); i++) {
                gba_put32(g->memory.iwram + i * 4, handler[i]);
            }
            gba_put32(g->memory.iwram + 0x7ffc, 0x03000000);
            gba_write16(g, 0x04000200, 1);
            gba_write16(g, 0x04000208, 1);
            gba_irq(g, 1);
            gba_advance(g, 6);
            CHECK(c->irq_delay == 1 && !c->hle_irq);
            gba_advance(g, 1);
            CHECK(gba_step(g) == 1);
            CHECK(c->hle_irq == !external && c->r[15] == 0x18 && (c->cpsr & 31) == 0x12 && !(c->cpsr & GBA_T));
            for (unsigned i = 0; i < 8 && c->r[15] != 0x03000000; i++) {
                gba_step(g);
            }
            CHECK(c->r[15] == 0x03000000 && c->r[0] == 0x04000000 && c->r[14] == 0x138);
            CHECK(c->r[13] == 0x03007f88 && gba_le32(g->memory.iwram + 0x7f88) == 0x12345678);
            for (unsigned i = 0; i < 12 && c->r[15] != 0x08000000; i++) {
                gba_step(g);
            }
            CHECK(!g->fault && !c->hle_irq && c->r[15] == 0x08000000 && c->cpsr == flags && c->r[13] == sp);
            CHECK(c->r[0] == 0x12345678 && c->r[1] == 0x11223344 && c->r[12] == 0xfeed1234 && c->r[14] == 0x87654321);
            CHECK(gba_le32(g->memory.ewram + 0x100) == 0x04000000 && gba_le32(g->memory.ewram + 0x104) == 0x138);
            CHECK(gba_le16(g->memory.iwram + 0x7ff8) == 0);
            if (!external) {
                hle_cycles[thumb_state] = g->machine.cycles;
            } else {
                CHECK(g->machine.cycles == hle_cycles[thumb_state]);
            }
        }
    }
#endif
}
static void irq_fetch_tests(void) {
    /* An IRQ becoming ready during fetch discards the current opcode.
     * Sampling only before fetch incorrectly executes this register write. */
    static uint8_t bios[16384];
    for (unsigned thumb_state = 0; thumb_state < 2; thumb_state++) {
        for (unsigned external = 0; external < 2; external++) {
            gba_t *g = setup();
            gba_cpu_t *c = &g->machine.cpu;
            if (external || !GBA_BIOS_HLE) {
                g->bios = bios;
            }
            gba_put32(g->memory.iwram + 0x7ffc, 0x03000400);
            c->cpsr = 0x1f | (thumb_state ? GBA_T : 0);
            c->r[15] = 0x03000000;
            c->r[0] = 0x12345678;
            c->pipeline_valid = c->fetch_sequential = 1;
            c->pipeline[0] = thumb_state ? 0x3001 : 0xe2800001;
            c->pipeline[1] = thumb_state ? 0x46c0 : 0xe1a00000;
            gba_write16(g, 0x04000200, 1);
            gba_write16(g, 0x04000208, 1);
            gba_irq(g, 1);
            gba_advance(g, GBA_IRQ_DELAY - 1);
            CHECK(c->irq_delay == 1);
            CHECK(gba_step(g) == 1 && c->r[0] == 0x12345678 && c->r[15] == 0x18 && c->r[14] == 0x03000004 &&
                  c->cpsr == 0x92 && !c->pipeline_valid && !c->irq_delay && !g->fault);
            CHECK(c->spsr[2] == (0x1fu | (thumb_state ? GBA_T : 0)));
        }
    }
}
static void timing_tests(void) {
    gba_t *g = setup();
    gba_write16(g, 0x04000100, 0xfffe);
    gba_write16(g, 0x04000102, 0xc0);
    gba_advance(g, 2);
    CHECK(g->machine.timer[0].counter == 0xfffe && !(gba_io16(g, 0x202) & 8));
    gba_advance(g, 2);
    CHECK(g->machine.timer[0].counter == 0xfffe && (gba_io16(g, 0x202) & 8));
    gba_write16(g, 0x04000104, 0xffff);
    gba_write16(g, 0x04000106, 0xc4);
    gba_advance(g, 2);
    CHECK(!(gba_io16(g, 0x202) & 16));
    gba_advance(g, 2);
    CHECK(gba_io16(g, 0x202) & 16);
    gba_write16(g, 0x04000102, 0);
    gba_write16(g, 0x04000106, 0);
    gba_reset(g);
    gba_advance(g, 10);
    gba_write16(g, 0x04000100, 0);
    gba_write16(g, 0x04000102, 0x81);
    gba_advance(g, 53);
    CHECK(g->machine.timer[0].counter == 0 && g->machine.timer[0].phase == 63);
    gba_advance(g, 1);
    CHECK(g->machine.timer[0].counter == 1);
    gba_write16(g, 0x04000102, 0x82);
    gba_advance(g, 191);
    CHECK(g->machine.timer[0].counter == 1);
    gba_advance(g, 1);
    CHECK(g->machine.timer[0].counter == 2);
    gba_write16(g, 0x04000102, 0);
    gba_put32(g->memory.ewram, 0xdeadbeef);
    gba_put32(g->memory.ewram + 4, 0x12345678);
    gba_write32(g, 0x040000b0, 0x02000000);
    gba_write32(g, 0x040000b4, 0x03000100);
    gba_write16(g, 0x040000b8, 2);
    gba_write16(g, 0x040000ba, 0xc400);
    gba_step(g);
    gba_step(g);
    CHECK(gba_le32(g->memory.iwram + 0x100) == 0xdeadbeef && gba_le32(g->memory.iwram + 0x104) == 0x12345678);
    CHECK((gba_io16(g, 0x202) & 0x100) && !(gba_io16(g, 0xba) & 0x8000));
    gba_reset(g);
    gba_write16(g, 0x04000004, 0x18);
    gba_advance(g, GBA_HBLANK_START);
    CHECK(gba_io16(g, 4) & 2);
    gba_advance(g, GBA_CYCLES_PER_LINE - GBA_HBLANK_START);
    CHECK(g->machine.ppu.line == 1 && !(gba_io16(g, 4) & 2));
    gba_advance(g, GBA_CYCLES_PER_LINE * 159);
    CHECK(g->machine.ppu.line == 160 && (gba_io16(g, 4) & 1) && (gba_io16(g, 0x202) & 1));
    gba_advance(g, GBA_CYCLES_PER_LINE * 68);
    CHECK(g->machine.frames == 1 && g->machine.ppu.line == 0);
}
static gba_t *event_fixture(void) {
    gba_t *g = setup();
    gba_put32(g->memory.iwram, 0xe1d010b0);
    gba_put32(g->memory.iwram + 4, 0xe1c010b0);
    gba_put32(g->memory.iwram + 8, 0xe2822001);
    gba_put32(g->memory.iwram + 12, 0xeafffffb);
    g->machine.cpu.r[0] = 0x04000100;
    g->machine.cpu.r[15] = 0x03000000;
    gba_write32(g, 0x04000100, 0x00c0ff00);
    gba_write32(g, 0x04000104, 0x00c1fffc);
    gba_write32(g, 0x04000108, 0x00c4fffe);
    gba_write32(g, 0x040000b0, 0x03000000);
    gba_write32(g, 0x040000b4, 0x02002000);
    gba_write32(g, 0x040000b8, 0xa6000002);
    gba_write16(g, 0x04000004, 0x18);
    gba_write16(g, 0x04000200, 0x3fff);
#if GBA_BIOS_HLE
    gba_put32(g->memory.iwram + 0x7ffc, 0x03000200);
    gba_put32(g->memory.iwram + 0x200, 0xe2803c02);
    gba_put32(g->memory.iwram + 0x204, 0xe1d310b2);
    gba_put32(g->memory.iwram + 0x208, 0xe1c310b2);
    gba_put32(g->memory.iwram + 0x20c, 0xe12fff1e);
    gba_write16(g, 0x04000208, 1);
#endif
    gba_write16(g, 0x04000084, 0x80);
    gba_write16(g, 0x04000062, 0xf080);
    gba_write16(g, 0x04000064, 0x8700);
    gba_test_audio_hash = 2166136261u;
    return g;
}
static void event_cache_tests(void) {
    static gba_machine_t expected;
    static uint8_t expected_io[1024];
    gba_t *g = event_fixture();
    uint32_t cycles = gba_run_cycles(g, 24000);
    expected = g->machine;
    gba_memcpy(expected_io, g->io, sizeof(expected_io));
    uint32_t ram = gba_crc32(~0u, g->memory.ewram, 0x40000);
    uint32_t audio = gba_test_audio_hash;
    CHECK(!g->fault && !g->running && !g->stepping);
    g = event_fixture();
    uint32_t stepped = 0;
    while (stepped < 24000 && !g->fault) {
        stepped += gba_step(g);
    }
    CHECK(stepped == cycles && !g->fault && !memcmp(&expected, &g->machine, sizeof(expected)));
    CHECK(!memcmp(expected_io, g->io, sizeof(expected_io)) && ram == gba_crc32(~0u, g->memory.ewram, 0x40000));
    CHECK(audio == gba_test_audio_hash);
    CHECK(!g->event_cycles || g->event_cycles == gba_next_event(g));
}
static void timer_reload_tests(void) {
    for (unsigned ch = 0; ch < 4; ch++) {
        gba_t *g = setup();
        gba_timer_t *t = &g->machine.timer[ch];
        for (unsigned i = 0; i < 24; i += 4) {
            gba_put32(g->memory.iwram + i, 0xe1a00000);
        }
        gba_write32(g, 0x04000100 + ch * 4, 0x0080fffe);
        gba_advance(g, GBA_TIMER_START_DELAY);
        t->counter = 0xfffd;
        g->machine.cpu.r[15] = 0x03000000;
        g->machine.cpu.r[0] = 0x04000100 + ch * 4;
        g->machine.cpu.r[1] = 0;
        g->machine.cpu.pipeline_valid = g->machine.cpu.fetch_sequential = 1;
        g->machine.cpu.pipeline[0] = 0xe1c010b0; /* strh r1, [r0] */
        g->machine.cpu.pipeline[1] = 0xe1a00000;
        CHECK(gba_step(g) == 2 && t->counter == 0xffff && t->reload == 0xfffe && t->reload_delay == 1 &&
              gba_io16(g, 0x100 + ch * 4) == 0);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
        CHECK(gba_state_save_file(g, "test/out/timer-reload.state") == 0);
#endif
        gba_advance(g, 1);
        CHECK(t->counter == 0xfffe && !t->reload && !t->reload_delay);
        gba_advance(g, 2);
        CHECK(t->counter == 0);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
        CHECK(gba_state_load_file(g, "test/out/timer-reload.state") == 0 && t->counter == 0xffff &&
              t->reload == 0xfffe && t->reload_delay == 1 && gba_io16(g, 0x100 + ch * 4) == 0);
        gba_advance(g, 3);
        CHECK(t->counter == 0 && !t->reload && !t->reload_delay);
#endif
        gba_write32(g, 0x04000100 + ch * 4, 0);
        gba_write32(g, 0x04000100 + ch * 4, 0x00801234);
        CHECK(t->counter == 0x1234 && t->reload == 0x1234 && !t->reload_delay);
    }
}
static void dma_finish(gba_t *g, unsigned channel) {
    for (unsigned i = 0; i < 16 && g->machine.dma[channel].pending; i++) {
        gba_step(g);
    }
    CHECK(!g->machine.dma[channel].pending && !g->fault);
}
static void dma_start(
    gba_t *g, unsigned channel, uint32_t source, uint32_t destination, unsigned count, unsigned control) {
    unsigned base = 0x040000b0 + channel * 12;
    gba_write32(g, base, source);
    gba_write32(g, base + 4, destination);
    gba_write16(g, base + 8, count);
    gba_write16(g, base + 10, control);
}
static void dma_tests(void) {
    gba_t *g = setup();
    gba_put32(g->memory.iwram + 0x1000, 0x11223344);
    gba_put32(g->memory.iwram + 0x1004, 0xaabbccdd);
    dma_start(g, 0, 0x03001000, 0x03000300, 1, 0x8400);
    gba_advance(g, GBA_DMA_START_DELAY);
    g->machine.cpu.stopped = 1;
    uint32_t stopped_cycles = g->machine.cycles;
    CHECK(gba_step(g) == 0 && g->machine.cycles == stopped_cycles && g->machine.dma[0].remaining == 1);
    g->machine.cpu.stopped = 0;
    dma_finish(g, 0);
    CHECK(gba_le32(g->memory.iwram + 0x300) == 0x11223344);
    dma_start(g, 0, 0x03001000, 0x03000300, 1, 0x8400);
    dma_finish(g, 0);
    dma_start(g, 1, 0x03001004, 0x03000300, 1, 0x8400);
    dma_finish(g, 1);
    arm(g, 0xe3a00042);
    dma_start(g, 0, 0, 0x03000300, 1, 0x8000);
    dma_finish(g, 0);
    CHECK(gba_le16(g->memory.iwram + 0x300) == 0x3344);
    dma_start(g, 0, 0, 0x03000302, 1, 0x8000);
    dma_finish(g, 0);
    CHECK(gba_le16(g->memory.iwram + 0x302) == 0x1122);
    CHECK(g->machine.dma[0].latch == 0x11223344 && g->machine.dma[1].latch == 0xaabbccdd);
    for (unsigned mode = 1; mode <= 2; mode++) {
        for (unsigned i = 0; i < 4; i++) {
            gba_put16(rom + 0x100 + i * 2, (uint16_t)(0x1020 + i * 0x2020));
        }
        dma_start(g, 1, 0x08000100, 0x03000380, 4, 0x8000 | (mode << 7));
        dma_finish(g, 1);
        for (unsigned i = 0; i < 4; i++) {
            CHECK(gba_le16(g->memory.iwram + 0x380 + i * 2) == 0x1020 + i * 0x2020);
        }
        CHECK(g->machine.dma[1].source == 0x08000108);
    }
    g = setup();
    gba_put32(rom + 0x100, 0x11111111);
    gba_put32(rom + 0x104, 0x22222222);
    gba_put32(g->memory.iwram + 0x1000, 0xaaaaaaaa);
    dma_start(g, 0, 0x03001000, 0x03000400, 1, 0xa440);
    dma_start(g, 1, 0x08000100, 0x03000400, 2, 0x8440);
    gba_advance(g, GBA_DMA_START_DELAY);
    g->machine.ppu.phase = g->machine.cycles = GBA_HBLANK_START - 4;
    gba_step(g);
    CHECK(gba_le32(g->memory.iwram + 0x400) == 0x11111111 && g->machine.dma[1].remaining == 1 &&
          g->machine.dma[0].pending);
    gba_step(g);
    CHECK(gba_le32(g->memory.iwram + 0x400) == 0xaaaaaaaa);
    uint32_t resumed = gba_step(g);
    CHECK(gba_le32(g->memory.iwram + 0x400) == 0x22222222 && resumed == 11);
    g = setup();
    gba_put32(g->memory.iwram + 0x1000, 0xdead0000);
    dma_start(g, 0, 0x03001000, 0x03001100, 1, 0xa400);
    g->machine.ppu.phase = g->machine.cycles = GBA_HBLANK_START - 1;
    g->machine.cpu.r[1] = 0x10000000;
    arm(g, 0xe5910000);
    CHECK(g->machine.cpu.r[0] == 0xe1a00000 && gba_le32(g->memory.iwram + 0x1100) == 0xdead0000);
    g = setup();
    gba_put32(g->memory.iwram + 0x1000, 0xdead0000);
    dma_start(g, 0, 0x03001000, 0x03001100, 1, 0xa400);
    gba_advance(g, GBA_HBLANK_START);
    g->machine.cpu.r[1] = 0x10000000;
    arm(g, 0xe5910000);
    CHECK(g->machine.cpu.r[0] == 0xdead0000 && gba_le32(g->memory.iwram + 0x1100) == 0xdead0000);
#if GBA_ENABLE_SAVE
    g = setup();
    CHECK(gba_set_save_type(g, GBA_SAVE_EEPROM512) == 0);
    g->memory.save_size = 512;
    dma_start(g, 3, 0x03000000, 0x0d000000, 17, 0x8000);
    CHECK(g->fault == GBA_ERROR_MEMORY && g->machine.cart.size == 512);
#endif
}
static void dma_open_bus_tests(void) {
    for (unsigned thumb_state = 0; thumb_state < 2; thumb_state++) {
        gba_t *g = setup();
        gba_cpu_t *c = &g->machine.cpu;
        c->cpsr = 0x1f | (thumb_state ? GBA_T : 0);
        c->r[15] = 0x03000000;
        c->r[1] = 0x10000000;
        c->pipeline_valid = c->fetch_sequential = 1;
        c->pipeline[0] = thumb_state ? 0x6808 : 0xe5910000;
        c->pipeline[1] = thumb_state ? 0x680a : 0xe5912000;
        uint32_t nop = thumb_state ? 0x46c046c0 : 0xe1a00000;
        for (unsigned offset = 0; offset < 20; offset += 4) {
            gba_put32(g->memory.iwram + offset, nop);
        }
        gba_put32(g->memory.iwram + 0x1000, 0xdead0000);
        dma_start(g, 0, 0x03001000, 0x03001100, 1, 0x8400);
        gba_advance(g, GBA_DMA_START_DELAY);
        dma_finish(g, 0);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
        CHECK(gba_state_save_file(g, "test/out/dma-bus.state") == 0);
#endif
        gba_step(g);
        CHECK(c->r[0] == 0xdead0000 && !g->fault);
        gba_step(g);
        CHECK(c->r[2] == nop && !g->fault);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
        CHECK(gba_state_load_file(g, "test/out/dma-bus.state") == 0);
        gba_step(g);
        CHECK(c->r[0] == 0xdead0000 && !g->fault);
#endif
    }
}
static unsigned pixel_rgb(gba_color_t pixel) {
#if GBA_COLOR_DEPTH == 16
    uint16_t p = pixel;
#if GBA_COLOR_SWAP
    p = (uint16_t)((p >> 8) | (p << 8));
#endif
    return ((p >> 11) & 31) | (((p >> 6) & 31) << 5) | ((p & 31) << 10);
#else
    return ((((pixel >> 16) & 255) * 31 + 127) / 255) | (((((pixel >> 8) & 255) * 31 + 127) / 255) << 5) |
           (((pixel & 255) * 31 + 127) / 255 << 10);
#endif
}
static void ppu_event_tests(void) {
    gba_t *g = setup();
    gba_write16(g, 0x04000044, 0x50e3);
    gba_write16(g, 0x04000046, 0x50e4);
    gba_advance(g, GBA_CYCLES_PER_LINE * 80);
    CHECK(g->machine.ppu.window_active == 3);
    gba_advance(g, GBA_CYCLES_PER_LINE * 147);
    CHECK(g->machine.ppu.window_active == 2 && g->machine.ppu.line == 227);
    gba_advance(g, GBA_CYCLES_PER_LINE);
    CHECK(g->machine.ppu.window_active == 2 && g->machine.ppu.line == 0);
    gba_write16(g, 0x04000046, 0);
    CHECK(g->machine.ppu.window_active == 0);

    g = setup();
    gba_write16(g, 0x04000008, 0x100);
    gba_memset(g->memory.vram, 0x11, 32);
    gba_write16(g, 0x05000002, 0x1f);
    g->stepping = 1;
    gba_write16(g, 0x04000000, 0x100);
    g->stepping = 0;
    gba_advance(g, GBA_CYCLES_PER_LINE * 2);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[2 * GBA_WIDTH]) == 0);
    gba_advance(g, GBA_CYCLES_PER_LINE);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[3 * GBA_WIDTH]) == 0x1f);

    g = setup();
    gba_write16(g, 0x04000000, 0x1000);
    for (unsigned obj = 0; obj < 128; obj++) {
        gba_put16(g->memory.oam + obj * 8, 0x200);
    }
    gba_put16(g->memory.oam, 0);
    gba_memset(g->memory.vram + 0x10000, 0x11, 32);
    gba_write16(g, 0x05000202, 0x1f);
    gba_advance(g, GBA_HBLANK_START);
    gba_write16(g, 0x07000002, 8);
    gba_advance(g, GBA_CYCLES_PER_LINE - GBA_HBLANK_START);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[GBA_WIDTH]) == 0x1f && pixel_rgb(g->memory.framebuffer[GBA_WIDTH + 8]) == 0);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
    CHECK(gba_state_save_file(g, "test/out/ppu-latch.state") == 0);
#endif
    gba_advance(g, GBA_HBLANK_START);
    gba_advance(g, GBA_CYCLES_PER_LINE - GBA_HBLANK_START);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[2 * GBA_WIDTH]) == 0 &&
          pixel_rgb(g->memory.framebuffer[2 * GBA_WIDTH + 8]) == 0x1f);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
    CHECK(gba_state_load_file(g, "test/out/ppu-latch.state") == 0);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[GBA_WIDTH]) == 0x1f && pixel_rgb(g->memory.framebuffer[GBA_WIDTH + 8]) == 0);
#endif
}
static void ppu_batch_tests(void) {
    gba_t *g = setup();
    uint32_t random = 0x574241u;
    for (unsigned i = 0; i < 0x18000; i++) {
        random = random * 1664525u + 1013904223u;
        g->memory.vram[i] = (uint8_t)(random >> 24);
    }
    for (unsigned i = 0; i < 512; i++) {
        gba_put16(g->memory.palette + i * 2, (uint16_t)(i * 73u & 0x7fff));
    }
    gba_color_t expected[GBA_WIDTH];
    for (unsigned scene = 0; scene < 64; scene++) {
        unsigned mode = scene % 6;
        g->machine.ppu.line = scene % 160;
        g->machine.ppu.window_active = 3;
        gba_write16(g, 0x04000000, (uint16_t)(mode | 0x6f00 | ((scene & 1) ? 0x10 : 0)));
        gba_write16(g, 0x04000040, (uint16_t)(scene * 3u << 8 | (scene * 7u & 255)));
        gba_write16(g, 0x04000042, 0xc828);
        gba_write16(g, 0x04000048, 0x2d3f);
        gba_write16(g, 0x0400004a, 0x37);
        gba_write16(g, 0x04000050, (uint16_t)(0x3f3f | ((scene & 3) << 6)));
        gba_write16(g, 0x04000052, 0x080c);
        gba_write16(g, 0x04000054, (uint16_t)(scene & 31));
        for (unsigned bg = 0; bg < 4; bg++) {
            unsigned control = ((scene + bg) & 3) | ((scene & 3) << 2) | ((scene & 1) << 7) |
                               (((scene * 3 + bg) & 31) << 8) | ((scene & 3) << 14) | ((scene & 4) << 11);
            gba_write16(g, 0x04000008 + bg * 2, (uint16_t)control);
            gba_write16(g, 0x04000010 + bg * 4, (uint16_t)(scene * 13 + bg * 8));
            gba_write16(g, 0x04000012 + bg * 4, (uint16_t)(scene * 17 + bg * 5));
        }
        for (unsigned affine = 0; affine < 2; affine++) {
            gba_write16(g, 0x04000020 + affine * 16, (uint16_t)(256 - scene * 17));
            gba_write16(g, 0x04000024 + affine * 16, (uint16_t)(scene * 9 - 128));
            gba_write32(g, 0x04000028 + affine * 16, (uint32_t)(scene * 411 - 2048));
            gba_write32(g, 0x0400002c + affine * 16, (uint32_t)(scene * 737 - 1024));
        }
        gba_ppu_render(g);
        gba_color_t *line = g->memory.framebuffer + g->machine.ppu.line % GBA_DRAW_LINES * GBA_WIDTH;
        gba_memcpy(expected, line, sizeof(expected));
        /* A 1x1 mosaic has identical hardware output, but exercises the scalar
         * path across scroll boundaries, flips, affine steps and window masks. */
        gba_write16(g, 0x0400004c, 0);
        for (unsigned bg = 0; bg < 4; bg++) {
            gba_write16(g, 0x04000008 + bg * 2, (uint16_t)(gba_io16(g, 8 + bg * 2) | 0x40));
        }
        gba_ppu_render(g);
        CHECK(!memcmp(expected, line, sizeof(expected)));
    }
}
static void ppu_tests(void) {
    gba_t *g = setup();
    gba_write16(g, 0x04000000, 0x403);
    gba_write16(g, 0x04000020, 0x100);
    gba_write16(g, 0x04000026, 0x100);
    gba_write16(g, 0x06000000, 0x001f);
    gba_write16(g, 0x06000002, 0x7c00);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x001f && pixel_rgb(g->memory.framebuffer[1]) == 0x7c00);
    gba_write16(g, 0x04000000, 0x404);
    gba_write16(g, 0x05000002, 0x03e0);
    g->memory.vram[0] = 1;
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x03e0);
    gba_write16(g, 0x04000000, 0x414);
    g->memory.vram[0xa000] = 1;
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x03e0);
    gba_write16(g, 0x04000000, 0x405);
    gba_write16(g, 0x06000000, 0x001f);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x001f && pixel_rgb(g->memory.framebuffer[160]) == 0);
    gba_write16(g, 0x04000000, 0x100);
    gba_write16(g, 0x04000008, 0x100);
    gba_memset(g->memory.vram, 0x11, 32);
    gba_write16(g, 0x06000800, 0);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x03e0);
    gba_write16(g, 0x04000000, 0x0402);
    gba_write16(g, 0x0400000c, 0x100);
    gba_memset(g->memory.vram, 1, 64);
    g->memory.vram[0x800] = 0;
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x03e0);
    gba_write16(g, 0x04000000, 0x0401);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x03e0);
    gba_write16(g, 0x04000050, 0x84);
    gba_write16(g, 0x04000054, 16);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x7fff);
    gba_write16(g, 0x04000000, 0x80);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x7fff);
    g = setup();
    gba_write16(g, 0x04000000, 0x403);
    gba_write16(g, 0x06000000, 0x03e0);
    gba_write16(g, 0x06000002, 0x7c00);
    gba_write16(g, 0x04000002, 1);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0 && pixel_rgb(g->memory.framebuffer[1]) == 0x7fe0);
    /* WIN0 suppresses BG0 inside its 8-pixel interval, then restores it outside. */
    g = setup();
    gba_write16(g, 0x04000000, 0x2100);
    gba_write16(g, 0x04000008, 0x100);
    gba_memset(g->memory.vram, 0x11, 32);
    gba_write16(g, 0x05000002, 0x001f);
    gba_write16(g, 0x04000040, 8);
    gba_write16(g, 0x04000044, 1);
    gba_write16(g, 0x04000048, 0);
    gba_write16(g, 0x0400004a, 0x3f);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0 && pixel_rgb(g->memory.framebuffer[8]) == 0x1f);
    /* The lower OBJ does not become an alpha-blending target for the upper OBJ. */
    g = setup();
    gba_write16(g, 0x04000000, 0x1000);
    for (unsigned obj = 0; obj < 128; obj++) {
        gba_put16(g->memory.oam + obj * 8, 0x200);
    }
    gba_memset(g->memory.vram + 0x10000, 0x11, 32);
    gba_memset(g->memory.vram + 0x10020, 0x22, 32);
    gba_write16(g, 0x05000202, 0x1f);
    gba_write16(g, 0x05000204, 0x7c00);
    gba_write16(g, 0x07000000, 0x400);
    gba_write16(g, 0x07000004, 0);
    gba_write16(g, 0x07000008, 0);
    gba_write16(g, 0x0700000c, 1);
    gba_write16(g, 0x04000050, 0x1040);
    gba_write16(g, 0x04000052, 0x0808);
    gba_ppu_render(g);
    CHECK(pixel_rgb(g->memory.framebuffer[0]) == 0x1f);
}
static void cart_tests(void) {
#if GBA_ENABLE_SAVE
    gba_t *g = setup();
    CHECK(gba_set_save_type(g, GBA_SAVE_SRAM) == 0);
    gba_write8(g, 0x0e008000, 0x42);
    CHECK(gba_read8(g, 0x0e000000) == 0x42 && g->machine.cart.dirty);
    g->machine.cart.dirty = 0;
    CHECK(gba_set_save_type(g, GBA_SAVE_FLASH128) == 0);
    gba_write8(g, 0x0e005555, 0xaa);
    gba_write8(g, 0x0e002aaa, 0x55);
    gba_write8(g, 0x0e005555, 0xa0);
    gba_write8(g, 0x0e000100, 0x55);
    CHECK(gba_read8(g, 0x0e000100) == 0x55);
    gba_write8(g, 0x0e005555, 0xaa);
    gba_write8(g, 0x0e002aaa, 0x55);
    gba_write8(g, 0x0e005555, 0xb0);
    gba_write8(g, 0x0e000000, 1);
    CHECK(gba_read8(g, 0x0e000100) == 0xff);
    g->machine.cart.dirty = 0;
    gba_set_save_type(g, GBA_SAVE_EEPROM512);
    gba_write16(g, 0x0d000000, 1);
    gba_write16(g, 0x0d000000, 0);
    for (unsigned i = 0; i < 6; i++) {
        gba_write16(g, 0x0d000000, 0);
    }
    for (unsigned i = 0; i < 64; i++) {
        gba_write16(g, 0x0d000000, i & 1);
    }
    gba_write16(g, 0x0d000000, 0);
    CHECK(g->memory.save[0] == 0x55 && g->memory.save[7] == 0x55);
    gba_write16(g, 0x0d000000, 1);
    gba_write16(g, 0x0d000000, 1);
    for (unsigned i = 0; i < 7; i++) {
        gba_write16(g, 0x0d000000, 0);
    }
    for (unsigned i = 0; i < 4; i++) {
        CHECK(gba_read16(g, 0x0d000000) == 0);
    }
    for (unsigned i = 0; i < 64; i++) {
        CHECK(gba_read16(g, 0x0d000000) == (i & 1));
    }
#endif
#if GBA_ENABLE_RTC
    gba_t *r = setup();
    r->machine.cart.rtc_enabled = 1;
    r->machine.rtc.year = 24;
    r->machine.rtc.month = 2;
    r->machine.rtc.day = 28;
    r->machine.rtc.hour = 23;
    r->machine.rtc.minute = 59;
    r->machine.rtc.second = 59;
    gba_rtc_rebase(r);
    gba_test_ticks += 1000;
    gba_rtc_sync(r);
    CHECK(r->machine.rtc.day == 29 && r->machine.rtc.hour == 0);
    gba_test_ticks += 86400000;
    gba_rtc_sync(r);
    CHECK(r->machine.rtc.month == 3 && r->machine.rtc.day == 1);
    gba_test_ticks += 100000;
    gba_rtc_rebase(r);
    unsigned second = r->machine.rtc.second;
    gba_rtc_sync(r);
    CHECK(r->machine.rtc.second == second);
    gba_rtc_gpio(r, 0);
    gba_rtc_gpio(r, 4);
    for (unsigned bit = 0; bit < 8; bit++) {
        unsigned value = 4 | (((0x63u >> bit) & 1) << 1);
        gba_rtc_gpio(r, value);
        gba_rtc_gpio(r, value | 1);
    }
    unsigned control = 0;
    for (unsigned bit = 0; bit < 8; bit++) {
        gba_rtc_gpio(r, 4);
        gba_rtc_gpio(r, 5);
        control |= r->machine.rtc.output << bit;
    }
    CHECK(control == 0x40);
    gba_write16(r, 0x080000c8, 1);
    gba_write16(r, 0x080000c6, 5);
    CHECK(gba_read16(r, 0x080000c6) == 5 && gba_read16(r, 0x080000c8) == 1);
#endif
}
#if GBA_ENABLE_RTC
static void rtc_send(gba_t *g, unsigned value, int reverse) {
    for (unsigned bit = 0; bit < 8; bit++) {
        unsigned data = 4 | (((value >> (reverse ? 7 - bit : bit)) & 1) << 1);
        gba_write16(g, 0x080000c4, (uint16_t)data);
        gba_write16(g, 0x080000c4, (uint16_t)(data | 1));
    }
}
static void rtc_begin(gba_t *g, unsigned command, int reverse) {
    gba_write16(g, 0x080000c8, 1);
    gba_write16(g, 0x080000c6, 7);
    gba_write16(g, 0x080000c4, 0);
    gba_write16(g, 0x080000c4, 4);
    rtc_send(g, command, reverse);
}
static void rtc_write(gba_t *g, unsigned command, const uint8_t *data, unsigned count) {
    rtc_begin(g, command, 0);
    for (unsigned i = 0; i < count; i++) {
        rtc_send(g, data[i], 0);
    }
    gba_write16(g, 0x080000c4, 0);
}
static void rtc_read(gba_t *g, unsigned command, uint8_t *data, unsigned count, int reverse) {
    rtc_begin(g, command, reverse);
    gba_write16(g, 0x080000c6, 5);
    for (unsigned i = 0; i < count; i++) {
        data[i] = 0;
        for (unsigned bit = 0; bit < 8; bit++) {
            gba_write16(g, 0x080000c4, 4);
            gba_write16(g, 0x080000c4, 5);
            data[i] |= ((gba_read16(g, 0x080000c4) >> 1) & 1) << bit;
        }
    }
    gba_write16(g, 0x080000c4, 0);
}
#endif
static void rtc_minute_tests(void) {
#if GBA_ENABLE_RTC
    gba_t *g = setup();
    gba_rtc_t *r = &g->machine.rtc;
    g->machine.cart.rtc_enabled = 1;
    r->control = 0x48;
    r->second = 59;
    r->milliseconds = 999;
    g->machine.cpu.stopped = 1;
    gba_rtc_rebase(g);
    gba_test_ticks++;
    gba_rtc_sync(g);
    CHECK(r->second == 0 && (gba_io16(g, 0x202) & 0x2000) && g->machine.cpu.stopped);
    gba_write16(g, 0x04000202, 0x2000);
    gba_write16(g, 0x04000200, 0x2000);
    gba_test_ticks++;
    CHECK(gba_run_frame(g) != 0 && !g->machine.cpu.stopped && (gba_io16(g, 0x202) & 0x2000));
    r->second = 29;
    r->milliseconds = 999;
    gba_write16(g, 0x04000202, 0x2000);
    gba_rtc_rebase(g);
    gba_test_ticks++;
    gba_rtc_sync(g);
    CHECK(r->second == 30 && !(gba_io16(g, 0x202) & 0x2000));
    gba_test_ticks += 31000;
    gba_rtc_sync(g);
    CHECK(r->second == 1 && (gba_io16(g, 0x202) & 0x2000));
    gba_write16(g, 0x04000202, 0x2000);
    gba_test_ticks += 60000;
    gba_rtc_rebase(g);
    gba_rtc_sync(g);
    CHECK(r->second == 1 && !(gba_io16(g, 0x202) & 0x2000));
    r->control = 0x40;
    gba_test_ticks++;
    gba_rtc_sync(g);
    CHECK(!(gba_io16(g, 0x202) & 0x2000));
#endif
}
static void rtc_tests(void) {
#if GBA_ENABLE_RTC
    gba_t *g = setup();
    gba_rtc_t *r = &g->machine.rtc;
    g->machine.cart.rtc_enabled = 1;
    uint8_t data[7];
    for (unsigned mode = 0; mode < 2; mode++) {
        data[0] = mode ? 0x40 : 0;
        rtc_write(g, 0x62, data, 1);
        for (unsigned hour = 0; hour < 24; hour++) {
            unsigned number = mode ? hour : hour % 12;
            data[0] = (uint8_t)((number / 10 * 16 + number % 10) | (hour >= 12 ? 0x80 : 0));
            data[1] = 0x59;
            data[2] = 0x58;
            uint8_t expected = data[0];
            rtc_write(g, 0x66, data, 3);
            CHECK(r->hour == hour && r->minute == 59 && r->second == 58);
            rtc_read(g, 0x67, data, 3, hour & 1);
            CHECK(data[0] == expected && data[1] == 0x59 && data[2] == 0x58);
        }
    }
    const uint8_t leap[7] = {0x24, 0x02, 0x29, 4, 0x92, 0x34, 0x56};
    rtc_write(g, 0x64, leap, 7);
    CHECK(r->year == 24 && r->month == 2 && r->day == 29 && r->weekday == 4 && r->hour == 12);
    rtc_read(g, 0x65, data, 7, 0);
    CHECK(!memcmp(data, leap, 7));
    const uint8_t malformed[][7] = {{0x24, 0x02, 0x30, 4, 0x11, 0x22, 0x33}, {0x23, 0x02, 0x29, 4, 0x11, 0x22, 0x33},
        {0x1a, 0x02, 0x20, 4, 0x11, 0x22, 0x33}, {0x24, 0x0a, 0x20, 4, 0x11, 0x22, 0x33},
        {0x24, 0x02, 0x1a, 4, 0x11, 0x22, 0x33}, {0x24, 0x02, 0x20, 7, 0x11, 0x22, 0x33},
        {0x24, 0x02, 0x20, 4, 0x1a, 0x22, 0x33}, {0x24, 0x02, 0x20, 4, 0x24, 0x22, 0x33},
        {0x24, 0x02, 0x20, 4, 0x11, 0x1a, 0x33}, {0x24, 0x02, 0x20, 4, 0x11, 0x22, 0x1a}};
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(*malformed); i++) {
        r->dirty = 0;
        r->milliseconds = 321;
        rtc_write(g, 0x64, malformed[i], 7);
        rtc_read(g, 0x65, data, 7, 0);
        CHECK(!memcmp(data, leap, 7) && r->milliseconds == 321 && !r->dirty);
    }
    data[0] = 0;
    rtc_write(g, 0x62, data, 1);
    const uint8_t invalid_hour[3] = {0x12, 0x22, 0x33};
    rtc_write(g, 0x66, invalid_hour, 3);
    CHECK(r->hour == 12 && r->minute == 34 && r->second == 56);
    r->control = 0xc0;
    r->dirty = 0;
    rtc_read(g, 0x63, data, 1, 0);
    CHECK(data[0] == 0xc0 && r->control == 0x40 && r->dirty);
    rtc_read(g, 0x63, data, 1, 1);
    CHECK(data[0] == 0x40);
    for (unsigned op = 5; op < 8; op++) {
        r->dirty = 0;
        rtc_read(g, 0x61 | (op << 1), data, 1, 0);
        CHECK(data[0] == 0xff && !r->dirty);
        data[0] = 0;
        rtc_write(g, 0x60 | (op << 1), data, 1);
        CHECK(!r->dirty && r->control == 0x40);
    }
    rtc_begin(g, 0, 0);
    rtc_send(g, 0x60, 0);
    CHECK(r->year == 24 && r->month == 2 && r->day == 29);
    rtc_begin(g, 0x60, 0);
    CHECK(!r->control && !r->year && r->month == 1 && r->day == 1 && !r->weekday && !r->hour && !r->minute &&
          !r->second && !r->milliseconds);
    rtc_send(g, 0x62, 0);
    rtc_send(g, 0x40, 0);
    CHECK(!r->control && r->bits == 8);
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
    data[0] = 0x40;
    rtc_write(g, 0x62, data, 1);
    rtc_begin(g, 0x66, 0);
    rtc_send(g, 0x92, 0);
    CHECK(gba_state_save_file(g, "test/out/rtc-protocol.state") == 0);
    rtc_send(g, 0x34, 0);
    rtc_send(g, 0x56, 0);
    CHECK(r->hour == 12 && r->minute == 34 && r->second == 56);
    CHECK(gba_state_load_file(g, "test/out/rtc-protocol.state") == 0 && r->bits == 8 && r->transfer_size == 3);
    rtc_send(g, 0x34, 0);
    rtc_send(g, 0x56, 0);
    CHECK(r->hour == 12 && r->minute == 34 && r->second == 56);
    rtc_begin(g, 0x67, 0);
    gba_write16(g, 0x080000c6, 5);
    for (unsigned bit = 0; bit < 4; bit++) {
        gba_write16(g, 0x080000c4, 4);
        gba_write16(g, 0x080000c4, 5);
    }
    CHECK(gba_state_save_file(g, "test/out/rtc-protocol.state") == 0);
    uint32_t stream[2] = {0};
    for (unsigned replay = 0; replay < 2; replay++) {
        if (replay) {
            CHECK(gba_state_load_file(g, "test/out/rtc-protocol.state") == 0 && r->bits == 4 && r->reading);
        }
        for (unsigned bit = 0; bit < 20; bit++) {
            gba_write16(g, 0x080000c4, 4);
            gba_write16(g, 0x080000c4, 5);
            stream[replay] |= ((gba_read16(g, 0x080000c4) >> 1) & 1) << bit;
        }
    }
    CHECK(stream[0] == stream[1] && stream[0] == 0x56349);
#endif
    gba_write16(g, 0x04000200, 0x2000);
    g->machine.cpu.stopped = 1;
    rtc_begin(g, 0x69, 0);
    CHECK((gba_io16(g, 0x202) & 0x2000) && !g->machine.cpu.stopped && g->machine.cpu.irq_delay == GBA_IRQ_DELAY);
    gba_write16(g, 0x04000202, 0x2000);
    rtc_send(g, 0x69, 0);
    CHECK(!(gba_io16(g, 0x202) & 0x2000));
    gba_write16(g, 0x04000200, 0);
    g->machine.cpu.stopped = 1;
    rtc_begin(g, 0x68, 0);
    CHECK((gba_io16(g, 0x202) & 0x2000) && g->machine.cpu.stopped);
    g->machine.cpu.stopped = 0;
    r->year = 99;
    r->month = 12;
    r->day = 31;
    r->hour = 23;
    r->minute = r->second = 59;
    uint32_t old_ticks = gba_test_ticks;
    r->last_ticks = UINT32_MAX - 499;
    gba_test_ticks = 500;
    gba_rtc_sync(g);
    CHECK(!r->year && r->month == 1 && r->day == 1 && !r->hour && !r->minute && !r->second);
    gba_test_ticks = old_ticks;
    gba_rtc_rebase(g);
#endif
}
static void bios_tests(void) {
#if GBA_BIOS_HLE
    gba_t *g = setup();
    g->machine.cpu.r[0] = 100;
    g->machine.cpu.r[1] = 7;
    gba_bios_swi(g, 6);
    CHECK(g->machine.cpu.r[0] == 14 && g->machine.cpu.r[1] == 2);
    g->machine.cpu.r[0] = 123456789;
    gba_bios_swi(g, 8);
    CHECK(g->machine.cpu.r[0] == 11111);
    gba_put32(g->memory.ewram, 0x11223344);
    g->machine.cpu.r[0] = 0x02000000;
    g->machine.cpu.r[1] = 0x03000100;
    g->machine.cpu.r[2] = 0x05000002;
    gba_bios_swi(g, 11);
    CHECK(gba_le32(g->memory.iwram + 0x100) == 0x11223344 && gba_le32(g->memory.iwram + 0x104) == 0x11223344);
    gba_put32(g->memory.ewram, 0xaabbccdd);
    g->machine.cpu.r[0] = 0x02000001;
    g->machine.cpu.r[1] = 0x03000100;
    g->machine.cpu.r[2] = 2;
    gba_bios_swi(g, 11);
    CHECK(gba_le32(g->memory.iwram + 0x100) == 0x00aa00cc);
    gba_put32(g->memory.iwram + 0x100, 0x76543210);
    g->machine.cpu.r[0] = 0x00010000;
    g->machine.cpu.r[2] = 0x04000001;
    gba_bios_swi(g, 11);
    CHECK(gba_le32(g->memory.iwram + 0x100) == 0x76543210);
    g->machine.cpu.wait_mask = 1;
    gba_put16(g->memory.iwram + 0x7ff8, 1);
    gba_step(g);
    CHECK(!g->machine.cpu.wait_mask && g->machine.bios_latch == 0xe3a02004);
#if GBA_ENABLE_SAVE
    CHECK(gba_set_save_type(g, GBA_SAVE_SRAM) == 0);
    gba_memcpy(g->memory.save, "Game", 4);
    g->machine.cpu.r[0] = 0x0e000001;
    g->machine.cpu.r[1] = 0x03000100;
    g->machine.cpu.r[2] = 0x04000001;
    gba_bios_swi(g, 11);
    CHECK(gba_le32(g->memory.iwram + 0x100) == 0x61616161);
#endif
    const uint8_t compressed[] = {0x10, 6, 0, 0, 0x10, 'A', 'B', 'C', 0, 2};
    gba_memcpy(g->memory.ewram, compressed, sizeof(compressed));
    g->machine.cpu.r[0] = 0x02000000;
    g->machine.cpu.r[1] = 0x03000200;
    gba_bios_swi(g, 17);
    CHECK(!memcmp(g->memory.iwram + 0x200, "ABCABC", 6));
    g = setup();
    g->machine.cpu.r[0] = 0x02001000;
    gba_bios_swi(g, 0x1a);
    CHECK(gba_le32(g->memory.ewram + 0x1000) == 0x68736d53 && gba_io16(g, 0x102) == 0x80);
    uint8_t *channel = g->memory.ewram + 0x1050, *wave = g->memory.ewram + 0x4000;
    channel[0] = 0x80;
    channel[2] = channel[3] = channel[4] = channel[5] = channel[6] = 255;
    channel[7] = 128;
    gba_put32(channel + 32, 13379);
    gba_put32(channel + 36, 0x02004000);
    gba_put16(wave + 2, 0x4000);
    gba_put32(wave + 4, 13379);
    gba_put32(wave + 12, 1024);
    gba_memset(wave + 16, 127, 1024);
    gba_bios_swi(g, 0x1c);
    CHECK(g->memory.ewram[0x1350] == 126 && g->memory.ewram[0x1980] == 126);
    g->machine.cpu.r[0] = 0x02004000;
    g->machine.cpu.r[1] = 180;
    g->machine.cpu.r[2] = 0;
    gba_bios_swi(g, 0x1f);
    CHECK(g->machine.cpu.r[0] == 13379);
    gba_bios_swi(g, 0x1e);
    CHECK(channel[0] == 0);
#endif
}
static void apu_tests(void) {
    gba_t *g = setup();
    gba_write16(g, 0x04000084, 0x80);
    gba_write16(g, 0x04000080, 0x1177);
    gba_write16(g, 0x04000082, 2);
    gba_write16(g, 0x04000062, 0xf080);
    gba_write16(g, 0x04000064, 0x8700);
    CHECK(g->machine.apu.channel[0].enabled);
    gba_test_audio_bytes = 0;
    gba_advance(g, GBA_CYCLES_PER_FRAME);
#if GBA_ENABLE_SOUND
    CHECK(gba_test_audio_bytes > 0 && gba_test_audio_hash != 2166136261u);
#endif
    gba_write32(g, 0x040000a0, 0x807f0100);
    CHECK(g->machine.apu.fifo_count[0] == 4);
    gba_apu_timer(g, 0);
    CHECK(g->machine.apu.fifo_count[0] == 3 && g->machine.apu.fifo_sample[0] == 0);
    gba_write16(g, 0x04000082, 0x0800);
    CHECK(g->machine.apu.fifo_count[0] == 0);
    gba_write16(g, 0x04000084, 0);
    CHECK(!g->machine.apu.channel[0].enabled);
#if GBA_ENABLE_SOUND
    g = setup();
    gba_write16(g, 0x04000084, 0x80);
    gba_write16(g, 0x04000080, 0x1177);
    gba_write16(g, 0x04000082, 2);
    gba_write16(g, 0x04000062, 0xf080);
    gba_write16(g, 0x04000064, 0x8000 | 1984);
    gba_test_tone_samples = gba_test_tone_edges = 0;
    gba_test_tone = 1;
    gba_advance(g, GBA_CPU_CLOCK);
    gba_test_tone = 0;
    uint32_t expected_edges = gba_test_tone_samples * 2048u / GBA_APU_SAMPLE_RATE;
    CHECK(gba_test_tone_samples > GBA_APU_SAMPLE_RATE - GBA_APU_BUFFER_SAMPLES &&
          gba_test_tone_edges + 2 >= expected_edges && gba_test_tone_edges <= expected_edges + 2);
#endif
    g = setup();
    gba_write16(g, 0x04000084, 0x80);
    gba_write16(g, 0x04000062, 0x3180);
    gba_write16(g, 0x04000064, 0x8000 | 1984);
    gba_advance(g, GBA_CPU_CLOCK * 3 / 64);
    CHECK(g->machine.apu.channel[0].enabled && g->machine.apu.channel[0].volume == 0);
    gba_write16(g, 0x04000062, 0xf080);
    gba_write16(g, 0x04000064, 0xc000 | 1984);
    gba_advance(g, GBA_CPU_CLOCK / 2);
    CHECK(!g->machine.apu.channel[0].enabled && !g->machine.apu.channel[0].length);
}
#if GBA_ROM_STREAM
static size_t stream_reader(void *user, uint32_t offset, void *data, size_t size) {
    if (user) {
        return size ? size - 1 : 0;
    }
    gba_memcpy(data, rom + offset, size);
    return size;
}
static void stream_tests(void) {
    gba_t *g = setup();
    gba_unload_rom_discard(g);
    rom[8191] = 0x12;
    rom[8192] = 0x34;
    CHECK(gba_load_stream(g, sizeof(rom), stream_reader, NULL) == 0);
    uint8_t data[2];
    CHECK(gba_rom_bytes(g, 8191, data, 2) == 0 && data[0] == 0x12 && data[1] == 0x34);
    gba_memset(g->cache_valid, 0, sizeof(g->cache_valid));
    g->rom_user = (void *)1;
    CHECK(gba_rom_bytes(g, 8191, data, 2) == GBA_ERROR_IO && g->fault == GBA_ERROR_IO);
    for (unsigned i = 0; i < GBA_ROM_CACHE_SLOTS; i++) {
        CHECK(!g->cache_valid[i]);
    }
}
#endif
static void eeprom_file_tests(void) {
#if GBA_USE_FS && GBA_ENABLE_SAVE
    gba_t *g = setup();
    CHECK(gba_unload_rom_discard(g) == 0);
    gba_memcpy(rom + 0x100, "EEPROM_V", 8);
    gba_file_t *file = gba_fopen("test/out/eeprom.gba", "wb");
    CHECK(file && gba_fwrite(rom, 1, sizeof(rom), file) == sizeof(rom));
    if (file) {
        CHECK(gba_fclose(file) == 0);
    }
    gba_file_remove("test/out/eeprom.gba.sav");
    CHECK(gba_load_file(g, "test/out/eeprom.gba") == 0 && g->machine.cart.size == 512);
    dma_start(g, 3, 0x03000000, 0x0d000000, 17, 0x8000);
    CHECK(g->machine.cart.type == GBA_SAVE_EEPROM8K && g->machine.cart.size == 8192);
    g->memory.save[0] = 0x12;
    g->memory.save[8191] = 0x34;
    g->machine.cart.dirty = 1;
    CHECK(gba_unload_rom(g) == 0);
    for (unsigned reload = 0; reload < 5; reload++) {
        CHECK(gba_load_file(g, "test/out/eeprom.gba") == 0 && g->machine.cart.type == GBA_SAVE_EEPROM8K &&
              g->machine.cart.size == 8192 && g->memory.save[0] == 0x12 && g->memory.save[8191] == 0x34);
        CHECK(gba_unload_rom(g) == 0);
    }
    CHECK(gba_set_save_type(g, GBA_SAVE_EEPROM512) == 0);
    CHECK(gba_load_file(g, "test/out/eeprom.gba") == GBA_ERROR_FORMAT && g->machine.cart.size == 512);
    CHECK(gba_unload_rom_discard(g) == 0);
    CHECK(gba_set_save_type(g, GBA_SAVE_AUTO) == 0);
    size_t capacity = g->memory.save_size;
    g->memory.save_size = 512;
    CHECK(gba_load_file(g, "test/out/eeprom.gba") == GBA_ERROR_MEMORY && g->machine.cart.size == 512);
    CHECK(gba_unload_rom_discard(g) == 0);
    g->memory.save_size = capacity;
    file = gba_fopen("test/out/eeprom.gba.sav", "wb");
    CHECK(file && gba_fwrite(memory, 1, 8191, file) == 8191);
    if (file) {
        CHECK(gba_fclose(file) == 0);
    }
    CHECK(gba_load_file(g, "test/out/eeprom.gba") == GBA_ERROR_FORMAT && g->machine.cart.size == 512);
    CHECK(gba_unload_rom_discard(g) == 0);
    gba_file_remove("test/out/eeprom.gba.sav");
#endif
}
static void storage_tests(void) {
#if GBA_USE_FS && GBA_ENABLE_SAVE
    gba_t *g = setup();
    gba_set_save_type(g, GBA_SAVE_SRAM);
    gba_file_remove("test/out/storage.sav");
    CHECK(gba_persist_set_path(g, "test/out/storage") == 0);
    CHECK(gba_persist_load(g) == 0);
    gba_write8(g, 0x0e000123, 0x42);
    CHECK(gba_persist_save(g) == 0);
    g->memory.save[0x123] = 0;
    CHECK(gba_persist_load(g) == 0 && g->memory.save[0x123] == 0x42);
    for (int failure = 1; failure <= 4; failure++) {
        gba_write8(g, 0x0e000123, 0x56);
        gba_test_fs_fail = failure;
        CHECK(gba_persist_save(g) == GBA_ERROR_IO && g->machine.cart.dirty && g->rom_size);
        gba_test_fs_fail = 0;
        gba_file_t *old = gba_fopen("test/out/storage.sav", "rb");
        uint8_t previous;
        CHECK(old && gba_fseek(old, 0x123, SEEK_SET) == 0 && gba_fread(&previous, 1, 1, old) == 1 && previous == 0x42);
        if (old) {
            gba_fclose(old);
        }
    }
    CHECK(gba_persist_save(g) == 0 && !g->machine.cart.dirty);
#if GBA_ENABLE_RTC
    gba_file_remove("test/out/storage.rtc");
    g->machine.cart.rtc_enabled = 1;
    g->machine.rtc.year = 24;
    g->machine.rtc.month = 2;
    g->machine.rtc.day = 29;
    g->machine.rtc.dirty = 1;
    CHECK(gba_persist_save(g) == 0);
    gba_test_ticks += 86400000;
    CHECK(gba_persist_load(g) == 0 && g->machine.rtc.day == 29);
    gba_rtc_sync(g);
    CHECK(g->machine.rtc.day == 29);
    uint8_t rtc_file[48];
    gba_file_t *rtc = gba_fopen("test/out/storage.rtc", "rb");
    CHECK(rtc && gba_fread(rtc_file, 1, sizeof(rtc_file), rtc) == sizeof(rtc_file));
    if (rtc) {
        gba_fclose(rtc);
    }
    gba_put32(rtc_file + 16, 30);
    gba_put32(rtc_file + 44, ~gba_crc32(~0u, rtc_file, 44));
    rtc = gba_fopen("test/out/storage.rtc", "wb");
    CHECK(rtc && gba_fwrite(rtc_file, 1, sizeof(rtc_file), rtc) == sizeof(rtc_file));
    if (rtc) {
        gba_fclose(rtc);
    }
    CHECK(gba_persist_load(g) == GBA_ERROR_FORMAT && g->machine.rtc.day == 29);
    gba_put32(rtc_file + 16, 29);
    gba_put32(rtc_file + 40, 1);
    gba_put32(rtc_file + 44, ~gba_crc32(~0u, rtc_file, 44));
    rtc = gba_fopen("test/out/storage.rtc", "wb");
    CHECK(rtc && gba_fwrite(rtc_file, 1, sizeof(rtc_file), rtc) == sizeof(rtc_file));
    if (rtc) {
        gba_fclose(rtc);
    }
    CHECK(gba_persist_load(g) == GBA_ERROR_FORMAT && g->machine.rtc.day == 29 && g->machine.rtc.control == 0x40);
    gba_persist_set_path(g, "test/out/storage");
    g->machine.rtc.dirty = 1;
    CHECK(gba_persist_save(g) == 0);
    g->machine.cart.rtc_enabled = 0;
    gba_file_remove("test/out/storage.rtc");
#endif
#if GBA_ENABLE_SAVE_STATE
    gba_write16(g, 0x04000100, 0xff00);
    gba_write16(g, 0x04000102, 0x80);
    gba_write16(g, 0x04000200, 1);
    gba_irq(g, 1);
    g->machine.dma[0].latch = 0x12345678;
    dma_start(g, 0, 0x02002000, 0x03000600, 1, 0x8400);
    gba_advance(g, 1);
    CHECK(gba_state_save_file(g, "test/out/events.state") == 0);
    gba_advance(g, 200);
    CHECK(gba_state_load_file(g, "test/out/events.state") == 0 && g->machine.timer[0].startup == 1 &&
          g->machine.cpu.irq_delay == 6 && g->machine.dma[0].startup == 1 && g->machine.dma[0].latch == 0x12345678);
    gba_advance(g, 1);
    CHECK(!g->machine.timer[0].startup && g->machine.timer[0].counter == 0xff00 && g->machine.cpu.irq_delay == 5);
    for (unsigned version = 1; version <= 6; version++) {
        gba_file_t *old_state = gba_fopen("test/out/events.state", "r+b");
        uint8_t old_version[4];
        gba_put32(old_version, version);
        CHECK(old_state && gba_fseek(old_state, 4, SEEK_SET) == 0 && gba_fwrite(old_version, 1, 4, old_state) == 4);
        if (old_state) {
            gba_fclose(old_state);
        }
        uint32_t before = gba_crc32(~0u, &g->machine, sizeof(g->machine));
        CHECK(gba_state_load_file(g, "test/out/events.state") == GBA_ERROR_FORMAT && !g->fault &&
              before == gba_crc32(~0u, &g->machine, sizeof(g->machine)));
    }
    uint32_t *invalid_fields[] = {&g->machine.rtc.control, &g->machine.rtc.transfer[0], &g->machine.rtc.output,
        &g->machine.rtc.bits, &g->machine.ppu.window_active, &g->machine.ppu.bg_delay[0],
        &g->machine.ppu.oam_latch_valid, &g->machine.dma_bus_valid};
    const uint32_t invalid_values[] = {1, 256, 2, 9, 4, 4, 2, 2};
    for (unsigned i = 0; i < sizeof(invalid_values) / sizeof(*invalid_values); i++) {
        uint32_t previous = *invalid_fields[i];
        *invalid_fields[i] = invalid_values[i];
        CHECK(gba_state_save_file(g, "test/out/events.state") == 0);
        *invalid_fields[i] = previous;
        uint32_t before = gba_crc32(~0u, &g->machine, sizeof(g->machine));
        CHECK(gba_state_load_file(g, "test/out/events.state") == GBA_ERROR_FORMAT && !g->fault &&
              before == gba_crc32(~0u, &g->machine, sizeof(g->machine)));
    }
    gba_write16(g, 0x04000102, 0);
    gba_write16(g, 0x040000ba, 0);
    gba_write16(g, 0x04000200, 0);
    gba_write16(g, 0x04000202, 1);
    gba_write16(g, 0x04000084, 0x80);
    gba_write16(g, 0x04000062, 0xf080);
    gba_write16(g, 0x04000064, 0x8700);
    g->machine.cpu.pipeline_valid = g->machine.cpu.fetch_sequential = 1;
    g->machine.cpu.r[15] = 0x08000100;
    g->machine.cpu.pipeline[0] = g->machine.cpu.pipeline[1] = 0xe1a00000;
    g->machine.last_fetch = 0x08000104;
    gba_write16(g, 0x04000204, 0x4000);
    g->machine.prefetch_count = 3;
    g->machine.prefetch_phase = 1;
    CHECK(gba_state_save_file(g, "test/out/replay.state") == 0);
    gba_run_cycles(g, 5000);
    uint32_t pc = g->machine.cpu.r[15], cycles = g->machine.cycles;
    CHECK(gba_state_load_file(g, "test/out/replay.state") == 0);
    CHECK(g->machine.prefetch_count == 3 && g->machine.prefetch_phase == 1 && g->machine.cpu.fetch_sequential == 1);
    gba_run_cycles(g, 5000);
    CHECK(g->machine.cpu.r[15] == pc && g->machine.cycles == cycles);
    CHECK(gba_state_save_file(g, "test/out/replay.state") == 0);
    gba_test_image_hash = gba_test_audio_hash = 2166136261u;
    for (unsigned i = 0; i < 3; i++) {
        gba_run_frame(g);
    }
    uint32_t image = gba_test_image_hash, audio = gba_test_audio_hash;
    uint32_t state_crc = gba_crc32(~0u, &g->machine, sizeof(g->machine));
    CHECK(gba_state_load_file(g, "test/out/replay.state") == 0);
    gba_test_image_hash = gba_test_audio_hash = 2166136261u;
    for (unsigned i = 0; i < 3; i++) {
        gba_run_frame(g);
    }
    CHECK(image == gba_test_image_hash && audio == gba_test_audio_hash &&
          state_crc == gba_crc32(~0u, &g->machine, sizeof(g->machine)));
    gba_test_fs_fail = 6;
    gba_test_fs_apply = 0;
    CHECK(gba_state_load_file(g, "test/out/replay.state") == GBA_ERROR_IO && g->fault == GBA_ERROR_IO &&
          gba_run_frame(g) == 0);
    CHECK(gba_reset(g) == GBA_ERROR_IO && g->reload_required);
    gba_test_fs_fail = 0;
    CHECK(gba_unload_rom_discard(g) == 0);
    g = setup();
    gba_set_save_type(g, GBA_SAVE_SRAM);
    gba_persist_set_path(g, "test/out/storage");
    CHECK(gba_state_save_file(g, "test/out/replay.state") == 0);
    gba_file_t *file = gba_fopen("test/out/replay.state", "r+b");
    uint8_t corrupt = 0x42;
    gba_fseek(file, 200, SEEK_SET);
    gba_fwrite(&corrupt, 1, 1, file);
    gba_fclose(file);
    cycles = g->machine.cycles;
    CHECK(gba_state_load_file(g, "test/out/replay.state") < 0 && g->machine.cycles == cycles && !g->fault);
#endif
    gba_file_t *file2 = gba_fopen("test/out/storage.sav", "wb");
    uint8_t byte = 0;
    gba_fwrite(&byte, 1, 1, file2);
    gba_fclose(file2);
    CHECK(gba_persist_load(g) < 0 && gba_persist_save(g) < 0);
    gba_persist_set_path(g, NULL);
    CHECK(gba_unload_rom_discard(g) == 0);
#if GBA_ROM_STREAM
    gba_file_t *stream_file = gba_fopen("test/out/lifecycle.gba", "wb");
    CHECK(stream_file && gba_fwrite(rom, 1, sizeof(rom), stream_file) == sizeof(rom));
    if (stream_file) {
        gba_fclose(stream_file);
    }
    CHECK(gba_load_file(g, "test/out/lifecycle.gba") == 0 && g->rom_file);
    gba_test_fs_fail = 3;
    CHECK(gba_unload_rom_discard(g) == GBA_ERROR_IO && !g->rom_file && !g->rom_read && !g->rom_user);
    CHECK(gba_reset(g) == GBA_ERROR_IO && g->reload_required);
    gba_test_fs_fail = 0;
    CHECK(gba_unload_rom_discard(g) == 0 && !g->reload_required && !g->rom_size);
#endif
#endif
}
int main(int argc, char **argv) {
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE
    if (argc == 3 && (!strcmp(argv[1], "--state-write") || !strcmp(argv[1], "--state-reject") ||
                         !strcmp(argv[1], "--state-damaged"))) {
        gba_t *g = setup();
        int result;
        if (!strcmp(argv[1], "--state-write")) {
            g->memory.ewram[17] = 0x91;
            result = gba_state_save_file(g, argv[2]);
        } else {
            uint32_t before = gba_crc32(~0u, &g->machine, sizeof(g->machine));
            uint32_t memory_before = gba_crc32(~0u, g->memory.ewram, 0x40000);
            result = gba_state_load_file(g, argv[2]);
            int expected = !strcmp(argv[1], "--state-damaged") ? GBA_ERROR_FORMAT : GBA_ERROR_MISMATCH;
            result = result == expected && !g->fault && before == gba_crc32(~0u, &g->machine, sizeof(g->machine)) &&
                             memory_before == gba_crc32(~0u, g->memory.ewram, 0x40000)
                         ? 0
                         : GBA_ERROR;
        }
        gba_unload_rom_discard(g);
        printf("state_configuration=%d\n", result);
        return result ? 1 : 0;
    }
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
#if GBA_USE_FS
    if (argc >= 3 && !strcmp(argv[1], "--rom")) {
        gba_t *g = gba_init();
        if (!g) {
            return 2;
        }
        int result = gba_load_file(g, argv[2]);
        if (result) {
            printf("load_error=%d\n", result);
            gba_unload_rom_discard(g);
            gba_deinit(g);
            return 2;
        }
        gba_test_frame_limit = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 600;
        gba_test_validate_pc = 1;
        if (getenv("GBA_TEST_LOG_PC")) {
            gba_test_log_pc = (uint32_t)strtoul(getenv("GBA_TEST_LOG_PC"), NULL, 0);
            gba_test_info_address =
                getenv("GBA_TEST_INFO_ADDRESS") ? (uint32_t)strtoul(getenv("GBA_TEST_INFO_ADDRESS"), NULL, 0) : 0;
        }
        uint32_t steps = 0;
        uint32_t step_limit =
            getenv("GBA_TEST_STEP_LIMIT") ? (uint32_t)strtoul(getenv("GBA_TEST_STEP_LIMIT"), NULL, 10) : 200000000u;
        while (!g->gba_quit && !g->fault && steps < step_limit) {
            steps++;
            if (!gba_step(g)) {
                break;
            }
            if (gba_le32(g->memory.ewram + 0x3f000) == 0x53554954 ||
                gba_le32(g->memory.ewram + 0x3f000) == 0x5649444f) {
                break;
            }
        }
        gba_machine_t normalized;
        gba_memcpy(&normalized, &g->machine, sizeof(normalized));
        normalized.frame_skip = 0;
        printf("hardware=%08x ", ~gba_crc32(~0u, &normalized, sizeof(normalized)));
        printf("frames=%u cycles=%u pc=%08x r12=%u fault=%d image=%08x audio=%08x draws=%u steps=%u\n",
            g->machine.frames, g->machine.cycles, g->machine.cpu.r[15], g->machine.cpu.r[12], g->fault,
            gba_test_image_hash, gba_test_audio_hash, gba_test_draws, steps);
        if (getenv("GBA_TEST_PLAY")) {
            printf("display=%04x status=%04x IE=%04x IF=%04x IME=%04x halted=%u stopped=%u wait=%04x save=%u\n",
                gba_io16(g, 0), gba_io16(g, 4), gba_io16(g, 0x200), gba_io16(g, 0x202), gba_io16(g, 0x208),
                g->machine.cpu.halted, g->machine.cpu.stopped, g->machine.cpu.wait_mask, g->machine.cart.size);
        }
        if (gba_le32(g->memory.ewram + 0x3f000) == 0x5649444f) {
            printf("video_magic=5649444f\n");
        }
        if ((gba_le32(g->memory.ewram + 0x3f000) & 0xffffff00) == 0x52554e00 ||
            gba_le32(g->memory.ewram + 0x3f000) == 0x53554954) {
            printf("suite_magic=%08x", gba_le32(g->memory.ewram + 0x3f000));
            for (unsigned i = 0; i < 11; i++) {
                printf(" suite%u=%u/%u", i, gba_le32(g->memory.ewram + 0x3f004 + i * 8),
                    gba_le32(g->memory.ewram + 0x3f008 + i * 8));
            }
            puts("");
            FILE *log = fopen("test/out/suite/log.txt", "wb");
            if (log) {
                fwrite(g->memory.save, 1, 32768, log);
                fclose(log);
            }
        }
        FILE *dump = fopen("test/out/last.ppm", "wb");
        if (dump) {
            fprintf(dump, "P6\n240 %u\n255\n", GBA_DRAW_LINES);
            for (unsigned i = 0; i < GBA_DRAW_SIZE; i++) {
                unsigned rgb = pixel_rgb(g->memory.framebuffer[i]);
                unsigned char p[3] = {(unsigned char)((rgb & 31) * 255 / 31),
                    (unsigned char)(((rgb >> 5) & 31) * 255 / 31), (unsigned char)(((rgb >> 10) & 31) * 255 / 31)};
                fwrite(p, 1, 3, dump);
            }
            fclose(dump);
        }
        result = g->fault;
        uint32_t marker = gba_le32(g->memory.ewram + 0x3f000);
        if (!result && g->machine.frames < gba_test_frame_limit && marker != 0x53554954 && marker != 0x5649444f) {
            fprintf(stderr, "run_incomplete frames=%u requested=%u steps=%u\n", g->machine.frames, gba_test_frame_limit,
                steps);
            result = GBA_ERROR_BUSY;
        }
        g->gba_quit = 0;
        if (result) {
            gba_unload_rom_discard(g);
        }
        gba_deinit(g);
        return result ? 1 : 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    cpu_tests();
    cpu_timing_tests();
    bus_tests();
    irq_tests();
    irq_fetch_tests();
    timing_tests();
    timer_reload_tests();
    event_cache_tests();
    dma_tests();
    dma_open_bus_tests();
    ppu_tests();
    ppu_batch_tests();
    ppu_event_tests();
    cart_tests();
    rtc_tests();
    rtc_minute_tests();
    bios_tests();
    apu_tests();
#if GBA_ROM_STREAM
    stream_tests();
#endif
    storage_tests();
    eeprom_file_tests();
    gba_memory_t invalid;
    CHECK(gba_memory_bind(&invalid, memory + 1, sizeof(memory) - 1) == GBA_ERROR_MEMORY);
    CHECK(gba_memory_bind(&invalid, memory, sizeof(memory)) == 0);
    invalid.iwram = invalid.ewram;
    CHECK(gba_init_static(&context, &invalid) == GBA_ERROR_MEMORY);
    unsigned before = gba_test_allocations;
    gba_t *static_g = setup();
    gba_run_frame(static_g);
    CHECK(gba_deinit_static(static_g) == 0);
    CHECK(gba_test_allocations == before);
    before = gba_test_allocations;
    unsigned freed = gba_test_frees;
    gba_t *dynamic_g = gba_init();
    CHECK(dynamic_g != NULL);
    if (dynamic_g) {
        CHECK(gba_deinit(dynamic_g) == 0);
    }
    CHECK(gba_test_allocations - before == gba_test_frees - freed);
    printf("assertions=%u failures=%u context=%zu memory=%zu config=color%u half%u sound%u fs%u stream%u\n", assertions,
        failures, sizeof(gba_t), gba_memory_size(), GBA_COLOR_DEPTH, GBA_RAM_LACK, GBA_ENABLE_SOUND, GBA_USE_FS,
        GBA_ROM_STREAM);
    return failures ? 1 : 0;
}
