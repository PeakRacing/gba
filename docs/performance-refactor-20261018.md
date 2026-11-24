# Optional performance candidates (2026-10-18)

All new candidates remain disabled by default. No 59.5 FPS board acceptance has been achieved.

| Option | Scope | Limits |
| --- | --- | --- |
| `GBA_DYNAREC_PAGE_INDEX` | Physical code-page reverse membership for persistent/Turbo invalidation | Caller arena, no heap; legacy masks remain conservative until full cache reset |
| `GBA_DYNAREC_TURBO_LIGHT` | Literal pools, native long multiply, instance-relative prefetch stores | Experimental primitives, not a completed independent trace IR; initial board candidate regressed |
| `GBA_PPU_SCANLINE_EFFECTS` | Brighten/darken window segments on text/affine fast renderer | Alpha and unsupported OBJ combinations retain the general renderer |
| `GBA_PPU_SCANLINE_SPANS` | Separate 4/8bpp text entries, packed 4bpp tile rows, segment palette dependencies | Requires fast scanlines; pixel exactness checked, board speed pending |
| `GBA_DYNAREC_TURBO_LOCAL_ALU` | One temporary guest register in consecutive unconditional ALU runs | Flush before all side effects, conditions and exits; board performance pending |
| `GBA_APU_LAZY_MIX` | Read mixer control snapshot only when a PCM sample is due | Same per-call snapshot lifetime and sample boundaries; board speed pending |

The native caller control budget is a correctness contract shared by all native backends, independent of LIGHT. EVENT_SLIP cannot expand that caller budget. A scalar instruction already started may finish across the boundary; ADD/B fixtures return 4096–4098 cycles for a 4096-cycle request. Derived budget state is cleared at reset/state load and does not enter GAST v7.

## Reproducible checks

```text
python test/scanline_effects.py --text-spans --results-dir <new-output-directory>
python test/scanline_effects.py --text-spans --dependency-dedup --results-dir <another-new-directory>
python test/apu_lazy.py --results-dir <new-output-directory>
python test/header_check.py --results <new-json-path>
python test/dynarec.py --turbo --turbo-light --page-index --turbo-trace-insns 16 --turbo-reg-map 3 --references --scanline-fast --scanline-effects --text-spans --lazy-mix --code-kb 8192 --data-kb 2048 --label <unique-label>
```

QEMU exact tests execute the generated A32 code and compare against the interpreter. Approximate replica replay is a separate determinism/runtime check and cannot establish timing accuracy. Candidate-only replay excludes the interpreter and semantic fixtures; it also cannot establish accuracy or hardware performance.

The portable APU check compiles both eager and lazy mixer implementations, applies 50,000 seeded register/FIFO/timer operations, and compares device state, partial PCM buffers, and emitted PCM after every operation. The scanline check compares independent expected brightness values and general-renderer pixels across color-depth/swap/cache configurations. It additionally writes palettes while preserving populated row caches before comparing results.

## Evidence limits

The first three board ABA groups validated a Metroid median gain of about 21.56% for the page index, while Mario/Emerald gains were below the 3% retention threshold and Zelda regressed about 2.77%. Later LIGHT board experiments also changed caller-budget enforcement relative to their sealed baseline; they are composite comparisons and cannot isolate literal pool speed. New comparisons must enforce the same control budget in both engines.

Production promotion requires the complete functional and performance plan, including normal audio/display, four fixed scenes, three 60-second rounds, zero steady PCM underflow, and verified NOR/SPL recovery. Local source, imported manifests and per-candidate ELF/BIN/MAP hashes identify dirty working snapshots; the repository HEAD alone does not identify these candidates.

## HLE IRQ admission candidate

GBA_DYNAREC_PERSISTENT_HLE_IRQ=0 is the default. The optional native-engine path permits guest handler execution when wait_mask is retained and hle_irq is active, matching the scalar and legacy admission rule. Ordinary waits, pending IRQ/DMA guards, BIOS execution and the HLE return trampoline remain scalar boundaries. Pipeline prefill uses the same admission condition. No state fields, allocation, guest cycle expression or event-slip limit were added.

	est/dynarec.py --hle-irq exercises actual ARM/Thumb native execution inside nested handler contexts, short and 4096-cycle budgets, rejection of inactive IntrWait, and the BIOS boundary. Source and imported snapshot each passed 55,709 exact assertions plus 13 reference ROMs; stats-off admission passed 25 assertions. Test-only native-entry counters are absent from firmware candidates. These tests do not establish full game compatibility or board speed.

The exact board Zelda seed was extracted from the archived staging image and verified by SHA256. Diagnostic 720-frame replay reduced C entries by 76.35 percent and scalar fallback by 84.83 percent. All three short QEMU timing groups exceeded the 3 percent control-drift limit and were invalidated. Do not claim a 35 percent speedup from the earlier single window. The candidate remains default-off pending RAM comparisons and the complete four-scene acceptance.
