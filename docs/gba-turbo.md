# GBA Turbo execution backend

`GBA_DYNAREC_TURBO` is an optional ARM926 native execution backend. It keeps
the portable C11 interpreter and the precise `GBA_DYNAREC_PERSISTENT` backend
as independent compatibility paths. Turbo is disabled by default and is
mutually exclusive with the precise persistent backend.

The implementation extends the existing fixed-register A32 executor rather
than changing the guest-visible bus model. It provides forward unconditional
branch continuation inside one generated trace, bounded eager compilation and
linking of known direct successors, corrected shared cold exits for MMIO and
multi-transfer rejects, EWRAM and post-header ROM execution, and an optional
32 KiB-page read/write TLB.

The generated-code arena remains caller-owned. The firmware performance
profile uses 8 MiB of code space and 2 MiB of metadata. Incoming-edge links use
32-bit indices, so the metadata table can grow through the full 16 K-slot
capacity without truncating a link.

## Build configuration

```text
GBA_DYNAREC=1
GBA_DYNAREC_TURBO=1
GBA_DYNAREC_TURBO_TRACE_INSNS=16
GBA_DYNAREC_TURBO_PRELINK=0
GBA_DYNAREC_TURBO_REGISTER_MAP=3
GBA_DYNAREC_PERSISTENT_INDIRECT_BITS=10
GBA_DYNAREC_PERSISTENT_READ_WAIT=1
GBA_DYNAREC_CODE_SIZE=8388608
GBA_DYNAREC_DATA_SIZE=2097152
GBA_PPU_TILE_DIRECT=1
GBA_PPU_OBJ_SPANS=1
GBA_PPU_OAM_DIRTY=1
GBA_PPU_SCANLINE_FAST=1
GBA_PPU_SCANLINE_IDENTITY=1
GBA_PPU_SCANLINE_PALETTE_DEDUP=1
GBA_PPU_SCANLINE_ZERO_ROWS=1
```

Turbo enables the precise engine's useful translator features by default:
literal loads, direct Thumb BL, register multiple transfers, shared exits,
bounded branch superblocks, ROM windows and inline refill. The exact
persistent backend remains selected with `GBA_DYNAREC_PERSISTENT=1` and
`GBA_DYNAREC_TURBO=0`.

`GBA_DYNAREC_TURBO_TLB=1` enables the 32 KiB-page TLB. It is retained as a
build option but is not selected by the measured firmware profile: the first
QEMU A/B result was slower than the shared mapper, and the board screen was
also slightly slower. The second implementation shares one compact cost table
and no longer requires the six sparse read/write cost tables, but still loses
5.8% in the large-cache fixed Mario replay, so it remains disabled.

Register map 3 caches Thumb `r0,r1,r2,r4,r6,r7`. It replaces the original
`r0..r5` map for Turbo only. Across the four fixed gameplay states the host
replays were neutral to 1.2% faster while generated code shrank by roughly one
third in three scenes. The precise persistent backend retains the original
map. OAM band indexing remains disabled because its isolated fixed-scene test
was 1.0% slower than scanline rendering alone.

APU sample advancement now splits a batched CPU interval at every 32 kHz PCM
boundary. The previous single-sample update lost samples whenever one hardware
event covered more than one sample period. Whole/split replay checks produce
the same sample count, phase, channel position and sequencer state.

## Host measurements

The following are QEMU ARM926 wall times for the same 60-frame replay, state,
input manifest, display options and FAST_BUS/32K event-slip configuration.
They are screening results, not board FPS claims.

| Fixed scene | Precise persistent | Turbo | Time reduction |
|---|---:|---:|---:|
| Mario | 9.358 s | 4.809 s | 48.6% |
| Zelda | 8.429 s | 7.868 s | 6.7% |
| Emerald | 18.425 s | 12.888 s | 30.1% |
| Metroid | 9.006 s | 6.346 s | 29.5% |

The tuned Mario trace compiles about 28 A32 host words per guest instruction
in the sampled ten-frame profile, down from the earlier compact engine's
about 42.9. EWRAM and post-header ROM support also removed the dominant
Emerald rejection path.

Board screening found two effects that host timing did not predict. Eager
four-target linking raised Emerald's startup rate from 6.41 to 7.47 FPS when
disabled, so eager linking is now opt-in. The 32 KiB TLB was neutral to 1.6%
slower. A 16-instruction trace remained the best target-scene choice; an
8-instruction trace dropped fixed Mario gameplay from 20.07 to 18.98 FPS.

With the final candidate, a ten-second in-game old/new measurement gave Mario
`20.05 FPS` versus `19.06 FPS`, and Emerald `21.47 FPS` versus `21.22 FPS`.
Enabling validated Mode 1 tile-direct, OBJ-span and OAM-dirty rendering raised
those same screening windows to Mario `21.74 FPS` and Emerald `23.82 FPS`.
These are short board screens, not the required three-minute acceptance sets.

With the board-size 8 MiB/2 MiB caches, a 30-frame fixed Mario replay measured
5.75 s with the generic scanline compositor and 5.47 s with the scanline fast
path, a 4.9% reduction. Enabling OBJ_INDEX in the same comparison regressed to
5.52 s. `-fomit-frame-pointer` and the direct 2x RGB565 row writer are included
in the current firmware build; their board effect remains pending.

## Validation

- Exact interpreter differential: `0x1886` assertions, zero failures with
  turbo enabled, including EWRAM execution and fused-branch MMIO/multi rejects.
- Exact persistent regression: `0x1874` assertions, zero failures.
- OAM index selection: 2,767,360 checks per variant, four cache/CPU variants
  equal, 2,074,561 indexed candidates selected.
- Thirteen pinned GBA reference ROMs: five-frame interpreter equivalence and
  FAST_BUS replica replay pass.
- Fixed Mario and Emerald: 600-frame FAST_BUS replica replay pass with the
  final PPU paths.
- The strict suite retains 685 reviewed timing differences and reports zero
  unreviewed failures.
- The one standalone `video.ps1` difference (`Layer toggle 2`, 958 pixels) is
  in the generic non-turbo renderer. It is retained as an open accuracy item
  and is not counted as a pass.

The required three-by-60-second, four-scene 59.5 FPS acceptance is still
unpassed. On 2026-10-18 COM30 was confirmed as the CH340 handheld UART; the
old Win32_SerialPort query had missed it. After physical reconnection the
original SPL and full 16 MiB NOR were SHA256 verified and the original NOR
menu answered on COM30. A subsequent complete RAM experiment also restored
and verified SPL, the full NOR image and normal menu boot.

The probe-free board sequence was baseline -> fast RGB565 row writer -> row
writer plus LTO -> baseline, at 408 MHz, no frameskip, raw 32 kHz stereo. Each
scene used the same state/input/RTC, 120 guest warm-up frames and 240 measured
frames. Every candidate had a complete RAM readback before execution.

| Scene | Baseline mean FPS | Fast row FPS | Fast row + LTO FPS |
|---|---:|---:|---:|
| Mario | 20.38 | 20.64 | 20.61 |
| Zelda | 28.90 | 29.31 | 30.10 |
| Emerald | 22.04 | 23.06 | 23.27 |
| Metroid | 19.65 | 19.97 | 20.04 |

Presented FPS equaled simulated FPS; all scenes still had PCM underruns and
silence insertion. Baseline drift was below 0.4%. These short screens do not
establish the final real-time target, and the fast build options remain
opt-in. The measured row-writer/LTO BIN SHA256 is
`7cb9aa23a2704552d00f2bf663a19ad8758b9855487d40c932a34a860e37443e`.
Raw UART, sealed sources, BIN/ELF/MAP, RAM readback and recovery evidence are
in `F:/code/codeup/tiny_box/.gba_turbo_evidence/compiler-screen-20261018`.

A yield-only control change was rejected before execution: delay(1) is 1 ms
at the configured 1000 Hz tick, and yield cannot guarantee the lower-priority
idle watchdog feed/cleanup. The bounded idle sleep was restored. Another
experiment allowed persistent execution in an active HLE IntrWait IRQ handler.
It passed exact CPU/cache/state tests, but reducing Zelda native entries by
about 77% did not produce a stable throughput gain; persistent block execution
increased. That candidate and its focused tests were archived and withdrawn
from the delivery core. QEMU candidate-only reports now explicitly identify
performance-only runs without interpreter comparison.
