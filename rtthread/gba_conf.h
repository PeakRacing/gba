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
#ifdef GBA_USING_STATE
#define GBA_ENABLE_SAVE_STATE 1
#endif
#ifdef GBA_USING_STREAM
#define GBA_ROM_STREAM 1
#endif
