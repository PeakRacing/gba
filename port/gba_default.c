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
#include "gba_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#if GBA_USE_FS
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <errno.h>
#endif
#endif
GBA_WEAK void *gba_malloc(size_t size) {
    return malloc(size);
}
GBA_WEAK void gba_free(void *address) {
    free(address);
}
GBA_WEAK void *gba_memcpy(void *destination, const void *source, size_t size) {
    return memcpy(destination, source, size);
}
GBA_WEAK void *gba_memset(void *destination, int value, size_t size) {
    return memset(destination, value, size);
}
GBA_WEAK int gba_log_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
#if GBA_USE_FS
#ifdef _WIN32
static int gba_wide_path(const char *path, wchar_t *out, int size) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, size) != 0;
}
#endif
GBA_WEAK gba_file_t *gba_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    wchar_t wide[GBA_PATH_MAX + 16], flags[8];
    if (!gba_wide_path(path, wide, GBA_PATH_MAX + 16) || !gba_wide_path(mode, flags, 8)) {
        return NULL;
    }
    return _wfopen(wide, flags);
#else
    return fopen(path, mode);
#endif
}
GBA_WEAK size_t gba_fread(void *data, size_t size, size_t count, gba_file_t *file) {
    return fread(data, size, count, (FILE *)file);
}
GBA_WEAK size_t gba_fwrite(const void *data, size_t size, size_t count, gba_file_t *file) {
    return fwrite(data, size, count, (FILE *)file);
}
GBA_WEAK int gba_fseek(gba_file_t *file, long offset, int origin) {
    return fseek((FILE *)file, offset, origin);
}
GBA_WEAK long gba_ftell(gba_file_t *file) {
    return ftell((FILE *)file);
}
GBA_WEAK int gba_fclose(gba_file_t *file) {
    return fclose((FILE *)file);
}
GBA_WEAK int gba_file_sync(gba_file_t *file) {
    if (fflush((FILE *)file)) {
        return -1;
    }
#ifdef _WIN32
    return _commit(_fileno((FILE *)file));
#else
    return fsync(fileno((FILE *)file));
#endif
}
GBA_WEAK int gba_file_replace(const char *temporary, const char *destination) {
#ifdef _WIN32
    wchar_t from[GBA_PATH_MAX + 16], to[GBA_PATH_MAX + 16];
    if (!gba_wide_path(temporary, from, GBA_PATH_MAX + 16) || !gba_wide_path(destination, to, GBA_PATH_MAX + 16)) {
        return -1;
    }
    return MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(temporary, destination);
#endif
}
GBA_WEAK int gba_file_remove(const char *path) {
#ifdef _WIN32
    wchar_t wide[GBA_PATH_MAX + 16];
    if (!gba_wide_path(path, wide, GBA_PATH_MAX + 16)) {
        return -1;
    }
    return _wremove(wide);
#else
    return remove(path);
#endif
}
GBA_WEAK int gba_file_exists(const char *path) {
#ifdef _WIN32
    wchar_t wide[GBA_PATH_MAX + 16];
    if (!gba_wide_path(path, wide, GBA_PATH_MAX + 16)) {
        return -1;
    }
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? -1 : 1;
    }
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
#else
    if (!access(path, F_OK)) {
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
#endif
}
#endif
#if GBA_TEST_MODE && !defined(GBA_CUSTOM_TEST_HOOK)
GBA_WEAK void gba_test_instruction(gba_t *g, uint32_t pc, uint32_t opcode) {
    (void)g;
    (void)pc;
    (void)opcode;
}
#endif
#if GBA_TEST_PROFILE
GBA_WEAK uint32_t gba_profile_ticks(void) {
    return gba_get_ticks();
}
#endif
