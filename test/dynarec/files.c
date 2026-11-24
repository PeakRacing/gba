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

#if GBA_USE_FS
typedef struct {
    char name[256];
    uint8_t bytes[800000];
    size_t length, position;
    unsigned used, opened;
} gba_ram_file_t;
static gba_ram_file_t files[8];
unsigned gba_short_write, gba_replace_failure;
static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static gba_ram_file_t *find(const char *path) {
    for (unsigned i = 0; i < 8; i++)
        if (files[i].used && same(files[i].name, path))
            return &files[i];
    return NULL;
}
gba_file_t *gba_fopen(const char *path, const char *mode) {
    gba_ram_file_t *f = find(path);
    if (*mode == 'w' && !f) {
        for (unsigned i = 0; i < 8; i++)
            if (!files[i].used) {
                f = &files[i];
                break;
            }
        if (f) {
            size_t n = 0;
            while (path[n] && n < sizeof(f->name) - 1) {
                f->name[n] = path[n];
                n++;
            }
            if (path[n])
                return NULL;
            f->name[n] = 0;
            f->used = 1;
        }
    }
    if (!f || f->opened)
        return NULL;
    f->position = 0;
    f->opened = 1;
    if (*mode == 'w')
        f->length = 0;
    return f;
}
size_t gba_fread(void *data, size_t size, size_t count, gba_file_t *handle) {
    gba_ram_file_t *f = handle;
    if (!size || count > SIZE_MAX / size)
        return 0;
    if (count > (f->length - f->position) / size)
        count = (f->length - f->position) / size;
    gba_memcpy(data, f->bytes + f->position, size * count);
    f->position += size * count;
    return count;
}
size_t gba_fwrite(const void *data, size_t size, size_t count, gba_file_t *handle) {
    gba_ram_file_t *f = handle;
    if (!size || count > SIZE_MAX / size)
        return 0;
    if (count > (sizeof(f->bytes) - f->position) / size)
        count = (sizeof(f->bytes) - f->position) / size;
    if (gba_short_write) {
        gba_short_write = 0;
        count /= 2;
    }
    gba_memcpy(f->bytes + f->position, data, size * count);
    f->position += size * count;
    if (f->length < f->position)
        f->length = f->position;
    return count;
}
int gba_fseek(gba_file_t *handle, long offset, int origin) {
    gba_ram_file_t *f = handle;
    int64_t position = origin == 0 ? offset : (int64_t)(origin == 1 ? f->position : f->length) + offset;
    if (position < 0 || (uint64_t)position > f->length)
        return -1;
    f->position = (size_t)position;
    return 0;
}
long gba_ftell(gba_file_t *handle) {
    return (long)((gba_ram_file_t *)handle)->position;
}
int gba_fclose(gba_file_t *handle) {
    ((gba_ram_file_t *)handle)->opened = 0;
    return 0;
}
int gba_file_sync(gba_file_t *handle) {
    (void)handle;
    return 0;
}
int gba_file_exists(const char *path) {
    return find(path) != NULL;
}
int gba_file_remove(const char *path) {
    gba_ram_file_t *f = find(path);
    if (!f || f->opened)
        return -1;
    f->used = 0;
    return 0;
}
int gba_file_replace(const char *temporary, const char *destination) {
    if (gba_replace_failure) {
        gba_replace_failure = 0;
        return -1;
    }
    gba_ram_file_t *from = find(temporary);
    if (!from || from->opened)
        return -1;
    gba_ram_file_t *to = gba_fopen(destination, "wb");
    if (!to)
        return -1;
    gba_memcpy(to->bytes, from->bytes, from->length);
    to->length = from->length;
    gba_fclose(to);
    from->used = 0;
    return 0;
}
void gba_state_loaded(gba_t *gba) {
    (void)gba;
}
int gba_ram_file_flip(const char *path, size_t offset) {
    gba_ram_file_t *f = find(path);
    if (!f || offset >= f->length)
        return -1;
    f->bytes[offset] ^= 1;
    return 0;
}
uint32_t gba_ram_file_hash(const char *path) {
    gba_ram_file_t *f = find(path);
    uint32_t hash = 2166136261u;
    if (!f)
        return 0;
    for (size_t i = 0; i < f->length; i++)
        hash = (hash ^ f->bytes[i]) * 16777619u;
    return hash;
}
#endif
