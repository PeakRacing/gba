/* Copyright (c) 2026; SPDX-License-Identifier: Apache-2.0 */
#pragma once
#define GBA_COLOR_DEPTH 16
#define GBA_COLOR_SWAP 0
#define GBA_RAM_LACK 0
#define GBA_FRAME_SKIP 0
#define GBA_ROM_STREAM 0
#define GBA_ENABLE_SOUND 1
#define GBA_APU_SAMPLE_RATE 32000
#define GBA_APU_BUFFER_SAMPLES 256
#define GBA_ENABLE_SAVE 1
#define GBA_SAVE_CAPACITY 131072U
#define GBA_ENABLE_RTC 1
#define GBA_BIOS_HLE 1
#define GBA_USE_FS 1
#define GBA_ENABLE_SAVE_STATE 1
#define GBA_PATH_MAX 272
#define GBA_TEST_MODE 0
#define GBA_TEST_PROFILE 0
#ifndef GBA_PORT_BATCH_EVENTS
#define GBA_PORT_BATCH_EVENTS 1
#endif
#ifndef GBA_PORT_FAST_RUN
#define GBA_PORT_FAST_RUN 1
#endif
