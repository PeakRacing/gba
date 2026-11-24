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

#define GBA_LOG_LEVEL_NONE  (0)
#define GBA_LOG_LEVEL_ERROR (1)
#define GBA_LOG_LEVEL_WARN  (2)
#define GBA_LOG_LEVEL_INFO  (3)
#define GBA_LOG_LEVEL_DEBUG (4)

#ifndef GBA_LOG_LEVEL
#define GBA_LOG_LEVEL GBA_LOG_LEVEL_NONE
#endif

#if GBA_LOG_LEVEL >= GBA_LOG_LEVEL_ERROR
#define GBA_LOG_ERROR(...) ((void)gba_log_printf(__VA_ARGS__))
#else
#define GBA_LOG_ERROR(...) ((void)0)
#endif

#if GBA_LOG_LEVEL >= GBA_LOG_LEVEL_WARN
#define GBA_LOG_WARN(...) ((void)gba_log_printf(__VA_ARGS__))
#else
#define GBA_LOG_WARN(...) ((void)0)
#endif

#if GBA_LOG_LEVEL >= GBA_LOG_LEVEL_INFO
#define GBA_LOG_INFO(...) ((void)gba_log_printf(__VA_ARGS__))
#else
#define GBA_LOG_INFO(...) ((void)0)
#endif

#if GBA_LOG_LEVEL >= GBA_LOG_LEVEL_DEBUG
#define GBA_LOG_DEBUG(...) ((void)gba_log_printf(__VA_ARGS__))
#else
#define GBA_LOG_DEBUG(...) ((void)0)
#endif
