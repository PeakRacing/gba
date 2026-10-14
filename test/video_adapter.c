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
#include "common.h"
#include "suite.h"
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_sprites.h>
#include <gba_video.h>
char textGrid[1024];
u16 *textBase = (u16 *)0x06000800;
IWRAM_DATA struct ActiveInfo activeTestInfo = {{'I', 'n', 'f', 'o'}, -1, -1, -1};
extern bool gba_video_case(unsigned index, bool expected, bool refresh);
int main(void) {
    irqInit();
    irqEnable(IRQ_VBLANK);
    REG_DISPCNT = LCDC_OFF;
    BG_PALETTE[0] = 0x7fff;
    BG_PALETTE[1] = 0;
    OBJ_COLORS[0] = 0x7f1c;
    for (unsigned obj = 0; obj < 128; obj++) {
        OAM[obj].attr0 = OBJ_DISABLE;
    }
    REG_BG2PA = REG_BG2PD = 0x100;
    REG_BG2PB = REG_BG2PC = 0;
    bool refresh = gba_video_case(GBA_VIDEO_CASE, GBA_VIDEO_EXPECTED, false);
    REG_DISPCNT |= OBJ_ON | OBJ_1D_MAP;
    for (unsigned frame = 0; frame < 5; frame++) {
        VBlankIntrWait();
        if (refresh) {
            refresh = gba_video_case(GBA_VIDEO_CASE, GBA_VIDEO_EXPECTED, true);
            REG_DISPCNT |= OBJ_ON | OBJ_1D_MAP;
        }
    }
    *(volatile u32 *)0x0203f000 = 0x5649444f;
    for (;;) {
        __asm__ volatile("nop");
    }
}
