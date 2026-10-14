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
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#if GBA_SDL_VERSION == 3
#include <SDL3/SDL.h>
#define GBA_SDL_Z SDLK_Z
#define GBA_SDL_X SDLK_X
#define GBA_SDL_A SDLK_A
#define GBA_SDL_S SDLK_S
#define GBA_SDL_Q SDLK_Q
#else
#include <SDL.h>
#define SDL_KMOD_CTRL KMOD_CTRL
#define GBA_SDL_Z SDLK_z
#define GBA_SDL_X SDLK_x
#define GBA_SDL_A SDLK_a
#define GBA_SDL_S SDLK_s
#define GBA_SDL_Q SDLK_q
#endif
#include "gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static SDL_Window *gba_window;
static SDL_Renderer *gba_renderer;
static SDL_Texture *gba_texture;
static SDL_AudioDeviceID gba_audio_device;
#if GBA_SDL_VERSION == 3
static SDL_AudioStream *gba_audio_stream;
#endif
static uint32_t gba_audio_underruns, gba_audio_overruns;
static int gba_audio_started, gba_video_pending;
#if GBA_TEST_MODE
#include "test_capture.h"
#endif

uint32_t gba_get_ticks(void) {
#if GBA_TEST_MODE
    /* Deterministic replay tests inject a stationary RTC clock. Playback pacing
     * continues to use SDL's real performance counter. */
    if (getenv("GBA_TEST_REPLAY")) {
        return 0;
    }
#endif
    return (uint32_t)SDL_GetTicks();
}
int gba_initex(gba_t *g) {
    (void)g;
    return 0;
}
int gba_deinitex(gba_t *g) {
    (void)g;
    return 0;
}
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels) {
#if GBA_TEST_MODE
    gba_sdl_test_draw(x1, y1, x2, y2, pixels);
#endif
    SDL_Rect rect = {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
#if GBA_SDL_VERSION == 3
    if (!SDL_UpdateTexture(gba_texture, &rect, pixels, rect.w * (int)sizeof(*pixels))) {
        return GBA_ERROR;
    }
#else
    if (SDL_UpdateTexture(gba_texture, &rect, pixels, rect.w * (int)sizeof(*pixels))) {
        return GBA_ERROR;
    }
#endif
    gba_video_pending = 1;
    return 0;
}
int gba_sound_output(uint8_t *buffer, size_t size) {
#if GBA_TEST_MODE
    gba_sdl_test_sound(buffer, size);
#endif
#if GBA_SDL_VERSION == 3
    int queued = SDL_GetAudioStreamQueued(gba_audio_stream);
    if (queued < 0) {
        return GBA_ERROR;
    }
    if (gba_audio_started && !queued) {
        gba_audio_underruns++;
    }
    if ((unsigned)queued > GBA_APU_SAMPLE_RATE * 4 / 4) {
        gba_audio_overruns++;
        return 0;
    }
    if (!SDL_PutAudioStreamData(gba_audio_stream, buffer, (int)size)) {
        return GBA_ERROR;
    }
    if (!gba_audio_started && (unsigned)queued + size >= GBA_APU_SAMPLE_RATE * 4 / 10) {
        if (!SDL_ResumeAudioDevice(gba_audio_device)) {
            return GBA_ERROR;
        }
        gba_audio_started = 1;
    }
#else
    uint32_t queued = SDL_GetQueuedAudioSize(gba_audio_device);
    if (gba_audio_started && !queued) {
        gba_audio_underruns++;
    }
    if (queued > GBA_APU_SAMPLE_RATE * 4 / 4) {
        gba_audio_overruns++;
        return 0;
    }
    if (SDL_QueueAudio(gba_audio_device, buffer, (uint32_t)size)) {
        return GBA_ERROR;
    }
    if (!gba_audio_started && queued + size >= GBA_APU_SAMPLE_RATE * 4 / 10) {
        SDL_PauseAudioDevice(gba_audio_device, 0);
        gba_audio_started = 1;
    }
#endif
    return 0;
}
void gba_state_loaded(gba_t *g) {
    (void)g;
#if GBA_SDL_VERSION == 3
    SDL_PauseAudioDevice(gba_audio_device);
    SDL_ClearAudioStream(gba_audio_stream);
#else
    SDL_PauseAudioDevice(gba_audio_device, 1);
    SDL_ClearQueuedAudio(gba_audio_device);
#endif
    gba_audio_started = 0;
}
static int gba_sdl_storage(gba_t *g, const char *path, int operation) {
    /* Disk synchronization can outlast the playback queue. Preserve it while paused;
     * successful state loading clears it through gba_state_loaded(). */
#if GBA_SDL_VERSION == 3
    if (!SDL_PauseAudioDevice(gba_audio_device)) {
        return GBA_ERROR;
    }
#else
    SDL_PauseAudioDevice(gba_audio_device, 1);
#endif
    int result = operation == 0   ? gba_persist_save(g)
                 : !path[0]       ? GBA_ERROR_FORMAT
                 : operation == 1 ? gba_state_save_file(g, path)
                                  : gba_state_load_file(g, path);
    gba_rtc_rebase(g);
    if (gba_audio_started) {
#if GBA_SDL_VERSION == 3
        if (!SDL_ResumeAudioDevice(gba_audio_device) && !result) {
            result = GBA_ERROR;
        }
#else
        SDL_PauseAudioDevice(gba_audio_device, 0);
#endif
    }
    return result;
}
void gba_frame(gba_t *g) {
    (void)g;
    if (!gba_video_pending) {
        return;
    }
    SDL_SetRenderDrawColor(gba_renderer, 0, 0, 0, 255);
    SDL_RenderClear(gba_renderer);
#if GBA_SDL_VERSION == 3
    SDL_RenderTexture(gba_renderer, gba_texture, NULL, NULL);
#else
    SDL_RenderCopy(gba_renderer, gba_texture, NULL, NULL);
#endif
    SDL_RenderPresent(gba_renderer);
    gba_video_pending = 0;
}
static uint16_t gba_key(SDL_Keycode key) {
    switch (key) {
    case GBA_SDL_Z:
        return GBA_KEY_A;
    case GBA_SDL_X:
        return GBA_KEY_B;
    case SDLK_RETURN:
        return GBA_KEY_START;
    case SDLK_BACKSPACE:
        return GBA_KEY_SELECT;
    case SDLK_RIGHT:
        return GBA_KEY_RIGHT;
    case SDLK_LEFT:
        return GBA_KEY_LEFT;
    case SDLK_UP:
        return GBA_KEY_UP;
    case SDLK_DOWN:
        return GBA_KEY_DOWN;
    case GBA_SDL_A:
        return GBA_KEY_L;
    case GBA_SDL_S:
        return GBA_KEY_R;
    default:
        return 0;
    }
}
static int gba_sdl_init(void) {
#if GBA_SDL_VERSION == 3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        return -1;
    }
    gba_window = SDL_CreateWindow("GBA", GBA_WIDTH * 3, GBA_HEIGHT * 3, SDL_WINDOW_RESIZABLE);
    if (!gba_window) {
        return -1;
    }
    gba_renderer = SDL_CreateRenderer(gba_window, NULL);
    if (!gba_renderer || !SDL_SetRenderLogicalPresentation(
                             gba_renderer, GBA_WIDTH, GBA_HEIGHT, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        return -1;
    }
#else
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        return -1;
    }
    gba_window = SDL_CreateWindow(
        "GBA", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, GBA_WIDTH * 3, GBA_HEIGHT * 3, SDL_WINDOW_RESIZABLE);
    if (!gba_window) {
        return -1;
    }
    gba_renderer = SDL_CreateRenderer(gba_window, -1, 0);
    if (!gba_renderer || SDL_RenderSetLogicalSize(gba_renderer, GBA_WIDTH, GBA_HEIGHT)) {
        return -1;
    }
    SDL_RenderSetIntegerScale(gba_renderer, SDL_TRUE);
#endif
    gba_texture =
        SDL_CreateTexture(gba_renderer, GBA_COLOR_DEPTH == 16 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, GBA_WIDTH, GBA_HEIGHT);
    if (!gba_texture) {
        return -1;
    }
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = GBA_APU_SAMPLE_RATE;
#if GBA_SDL_VERSION == 3
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 2;
    gba_audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    gba_audio_stream = SDL_CreateAudioStream(&spec, &spec);
    if (!gba_audio_device || !gba_audio_stream || !SDL_BindAudioStream(gba_audio_device, gba_audio_stream)) {
        return -1;
    }
    SDL_PauseAudioDevice(gba_audio_device);
#else
    spec.format = AUDIO_S16LSB;
    spec.channels = 2;
    spec.samples = 1024;
    gba_audio_device = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    if (!gba_audio_device) {
        return -1;
    }
#endif
    return 0;
}
static void gba_sdl_close(void) {
#if GBA_SDL_VERSION == 3
    if (gba_audio_stream) {
        SDL_DestroyAudioStream(gba_audio_stream);
    }
#endif
    if (gba_audio_device) {
        SDL_CloseAudioDevice(gba_audio_device);
    }
    if (gba_texture) {
        SDL_DestroyTexture(gba_texture);
    }
    if (gba_renderer) {
        SDL_DestroyRenderer(gba_renderer);
    }
    if (gba_window) {
        SDL_DestroyWindow(gba_window);
    }
    SDL_Quit();
}
static int gba_sdl_main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: gba ROM.gba [BIOS.bin] [--frames N]\nZ/X A/B; A/S L/R; Enter/Backspace Start/Select; "
                        "F5/F9 state; F6 save; Esc exit; Ctrl+Q discard\n");
        return 1;
    }
    if (gba_sdl_init()) {
        fprintf(stderr, "%s\n", SDL_GetError());
        gba_sdl_close();
        return 1;
    }
    gba_t *g = gba_init();
    uint8_t *bios = NULL;
    unsigned frame_limit = 0;
    if (!g) {
        gba_sdl_close();
        return 1;
    }
    int result = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = (unsigned)strtoul(argv[++i], NULL, 10);
            continue;
        }
        FILE *file = (FILE *)gba_fopen(argv[i], "rb");
        bios = (uint8_t *)gba_malloc(0x4000);
        if (!file || !bios) {
            result = GBA_ERROR_IO;
            if (file) {
                gba_fclose(file);
            }
            break;
        }
        size_t n = gba_fread(bios, 1, 0x4000, file);
        int extra = fgetc(file);
        int close_error = gba_fclose(file);
        if (n != 0x4000 || extra != EOF || close_error) {
            result = GBA_ERROR_FORMAT;
            break;
        }
        result = gba_set_bios(g, bios, 0x4000);
        if (result) {
            break;
        }
    }
    if (!result) {
        result = gba_load_file(g, argv[1]);
    }
#if GBA_TEST_MODE
    if (!result) {
        result = gba_sdl_test_input_load();
    }
    if (!result && getenv("GBA_TEST_LOAD_INITIAL")) {
        result = gba_state_load_file(g, getenv("GBA_TEST_LOAD_INITIAL"));
        fprintf(stderr, "test initial_load=%d\n", result);
    }
#endif
    if (result) {
        fprintf(stderr, "load failed: %d\n", result);
        gba_unload_rom_discard(g);
        gba_deinit(g);
        gba_free(bios);
        gba_sdl_close();
        return 1;
    }
    char state[GBA_PATH_MAX];
    if (snprintf(state, sizeof(state), "%s.state", argv[1]) >= (int)sizeof(state)) {
        state[0] = 0;
    }
    uint16_t keys = 0;
    int fault_reported = 0;
#if GBA_TEST_MODE
    unsigned save_at = getenv("GBA_TEST_SAVE_AT") ? (unsigned)strtoul(getenv("GBA_TEST_SAVE_AT"), NULL, 10) : 0;
    unsigned load_at = getenv("GBA_TEST_LOAD_AT") ? (unsigned)strtoul(getenv("GBA_TEST_LOAD_AT"), NULL, 10) : 0;
    int saved = 0, loaded = 0;
    unsigned auto_input = getenv("GBA_TEST_PLAY") != NULL;
    uint32_t test_frames = 0, test_cycles = 0, test_pc = 0;
    unsigned reloads = getenv("GBA_TEST_RELOADS") ? (unsigned)strtoul(getenv("GBA_TEST_RELOADS"), NULL, 10) : 0;
    unsigned reloaded = 0;
    int test_reported = 0;
#endif
    int done = 0, discard = 0, exit_pending = 0;
    uint64_t deadline = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();
    uint64_t frame_ticks = frequency * GBA_CYCLES_PER_FRAME / GBA_CPU_CLOCK;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#if GBA_SDL_VERSION == 3
            if (event.type == SDL_EVENT_QUIT) {
                exit_pending = 1;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                keys = 0;
            }
            if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
                continue;
            }
            SDL_Keycode key = event.key.key;
            int down = event.type == SDL_EVENT_KEY_DOWN;
#else
            if (event.type == SDL_QUIT) {
                exit_pending = 1;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                keys = 0;
            }
            if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
                continue;
            }
            SDL_Keycode key = event.key.keysym.sym;
            int down = event.type == SDL_KEYDOWN;
#endif
            if (down) {
                keys |= gba_key(key);
            } else {
                keys &= (uint16_t)~gba_key(key);
            }
            if (!down || event.key.repeat) {
                continue;
            }
            if (key == SDLK_ESCAPE) {
                exit_pending = 1;
            }
            if (key == GBA_SDL_Q && (SDL_GetModState() & SDL_KMOD_CTRL)) {
                discard = done = 1;
            }
            if (key == SDLK_F10) {
                result = gba_unload_rom_discard(g);
                if (!result) {
                    result = gba_load_file(g, argv[1]);
                }
                if (!result) {
                    gba_state_loaded(g);
                    fault_reported = 0;
                }
                fprintf(stderr, "reload: %d\n", result);
            }
            if (key == SDLK_F5 || key == SDLK_F9 || key == SDLK_F6) {
                result = gba_sdl_storage(g, state, key == SDLK_F6 ? 0 : key == SDLK_F5 ? 1 : 2);
                deadline = SDL_GetPerformanceCounter();
                fprintf(stderr, "storage: %d\n", result);
            }
        }
#if GBA_TEST_MODE
        uint16_t scripted = 0;
        if (gba_sdl_test_inputs) {
            scripted = gba_sdl_test_keys(g->machine.frames);
        } else if (auto_input) {
            unsigned frame = g->machine.frames;
            if (frame >= 120 && frame < 124) {
                scripted = GBA_KEY_START;
            } else if (frame >= 180 && frame % 60 < 4) {
                scripted = GBA_KEY_A;
            }
        }
        gba_set_keys(g, keys | scripted);
#else
        gba_set_keys(g, keys);
#endif
        if (!g->fault) {
            gba_run_frame(g);
        }
        if (g->fault && !fault_reported) {
            fprintf(stderr, "emulation fault: %d PC=%08x\n", g->fault, g->machine.cpu.r[15]);
            exit_pending = 1;
            fault_reported = 1;
        }
#if GBA_TEST_MODE
        test_frames = g->machine.frames;
        test_cycles = g->machine.cycles;
        test_pc = g->machine.cpu.r[15];
        if (getenv("GBA_TEST_REPLAY")) {
            gba_sdl_test_replay(g, save_at, load_at, loaded);
        }
        if (save_at && !saved && g->machine.frames == save_at) {
            result = gba_sdl_storage(g, state, 1);
            deadline = SDL_GetPerformanceCounter();
            saved = 1;
            fprintf(stderr, "test save=%d\n", result);
            if (result) {
                discard = done = 1;
            }
        }
        if (load_at && !loaded && g->machine.frames == load_at) {
            result = gba_sdl_storage(g, state, 2);
            deadline = SDL_GetPerformanceCounter();
            loaded = 1;
            fprintf(stderr, "test load=%d\n", result);
            if (result) {
                discard = done = 1;
            }
        }
#endif
        if (frame_limit && g->machine.frames >= frame_limit) {
            exit_pending = 1;
        }
        if (exit_pending) {
#if GBA_TEST_MODE
            size_t battery_size = gba_save_size(g);
            uint32_t battery_hash = gba_sdl_test_hash(2166136261u, g->memory.save, battery_size);
            if (frame_limit && !test_reported) {
                if (getenv("GBA_TEST_SAVE_FINAL") && state[0]) {
                    char final_state[GBA_PATH_MAX];
                    if (snprintf(final_state, sizeof(final_state), "%s.final", state) < (int)sizeof(final_state)) {
                        int final_result = gba_sdl_storage(g, final_state, 1);
                        fprintf(stderr, "test final_save=%d\n", final_result);
                        if (final_result) {
                            result = final_result;
                            discard = done = 1;
                            break;
                        }
                    }
                }
                gba_sdl_test_report(test_frames, test_cycles, test_pc);
                unsigned nonempty = 0;
                for (size_t i = 0; i < battery_size; i++) {
                    nonempty += g->memory.save[i] != 0xff;
                }
                fprintf(stderr, "test battery_bytes=%zu nonempty=%u hash=%08x\n", battery_size, nonempty, battery_hash);
                test_reported = 1;
            }
#endif
            result = gba_unload_rom(g);
            if (!result) {
#if GBA_TEST_MODE
                if (frame_limit && reloaded < reloads) {
                    result = gba_load_file(g, argv[1]);
                    uint32_t restored = gba_sdl_test_hash(2166136261u, g->memory.save, gba_save_size(g));
                    if (!result && (gba_save_size(g) != battery_size || restored != battery_hash)) {
                        result = GBA_ERROR_FORMAT;
                    }
                    fprintf(stderr, "test reload=%u result=%d\n", ++reloaded, result);
                    if (!result) {
                        gba_state_loaded(g);
                        frame_limit = 180;
                        exit_pending = 0;
                        saved = loaded = 1;
                        deadline = SDL_GetPerformanceCounter();
                        continue;
                    }
                }
#endif
                done = 1;
            } else {
                fprintf(stderr, "exit/save failed: %d; instance retained. F6 retry, Ctrl+Q discard.\n", result);
                exit_pending = 0;
                if (frame_limit) {
                    discard = done = 1;
                }
            }
        }
        deadline += frame_ticks;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < deadline) {
            SDL_Delay((uint32_t)((deadline - now) * 1000 / frequency));
        } else if (now - deadline > frame_ticks * 4) {
            deadline = now;
        }
    }
    fprintf(stderr, "audio underruns=%u overruns=%u\n", gba_audio_underruns, gba_audio_overruns);
#if GBA_TEST_MODE
    if (!test_reported) {
        gba_sdl_test_report(test_frames, test_cycles, test_pc);
    }
#endif
    if (discard) {
        gba_unload_rom_discard(g);
    }
    gba_deinit(g);
    gba_free(bios);
    gba_sdl_close();
#if GBA_TEST_MODE
    if (gba_sdl_test_replay_failed) {
        result = GBA_ERROR;
    }
#endif
    return result || discard ? 1 : 0;
}
int main(int argc, char **argv) {
#ifdef _WIN32
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) {
        return 1;
    }
    char **utf8 = (char **)gba_malloc((size_t)(count + 1) * sizeof(*utf8));
    if (!utf8) {
        LocalFree(wide);
        return 1;
    }
    gba_memset(utf8, 0, (size_t)(count + 1) * sizeof(*utf8));
    int result = 1, converted = 1;
    for (int i = 0; i < count; i++) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        utf8[i] = size ? (char *)gba_malloc((size_t)size) : NULL;
        if (!utf8[i] || !WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, utf8[i], size, NULL, NULL)) {
            converted = 0;
            break;
        }
    }
    if (converted) {
        result = gba_sdl_main(count, utf8);
    }
    for (int i = 0; i < count; i++) {
        gba_free(utf8[i]);
    }
    gba_free(utf8);
    LocalFree(wide);
    (void)argc;
    (void)argv;
    return result;
#else
    return gba_sdl_main(argc, argv);
#endif
}
