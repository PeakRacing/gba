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
/* Headless adapter only. Test cases and assembly remain in the pinned suite. */
#include "common.h"
#include "suite.h"
#include "shifter.h"
#include "carry.h"
#include "multiply-long.h"
#include "bios-math.h"
#include "memory.h"
#include "io-read.h"
#include "timing.h"
#include "timers.h"
#include "timer-irq.h"
#include "dma.h"
#include "misc-edge.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <gba_interrupt.h>
char textGrid[1024];
u16 *textBase = (u16 *)0x06000800;
IWRAM_DATA struct ActiveInfo activeTestInfo = {{'I', 'n', 'f', 'o'}, -1, -1, -1};
void updateTextGrid(void) {
}
void savprintf(const char *format, ...) {
    static unsigned offset;
    char buffer[128];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    if (n > 126) {
        n = 126;
    }
    /* Match the upstream linear log. A ring overwrites SRAM fixture bytes. */
    for (int i = 0; i < n; i++) {
        if (offset >= 32766) {
            return;
        }
        ((volatile u8 *)0x0e000000)[offset++] = (u8)buffer[i];
    }
    if (offset < 32767) {
        ((volatile u8 *)0x0e000000)[offset++] = '\n';
        ((volatile u8 *)0x0e000000)[offset] = 0;
    }
}
int debugprintf(const char *format, ...) {
    (void)format;
    return 0;
}
const char savetype[] = "SRAM_V123";
int main(void) {
    const struct TestSuite *suites[] = {&shifterTestSuite, &carryTestSuite, &multiplyLongTestSuite, &biosMathTestSuite,
        &memoryTestSuite, &ioReadTestSuite, &timingTestSuite, &timersTestSuite, &timerIRQTestSuite, &dmaTestSuite,
        &miscEdgeTestSuite};
    volatile u32 *result = (volatile u32 *)0x0203f000;
    irqInit();
    /* Upstream main initializes this area before the memory/DMA tests. */
    for (unsigned i = 0; i < 8; i++) {
        ((volatile u32 *)0x06000000)[i] = 0x50d0 + i * 0x101;
    }
    irqEnable(IRQ_VBLANK);
    for (unsigned i = 0; i < 32768; i++) {
        ((volatile u8 *)0x0e000000)[i] = 0;
    }
    savprintf("Game Boy Advance Test Suite\n===");
    for (unsigned i = 0; i < sizeof(suites) / sizeof(*suites); i++) {
        result[0] = 0x52554e00 | i;
        suites[i]->run();
        result[1 + i * 2] = *suites[i]->passes;
        result[2 + i * 2] = *suites[i]->totalResults;
    }
    result[0] = 0x53554954;
    for (;;) {
        __asm__ volatile("nop");
    }
}
