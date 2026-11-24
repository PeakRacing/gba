# GBA

An original C11 GBA emulator intended for embedded ports. The project follows the sibling NES project's naming, directory structure, port functions and full/half-framebuffer contract, with static ownership and streaming ideas from GBC. Core source is Apache 2.0. No external emulator core, Nintendo BIOS or commercial ROM is included.

**This uses the user-selected single fast core with scanline video.** ARM/Thumb, PSG/FIFO, cartridges, RTC, HLE and persistence are implemented and exercised. Reviewed timing differences remain visible under an exact precision policy. See [fast core](docs/fast-core.md) and [validation](docs/validation.md). Bounded game checks do not establish complete-game compatibility, and host speed is not MCU performance.

Build from the project root with xmake and MSVC:

```powershell
xmake f -m release -y
xmake build
xmake run gba-tests-full
./test/run.ps1
./test/run.ps1 -StrictHardware
./test/run.ps1 -SkipFetch -FastAcceptance
./test/video.ps1 -Strict
./test/sdl_roms.ps1 -ROMDirectory 'E:/Game/GBA汉化全集'
./test/sdl_roms.ps1 -Deep
./test/cross.ps1 -Toolchain 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin'
```

The strict command currently fails. The default runner checks local regressions, resident/stream equivalence and frameskip hardware equivalence, and separately reports the strict suite failures. Reference assets are fetched at fixed commits. Generated logs, JSON and PPM files are under `test/out/`. Cortex-M4 ELF files prove freestanding interface linkage; they are not bootable board firmware.

SDL2 system video/audio checks now cover eight privately supplied games for 600 simulated frames, with 120 restored frames compared against the original CPU, hardware, RAM, picture and PCM. All bounded checks pass; playback queue empty/full counters are zero in this run. RTC tick is held fixed for replay. These are menu/startup smoke checks, not complete-game compatibility or hardware audio validation. See [SDL2 results](docs/sdl-validation.md).

For SDL2 or SDL3, enter `sdl/sdl2` or `sdl/sdl3`, then run `xmake f -P . -m release -y`, `xmake build -P .`, and `xmake run -P . gba ROM.gba [BIOS.bin]`. The optional BIOS is borrowed and must contain exactly 16 KiB. Z/X map to A/B, A/S to L/R, arrows to the D-pad, Enter/Backspace to Start/Select. F5/F9 save/load state, F6 persists battery data, Esc exits normally, Ctrl+Q discards, and F10 discards/reloads after a fault. Failed saves retain the instance for retry.

The public interface is in `inc/gba.h`. `gba_init_static` borrows separate caller buffers, and `gba_load_rom` borrows ROM storage. `gba_load_stream` accepts an offset reader and uses four configurable 8 KiB pages; incomplete reads never commit a cache tag. Hardware memory remains necessary even with streaming. Default output is RGB565; `GBA_RAM_LACK=1` submits two 240×80 blocks. Display and PCM pointers are valid only during their callbacks.

Default sound output, filesystem and file save states are disabled. PSG/FIFO hardware still advances without sound output. Battery storage, runtime RTC and HLE are enabled. `rtthread/` contains an unintegrated template, not a tested board port. Board-specific display, input, audio and safe filesystem replacement must be supplied. Detailed contracts: [porting](docs/porting.md), [storage](docs/storage.md), [remaining work](docs/remaining.md).

## Portable optimization options

The MCU-qualified C11 changes include event batching, live instruction pages,
verified stable-loop acceleration, ARM/Thumb register spans, decoded IWRAM
blocks and exact scanline reuse. New options default to off to preserve the
existing memory budget. Enable the root xmake build with
`xmake f -m release --fast_core=y -y`, or run
`pwsh -File test/run.ps1 -SkipFetch -FastAcceptance -FastCore`. All core and
port translation units must use the same definitions. The row cache adds
about 85 KiB and decoded blocks add 21 KiB; neither is serialized in GAST v7.

See [configuration and standalone tests](docs/portable-optimizations.md) and
[synchronization validation](docs/optimization-sync-validation.md). Board FPS
refers to the previously sealed 408 MHz handheld run, not a new hardware test
of this repository. RT-Thread sessions, LCD scaling and audio queues remain
in the firmware port.

See [ARM32 dynarec and module layout](docs/dynarec_EN.md) for configuration, executable memory hooks and validation limits. The backend is disabled by default.
