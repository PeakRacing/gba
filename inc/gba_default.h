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
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <gba_conf.h>
/* Execution choices. All translation units must use the same configuration. */
#ifndef GBA_DYNAREC
#define GBA_DYNAREC (0)
#endif
#ifndef GBA_DYNAREC_TURBO
#define GBA_DYNAREC_TURBO (0)
#endif
/* Experimental physical-page reverse index. Caller-owned metadata only. */
#ifndef GBA_DYNAREC_PAGE_INDEX
#define GBA_DYNAREC_PAGE_INDEX (0)
#endif
#if (GBA_DYNAREC_PAGE_INDEX != 0 && GBA_DYNAREC_PAGE_INDEX != 1) || \
    (GBA_DYNAREC_PAGE_INDEX && !(GBA_DYNAREC_PERSISTENT || GBA_DYNAREC_TURBO))
#error "page index requires a persistent or turbo backend"
#endif
#ifndef GBA_DYNAREC_TURBO_LIGHT
#define GBA_DYNAREC_TURBO_LIGHT (0)
#endif
#if (GBA_DYNAREC_TURBO_LIGHT != 0 && GBA_DYNAREC_TURBO_LIGHT != 1) || \
    (GBA_DYNAREC_TURBO_LIGHT && !GBA_DYNAREC_TURBO)
#error "light emitter requires Turbo"
#endif
#ifndef GBA_DYNAREC_TURBO_TLB
#define GBA_DYNAREC_TURBO_TLB (0)
#endif
#ifndef GBA_DYNAREC_TURBO_TRACE_INSNS
#define GBA_DYNAREC_TURBO_TRACE_INSNS (64)
#endif
#ifndef GBA_DYNAREC_TURBO_TRACE_FUSION
#define GBA_DYNAREC_TURBO_TRACE_FUSION (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_TURBO_PRELINK
#define GBA_DYNAREC_TURBO_PRELINK (0)
#endif
#ifndef GBA_DYNAREC_TURBO_REGISTER_MAP
#define GBA_DYNAREC_TURBO_REGISTER_MAP (GBA_DYNAREC_TURBO ? 3 : 0)
#endif
#if (GBA_DYNAREC_TURBO != 0 && GBA_DYNAREC_TURBO != 1) || \
    (GBA_DYNAREC_TURBO_TLB != 0 && GBA_DYNAREC_TURBO_TLB != 1) || \
    (GBA_DYNAREC_TURBO_TRACE_FUSION != 0 && GBA_DYNAREC_TURBO_TRACE_FUSION != 1) || \
    GBA_DYNAREC_TURBO_PRELINK < 0 || GBA_DYNAREC_TURBO_PRELINK > 16 || \
    (GBA_DYNAREC_TURBO && GBA_DYNAREC_TURBO_TRACE_INSNS < 1) || \
    GBA_DYNAREC_TURBO_TRACE_INSNS > 128 || \
    GBA_DYNAREC_TURBO_REGISTER_MAP < 0 || GBA_DYNAREC_TURBO_REGISTER_MAP > 3
#error "invalid GBA turbo backend configuration"
#endif

#if GBA_DYNAREC_TURBO_REGISTER_MAP && !GBA_DYNAREC_TURBO
#error "GBA turbo register map requires turbo execution"
#endif
#if GBA_DYNAREC_TURBO && !GBA_DYNAREC
#error "turbo execution requires dynarec"
#endif
#if GBA_DYNAREC_TURBO_TRACE_FUSION && !GBA_DYNAREC_TURBO
#error "turbo trace fusion requires turbo execution"
#endif
#if GBA_DYNAREC_TURBO_TLB && !GBA_DYNAREC_TURBO
#error "turbo TLB requires turbo execution"
#endif
#if GBA_DYNAREC_TURBO_PRELINK && !GBA_DYNAREC_TURBO
#error "turbo eager linking requires turbo execution"
#endif
#ifndef GBA_DYNAREC_PERSISTENT
#define GBA_DYNAREC_PERSISTENT (0)
#endif
#if GBA_DYNAREC_TURBO && GBA_DYNAREC_PERSISTENT
#error "GBA_DYNAREC_TURBO and GBA_DYNAREC_PERSISTENT are mutually exclusive backends"
#endif
#define GBA_DYNAREC_PERSISTENT_ENGINE (GBA_DYNAREC_PERSISTENT || GBA_DYNAREC_TURBO)
#ifndef GBA_DYNAREC_PERSISTENT_LITERALS
#define GBA_DYNAREC_PERSISTENT_LITERALS (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_LITERAL_LEAF
#define GBA_DYNAREC_PERSISTENT_LITERAL_LEAF (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_MULTI
#define GBA_DYNAREC_PERSISTENT_MULTI (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_BL_DIRECT
#define GBA_DYNAREC_PERSISTENT_BL_DIRECT (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#if (GBA_DYNAREC_PERSISTENT_BL_DIRECT != 0 && GBA_DYNAREC_PERSISTENT_BL_DIRECT != 1) || \
    (GBA_DYNAREC_PERSISTENT_BL_DIRECT && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent direct Thumb BL requires GBA_DYNAREC_PERSISTENT"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_TAIL
#define GBA_DYNAREC_PERSISTENT_TAIL (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_INLINE_REFILL
#define GBA_DYNAREC_PERSISTENT_INLINE_REFILL (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#if (GBA_DYNAREC_PERSISTENT_INLINE_REFILL != 0 && GBA_DYNAREC_PERSISTENT_INLINE_REFILL != 1) || \
    (GBA_DYNAREC_PERSISTENT_INLINE_REFILL && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "inline persistent refill requires persistent execution"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH
#define GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#if (GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH != 0 && GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH != 1) || \
    (GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "direct persistent branches require persistent execution"
#endif
/* An active HLE IRQ handler runs ordinary guest code even while IntrWait
 * retains its wait mask. BIOS return still belongs to the scalar executor. */
#ifndef GBA_DYNAREC_PERSISTENT_HLE_IRQ
#define GBA_DYNAREC_PERSISTENT_HLE_IRQ (0)
#endif
#if (GBA_DYNAREC_PERSISTENT_HLE_IRQ != 0 && GBA_DYNAREC_PERSISTENT_HLE_IRQ != 1) || \
    (GBA_DYNAREC_PERSISTENT_HLE_IRQ && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "HLE IRQ admission requires persistent execution"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK
#define GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_BLOCK_INSNS
#define GBA_DYNAREC_PERSISTENT_BLOCK_INSNS (GBA_DYNAREC_TURBO ? GBA_DYNAREC_TURBO_TRACE_INSNS : 16)
#endif
#if (GBA_DYNAREC_PERSISTENT_BLOCK_INSNS < 1 || GBA_DYNAREC_PERSISTENT_BLOCK_INSNS > 128)
#error "invalid persistent block instruction limit"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_SHARED_EXITS
#define GBA_DYNAREC_PERSISTENT_SHARED_EXITS (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#if (GBA_DYNAREC_PERSISTENT_SHARED_EXITS != 0 && GBA_DYNAREC_PERSISTENT_SHARED_EXITS != 1) || \
    (GBA_DYNAREC_PERSISTENT_SHARED_EXITS && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "shared persistent exits require GBA_DYNAREC_PERSISTENT"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_MAP_HELPER
#define GBA_DYNAREC_PERSISTENT_MAP_HELPER (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_PAGE_TABLE
#define GBA_DYNAREC_PERSISTENT_PAGE_TABLE (0)
#endif
#if (GBA_DYNAREC_PERSISTENT_PAGE_TABLE != 0 && GBA_DYNAREC_PERSISTENT_PAGE_TABLE != 1) || \
    (GBA_DYNAREC_PERSISTENT_PAGE_TABLE && !GBA_DYNAREC_PERSISTENT_MAP_HELPER)
#error "persistent page tables require persistent mapping helpers"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_ROM_WINDOW
#define GBA_DYNAREC_PERSISTENT_ROM_WINDOW (GBA_DYNAREC_TURBO ? 1 : 0)
#endif
#if (GBA_DYNAREC_PERSISTENT_ROM_WINDOW != 0 && GBA_DYNAREC_PERSISTENT_ROM_WINDOW != 1) || \
    (GBA_DYNAREC_PERSISTENT_ROM_WINDOW && !GBA_DYNAREC_PERSISTENT_MAP_HELPER)
#error "primary ROM window requires persistent mapping helpers"
#endif
#if (GBA_DYNAREC_PERSISTENT_LITERAL_LEAF != 0 && GBA_DYNAREC_PERSISTENT_LITERAL_LEAF != 1) || \
    (GBA_DYNAREC_PERSISTENT_LITERAL_LEAF && (!GBA_DYNAREC_PERSISTENT_LITERALS || !GBA_DYNAREC_PERSISTENT_MAP_HELPER))
#error "literal leaf requires persistent literals and mapping helpers"
#endif
#ifndef GBA_DYNAREC_PERSISTENT_READ_WAIT
#define GBA_DYNAREC_PERSISTENT_READ_WAIT (0)
#endif
#ifndef GBA_DYNAREC_PERSISTENT_INDIRECT_BITS
#define GBA_DYNAREC_PERSISTENT_INDIRECT_BITS (GBA_DYNAREC_TURBO ? 10 : 6)
#endif
#if GBA_DYNAREC_PERSISTENT_INDIRECT_BITS < 6 || GBA_DYNAREC_PERSISTENT_INDIRECT_BITS > 11 || \
    (GBA_DYNAREC_PERSISTENT_INDIRECT_BITS != 6 && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "invalid persistent indirect cache geometry"
#endif
#if (GBA_DYNAREC_PERSISTENT_READ_WAIT != 0 && GBA_DYNAREC_PERSISTENT_READ_WAIT != 1) || \
    (GBA_DYNAREC_PERSISTENT_READ_WAIT && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent read wait preservation requires GBA_DYNAREC_PERSISTENT"
#endif
#if (GBA_DYNAREC_PERSISTENT_MAP_HELPER != 0 && GBA_DYNAREC_PERSISTENT_MAP_HELPER != 1) || \
    (GBA_DYNAREC_PERSISTENT_MAP_HELPER && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent mapping helpers require GBA_DYNAREC_PERSISTENT"
#endif
#if (GBA_DYNAREC_PERSISTENT_TAIL != 0 && GBA_DYNAREC_PERSISTENT_TAIL != 1) || \
    (GBA_DYNAREC_PERSISTENT_TAIL && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent tail reservation requires GBA_DYNAREC_PERSISTENT"
#endif
#if (GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK != 0 && GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK != 1) || \
    (GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent branch superblocks require persistent execution"
#endif
#if (GBA_DYNAREC_PERSISTENT_MULTI != 0 && GBA_DYNAREC_PERSISTENT_MULTI != 1) || \
    (GBA_DYNAREC_PERSISTENT_MULTI && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent multiple transfers require the persistent execution engine"
#endif
#ifndef GBA_DYNAREC_TURBO_LOCAL_ALU
#define GBA_DYNAREC_TURBO_LOCAL_ALU (0)
#endif
#if (GBA_DYNAREC_TURBO_LOCAL_ALU != 0 && GBA_DYNAREC_TURBO_LOCAL_ALU != 1) || \
    (GBA_DYNAREC_TURBO_LOCAL_ALU && !GBA_DYNAREC_TURBO)
#error "local ALU register requires Turbo"
#endif
#ifndef GBA_DYNAREC_TURBO_LOCAL_REGS
#define GBA_DYNAREC_TURBO_LOCAL_REGS (2)
#endif
#if GBA_DYNAREC_TURBO_LOCAL_REGS < 1 || GBA_DYNAREC_TURBO_LOCAL_REGS > 2
#error "local ALU capacity must be one or two registers"
#endif
#ifndef GBA_APU_LAZY_MIX
#define GBA_APU_LAZY_MIX (0)
#endif
#if GBA_APU_LAZY_MIX != 0 && GBA_APU_LAZY_MIX != 1
#error "invalid lazy mixer configuration"
#endif
#ifndef GBA_PPU_SCANLINE_SPANS
#define GBA_PPU_SCANLINE_SPANS (0)
#endif
#if (GBA_PPU_SCANLINE_SPANS != 0 && GBA_PPU_SCANLINE_SPANS != 1) || \
    (GBA_PPU_SCANLINE_SPANS && !GBA_PPU_SCANLINE_FAST)
#error "specialized text spans require fast scanlines"
#endif
#ifndef GBA_PPU_SCANLINE_EFFECTS
#define GBA_PPU_SCANLINE_EFFECTS (0)
#endif
#if (GBA_PPU_SCANLINE_EFFECTS != 0 && GBA_PPU_SCANLINE_EFFECTS != 1) || \
    (GBA_PPU_SCANLINE_EFFECTS && !GBA_PPU_SCANLINE_FAST)
#error "specialized effects require fast scanlines"
#endif
#ifndef GBA_PPU_SCANLINE_IDENTITY
#define GBA_PPU_SCANLINE_IDENTITY (0)
#endif
#ifndef GBA_PPU_SCANLINE_PALETTE_DEDUP
#define GBA_PPU_SCANLINE_PALETTE_DEDUP (0)
#endif
#ifndef GBA_PPU_SCANLINE_ZERO_ROWS
#define GBA_PPU_SCANLINE_ZERO_ROWS (0)
#endif
#if (GBA_PPU_SCANLINE_ZERO_ROWS != 0 && GBA_PPU_SCANLINE_ZERO_ROWS != 1) || \
    (GBA_PPU_SCANLINE_ZERO_ROWS && !GBA_PPU_SCANLINE_FAST)
#error "empty tile rows require GBA_PPU_SCANLINE_FAST"
#endif
#if (GBA_PPU_SCANLINE_PALETTE_DEDUP != 0 && GBA_PPU_SCANLINE_PALETTE_DEDUP != 1) || \
    (GBA_PPU_SCANLINE_PALETTE_DEDUP && !GBA_PPU_SCANLINE_FAST)
#error "scanline palette deduplication requires GBA_PPU_SCANLINE_FAST"
#endif
#if (GBA_DYNAREC_PERSISTENT_LITERALS != 0 && GBA_DYNAREC_PERSISTENT_LITERALS != 1) || \
    (GBA_DYNAREC_PERSISTENT_LITERALS && !GBA_DYNAREC_PERSISTENT_ENGINE)
#error "persistent literal loads require the persistent execution engine"
#endif
#ifndef GBA_DYNAREC_CHAIN
#define GBA_DYNAREC_CHAIN (0)
#endif
#if GBA_DYNAREC_CHAIN != 0 && GBA_DYNAREC_CHAIN != 1
#error "invalid GBA_DYNAREC_CHAIN configuration"
#endif
#if GBA_DYNAREC_CHAIN && !GBA_DYNAREC
#error "GBA_DYNAREC_CHAIN requires GBA_DYNAREC"
#endif
#ifndef GBA_DYNAREC_DIRECT
#define GBA_DYNAREC_DIRECT (0)
#endif
#if GBA_DYNAREC_DIRECT != 0 && GBA_DYNAREC_DIRECT != 1
#error "invalid GBA_DYNAREC_DIRECT configuration"
#endif
#if GBA_DYNAREC_DIRECT && !GBA_DYNAREC_CHAIN
#error "GBA_DYNAREC_DIRECT requires GBA_DYNAREC_CHAIN"
#endif
#ifndef GBA_DYNAREC_SOFT_TIMERS
#define GBA_DYNAREC_SOFT_TIMERS (0)
#endif
#if GBA_DYNAREC_SOFT_TIMERS != 0 && GBA_DYNAREC_SOFT_TIMERS != 1
#error "invalid GBA_DYNAREC_SOFT_TIMERS configuration"
#endif
#if GBA_DYNAREC_SOFT_TIMERS && !GBA_DYNAREC
#error "GBA_DYNAREC_SOFT_TIMERS requires GBA_DYNAREC"
#endif
#ifndef GBA_DYNAREC_INTERIOR
#define GBA_DYNAREC_INTERIOR (0)
#endif
#if GBA_DYNAREC_INTERIOR != 0 && GBA_DYNAREC_INTERIOR != 1
#error "invalid GBA_DYNAREC_INTERIOR configuration"
#endif
#if GBA_DYNAREC_INTERIOR && !GBA_DYNAREC
#error "GBA_DYNAREC_INTERIOR requires GBA_DYNAREC"
#endif
#ifndef GBA_DYNAREC_NEGATIVE
#define GBA_DYNAREC_NEGATIVE (0)
#endif
#if GBA_DYNAREC_NEGATIVE != 0 && GBA_DYNAREC_NEGATIVE != 1
#error "invalid GBA_DYNAREC_NEGATIVE configuration"
#endif
#if GBA_DYNAREC_NEGATIVE && !GBA_DYNAREC
#error "GBA_DYNAREC_NEGATIVE requires GBA_DYNAREC"
#endif
#ifndef GBA_PPU_SCANLINE_FAST
#define GBA_PPU_SCANLINE_FAST (0)
#endif
#if (GBA_PPU_SCANLINE_IDENTITY != 0 && GBA_PPU_SCANLINE_IDENTITY != 1) || \
    (GBA_PPU_SCANLINE_IDENTITY && !GBA_PPU_SCANLINE_FAST)
#error "identity effect scanlines require the fast scanline renderer"
#endif
#if GBA_PPU_SCANLINE_FAST != 0 && GBA_PPU_SCANLINE_FAST != 1
#error "invalid GBA_PPU_SCANLINE_FAST configuration"
#endif
#if GBA_PPU_SCANLINE_FAST && GBA_DYNAREC_VIDEO_STORE
#error "scanline palette coherence requires notified video stores"
#endif
#ifndef GBA_PPU_TILE_DIRECT
#define GBA_PPU_TILE_DIRECT (0)
#endif
#if GBA_PPU_TILE_DIRECT != 0 && GBA_PPU_TILE_DIRECT != 1
#error "invalid GBA_PPU_TILE_DIRECT configuration"
#endif
#ifndef GBA_PPU_ORDERED
#define GBA_PPU_ORDERED (0)
#endif
#ifndef GBA_PPU_OBJ_SPANS
#define GBA_PPU_OBJ_SPANS (0)
#endif
#ifndef GBA_PPU_OBJ_INDEX
#define GBA_PPU_OBJ_INDEX (0)
#endif
#if GBA_PPU_OBJ_INDEX != 0 && GBA_PPU_OBJ_INDEX != 1
#error "invalid GBA_PPU_OBJ_INDEX configuration"
#endif
#ifndef GBA_PPU_OAM_DIRTY
#define GBA_PPU_OAM_DIRTY (0)
#endif
#if GBA_PPU_OAM_DIRTY != 0 && GBA_PPU_OAM_DIRTY != 1
#error "invalid GBA_PPU_OAM_DIRTY configuration"
#endif
#ifndef GBA_PPU_SORTED_BLEND
#define GBA_PPU_SORTED_BLEND (0)
#endif
#ifndef GBA_PPU_FRONT_CULL
#define GBA_PPU_FRONT_CULL (0)
#endif
#if GBA_PPU_FRONT_CULL != 0 && GBA_PPU_FRONT_CULL != 1
#error "invalid GBA_PPU_FRONT_CULL configuration"
#endif
#if GBA_PPU_SORTED_BLEND != 0 && GBA_PPU_SORTED_BLEND != 1
#error "invalid GBA_PPU_SORTED_BLEND configuration"
#endif
#if GBA_PPU_OBJ_SPANS != 0 && GBA_PPU_OBJ_SPANS != 1
#error "invalid GBA_PPU_OBJ_SPANS configuration"
#endif
#if GBA_PPU_ORDERED != 0 && GBA_PPU_ORDERED != 1
#error "invalid GBA_PPU_ORDERED configuration"
#endif
#ifndef GBA_TEST_RENDER_REFERENCE
#define GBA_TEST_RENDER_REFERENCE (0)
#endif
#ifndef GBA_PPU_READ_DEDUP
#define GBA_PPU_READ_DEDUP (0)
#endif
#if GBA_PPU_READ_DEDUP != 0 && GBA_PPU_READ_DEDUP != 1
#error "invalid GBA_PPU_READ_DEDUP configuration"
#endif
#ifndef GBA_FAST_CORE
#define GBA_FAST_CORE (0)
#endif
#if defined(GBA_PORT_BATCH_EVENTS) && defined(GBA_BATCH_EVENTS) && GBA_PORT_BATCH_EVENTS != GBA_BATCH_EVENTS
#error "conflicting GBA_PORT_BATCH_EVENTS and GBA_BATCH_EVENTS"
#endif
#if defined(GBA_PORT_BATCH_EVENTS) && !defined(GBA_BATCH_EVENTS)
#define GBA_BATCH_EVENTS GBA_PORT_BATCH_EVENTS
#endif
#if defined(GBA_PORT_FAST_RUN) && defined(GBA_FAST_RUN) && GBA_PORT_FAST_RUN != GBA_FAST_RUN
#error "conflicting GBA_PORT_FAST_RUN and GBA_FAST_RUN"
#endif
#if defined(GBA_PORT_FAST_RUN) && !defined(GBA_FAST_RUN)
#define GBA_FAST_RUN GBA_PORT_FAST_RUN
#endif
#if defined(GBA_PORT_FAST_IO) && defined(GBA_FAST_IO) && GBA_PORT_FAST_IO != GBA_FAST_IO
#error "conflicting GBA_PORT_FAST_IO and GBA_FAST_IO"
#endif
#if defined(GBA_PORT_FAST_IO) && !defined(GBA_FAST_IO)
#define GBA_FAST_IO GBA_PORT_FAST_IO
#endif
#if defined(GBA_PORT_IDLE_LOOPS) && defined(GBA_IDLE_LOOPS) && GBA_PORT_IDLE_LOOPS != GBA_IDLE_LOOPS
#error "conflicting GBA_PORT_IDLE_LOOPS and GBA_IDLE_LOOPS"
#endif
#if defined(GBA_PORT_IDLE_LOOPS) && !defined(GBA_IDLE_LOOPS)
#define GBA_IDLE_LOOPS GBA_PORT_IDLE_LOOPS
#endif
#ifndef GBA_BATCH_EVENTS
#define GBA_BATCH_EVENTS ((GBA_FAST_CORE || GBA_DYNAREC))
#endif
#ifndef GBA_FAST_RUN
#define GBA_FAST_RUN ((GBA_FAST_CORE || GBA_DYNAREC))
#endif
#ifndef GBA_FAST_IO
#define GBA_FAST_IO (0)
#endif
#ifndef GBA_IDLE_LOOPS
#define GBA_IDLE_LOOPS (0)
#endif
#ifndef GBA_PORT_BATCH_EVENTS
#define GBA_PORT_BATCH_EVENTS GBA_BATCH_EVENTS
#endif
#ifndef GBA_PORT_FAST_RUN
#define GBA_PORT_FAST_RUN GBA_FAST_RUN
#endif
#ifndef GBA_PORT_FAST_IO
#define GBA_PORT_FAST_IO GBA_FAST_IO
#endif
#ifndef GBA_PORT_IDLE_LOOPS
#define GBA_PORT_IDLE_LOOPS GBA_IDLE_LOOPS
#endif
/* Speed-first experiment: complete at most this many predicted guest cycles
 * before committing pending devices. Actual instruction cycles are unchanged.
 * Zero retains exact CPU/device event boundaries. */
#ifndef GBA_DYNAREC_EVENT_SLIP
#define GBA_DYNAREC_EVENT_SLIP (0)
#endif
#ifndef GBA_DYNAREC_COARSE_CHAIN
#define GBA_DYNAREC_COARSE_CHAIN (0)
#endif
#ifndef GBA_DYNAREC_LINK_CACHE
#define GBA_DYNAREC_LINK_CACHE (0)
#endif
#ifndef GBA_DYNAREC_REFILL_MATCH
#define GBA_DYNAREC_REFILL_MATCH (0)
#endif
#if (GBA_DYNAREC_REFILL_MATCH != 0 && GBA_DYNAREC_REFILL_MATCH != 1) || (GBA_DYNAREC_REFILL_MATCH && !GBA_DYNAREC_COARSE_CHAIN)
#error "native refill matching requires COARSE_CHAIN"
#endif
#if (GBA_DYNAREC_LINK_CACHE != 0 && GBA_DYNAREC_LINK_CACHE != 1) || (GBA_DYNAREC_LINK_CACHE && !GBA_DYNAREC_COARSE_CHAIN)
#error "native successor cache requires COARSE_CHAIN"
#endif
#ifndef GBA_DYNAREC_TINY_BLOCKS
#define GBA_DYNAREC_TINY_BLOCKS (0)
#endif
#ifndef GBA_DYNAREC_BL_PREFIX
#define GBA_DYNAREC_BL_PREFIX (0)
#endif
#ifndef GBA_DYNAREC_FIXED_MAP
#define GBA_DYNAREC_FIXED_MAP (0)
#endif
#ifndef GBA_DYNAREC_FASTCALL
#define GBA_DYNAREC_FASTCALL (0)
#endif
#ifndef GBA_DYNAREC_VIDEO_STORE
#define GBA_DYNAREC_VIDEO_STORE (0)
#endif
#if (GBA_DYNAREC_VIDEO_STORE != 0 && GBA_DYNAREC_VIDEO_STORE != 1) || (GBA_DYNAREC_VIDEO_STORE && (!GBA_DYNAREC || !GBA_DYNAREC_FAST_BUS || GBA_DYNAREC_EVENT_SLIP<64))
#error "native video stores require FAST_BUS and EVENT_SLIP >=64"
#endif
#if (GBA_DYNAREC_FASTCALL != 0 && GBA_DYNAREC_FASTCALL != 1) || (GBA_DYNAREC_FASTCALL && (!GBA_DYNAREC || GBA_DYNAREC_CHAIN || GBA_DYNAREC_INTERIOR || GBA_DYNAREC_ROM_PREFIX))
#error "native caller-clobber ABI requires dynarec and excludes CHAIN/INTERIOR/ROM_PREFIX"
#endif
#ifndef GBA_DYNAREC_ROM_TRACE
#define GBA_DYNAREC_ROM_TRACE (0)
#endif
#ifndef GBA_DYNAREC_TRACE_LINKS
#define GBA_DYNAREC_TRACE_LINKS (0)
#endif
#if (GBA_DYNAREC_TRACE_LINKS != 0 && GBA_DYNAREC_TRACE_LINKS != 1) || (GBA_DYNAREC_TRACE_LINKS && (!GBA_DYNAREC_ROM_TRACE || GBA_DYNAREC_CHAIN))
#error "native trace links require ROM_TRACE and exclude CHAIN"
#endif
#ifndef GBA_DYNAREC_TRACE_CONTEXT
#define GBA_DYNAREC_TRACE_CONTEXT (0)
#endif
#if (GBA_DYNAREC_TRACE_CONTEXT != 0 && GBA_DYNAREC_TRACE_CONTEXT != 1) || (GBA_DYNAREC_TRACE_CONTEXT && !GBA_DYNAREC_TRACE_LINKS)
#error "native trace context requires TRACE_LINKS"
#endif
#if (GBA_DYNAREC_ROM_TRACE != 0 && GBA_DYNAREC_ROM_TRACE != 1) || (GBA_DYNAREC_ROM_TRACE && (!GBA_DYNAREC_FAST_BUS || !GBA_DYNAREC_WIDE_ROM || GBA_DYNAREC_EVENT_SLIP<256 || GBA_DYNAREC_INTERIOR || GBA_DYNAREC_ROM_PREFIX))
#error "ROM trace requires FAST_BUS, WIDE_ROM and excludes INTERIOR"
#endif
#if GBA_DYNAREC_FIXED_MAP < 0 || GBA_DYNAREC_FIXED_MAP > 2 || (GBA_DYNAREC_FIXED_MAP && !GBA_DYNAREC)
#error "fixed native map requires dynarec and mode 0..2"
#endif
#ifndef GBA_DYNAREC_LINE_INVALIDATE
#define GBA_DYNAREC_LINE_INVALIDATE (0)
#endif
#ifndef GBA_DYNAREC_HARD_CACHE
#define GBA_DYNAREC_HARD_CACHE (0)
#endif
#if (GBA_DYNAREC_HARD_CACHE != 0 && GBA_DYNAREC_HARD_CACHE != 1) || (GBA_DYNAREC_HARD_CACHE && !GBA_DYNAREC_EVENT_SLIP)
#error "native deadline cache requires EVENT_SLIP"
#endif
#if (GBA_DYNAREC_LINE_INVALIDATE != 0 && GBA_DYNAREC_LINE_INVALIDATE != 1) || (GBA_DYNAREC_LINE_INVALIDATE && !GBA_DYNAREC)
#error "code-line invalidation requires dynarec"
#endif
#if (GBA_DYNAREC_BL_PREFIX != 0 && GBA_DYNAREC_BL_PREFIX != 1) || (GBA_DYNAREC_BL_PREFIX && !GBA_DYNAREC)
#error "native BL prefix requires dynarec"
#endif
#if (GBA_DYNAREC_TINY_BLOCKS != 0 && GBA_DYNAREC_TINY_BLOCKS != 1) || (GBA_DYNAREC_TINY_BLOCKS && !GBA_DYNAREC_COARSE_CHAIN)
#error "tiny native blocks require COARSE_CHAIN"
#endif
#if GBA_DYNAREC_EVENT_SLIP < 0 || GBA_DYNAREC_EVENT_SLIP > 65536 || (GBA_DYNAREC_EVENT_SLIP && (!GBA_DYNAREC || (GBA_DYNAREC_CHAIN && !GBA_DYNAREC_COARSE_CHAIN)))
#error "event slip requires non-chained dynarec and a 0..65536 bound"
#endif
#ifndef GBA_DYNAREC_FAST_BUS
#define GBA_DYNAREC_FAST_BUS (0)
#endif
#ifndef GBA_DYNAREC_BULK_PREFILL
#define GBA_DYNAREC_BULK_PREFILL (0)
#endif
#if (GBA_DYNAREC_BULK_PREFILL != 0 && GBA_DYNAREC_BULK_PREFILL != 1) || (GBA_DYNAREC_BULK_PREFILL && (!GBA_DYNAREC_FAST_BUS || GBA_DYNAREC_EVENT_SLIP<64))
#error "bulk native pipeline prefill requires FAST_BUS and EVENT_SLIP >= 64"
#endif
#if (GBA_DYNAREC_FAST_BUS != 0 && GBA_DYNAREC_FAST_BUS != 1) || (GBA_DYNAREC_FAST_BUS && !GBA_DYNAREC_EVENT_SLIP)
#error "approximate ROM fetches require explicit event-slip mode"
#endif
#ifndef GBA_DYNAREC_WIDE_ROM
#define GBA_DYNAREC_WIDE_ROM (0)
#endif
#if (GBA_DYNAREC_WIDE_ROM != 0 && GBA_DYNAREC_WIDE_ROM != 1) || (GBA_DYNAREC_WIDE_ROM && (!GBA_DYNAREC_FAST_BUS || GBA_DYNAREC_INTERIOR))
#error "wide ROM loops require FAST_BUS and exclude INTERIOR"
#endif
#ifndef GBA_DYNAREC_LONG_LOOPS
#define GBA_DYNAREC_LONG_LOOPS (0)
#endif
#if (GBA_DYNAREC_LONG_LOOPS != 0 && GBA_DYNAREC_LONG_LOOPS != 1) || (GBA_DYNAREC_LONG_LOOPS && !GBA_DYNAREC_WIDE_ROM)
#error "long native loops require WIDE_ROM approximate timing"
#endif
#ifndef GBA_DYNAREC_ROM_CHAIN
#define GBA_DYNAREC_ROM_CHAIN (0)
#endif
#if (GBA_DYNAREC_ROM_CHAIN != 0 && GBA_DYNAREC_ROM_CHAIN != 1) || (GBA_DYNAREC_ROM_CHAIN && (!GBA_DYNAREC_CHAIN || GBA_DYNAREC_INTERIOR))
#error "ROM chains require CHAIN and exclude INTERIOR"
#endif
#if (GBA_DYNAREC_COARSE_CHAIN != 0 && GBA_DYNAREC_COARSE_CHAIN != 1) || (GBA_DYNAREC_COARSE_CHAIN && (!GBA_DYNAREC_FAST_BUS || !GBA_DYNAREC_ROM_CHAIN || !GBA_DYNAREC_DIRECT || GBA_DYNAREC_STATS || GBA_DYNAREC_ROM_PREFIX))
#error "coarse ROM chains require FAST_BUS, ROM_CHAIN, DIRECT and statistics disabled"
#endif
#ifndef GBA_DYNAREC_RAM_INLINE
#define GBA_DYNAREC_RAM_INLINE (0)
#endif
#if (GBA_DYNAREC_RAM_INLINE != 0 && GBA_DYNAREC_RAM_INLINE != 1) || (GBA_DYNAREC_RAM_INLINE && !GBA_DYNAREC)
#error "inline RAM access requires GBA_DYNAREC"
#endif
#ifndef GBA_DYNAREC_ROM_PREFIX
#define GBA_DYNAREC_ROM_PREFIX (0)
#endif
#if (GBA_DYNAREC_ROM_PREFIX != 0 && GBA_DYNAREC_ROM_PREFIX != 1) || (GBA_DYNAREC_ROM_PREFIX && !GBA_DYNAREC)
#error "ROM prefixes require GBA_DYNAREC"
#endif
#ifndef GBA_DYNAREC_PARTIAL_REGS
#define GBA_DYNAREC_PARTIAL_REGS (0)
#endif
#if (GBA_DYNAREC_PARTIAL_REGS != 0 && GBA_DYNAREC_PARTIAL_REGS != 1) || (GBA_DYNAREC_PARTIAL_REGS && !GBA_DYNAREC)
#error "partial register mapping requires dynarec"
#endif
#ifndef GBA_DYNAREC_STORE_INLINE
#define GBA_DYNAREC_STORE_INLINE (0)
#endif
#ifndef GBA_DYNAREC_MULTI_INLINE
#define GBA_DYNAREC_MULTI_INLINE (0)
#endif
#ifndef GBA_DYNAREC_MULTI_PC
#define GBA_DYNAREC_MULTI_PC (0)
#endif
#if (GBA_DYNAREC_MULTI_PC != 0 && GBA_DYNAREC_MULTI_PC != 1) || (GBA_DYNAREC_MULTI_PC && !GBA_DYNAREC_MULTI_INLINE)
#error "inline PC multiple loads require MULTI_INLINE"
#endif
#if (GBA_DYNAREC_STORE_INLINE != 0 && GBA_DYNAREC_STORE_INLINE != 1) || (GBA_DYNAREC_STORE_INLINE && !GBA_DYNAREC_MULTI_INLINE)
#error "inline multiple stores require MULTI_INLINE"
#endif
#if (GBA_DYNAREC_MULTI_INLINE != 0 && GBA_DYNAREC_MULTI_INLINE != 1) || (GBA_DYNAREC_MULTI_INLINE && !GBA_DYNAREC)
#error "inline multiple transfers require GBA_DYNAREC"
#endif
#ifndef GBA_DYNAREC_STATS
#define GBA_DYNAREC_STATS (0)
#endif
#if (GBA_DYNAREC_PERSISTENT != 0 && GBA_DYNAREC_PERSISTENT != 1) || \
    (GBA_DYNAREC_PERSISTENT_ENGINE && (!GBA_DYNAREC || GBA_DYNAREC_CHAIN || GBA_DYNAREC_ROM_TRACE || \
        GBA_DYNAREC_TRACE_LINKS || GBA_DYNAREC_INTERIOR || GBA_DYNAREC_FASTCALL))
#error "persistent execution requires dynarec and excludes legacy execution organizations"
#endif
#if GBA_DYNAREC_STATS != 0 && GBA_DYNAREC_STATS != 1
#error "invalid GBA_DYNAREC_STATS configuration"
#endif
#ifndef GBA_DYNAREC_CODE_SIZE
#define GBA_DYNAREC_CODE_SIZE (256u * 1024u)
#endif
#ifndef GBA_DYNAREC_DATA_SIZE
#define GBA_DYNAREC_DATA_SIZE (64u * 1024u)
#endif
#ifndef GBA_DYNAREC_RECENT_BITS
#define GBA_DYNAREC_RECENT_BITS (6)
#endif
#if GBA_DYNAREC_RECENT_BITS < 6 || GBA_DYNAREC_RECENT_BITS > 10 || (GBA_DYNAREC_DIRECT && GBA_DYNAREC_RECENT_BITS != 6) || (GBA_DYNAREC_RECENT_BITS > 6 && GBA_DYNAREC_DATA_SIZE < 512u*1024u)
#error "recent cache: 6..10 bits; larger caches require >=512KiB and DIRECT=0"
#endif
#if GBA_FAST_CORE != 0 && GBA_FAST_CORE != 1
#error "invalid GBA_FAST_CORE configuration"
#endif
#if GBA_DYNAREC != 0 && GBA_DYNAREC != 1
#error "invalid GBA_DYNAREC configuration"
#endif
#if GBA_DYNAREC
#if !defined(__arm__) || defined(__ARMEB__) || !defined(__ARM_ARCH) || __ARM_ARCH < 5 ||                               \
    (defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M')
#error "GBA_DYNAREC requires a little endian ARMv5TE or newer A32 host"
#endif
#endif

#ifndef GBA_FAST_CPU
#define GBA_FAST_CPU ((GBA_FAST_CORE || GBA_DYNAREC))
#endif
#ifndef GBA_FAST_WAIT
#define GBA_FAST_WAIT (GBA_FAST_CORE)
#endif
#if GBA_DYNAREC_PERSISTENT_READ_WAIT && !GBA_FAST_WAIT
#error "persistent read wait preservation requires GBA_FAST_WAIT"
#endif
#ifndef GBA_VIDEO_CACHE
#define GBA_VIDEO_CACHE (GBA_FAST_CORE)
#endif
#ifndef GBA_FAST_ALU
#define GBA_FAST_ALU (GBA_FAST_CORE)
#endif
#ifndef GBA_FAST_BLOCKS
#define GBA_FAST_BLOCKS ((GBA_FAST_CORE && !GBA_DYNAREC))
#endif
#if (GBA_FAST_BLOCKS != 0 && GBA_FAST_BLOCKS != 1) || (GBA_FAST_BLOCKS && (!GBA_FAST_CPU || !GBA_FAST_ALU))
#error "register blocks require GBA_FAST_CPU and GBA_FAST_ALU"
#endif
#if (GBA_FAST_CPU != 0 && GBA_FAST_CPU != 1) || (GBA_FAST_WAIT != 0 && GBA_FAST_WAIT != 1) ||                          \
    (GBA_FAST_ALU != 0 && GBA_FAST_ALU != 1)
#error "invalid CPU optimization configuration"
#endif
#if GBA_FAST_ALU && !GBA_FAST_CPU
#error "register arithmetic spans require GBA_FAST_CPU"
#endif
#if GBA_FAST_WAIT && !GBA_FAST_CPU
#error "stable loop acceleration requires GBA_FAST_CPU"
#endif

#if GBA_DYNAREC && (!GBA_FAST_CPU || !GBA_BATCH_EVENTS || !GBA_FAST_RUN || GBA_FAST_BLOCKS)
#error "dynarec requires CPU/event execution and replaces register blocks"
#endif

#ifndef GBA_ENABLE_SOUND
#define GBA_ENABLE_SOUND 0
#endif
#ifndef GBA_USE_FS
#define GBA_USE_FS 0
#endif
#ifndef GBA_ENABLE_SAVE_STATE
#define GBA_ENABLE_SAVE_STATE 0
#endif
#ifndef GBA_ENABLE_SAVE
#define GBA_ENABLE_SAVE 1
#endif
#ifndef GBA_ENABLE_RTC
#define GBA_ENABLE_RTC 1
#endif
#ifndef GBA_BIOS_HLE
#define GBA_BIOS_HLE 1
#endif
#ifndef GBA_ROM_STREAM
#define GBA_ROM_STREAM 0
#endif
#ifndef GBA_ROM_CACHE_SLOTS
#define GBA_ROM_CACHE_SLOTS 4
#endif
#ifndef GBA_RAM_LACK
#define GBA_RAM_LACK 0
#endif
#ifndef GBA_COLOR_DEPTH
#define GBA_COLOR_DEPTH 16
#endif
#ifndef GBA_COLOR_SWAP
#define GBA_COLOR_SWAP 0
#endif
#ifndef GBA_FRAME_SKIP
#define GBA_FRAME_SKIP 0
#endif
#ifndef GBA_APU_SAMPLE_RATE
#define GBA_APU_SAMPLE_RATE 32000
#endif
#ifndef GBA_APU_BUFFER_SAMPLES
#define GBA_APU_BUFFER_SAMPLES 256
#endif
#ifndef GBA_TEST_MODE
#define GBA_TEST_MODE 0
#endif
#ifndef GBA_TEST_PROFILE
#define GBA_TEST_PROFILE 0
#endif
#ifndef GBA_PATH_MAX
#define GBA_PATH_MAX 256
#endif
#if GBA_COLOR_DEPTH == 16
typedef uint16_t gba_color_t;
#elif GBA_COLOR_DEPTH == 32
typedef uint32_t gba_color_t;
#else
#error "GBA_COLOR_DEPTH must be 16 or 32"
#endif
#if GBA_RAM_LACK != 0 && GBA_RAM_LACK != 1
#error "GBA_RAM_LACK must be 0 or 1"
#endif
#if GBA_ROM_CACHE_SLOTS < 1 || GBA_APU_SAMPLE_RATE < 8000 || GBA_APU_SAMPLE_RATE > 96000
#error "invalid cache or audio configuration"
#endif
#if GBA_APU_BUFFER_SAMPLES < 1 || GBA_APU_BUFFER_SAMPLES > 65535 || GBA_FRAME_SKIP < 0 || GBA_FRAME_SKIP > 255
#error "invalid audio buffer or frame skip configuration"
#endif
#if GBA_ENABLE_SAVE_STATE && !GBA_USE_FS
#error "file save states require GBA_USE_FS"
#endif
#define GBA_WIDTH            240
#define GBA_HEIGHT           160
#define GBA_DRAW_LINES       (GBA_HEIGHT / (GBA_RAM_LACK + 1))
#define GBA_DRAW_SIZE        (GBA_WIDTH * GBA_DRAW_LINES)
#define GBA_CPU_CLOCK        16777216u
#define GBA_CYCLES_PER_LINE  1232u
#define GBA_HBLANK_START     1006u
#define GBA_LINES_PER_FRAME  228u
#define GBA_CYCLES_PER_FRAME (GBA_CYCLES_PER_LINE * GBA_LINES_PER_FRAME)
#define GBA_ROM_PAGE_SIZE    8192u
#ifndef GBA_SAVE_CAPACITY
#define GBA_SAVE_CAPACITY (GBA_ENABLE_SAVE ? 131072u : 0u)
#endif
#if GBA_SAVE_CAPACITY > 131072 || (GBA_ENABLE_SAVE && GBA_SAVE_CAPACITY < 512) ||                                      \
    (!GBA_ENABLE_SAVE && GBA_SAVE_CAPACITY)
#error "invalid save capacity"
#endif
/* Compiler attributes; platform definitions may override these. */
#ifndef GBA_INLINE
#define GBA_INLINE static inline
#endif
#ifndef GBA_FORCE_INLINE
#if defined(_MSC_VER)
#define GBA_FORCE_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
#define GBA_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define GBA_FORCE_INLINE static inline
#endif
#ifndef GBA_NOINLINE
#if defined(_MSC_VER)
#define GBA_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
#define GBA_NOINLINE __attribute__((noinline))
#else
#define GBA_NOINLINE
#endif
#endif
#endif
#ifndef GBA_ALIGN
#if defined(_MSC_VER)
#define GBA_ALIGN(n) __declspec(align(n))
#elif defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
#define GBA_ALIGN(n) __attribute__((aligned(n)))
#elif defined(__cplusplus)
#define GBA_ALIGN(n) alignas(n)
#else
#define GBA_ALIGN(n) _Alignas(n)
#endif
#endif
#ifndef GBA_WEAK
#if defined(__GNUC__) || defined(__ARMCC_VERSION)
#define GBA_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define GBA_WEAK __weak
#else
#define GBA_WEAK
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct gba gba_t;

enum {
    GBA_OK = 0,
    GBA_ERROR = -1,
    GBA_ERROR_ALLOC = -2,
    GBA_ERROR_ROM = -3,
    GBA_ERROR_IO = -4,
    GBA_ERROR_UNSUPPORTED = -5,
    GBA_ERROR_FORMAT = -6,
    GBA_ERROR_MISMATCH = -7,
    GBA_ERROR_MEMORY = -8,
    GBA_ERROR_BUSY = -9
};
void *gba_malloc(size_t size);
void gba_free(void *address);
void *gba_memcpy(void *destination, const void *source, size_t size);
void *gba_memset(void *destination, int value, size_t size);
int gba_log_printf(const char *format, ...);
uint32_t gba_get_ticks(void);
int gba_draw(int x1, int y1, int x2, int y2, gba_color_t *pixels);
int gba_sound_output(uint8_t *buffer, size_t bytes);
#if GBA_USE_FS
typedef void gba_file_t;
gba_file_t *gba_fopen(const char *path, const char *mode);
size_t gba_fread(void *data, size_t size, size_t count, gba_file_t *file);
size_t gba_fwrite(const void *data, size_t size, size_t count, gba_file_t *file);
int gba_fseek(gba_file_t *file, long offset, int origin);
long gba_ftell(gba_file_t *file);
int gba_fclose(gba_file_t *file);
int gba_file_sync(gba_file_t *file);
int gba_file_replace(const char *temporary, const char *destination);
int gba_file_remove(const char *path);
int gba_file_exists(const char *path);
#endif
#ifdef __cplusplus
}
#endif
