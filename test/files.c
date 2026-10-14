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
#define gba_fread gba_test_real_fread
#define gba_fwrite gba_test_real_fwrite
#define gba_file_sync gba_test_real_sync
#define gba_fclose gba_test_real_close
#define gba_file_replace gba_test_real_replace
#define gba_fseek gba_test_real_seek
#define gba_malloc gba_test_real_malloc
#define gba_free gba_test_real_free
#include "../port/gba_default.c"
#undef gba_fread
#undef gba_fwrite
#undef gba_file_sync
#undef gba_fclose
#undef gba_file_replace
#undef gba_fseek
#undef gba_malloc
#undef gba_free
unsigned gba_test_allocations, gba_test_frees;
void *gba_malloc(size_t size) {
    gba_test_allocations++;
    return gba_test_real_malloc(size);
}
void gba_free(void *data) {
    if (data) {
        gba_test_frees++;
    }
    gba_test_real_free(data);
}
#if GBA_USE_FS
int gba_test_fs_fail, gba_test_fs_apply;
size_t gba_fread(void *data, size_t size, size_t count, gba_file_t *file) {
    if (gba_test_fs_fail == 5 || (gba_test_fs_fail == 6 && gba_test_fs_apply)) {
        return 0;
    }
    return gba_test_real_fread(data, size, count, file);
}
size_t gba_fwrite(const void *data, size_t size, size_t count, gba_file_t *file) {
    return gba_test_real_fwrite(data, size, gba_test_fs_fail == 1 && count ? count - 1 : count, file);
}
int gba_file_sync(gba_file_t *file) {
    return gba_test_fs_fail == 2 ? -1 : gba_test_real_sync(file);
}
int gba_fclose(gba_file_t *file) {
    int result = gba_test_real_close(file);
    return gba_test_fs_fail == 3 ? -1 : result;
}
int gba_file_replace(const char *source, const char *destination) {
    return gba_test_fs_fail == 4 ? -1 : gba_test_real_replace(source, destination);
}
int gba_fseek(gba_file_t *file, long offset, int origin) {
    if (gba_test_fs_fail == 6 && offset == 32 && origin == SEEK_SET) {
        gba_test_fs_apply = 1;
    }
    return gba_test_real_seek(file, offset, origin);
}
#endif
