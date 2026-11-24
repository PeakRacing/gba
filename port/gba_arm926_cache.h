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
#pragma once

#include "gba_default.h"

/* Privileged ARM926 only. The caller guarantees an executable RAM mapping. */
#if GBA_DYNAREC && defined(GBA_ARM926_EXECUTABLE_RAM) && GBA_ARM926_EXECUTABLE_RAM
#if !defined(__GNUC__) || (!defined(__ARM_ARCH_5TEJ__) && !defined(__ARM_ARCH_5TE__))
#error "provide native code port hooks for this compiler or processor"
#endif
GBA_INLINE int gba_arm926_publish(void *address, size_t bytes) {
    uintptr_t line = (uintptr_t)address & ~(uintptr_t)31;
    uintptr_t end = (uintptr_t)address + bytes;
    for (; line < end; line += 32) {
        __asm__ volatile("mcr p15,0,%0,c7,c10,1" ::"r"(line) : "memory");
    }
    unsigned zero = 0;
    __asm__ volatile("mcr p15,0,%0,c7,c10,4" ::"r"(zero) : "memory");
    for (line = (uintptr_t)address & ~(uintptr_t)31; line < end; line += 32) {
        __asm__ volatile("mcr p15,0,%0,c7,c5,1" ::"r"(line) : "memory");
    }
    /* F1C200S traps on c7,c5,4 although QEMU accepts it. After draining writes
     * and invalidating only the published I-cache lines, an A32 branch discards the prefetched stream.
     * Skip a real instruction so the publication never relies on fallthrough. */
    __asm__ volatile("b 1f\n nop\n1:" ::: "memory");
    return GBA_OK;
}
#endif
