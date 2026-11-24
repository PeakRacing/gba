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
#include <rtconfig.h>
/* Enable these in the project only after binding the corresponding port. */
#ifndef GBA_RAM_LACK
#define GBA_RAM_LACK 1
#endif
#ifndef GBA_COLOR_DEPTH
#define GBA_COLOR_DEPTH 16
#endif
#ifndef GBA_USE_FS
#ifdef GBA_USING_FS
#define GBA_USE_FS 1
#else
#define GBA_USE_FS 0
#endif
#endif
#ifndef GBA_ENABLE_SOUND
#ifdef GBA_USING_SOUND
#define GBA_ENABLE_SOUND 1
#else
#define GBA_ENABLE_SOUND 0
#endif
#endif
#if defined(GBA_USING_STATE) && !defined(GBA_ENABLE_SAVE_STATE)
#define GBA_ENABLE_SAVE_STATE 1
#endif
#if defined(GBA_USING_STREAM) && !defined(GBA_ROM_STREAM)
#define GBA_ROM_STREAM 1
#endif

/* A32 backend; Cortex-M requires a different code generator. */
#ifndef GBA_DYNAREC
#ifdef GBA_USING_DYNAREC
#define GBA_DYNAREC (1)
#else
#define GBA_DYNAREC (0)
#endif
#endif
#ifndef GBA_ARM926_EXECUTABLE_RAM
#define GBA_ARM926_EXECUTABLE_RAM (0)
#endif
#ifndef GBA_FAST_CORE
#define GBA_FAST_CORE (0)
#endif

/* Remaining defaults, matching the portable profile. */

#ifndef GBA_ENABLE_SOUND
#define GBA_ENABLE_SOUND (0)
#endif

#ifndef GBA_USE_FS
#define GBA_USE_FS (0)
#endif

#ifndef GBA_ENABLE_SAVE_STATE
#define GBA_ENABLE_SAVE_STATE (0)
#endif

#ifndef GBA_ENABLE_SAVE
#define GBA_ENABLE_SAVE (1)
#endif

#ifndef GBA_ENABLE_RTC
#define GBA_ENABLE_RTC (1)
#endif

#ifndef GBA_BIOS_HLE
#define GBA_BIOS_HLE (1)
#endif

#ifndef GBA_COLOR_DEPTH
#define GBA_COLOR_DEPTH (16)
#endif

#ifndef GBA_COLOR_SWAP
#define GBA_COLOR_SWAP (0)
#endif

#ifndef GBA_RAM_LACK
#define GBA_RAM_LACK (0)
#endif

#ifndef GBA_FRAME_SKIP
#define GBA_FRAME_SKIP (0)
#endif

#ifndef GBA_ROM_STREAM
#define GBA_ROM_STREAM (0)
#endif

#ifndef GBA_ROM_CACHE_SLOTS
#define GBA_ROM_CACHE_SLOTS (4)
#endif

#ifndef GBA_APU_SAMPLE_RATE
#define GBA_APU_SAMPLE_RATE (32000)
#endif

#ifndef GBA_APU_BUFFER_SAMPLES
#define GBA_APU_BUFFER_SAMPLES (256)
#endif

#ifndef GBA_FAST_CORE
#define GBA_FAST_CORE (0)
#endif

#ifndef GBA_DYNAREC
#define GBA_DYNAREC (0)
#endif

#ifndef GBA_ARM926_EXECUTABLE_RAM
#define GBA_ARM926_EXECUTABLE_RAM (0)
#endif

#ifndef GBA_TEST_MODE
#define GBA_TEST_MODE (0)
#endif

#ifndef GBA_TEST_PROFILE
#define GBA_TEST_PROFILE (0)
#endif

#ifndef GBA_DYNAREC_CODE_SIZE
#define GBA_DYNAREC_CODE_SIZE (256u * 1024u)
#endif

#ifndef GBA_DYNAREC_DATA_SIZE
#define GBA_DYNAREC_DATA_SIZE (64u * 1024u)
#endif

#ifndef GBA_LOG_LEVEL
#define GBA_LOG_LEVEL (0)
#endif

#ifndef GBA_DYNAREC_STATS
#define GBA_DYNAREC_STATS (0)
#endif
