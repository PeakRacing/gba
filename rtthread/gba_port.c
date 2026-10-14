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
#include "gba.h"
#include <rtthread.h>
#include <stdarg.h>

void *gba_malloc(size_t size) {
#ifdef RT_USING_HEAP
    return rt_malloc(size);
#else
    (void)size;
    return NULL;
#endif
}
void gba_free(void *memory) {
#ifdef RT_USING_HEAP
    rt_free(memory);
#else
    (void)memory;
#endif
}
void *gba_memcpy(void *destination, const void *source, size_t size) {
    return rt_memcpy(destination, source, size);
}
void *gba_memset(void *destination, int value, size_t size) {
    return rt_memset(destination, value, size);
}
int gba_log_printf(const char *format, ...) {
    char buffer[192];
    va_list args;
    va_start(args, format);
    int result = rt_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    rt_kprintf("%s", buffer);
    return result;
}
uint32_t gba_get_ticks(void) {
    return (uint32_t)((uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}
int gba_initex(gba_t *g) {
    (void)g;
    return 0;
}
int gba_deinitex(gba_t *g) {
    (void)g;
    return 0;
}
/* Board implementations must consume or copy each buffer before returning. */
GBA_WEAK int gba_board_draw(int x1, int y1, int x2, int y2, const gba_color_t *pixels) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)pixels;
    return GBA_ERROR_UNSUPPORTED;
}
GBA_WEAK int gba_board_sound(const uint8_t *pcm, size_t bytes) {
    (void)pcm;
    (void)bytes;
    return GBA_ERROR_UNSUPPORTED;
}
GBA_WEAK uint16_t gba_board_keys(void) {
    return 0;
}
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels) {
    return gba_board_draw(x1, y1, x2, y2, pixels);
}
int gba_sound_output(uint8_t *pcm, size_t bytes) {
    return gba_board_sound(pcm, bytes);
}
void gba_frame(gba_t *g) {
    gba_set_keys(g, gba_board_keys());
}
#if GBA_TEST_MODE
void gba_test_instruction(gba_t *g, uint32_t pc, uint32_t opcode) {
    (void)g;
    (void)pc;
    (void)opcode;
}
#endif
#if GBA_TEST_PROFILE
uint32_t gba_profile_ticks(void) {
    return gba_get_ticks();
}
#endif
/* With GBA_USE_FS=1, supply the gba_file_* and gba_f* hooks in your BSP.
 * gba_file_replace must preserve the old destination on failure. */
