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

#ifdef __cplusplus
extern "C" {
#endif

uint32_t gba_crc32(uint32_t crc, const void *data, size_t size);

#if GBA_USE_FS
int gba_load_file(gba_t *gba, const char *path);
int gba_persist_set_path(gba_t *gba, const char *path);
int gba_persist_load(gba_t *gba);
int gba_persist_save(gba_t *gba);
#if GBA_ENABLE_SAVE_STATE
int gba_state_save_file(gba_t *gba, const char *path);
int gba_state_load_file(gba_t *gba, const char *path);
void gba_state_loaded(gba_t *gba);
#endif
#endif

#ifdef __cplusplus
}
#endif
