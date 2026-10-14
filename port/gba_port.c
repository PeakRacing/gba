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
#if !defined(GBA_CUSTOM_TICKS)
#if defined(_WIN32) && !defined(GBA_PORT_BAREMETAL)
#include <windows.h>
GBA_WEAK uint32_t gba_get_ticks(void) {
    return GetTickCount();
}
#elif !defined(GBA_PORT_BAREMETAL)
#include <time.h>
GBA_WEAK uint32_t gba_get_ticks(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) {
        return 0;
    }
    return (uint32_t)((uint64_t)time.tv_sec * 1000 + (uint32_t)time.tv_nsec / 1000000);
}
#else
GBA_WEAK uint32_t gba_get_ticks(void) {
    return 0;
}
#endif
#endif
#if !defined(GBA_CUSTOM_OUTPUT)
GBA_WEAK int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)pixels;
    return 0;
}
GBA_WEAK int gba_sound_output(uint8_t *buffer, size_t bytes) {
    (void)buffer;
    (void)bytes;
    return 0;
}
#endif
#if !defined(GBA_CUSTOM_FRAME)
GBA_WEAK void gba_frame(gba_t *g) {
    (void)g;
}
#endif
#if !defined(GBA_CUSTOM_LIFECYCLE)
GBA_WEAK int gba_initex(gba_t *g) {
    (void)g;
    return 0;
}
GBA_WEAK int gba_deinitex(gba_t *g) {
    (void)g;
    return 0;
}
#endif
#if GBA_USE_FS && GBA_ENABLE_SAVE_STATE && !defined(GBA_CUSTOM_STATE)
GBA_WEAK void gba_state_loaded(gba_t *g) {
    (void)g;
}
#endif
