# ARM32 dynarec and NES-style module layout

The public umbrella header now aggregates CPU, bus, timer/DMA, PPU, APU,
cartridge/RTC, BIOS and storage headers. Private inline helpers belong to their
modules. Compiler attributes use GBA_INLINE, GBA_FORCE_INLINE, GBA_WEAK and
GBA_ALIGN. Compiler definitions override platform defaults; SDL2 and SDL3 have
separate configuration headers. GBA_FAST_CORE selects the portable optimizations.
Legacy GBA_PORT_* options remain aliases and conflicting definitions are rejected.

GBA_DYNAREC=1 enables an independently implemented little-endian ARMv5TE/A32
code generator. It automatically selects the required CPU/event execution path
and replaces GBA_FAST_BLOCKS. Cortex-M, AArch64 and x64 backends are not included.
The default borrowed workspace is 256 KiB executable code plus 64 KiB metadata.
Bind it with gba_dynarec_bind() before execution; normal initialization signatures
and hardware state layout remain unchanged. No runtime heap allocation occurs.

ARM/Thumb operations emit A32 code. Four guest registers are selected from the
complete mixed block, including noncontiguous registers and SP/LR. RAM blocks
can contain 64 instructions across two validated pages; ROM blocks contain 32,
or 16 with memory operations. Conditional fallthrough can stay native. Taken
edges return a committed prefix, and bounded IWRAM loops can repeat natively.
Guarded stubs cover RAM, resident ROM and selected stable status reads. Signed
and odd signed-halfword loads, writeback and RAM multiple transfers retain
checked fallbacks. Writes into protected 32-byte code/fetch lines fall back before mutation. ROM
literals read live backing memory. Streaming never retains transient pointers.
Short IWRAM budgets can use an attached per-instruction checked entry; it consumes no independent hash way and falls back to C when unavailable. CPU
execution may span PCM sample points, but device replay retains every exact
sample and timer, PPU, sequencer, IRQ/DMA deadlines. TEST_MODE traces C execution;
generated code is validated separately. Optional cross-block prototypes are described below.
Native, memory-helper and fallback counts are reported separately when
GBA_DYNAREC_STATS=1. Statistics default to off and compile out of production;
the QEMU runner enables them for coverage and accepts --no-stats for validation
of the production path. Final speed measurements must disable all statistics.

GBA_DYNAREC_CHAIN=1 optionally retains one native call frame across cached
IWRAM blocks and avoids spilling an unchanged four-register mapping. The C
continuation still commits exact cycles and pipeline state. GBA_DYNAREC_DIRECT=1
requires CHAIN and can validate and enter full IWRAM successors without C,
including taken branches and changed register maps. Both options default to off.
Missing successors, generation/source/width mismatches, short budgets and partial
execution return to the established path. ROM successors still use C. Workspaces
below 32 KiB retain the original entry. This is not gpSP's complete direct engine.

The DIRECT prototype runs with GBA_DYNAREC_STATS=0 and retains its own direct_links
diagnostic counter. With statistics enabled it uses the C continuation so native
coverage counts are not silently incomplete. It has not been accepted as a
probe-free production performance configuration. Use --chain --direct --no-stats
for QEMU validation, including a nonzero direct-link assertion and complete
machine-state comparison. ROM MUL/register shifts now record exact per-instruction
internal cycles for FIFO replay; shifted writes to PC include their internal cycle.
Constant loads prefer MOV/MVN or MOV+ORR; patched failure targets remain literals.

The code port must implement writable/executable memory handling and data/instruction
cache publication. Unimplemented hooks reject binding. The ARM926 cache helper
requires privileged execution and an explicitly guaranteed executable RAM mapping;
it is not a generic userspace or MPU permission implementation. Cache recycling
and compilation occur outside generated code. CPU/DMA/HLE RAM writes invalidate
physical pages, including mirrors; direct caller edits require
`gba_dynarec_invalidate(gba, address, size)`. Reset/reload/state restoration invalidate
all blocks. Generated code, page generations and pointers are excluded from GAST v7.

Run `python test/dynarec.py --references --instruction-compare --label instructions`
for actual generated-code execution under QEMU ARM926. Omit instruction-compare
to exercise native spans, add `--stream --fast-core` for streamed ROMs, or add
`--rom-directory test/out/speed-roms --frames 900` for local commercial fixtures.
The test port compares CPU, hardware state, RAM, video and PCM and checks state
replay, short writes, replace failures and damaged files. ROMs are not distributed.

**Native ARM32 performance has not been accepted on hardware.** QEMU timings are
functional evidence only. Windows SDL2 still runs the portable interpreter.
Code generation and helper overhead may outweigh dispatch savings; measure a
fixed workload on the intended ARM32 processor with test/profile probes disabled.
The F1C firmware project has separate RAM evidence and recovery records. One
Metroid startup scene reached about 59.3 FPS; other games remain far below real
time. This is not full-speed/gameplay acceptance. NOR application and resource
partitions were not updated. See the firmware project comparison report.

The local notaz/gpsp revision 862e940b8c108fb0d29f9a1644be7a5360d7230d was a
design reference. No GPL emitter macros, code or assembly stubs were copied;
this implementation remains Apache-2.0. See [Chinese implementation notes](dynarec.md)
and [validation results](dynarec-validation.md) for interfaces and evidence.

## Optional experiments (all default to zero)

`GBA_DYNAREC_SOFT_TIMERS` relaxes CPU-only deadlines for unobservable audio timer
rollovers, retaining event-by-event device replay and hard IRQ/cascade/FIFO-DMA
boundaries. `GBA_DYNAREC_INTERIOR` reuses immutable ROM tails; early board results
regressed, so it is not a production selection. `GBA_DYNAREC_NEGATIVE` caches only
exact decoder rejections, never dynamic bus or budget failures.
`GBA_DYNAREC_MULTI_INLINE` emits ordinary RAM LDM/POP loads after whole-span guards;
PC, device and rejected accesses retain the C path. `GBA_DYNAREC_ROM_PREFIX` retains
short immutable-ROM prefixes with their own exact FIFO bounds near event deadlines.
`GBA_PPU_ORDERED` uses back-to-front overwrite without alpha blending; other scenes
retain the original compositor. `GBA_PPU_READ_DEDUP` separately deduplicates the
last VRAM/palette dependency block within each dirty scanline.

The corresponding test options are `--soft-timers --interior --negative-cache
--inline-multiple --rom-prefix --ordered-ppu --ppu-read-dedup`. Ordered-render tests
force the reference instance through the original compositor. Separate F1C host
tests compare cached/uncached, legacy/ordered and dedup variants at both color
depths. These are portable C11 core options; only executable RAM and cache
publication use platform hooks. Full-speed board acceptance remains open.


`GBA_DYNAREC_RAM_INLINE=1` emits guarded RAM reads selected by observed address
regions; changed effective addresses fall back before mutation.
`GBA_DYNAREC_ROM_CHAIN=1` extends the persistent call frame to immutable-ROM
successors; it requires CHAIN and excludes INTERIOR. It saves all pre-entry FIFO
state before branches can reset it. Neither option is a full gpSP-style native
ROM dispatcher; the shared continuation still commits each ROM block through C.
Both remain disabled by default.

`GBA_DYNAREC_EVENT_SLIP` defaults to 0 (exact events). A nonzero bound (up to 2048)
requires non-chained dynarec and permits a bounded block to finish before device
events commit. Instructions and their cycle charges still execute; CPU relative
IRQ/DMA/PPU/audio timing changes. This is an explicit accuracy tradeoff, not an
interpreter-equivalent mode. Test it using `--event-slip 1024 --fixtures-only
--replica-validation`; the report labels two independent engine replicas,
runtime/input/state roundtrip and determinism checks. Do not count those as
hardware accuracy or game compatibility acceptance. Frame APIs can complete an
atomic DMA spanning multiple vblanks; performance uses actual frame counters,
not API invocation counts. Full-speed target acceptance remains open.


## 2026-10-16: approximate timing and ROM continuations

All new experiments default off. EVENT_SLIP (1..2048) relaxes CPU/device observation boundaries; FAST_BUS requires it and replaces ROM FIFO replay with the legal per-fetch WAITCNT lower bound. Every executed instruction still pays a fetch cost; data/internal costs remain. WIDE_ROM requires FAST_BUS and excludes INTERIOR, extending blocks within one source page and allowing bounded ordinary-RAM loops. These modes change observable timing and are not accuracy acceptance.

STORE_INLINE requires MULTI_INLINE: all span, alignment and code-page guards precede STM/PUSH writes. PARTIAL_REGS maps each available ALU operand separately; it passed computation tests but gave no useful handheld speed gain. RECENT_BITS supports 6..10; values above 6 require 512 KiB metadata and DIRECT requires 6. A 1 MiB metadata arena supports 16384 slots.

COARSE_CHAIN requires FAST_BUS, ROM_CHAIN, DIRECT and excludes statistics and ROM_PREFIX. Cached guest registers persist across native ROM blocks; changed maps transfer registers. Pipeline/open-bus metadata is materialized on the final C exit, including a zero-instruction memory-guard exit. Device callbacks must wait until the A32 exit stub spills guest registers. ROM cannot change during a CPU call. The bounded tests compare finite scalar computation, including RAM writes and taken/not-taken edges; approximate game replicas test determinism and state restoration, not device timing equivalence. Actual performance requires board evidence with the exact RAM image identity.

MULTI_PC defaults off and requires MULTI_INLINE: guarded ordinary-RAM LDM/POP PC preserves ARMv4T mode/alignment and falls back atomically. LONG_LOOPS defaults off and requires WIDE_ROM: it removes the extra seven-iteration cap, retaining the worst-cycle budget and 255 executed-instruction limit. Finite scalar computation tests cover 127 iterations and the 255-instruction boundary; board speed remains a separate measurement. Current short handheld windows show no useful COARSE_CHAIN benefit; keep it disabled.

COARSE_CHAIN now accepts ordinary conditional-branch prefixes and IWRAM/ROM transitions while retaining RAM validation. LINK_CACHE defaults off, requires COARSE_CHAIN, and reuses the otherwise unused prefix_offset for a validated successor; native hits and C priming do not bypass validity checks. TINY_BLOCKS defaults off and requires COARSE_CHAIN, allowing single-instruction native spans. BL_PREFIX defaults off and requires DYNAREC: the first Thumb BL half computes LR without ending the block; the second half retains its branch semantics. This change itself does not relax timing. Board performance remains a separate measurement.

REFILL_MATCH defaults off and requires COARSE_CHAIN. Cross-block branches pay the actual empty-pipeline two-fetch cost, validated against 393216 bus cases. BULK_PREFILL defaults off and requires FAST_BUS with EVENT_SLIP >=64: checked RAM/resident-ROM pipeline reads are charged inside the same relaxed budget, and device callbacks wait for coherent guest registers. With LINK_CACHE, oversized IWRAM spans fall back rather than aliasing prefix_offset with the old bounded-prefix mechanism; the old snapshot fails its regression and the fix passes. OBJ_SPANS defaults off: ordinary sprites use tile-row spans, while affine/mosaic sprites retain the general renderer. Color, evaluation budget, window, blending and cache equivalence tests pass.


Further 2026-10-16 experiments remain off by default. LINE_INVALIDATE bumps a RAM
page generation only for writes touching a protected 32-byte code line, including
pipeline lookahead. FIXED_MAP=1 pins guest r0-r3; mode 2 uses r0/r1/r6/r14 for ARM.
FASTCALL uses a private caller-clobber ARM entry to avoid repeated AAPCS saves in
each generated block. The enclosing GCC caller preserves its ABI once; compile
with -fomit-frame-pointer and disable CHAIN/INTERIOR/ROM_PREFIX. It uses the real
thread stack and does not mask IRQs. ROM_TRACE requires FAST_BUS/WIDE_ROM and
EVENT_SLIP>=256, excludes INTERIOR/ROM_PREFIX, and emits up to 120 straight-line
instructions across immutable resident-ROM pages. Code/snapshot bounds, GPIO,
ROM capacity and cycle bounds remain checked; cross-page native backedge fusion
is excluded. This is still explicitly approximate timing. SORTED_BLEND is a
separate exact rendering experiment retaining the previous distinct layer for
blending and the same-layer OBJ replacement rule. Target-board measurements are
required for speed claims; QEMU checks alone do not establish performance.


EVENT_SLIP must retain hard deadlines for DMA startup and armed HBlank/VBlank
memory transfers, including the frame wrap. BULK_PREFILL obeys the same limits.
Old approximate candidates could read a function pointer before its DMA copy in
Golden Sun and are withdrawn. A frozen pre-fix source fails the copy-before-load
regression; 96 functional DMA cases and all-line deadline checks pass after repair.

TRACE_LINKS is off by default, requires ROM_TRACE, and excludes CHAIN. It joins
known same-region resident-ROM B/BL edges and complete Thumb BL pairs within one
native trace, storing each source PC beside the opcode snapshot for prefix exits.
BX LR continuation compares the runtime LR against a predicted connected call;
a mismatch takes the ordinary committed BX exit. Nested predictions are capped
at 16. Every connected edge charges real refill costs in both bounds and actual
work, with at most 120 instructions and all DMA hard deadlines retained. Conditions
and memory guards can exit any prefix; unjoined contiguous loops retain their
existing bounded native repetition. Target-board speed remains unmeasured.


TRACE_CONTEXT is off by default and requires TRACE_LINKS. ROM-entry BX blocks
include the actual source-register target in metadata lookup and hot-cache checks.
Generated code still compares the full target, including the instruction-set bit;
a mismatch takes the ordinary BX exit without executing a predicted instruction.
The context uses prefix_offset only where ROM_PREFIX is prohibited. Narrow-budget
ROM entries return directly and cannot reinterpret it as an IWRAM prefix offset.
Alternating targets, LR changes, forced native target/mode mismatches and tight
budgets have directed checks. No complete same-cycle board speed result yet.

FRONT_CULL is off by default. On rows without second-layer blend dependencies it
renders OBJ/BG front to back and skips covered background samples. Alpha or
semitransparent-object rows retain the original compositor. Object evaluation
budgets and resolved windows remain independent; affine/mosaic paths retain the
coverage guard. RGB565/32-bit, cache, OBJ spans and ordering combinations are
pixel-compared; speed requires an independent same-source board comparison.


HARD_CACHE is off by default and requires EVENT_SLIP. It caches the hard DMA/IRQ
deadline relative to the committed device clock; each lookup subtracts current
cpu_pending. CPU-run entry, every CPU/device synchronization, full invalidation
and reset clear it; a changed committed clock also misses. Ordinary RAM writes
and native blocks cannot arm DMA, so four-channel scans need not repeat per block.
MMIO writes synchronize before changing control, including zero-cycle syncs.
Tests retain the real stale-DMA-load regression and compare cache-on/off engines'
complete state, RAM and PCM streams under identical inputs.


GBA_PPU_OBJ_INDEX and GBA_PPU_OAM_DIRTY default to zero and are independent
portable C11 experiments (also available without GBA_DYNAREC). OBJ_INDEX
retains 20 eight-line OAM candidate bands, updates them on actual latch changes,
and preserves original ordering and evaluation budgets; initial live OAM uses
the original selector. Its first F1C200S screening was slower, so it stays off.
OAM_DIRTY tracks 256 words in eight bitsets and copies only modified words at
the original latch boundary. Initial/invalidated snapshots copy all OAM. CPU,
DMA and native video writes share the tracker; ignored byte stores stay ignored.
Reset, state restore and external gba_video_invalidate clear the derived cache.
Neither cache is serialized or changes GAST identity. Selection/full latch
differentials, pixel matrices and real ARM execution are separate from target
performance measurements. HARD_CACHE's same-cycle board screening improved
only about 0.7 to 1.9 percent; it also remains off by default.


F1C200S 408 MHz D176/D177 OAM_DIRTY on/off screening (OBJ_INDEX off,
no frameskip, raw 32 kHz stereo, probes off, 12 s warmup + about 30 s):
Mario 24.007/23.524 simulated FPS, Zelda 25.420/24.105, fixed Emerald room
11.873/11.565. The control Zelda presentation window includes one additional
frame (24.138 presented FPS). These small gains do not reach 59.73 FPS or
constitute three-round final acceptance. OAM_DIRTY remains opt-in. Exact ARM
reference, 900-frame game/state replay, 16/32-bit pixel matrices, strict
difference gate and minimal Cortex-M4 coverage passed. Original SPL/full NOR
and menu boot were restored; no NOR application/resources were updated.
Firmware evidence: F:/code/codeup/f1c_rtthread/tools/host_test/results/gba-dynarec-oamdirty-20261016.
