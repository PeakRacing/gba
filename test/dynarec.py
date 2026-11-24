#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Build and execute native ARMv5TE code on QEMU ARM926 (never board performance)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import threading

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--toolchain',type=Path,default=Path('F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin'))
p.add_argument('--qemu',type=Path,default=Path('C:/Program Files/qemu/qemu-system-arm.exe'))
p.add_argument('--frames',type=int,default=60)
p.add_argument('--rom-directory',type=Path)
p.add_argument('--rom',type=Path,action='append',default=[])
p.add_argument('--no-input',action='store_true')
p.add_argument('--state',type=Path,help='Load the same host GAST seed into both engines; requires exactly one --rom')
p.add_argument('--wait-diag',action='store_true')
p.add_argument('--references',action='store_true')
p.add_argument('--fixtures-only',action='store_true',help='Skip established synthetic matrices for a focused state/ROM replay')
p.add_argument('--progress',action='store_true',help='Print replay frame/PC progress; diagnostic tests only')
p.add_argument('--instruction-compare',action='store_true')
p.add_argument('--fast-core',action='store_true')
p.add_argument('--fast-wait',action='store_true')
p.add_argument('--stream',action='store_true')
p.add_argument('--chain',action='store_true')
p.add_argument('--persistent',action='store_true')
p.add_argument('--turbo',action='store_true')
p.add_argument('--turbo-tlb',action='store_true')
p.add_argument('--page-index',action='store_true')
p.add_argument('--turbo-light',action='store_true')
p.add_argument('--local-alu',action='store_true')
p.add_argument('--hle-irq',action='store_true')
p.add_argument('--local-registers',type=int,choices=(1,2),default=2)
p.add_argument('--light-pool-compare',action='store_true')
p.add_argument('--turbo-trace-insns',type=int,default=64)
p.add_argument('--no-turbo-trace-fusion',action='store_true')
p.add_argument('--turbo-prelink',type=int,default=None)
p.add_argument('--turbo-reg-map',type=int,choices=range(4),default=None)
p.add_argument('--turbo-trace-diag',action='store_true')
p.add_argument('--persistent-literals',action='store_true')
p.add_argument('--literal-leaf',action='store_true')
p.add_argument('--persistent-multi',action='store_true')
p.add_argument('--direct-bl',action='store_true')
p.add_argument('--direct-bl-proof',action='store_true')
p.add_argument('--persistent-tail',action='store_true')
p.add_argument('--inline-refill',action='store_true')
p.add_argument('--direct-branch',action='store_true')
p.add_argument('--branch-superblock',action='store_true')
p.add_argument('--branch-proof',action='store_true')
p.add_argument('--block-insns',type=int,default=None)
p.add_argument('--shared-exits',action='store_true')
p.add_argument('--map-helper',action='store_true')
p.add_argument('--page-table',action='store_true')
p.add_argument('--rom-window',action='store_true')
p.add_argument('--read-wait',action='store_true')
p.add_argument('--read-wait-proof',action='store_true')
p.add_argument('--indirect-bits',type=int,choices=range(6,12),default=None)
p.add_argument('--indirect-capacity-proof',action='store_true')
p.add_argument('--map-far-call',action='store_true',help='Exercise absolute BLX mapping calls for caller-owned arenas beyond relative-branch reach')
p.add_argument('--tail-proof',action='store_true',help='Load followed by multiply-accumulate deadline boundary regression')
p.add_argument('--literal-compare',action='store_true',help='Compare native ROM literal specialization on/off in otherwise identical approximate engines')
p.add_argument('--persistent-diag',action='store_true')
p.add_argument('--persistent-proof',action='store_true')
p.add_argument('--scanline-fast',action='store_true')
p.add_argument('--scanline-effects',action='store_true')
p.add_argument('--text-spans',action='store_true')
p.add_argument('--lazy-mix',action='store_true')
p.add_argument('--scanline-runtime',action='store_true',help='Enable scanline fast configuration without render-reference instrumentation')
p.add_argument('--scanline-counters',action='store_true',help='Count scanline fast acceptance and fallback reasons')
p.add_argument('--hot-blocks',action='store_true',help='Count native block entries by guest PC')
p.add_argument('--identity-effects',action='store_true',help='Enable pixel-exact identity alpha/brightness/mosaic scanline cases')
p.add_argument('--palette-dedup',action='store_true')
p.add_argument('--zero-rows',action='store_true')
p.add_argument('--direct',action='store_true')
p.add_argument('--soft-timers',action='store_true')
p.add_argument('--interior',action='store_true')
p.add_argument('--replica-validation',action='store_true',help='Two independent approximate engines: determinism/runtime, not interpreter equivalence')
p.add_argument('--candidate-only',action='store_true',help='Performance-only fixed replay; no interpreter comparison')
p.add_argument('--slice-cycles',type=int,default=0,help='Execute fixtures through bounded cycle calls, matching the port control API')
p.add_argument('--perf-warm-frames',type=int,help='Candidate-only timed window after this many replay calls')
p.add_argument('--fast-loop-proof',action='store_true')
p.add_argument('--wide-rom',action='store_true')
p.add_argument('--long-loops',action='store_true')
p.add_argument('--fast-bus',action='store_true')
p.add_argument('--optimization',choices=('2','3','s'),default='2')
p.add_argument('--event-slip',type=int,default=0)
p.add_argument('--chain-trace',action='store_true')
p.add_argument('--rom-chain',action='store_true')
p.add_argument('--coarse-chain',action='store_true')
p.add_argument('--chain-rejects',action='store_true')
p.add_argument('--link-cache',action='store_true')
p.add_argument('--tiny-blocks',action='store_true')
p.add_argument('--bl-prefix',action='store_true')
p.add_argument('--refill-match',action='store_true')
p.add_argument('--bulk-prefill',action='store_true')
p.add_argument('--line-invalidate',action='store_true')
p.add_argument('--fixed-map',type=int,choices=(0,1,2),default=0)
p.add_argument('--rom-trace',action='store_true')
p.add_argument('--trace-links',action='store_true')
p.add_argument('--trace-context',action='store_true')
p.add_argument('--fastcall',action='store_true')
p.add_argument('--video-stores',action='store_true')
p.add_argument('--dma-bound-proof',action='store_true')
p.add_argument('--hard-cache',action='store_true')
p.add_argument('--hard-cache-compare',action='store_true')
p.add_argument('--rom-edge-proof',action='store_true')
p.add_argument('--inline-ram',action='store_true')
p.add_argument('--rom-prefix',action='store_true')
p.add_argument('--partial-registers',action='store_true')
p.add_argument('--inline-stores',action='store_true')
p.add_argument('--inline-multiple',action='store_true')
p.add_argument('--inline-pc',action='store_true')
p.add_argument('--negative-cache',action='store_true')
p.add_argument('--tile-direct',action='store_true')
p.add_argument('--ordered-ppu',action='store_true')
p.add_argument('--obj-spans',action='store_true')
p.add_argument('--obj-index',action='store_true')
p.add_argument('--oam-dirty',action='store_true')
p.add_argument('--oam-dirty-runtime',action='store_true',help='Enable OAM dirty tracking without render-reference instrumentation')
p.add_argument('--obj-index-runtime',action='store_true',help='Enable OBJ band indexing without render-reference instrumentation')
p.add_argument('--sorted-blend',action='store_true')
p.add_argument('--front-cull',action='store_true')
p.add_argument('--ppu-read-dedup',action='store_true')
p.add_argument('--no-stats',action='store_true')
p.add_argument('--code-kb',type=int,default=256)
p.add_argument('--recent-bits',type=int,default=6)
p.add_argument('--data-kb',type=int,default=64)
p.add_argument('--timeout',type=int,default=7200)
p.add_argument('--label',default='native')
p.add_argument('--core-source',type=Path,help='Validate an immutable imported inc/src snapshot')
a=p.parse_args()
if a.indirect_bits is None:a.indirect_bits=10 if a.turbo else 6
if a.turbo_prelink is None:a.turbo_prelink=0
if a.turbo_reg_map is None:a.turbo_reg_map=3 if a.turbo else 0
if a.block_insns is None:a.block_insns=a.turbo_trace_insns if a.turbo else 16
if not 0<=a.turbo_prelink<=16:p.error('--turbo-prelink must be 0..16')
if a.replica_validation and (not a.fixtures_only or not a.event_slip or a.instruction_compare):p.error('replica validation requires event slip and fixtures-only')
if a.candidate_only and (not a.fixtures_only or a.instruction_compare or a.replica_validation):p.error('candidate-only performance requires fixtures-only and excludes comparison modes')
if a.literal_compare and not (a.replica_validation and a.persistent_literals and a.persistent and a.fast_bus):p.error('literal compare requires persistent literals and FAST_BUS replica mode')
if a.literal_compare and (a.rom or a.rom_directory or a.state or a.references):p.error('literal compare is synthetic-only; run ROM replay with one uniform configuration')
if a.light_pool_compare and not (a.turbo_light and a.replica_validation):p.error('pool comparison requires LIGHT replica mode')
if a.slice_cycles and not 1<=a.slice_cycles<=4096:p.error('slice cycles must be 1..4096')
if a.perf_warm_frames is not None and not (a.candidate_only and 0<=a.perf_warm_frames<a.frames and len(a.rom)==1 and not a.references and not a.rom_directory):p.error('perf window requires candidate-only, one ROM, and warm frames below total')
if a.turbo and a.persistent:p.error('turbo and persistent backends are mutually exclusive')
if a.turbo_tlb and not a.turbo:p.error('turbo TLB requires turbo execution')
if a.persistent_proof and (not (a.persistent or a.turbo) or a.event_slip):p.error('persistent proof requires exact native execution')
if a.event_slip and not (a.replica_validation or a.candidate_only):p.error('event-slip accuracy differs; explicitly select replica-validation or candidate-only for bounded runtime checks')
if a.chain_rejects and not a.coarse_chain:p.error('chain-rejects requires coarse-chain')
if a.hard_cache_compare and not (a.hard_cache and a.replica_validation):p.error('hard-cache-compare requires hard-cache and replica-validation')
if not 1<=a.frames<=36000: p.error('--frames must be between 1 and 36000')
if a.timeout<1: p.error('--timeout must be positive')
if a.state and (not a.state.is_file() or len(a.rom)!=1 or a.references or a.rom_directory):p.error('--state requires one explicit ROM and an existing state')
if not 16<=a.code_kb<=8192 or not 16<=a.data_kb<=4096: p.error('invalid native cache size')
if not re.fullmatch(r'[A-Za-z0-9_-]+',a.label): p.error('invalid output label')
root=Path(__file__).resolve().parents[1]
core=(a.core_source or root).resolve()
if not (core/'inc/gba.h').is_file() or not (core/'src/gba.c').is_file():p.error('invalid core snapshot')
os.chdir(root)
out=root/'test/out/dynarec'
out.mkdir(parents=True,exist_ok=True)
if any((out/(a.label+suffix)).exists() for suffix in ('.json','.elf','.log','-fixtures.bin')):
    p.error('preserve previous native evidence; choose a new --label')
cases=[]
inputs=json.loads((root/'test/game-inputs.json').read_text(encoding='utf-8'))
if a.references:
    reference=root/'tools/reference/gba-tests'
    revision=subprocess.check_output(['git','-C',str(reference),'rev-parse','HEAD'],text=True).strip()
    if revision!='a7113b67e63f83a9b321696ddd7042ccfad6c881': p.error('fixed reference revision mismatch')
    reference_roms=sorted(reference.rglob('*.gba'))
    if len(reference_roms)!=13: p.error('expected 13 fixed reference ROMs')
    cases += [(x,5,{}) for x in reference_roms]
if a.rom_directory:
    if not a.rom_directory.is_dir() or not list(a.rom_directory.glob('*.gba')):p.error('ROM directory is missing or contains no .gba fixtures')
    cases += [(x,a.frames,inputs.get(x.stem,{})) for x in sorted(a.rom_directory.glob('*.gba'))]
cases += [(x,a.frames,inputs.get(x.stem,{})) for x in a.rom]
if a.no_input:cases=[(x,n,{}) for x,n,_ in cases]
manifest=bytearray(struct.pack('<I',len(cases)))
rows=[]
for path,frames,script in cases:
    path=path.resolve()
    active=[]
    for frame in script.get('StartFrames',[]): active.append((frame,4,8))
    for frame in range(180,frames,60):
        if frame not in script.get('StartFrames',[]) and not any(lo<=frame<hi for lo,hi in script.get('SkipA',[])):
            active.append((frame,4,1))
    active += [tuple(x) for x in script.get('Directions',[])]+[tuple(x) for x in script.get('ExtraEvents',[])]
    transitions=[]
    last=-1
    for frame in range(frames):
        keys=0
        for start,duration,mask in active:
            if start<=frame<start+duration: keys|=mask
        if keys!=last: transitions.append((frame,keys));last=keys
    name=path.as_posix().encode('utf-8')
    manifest+=struct.pack('<III',len(name),frames,len(transitions))+name
    manifest+=b''.join(struct.pack('<II',*event) for event in transitions)
    rows.append(dict(rom=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),frames=frames,
                     state_sha256=hashlib.sha256(a.state.read_bytes()).hexdigest() if a.state else None,
                     input_sha256=hashlib.sha256(json.dumps(transitions).encode()).hexdigest()))
manifest_path=out/(a.label+'-fixtures.bin')
manifest_path.write_bytes(manifest)
cc=a.toolchain/'arm-none-eabi-gcc.exe'
elf=out/(a.label+'.elf')
command=[str(cc),'-std=c11','-O'+a.optimization,'-Wall','-Wextra','-Werror','-mcpu=arm926ej-s','-marm','-fno-builtin',
         '-ffunction-sections','-fdata-sections','-fstack-usage','-DGBA_DYNAREC=1','-DGBA_ARM926_EXECUTABLE_RAM=1','-Itest/dynarec','-I'+str(core/'inc'),'-I'+str(core/'src'),'-I'+str(core/'port')]
command+=['-DGBA_QEMU_MANIFEST="'+manifest_path.as_posix()+'"']
if a.state:command+=['-DGBA_QEMU_STATE="'+a.state.resolve().as_posix()+'"']
command += ['-DGBA_DYNAREC_CODE_SIZE='+str(a.code_kb*1024),'-DGBA_DYNAREC_DATA_SIZE='+str(a.data_kb*1024)]
command+=['-DGBA_DYNAREC_RECENT_BITS='+str(a.recent_bits)]
if a.chain:command+=['-DGBA_DYNAREC_CHAIN=1']
if a.persistent:command+=['-DGBA_DYNAREC_PERSISTENT=1']
if a.turbo:command+=['-DGBA_DYNAREC_TURBO=1']
if a.turbo_light:command+=['-DGBA_DYNAREC_TURBO_LIGHT=1']
if a.hle_irq:command+=['-DGBA_DYNAREC_PERSISTENT_HLE_IRQ=1']
if a.hle_irq and not a.candidate_only:command+=['-DGBA_TEST_HLE_IRQ_ADMISSION_COUNT=1']
if a.local_alu:command+=['-DGBA_DYNAREC_TURBO_LOCAL_ALU=1','-DGBA_DYNAREC_TURBO_LOCAL_REGS='+str(a.local_registers)]
if a.local_alu and not a.candidate_only:command+=['-DGBA_TEST_LOCAL_ALU_COUNT=1']
if a.light_pool_compare:command+=['-DGBA_TEST_LIGHT_POOL_COMPARE=1']
if a.turbo_tlb:command+=['-DGBA_DYNAREC_TURBO_TLB=1']
if a.page_index:command+=['-DGBA_DYNAREC_PAGE_INDEX=1']
if a.page_index and not a.candidate_only:command+=['-DGBA_TEST_PAGE_INDEX=1']
command+=['-DGBA_DYNAREC_TURBO_TRACE_INSNS='+str(a.turbo_trace_insns)]
if a.no_turbo_trace_fusion:command+=['-DGBA_DYNAREC_TURBO_TRACE_FUSION=0']
command+=['-DGBA_DYNAREC_TURBO_PRELINK='+str(a.turbo_prelink)]
command+=['-DGBA_DYNAREC_TURBO_REGISTER_MAP='+str(a.turbo_reg_map)]
if a.turbo_trace_diag:command+=['-DGBA_TEST_TURBO_TRACE_DIAG=1']
if a.persistent_literals:command+=['-DGBA_DYNAREC_PERSISTENT_LITERALS=1']
if a.literal_leaf:command+=['-DGBA_DYNAREC_PERSISTENT_LITERAL_LEAF=1']
if a.persistent_multi:command+=['-DGBA_DYNAREC_PERSISTENT_MULTI=1']
if a.direct_bl:command+=['-DGBA_DYNAREC_PERSISTENT_BL_DIRECT=1']
if a.direct_bl_proof:command+=['-DGBA_TEST_DIRECT_BL_PROOF=1']
if a.persistent_tail:command+=['-DGBA_DYNAREC_PERSISTENT_TAIL=1']
if a.inline_refill:command+=['-DGBA_DYNAREC_PERSISTENT_INLINE_REFILL=1']
if a.direct_branch:command+=['-DGBA_DYNAREC_PERSISTENT_DIRECT_BRANCH=1']
if a.branch_superblock:command+=['-DGBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK=1']
if a.branch_proof:command+=['-DGBA_TEST_BRANCH_SUPERBLOCK_PROOF=1']
command+=['-DGBA_DYNAREC_PERSISTENT_BLOCK_INSNS='+str(a.block_insns)]
if a.shared_exits:command+=['-DGBA_DYNAREC_PERSISTENT_SHARED_EXITS=1']
if a.map_helper:command+=['-DGBA_DYNAREC_PERSISTENT_MAP_HELPER=1']
if a.page_table:command+=['-DGBA_DYNAREC_PERSISTENT_PAGE_TABLE=1']
if a.rom_window:command+=['-DGBA_DYNAREC_PERSISTENT_ROM_WINDOW=1']
if a.read_wait:command+=['-DGBA_DYNAREC_PERSISTENT_READ_WAIT=1']
if a.read_wait_proof:command+=['-DGBA_TEST_READ_WAIT_PROOF=1']
command+=['-DGBA_DYNAREC_PERSISTENT_INDIRECT_BITS='+str(a.indirect_bits)]
if a.indirect_capacity_proof:command+=['-DGBA_TEST_INDIRECT_CAPACITY_PROOF=1']
if a.map_far_call:
    assert a.map_helper,'--map-far-call requires --map-helper'
    command+=['-DGBA_TEST_MAP_FAR_CALL=1']
if a.tail_proof:command+=['-DGBA_TEST_PERSISTENT_TAIL_PROOF=1']
if a.literal_compare:command+=['-DGBA_TEST_LITERAL_COMPARE=1']
if a.persistent_diag:command+=['-DGBA_TEST_PERSISTENT_DIAG=1']
if a.persistent_proof:command+=['-DGBA_TEST_PERSISTENT_PROOF=1','-DGBA_QEMU_FIXTURES_ONLY=1']
if a.scanline_effects:command+=['-DGBA_PPU_SCANLINE_EFFECTS=1']
if a.text_spans:command+=['-DGBA_PPU_SCANLINE_SPANS=1']
if a.lazy_mix:command+=['-DGBA_APU_LAZY_MIX=1']
if a.scanline_fast:command+=['-DGBA_PPU_SCANLINE_FAST=1','-DGBA_TEST_RENDER_REFERENCE=1']
if a.scanline_runtime:command+=['-DGBA_PPU_SCANLINE_FAST=1']
if a.scanline_counters:command+=['-DGBA_TEST_SCANLINE_COUNTERS=1']
if a.hot_blocks:command+=['-DGBA_TEST_HOT_BLOCKS=1']
if a.identity_effects:
    assert a.scanline_fast or a.scanline_runtime,'--identity-effects requires scanline fast execution'
    command+=['-DGBA_PPU_SCANLINE_IDENTITY=1']
if a.palette_dedup:command+=['-DGBA_PPU_SCANLINE_PALETTE_DEDUP=1']
if a.zero_rows:command+=['-DGBA_PPU_SCANLINE_ZERO_ROWS=1']
if a.direct:command+=['-DGBA_DYNAREC_DIRECT=1']
if a.soft_timers:command+=['-DGBA_DYNAREC_SOFT_TIMERS=1']
if a.interior:command+=['-DGBA_DYNAREC_INTERIOR=1']
if a.replica_validation:command+=['-DGBA_TEST_REPLICA_VALIDATION=1']
if a.candidate_only:command+=['-DGBA_TEST_CANDIDATE_ONLY=1']
if a.fast_loop_proof:command+=['-DGBA_TEST_FAST_LOOP_PROOF=1']
if a.wide_rom:command+=['-DGBA_DYNAREC_WIDE_ROM=1']
if a.long_loops:command+=['-DGBA_DYNAREC_LONG_LOOPS=1']
if a.fast_bus:command+=['-DGBA_DYNAREC_FAST_BUS=1']
if a.event_slip:command+=['-DGBA_DYNAREC_EVENT_SLIP='+str(a.event_slip)]
if a.chain_trace:command+=['-DGBA_TEST_CHAIN_TRACE=1']
if a.rom_chain:command+=['-DGBA_DYNAREC_ROM_CHAIN=1']
if a.coarse_chain:command+=['-DGBA_DYNAREC_COARSE_CHAIN=1']
if a.chain_rejects:command+=['-DGBA_TEST_CHAIN_REJECTS=1']
if a.link_cache:command+=['-DGBA_DYNAREC_LINK_CACHE=1']
if a.tiny_blocks:command+=['-DGBA_DYNAREC_TINY_BLOCKS=1']
if a.bl_prefix:command+=['-DGBA_DYNAREC_BL_PREFIX=1']
if a.refill_match:command+=['-DGBA_DYNAREC_REFILL_MATCH=1']
if a.line_invalidate:command+=['-DGBA_DYNAREC_LINE_INVALIDATE=1']
if a.fixed_map:command+=['-DGBA_DYNAREC_FIXED_MAP='+str(a.fixed_map)]
if a.rom_trace:command+=['-DGBA_DYNAREC_ROM_TRACE=1']
if a.trace_links:command+=['-DGBA_DYNAREC_TRACE_LINKS=1']
if a.trace_context:command+=['-DGBA_DYNAREC_TRACE_CONTEXT=1']
if a.video_stores:command+=['-DGBA_DYNAREC_VIDEO_STORE=1','-DGBA_TEST_VIDEO_STORE_PROOF=1']
if a.dma_bound_proof:command+=['-DGBA_TEST_DMA_HARD_PROOF=1']
if a.hard_cache:command+=['-DGBA_DYNAREC_HARD_CACHE=1']
if a.hard_cache_compare:command+=['-DGBA_TEST_HARD_CACHE_COMPARE=1']
if a.rom_edge_proof:command+=['-DGBA_TEST_ROM_EDGE_PROOF=1']
if a.fastcall:command+=['-DGBA_DYNAREC_FASTCALL=1','-fomit-frame-pointer']
if a.bulk_prefill:command+=['-DGBA_DYNAREC_BULK_PREFILL=1','-DGBA_TEST_BULK_PREFILL_PROOF=1']
else:command+=['-DGBA_DYNAREC_BULK_PREFILL=0']
if a.inline_ram:command+=['-DGBA_DYNAREC_RAM_INLINE=1']
if a.rom_prefix:command+=['-DGBA_DYNAREC_ROM_PREFIX=1']
if a.partial_registers:command+=['-DGBA_DYNAREC_PARTIAL_REGS=1']
if a.inline_stores:command+=['-DGBA_DYNAREC_STORE_INLINE=1']
if a.inline_multiple:command+=['-DGBA_DYNAREC_MULTI_INLINE=1']
if a.inline_pc:command+=['-DGBA_DYNAREC_MULTI_PC=1','-DGBA_TEST_INLINE_PC_PROOF=1']
if a.negative_cache:command+=['-DGBA_DYNAREC_NEGATIVE=1']
if a.tile_direct:command+=['-DGBA_PPU_TILE_DIRECT=1']
if a.obj_spans:command+=['-DGBA_PPU_OBJ_SPANS=1']
if a.obj_index:command+=['-DGBA_PPU_OBJ_INDEX=1','-DGBA_TEST_RENDER_REFERENCE=1']
if a.oam_dirty:command+=['-DGBA_PPU_OAM_DIRTY=1','-DGBA_TEST_RENDER_REFERENCE=1']
if a.oam_dirty_runtime:command+=['-DGBA_PPU_OAM_DIRTY=1']
if a.obj_index_runtime:command+=['-DGBA_PPU_OBJ_INDEX=1']
if a.front_cull:command+=['-DGBA_PPU_FRONT_CULL=1','-DGBA_TEST_RENDER_REFERENCE=1']
if a.sorted_blend:command+=['-DGBA_PPU_SORTED_BLEND=1','-DGBA_TEST_RENDER_REFERENCE=1']
if a.ordered_ppu:command+=['-DGBA_PPU_ORDERED=1','-DGBA_TEST_RENDER_REFERENCE=1']
if a.ppu_read_dedup:command+=['-DGBA_PPU_READ_DEDUP=1']
if a.fixtures_only:command+=['-DGBA_QEMU_FIXTURES_ONLY=1']
if a.slice_cycles:command+=['-DGBA_QEMU_SLICE_CYCLES='+str(a.slice_cycles)]
if a.perf_warm_frames is not None:command+=['-DGBA_QEMU_PERF_WARM='+str(a.perf_warm_frames)]
if a.progress:command+=['-DGBA_QEMU_PROGRESS=1']
if a.instruction_compare: command+=['-DGBA_JIT_COMPARE_STEPS=1']
if not a.no_stats: command+=['-DGBA_DYNAREC_STATS=1']
if a.fast_core: command+=['-DGBA_FAST_CORE=1']
if a.fast_wait: command+=['-DGBA_FAST_WAIT=1']
if a.stream: command+=['-DGBA_ROM_STREAM=1']
if a.wait_diag: command+=['-include','test/dynarec/waitdiag.h']
command += [str(x) for x in sorted((core/'src').glob('*.c'))]
command += ['test/dynarec/main.c','test/dynarec/files.c','test/dynarec/start.s','-nostdlib',
            '-Wl,--gc-sections,-T,test/dynarec/link.ld','-lgcc','-o',str(elf)]
def source_hashes():
    return {str(x.relative_to(core)):hashlib.sha256(x.read_bytes()).hexdigest()
            for folder in ('inc','src','port') for x in sorted((core/folder).glob('*')) if x.is_file()}
core_hashes=source_hashes()
harness=out/(a.label+'-harness');harness.mkdir()
test_hashes={}
for name in ('main.c','files.c','start.s','link.ld','gba_conf.h'):
    data=(root/'test/dynarec'/name).read_bytes();(harness/name).write_bytes(data)
    test_hashes[name]=hashlib.sha256(data).hexdigest()
runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
compiler_command=list(command)
subprocess.run(command,check=True)
assert source_hashes()==core_hashes,'Core source changed during compilation'
assert all(hashlib.sha256((root/'test/dynarec'/name).read_bytes()).hexdigest()==digest
           for name,digest in test_hashes.items()),'Harness changed during compilation'
version=subprocess.check_output([str(a.qemu),'--version'],text=True).splitlines()[0]
command=[str(a.qemu),'-M','versatilepb','-global','pl041.audiodev=none','-audiodev','none,id=none',
         '-cpu','arm926','-m','128M','-kernel',str(elf),'-display','none','-serial','stdio','-monitor','none',
         '-semihosting-config','enable=on,target=native']
execution_command=list(command)
start=time.monotonic();perf_begin=None;perf_window=None
with (out/(a.label+'.log')).open('w',encoding='utf-8') as log:
    process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    timer=threading.Timer(a.timeout,process.kill);timer.start()
    try:
        for line in process.stdout:
            print(line,end='',flush=True);log.write(line);log.flush()
            if line.startswith('PERF BEGIN '):perf_begin=(time.monotonic(),line.strip())
            if line.startswith('PERF END ') and perf_begin:perf_window=dict(seconds=time.monotonic()-perf_begin[0],begin=perf_begin[1],end=line.strip())
            if time.monotonic()-start>a.timeout:
                process.kill();raise TimeoutError('QEMU validation time limit')
        code=process.wait(timeout=30)
    finally:
        timer.cancel()
        if process.poll() is None: process.kill();process.wait()
text=(out/(a.label+'.log')).read_text()
m=re.search(r'PASS assertions=([0-9a-f]+) failures=([0-9a-f]+) native=([0-9a-f]+) fallback=([0-9a-f]+) compiled=([0-9a-f]+) flushes=([0-9a-f]+) helper=([0-9a-f]+) hits=([0-9a-f]+) code_bytes=([0-9a-f]+)',text)
passed=code==0 and m is not None and int(m[2],16)==0 and (a.no_stats or int(m[3],16)>0) and text.count('ROM PASS')==len(cases)
result=dict(oam_dirty=a.oam_dirty,obj_index=a.obj_index,hard_cache_compare=a.hard_cache_compare,hard_cache=a.hard_cache,front_cull=a.front_cull,trace_context=a.trace_context,rom_edge_proof=a.rom_edge_proof,trace_links=a.trace_links,dma_bound_proof=a.dma_bound_proof,progress=a.progress,video_stores=a.video_stores,fastcall=a.fastcall,rom_trace=a.rom_trace,fixed_map=a.fixed_map,sorted_blend=a.sorted_blend,line_invalidate=a.line_invalidate,bulk_prefill=a.bulk_prefill,refill_match=a.refill_match,obj_spans=a.obj_spans,bl_prefix=a.bl_prefix,tiny_blocks=a.tiny_blocks,link_cache=a.link_cache,chain_rejects=a.chain_rejects,long_loops=a.long_loops,inline_pc=a.inline_pc,coarse_chain=a.coarse_chain,passed=passed,emulator=version,host_cpu='ARM926 via QEMU; not native board performance',validation_kind='deadline cache on/off equivalence in approximate mode; not timing accuracy acceptance' if a.hard_cache_compare else 'two independent approximate engines; not accuracy acceptance' if a.replica_validation else 'candidate-only fixed-work performance; no interpreter comparison' if a.candidate_only else 'interpreter equivalence',
            turbo=a.turbo,turbo_tlb=a.turbo_tlb,turbo_trace_insns=a.turbo_trace_insns,turbo_trace_fusion=not a.no_turbo_trace_fusion,turbo_prelink=a.turbo_prelink,
            turbo_register_map=a.turbo_reg_map,
            seconds=time.monotonic()-start,fast_core=a.fast_core,stream=a.stream,chain_enabled=a.chain,direct_enabled=a.direct,soft_timers=a.soft_timers,interior_entries=a.interior,inline_multiple=a.inline_multiple,inline_stores=a.inline_stores,partial_registers=a.partial_registers,rom_prefix=a.rom_prefix,inline_ram=a.inline_ram,rom_chain=a.rom_chain,event_slip=a.event_slip,fast_bus=a.fast_bus,wide_rom=a.wide_rom,fast_loop_proof=a.fast_loop_proof,optimization=a.optimization,negative_cache=a.negative_cache,ordered_ppu=a.ordered_ppu,tile_direct=a.tile_direct,ppu_read_dedup=a.ppu_read_dedup,fixtures_only=a.fixtures_only,code_bytes=a.code_kb*1024,data_bytes=a.data_kb*1024,recent_bits=a.recent_bits,statistics_enabled=not a.no_stats,fixtures=rows,
            elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
            core_sha256=core_hashes,
            core_source=str(core),
            compiler_command=compiler_command,execution_command=execution_command,
            runner_sha256=runner_sha256,harness_sha256=test_hashes,
            counters=dict(zip(('assertions','failures','native','fallback','compiled','flushes','helper','cache_hits','code_bytes'),
                              [int(x,16) for x in m.groups()])) if m else None)
result['turbo_light']=a.turbo_light
result['slice_cycles']=a.slice_cycles
result['perf_window']=perf_window
result['hle_irq_admission']=a.hle_irq
result['local_alu']=a.local_alu
result['light_pool_compare']=a.light_pool_compare
result['page_index']=a.page_index
result['candidate_only']=a.candidate_only
result['persistent']=a.persistent;result['persistent_literals']=a.persistent_literals;result['scanline_fast']=a.scanline_fast
result['scanline_runtime']=a.scanline_runtime
result['scanline_effects']=a.scanline_effects
result['literal_compare']=a.literal_compare
result['persistent_multi']=a.persistent_multi
result['direct_bl']=a.direct_bl
result['persistent_tail']=a.persistent_tail
result['branch_superblock']=a.branch_superblock
result['block_insns']=a.block_insns
result['shared_exits']=a.shared_exits
result['map_helper']=a.map_helper
result['rom_window']=a.rom_window
result['literal_leaf']=a.literal_leaf
result['read_wait']=a.read_wait
result['indirect_bits']=a.indirect_bits
geometry=re.search(r'Persistent cache geometry private=([0-9a-f]+) legacy=([0-9a-f]+) native=([0-9a-f]+) indirect=([0-9a-f]+)',text)
result['cache_geometry']=dict(zip(('private_bytes','legacy_slots','persistent_slots','indirect_slots'),[int(x,16) for x in geometry.groups()])) if geometry else None
result['identity_effects']=a.identity_effects
result['palette_dedup']=a.palette_dedup
result['zero_rows']=a.zero_rows
pm=re.search(r'Persistent guest instructions=([0-9a-f]+) entries=([0-9a-f]+) blocks=([0-9a-f]+) links=([0-9a-f]+)',text)
result['persistent_proof']=dict(zip(('instructions','entries','blocks','links'),[int(x,16) for x in pm.groups()])) if pm else None
(out/(a.label+'.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if not passed: raise SystemExit('ARM926 native validation failed; see log')
