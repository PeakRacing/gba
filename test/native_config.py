#!/usr/bin/env python3
"""Validate native option dependencies with the real ARM target preprocessor."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin/arm-none-eabi-gcc.exe')
p.add_argument('--results', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
approx = dict(GBA_DYNAREC=1, GBA_DYNAREC_EVENT_SLIP=1024,
              GBA_DYNAREC_FAST_BUS=1, GBA_DYNAREC_WIDE_ROM=1)
chain = dict(approx, GBA_DYNAREC_CHAIN=1, GBA_DYNAREC_DIRECT=1,
             GBA_DYNAREC_ROM_CHAIN=1, GBA_DYNAREC_COARSE_CHAIN=1)
valid = [dict(GBA_DYNAREC=1), approx, chain, dict(approx,GBA_DYNAREC_HARD_CACHE=1)]
valid += [dict(approx, GBA_DYNAREC_FASTCALL=1, GBA_DYNAREC_ROM_TRACE=trace,
               GBA_DYNAREC_FIXED_MAP=mapping) for trace in (0, 1) for mapping in (0, 1, 2)]
valid += [dict(approx, GBA_DYNAREC_VIDEO_STORE=1)]
valid += [dict(approx, GBA_DYNAREC_ROM_TRACE=1, GBA_DYNAREC_TRACE_LINKS=1)]
valid += [dict(approx, GBA_DYNAREC_ROM_TRACE=1, GBA_DYNAREC_TRACE_LINKS=1, GBA_DYNAREC_TRACE_CONTEXT=1)]
valid += [dict(chain, GBA_DYNAREC_LINK_CACHE=1, GBA_DYNAREC_TINY_BLOCKS=1,
               GBA_DYNAREC_REFILL_MATCH=1, GBA_DYNAREC_ROM_TRACE=1)]
cases = [(options, None) for options in valid]
for option in ('FASTCALL', 'ROM_TRACE'):
    name = 'GBA_DYNAREC_' + option
    message = 'native caller-clobber ABI' if option == 'FASTCALL' else 'ROM trace requires'
    cases.append((dict(approx, **{name: 2}), message))
    conflicts = ('CHAIN', 'INTERIOR', 'ROM_PREFIX') if option == 'FASTCALL' else ('INTERIOR', 'ROM_PREFIX')
    for conflict in conflicts:
        cases.append((dict(approx, **{name: 1, 'GBA_DYNAREC_' + conflict: 1}), message))
    if option == 'FASTCALL':
        cases.append((dict(approx, GBA_DYNAREC=0, GBA_DYNAREC_FASTCALL=1), message))
    else:
        for dependency, value in (('FAST_BUS', 0), ('WIDE_ROM', 0), ('EVENT_SLIP', 255)):
            cases.append((dict(approx, **{name: 1, 'GBA_DYNAREC_' + dependency: value}), message))
for dependency,value in (('GBA_DYNAREC',0),('GBA_DYNAREC_FAST_BUS',0),('GBA_DYNAREC_EVENT_SLIP',63),('GBA_DYNAREC_VIDEO_STORE',2)):
    cases.append((dict(approx, **{'GBA_DYNAREC_VIDEO_STORE':1,dependency:value}), 'native video stores require'))
for values in ({'GBA_DYNAREC_ROM_TRACE':0,'GBA_DYNAREC_TRACE_LINKS':1},{'GBA_DYNAREC_ROM_TRACE':1,'GBA_DYNAREC_TRACE_LINKS':1,'GBA_DYNAREC_CHAIN':1},{'GBA_DYNAREC_ROM_TRACE':1,'GBA_DYNAREC_TRACE_LINKS':2}):
    cases.append((dict(approx, **values), 'native trace links require'))
for values in ({'GBA_DYNAREC_TRACE_CONTEXT':1,'GBA_DYNAREC_TRACE_LINKS':0},{'GBA_DYNAREC_ROM_TRACE':1,'GBA_DYNAREC_TRACE_LINKS':1,'GBA_DYNAREC_TRACE_CONTEXT':2}):
    cases.append((dict(approx, **values), 'native trace context requires'))
cases += [(dict(approx,GBA_DYNAREC_HARD_CACHE=2),'native deadline cache requires'),(dict(approx,GBA_DYNAREC_HARD_CACHE=1,GBA_DYNAREC_EVENT_SLIP=0),'native deadline cache requires')]
cases += [(dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1),None),
          (dict(approx,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_LITERALS=1,GBA_DYNAREC_PERSISTENT_MAP_HELPER=1,GBA_DYNAREC_PERSISTENT_LITERAL_LEAF=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_LITERAL_LEAF=1),'literal leaf requires'),
          (dict(GBA_DYNAREC_PERSISTENT_LITERAL_LEAF=2),'literal leaf requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_SHARED_EXITS=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_SHARED_EXITS=1),'shared persistent exits require'),
          (dict(GBA_DYNAREC_PERSISTENT_SHARED_EXITS=2),'shared persistent exits require'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_MAP_HELPER=1,GBA_DYNAREC_PERSISTENT_ROM_WINDOW=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_ROM_WINDOW=1),'primary ROM window requires'),
          (dict(GBA_DYNAREC_PERSISTENT_ROM_WINDOW=2),'primary ROM window requires'),
          (dict(GBA_PPU_SCANLINE_FAST=1,GBA_PPU_SCANLINE_ZERO_ROWS=1),None),
          (dict(GBA_PPU_SCANLINE_ZERO_ROWS=1),'empty tile rows require'),
          (dict(GBA_PPU_SCANLINE_ZERO_ROWS=2),'empty tile rows require'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_BL_DIRECT=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_BL_DIRECT=1),'persistent direct Thumb BL requires'),
          (dict(GBA_DYNAREC_PERSISTENT_BL_DIRECT=2),'persistent direct Thumb BL requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_INDIRECT_BITS=9),None),
          (dict(GBA_DYNAREC_PERSISTENT_INDIRECT_BITS=8),'invalid persistent indirect cache geometry'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_INDIRECT_BITS=5),'invalid persistent indirect cache geometry'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_INDIRECT_BITS=11),None),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_INDIRECT_BITS=12),'invalid persistent indirect cache geometry'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_FAST_CORE=1,GBA_DYNAREC_PERSISTENT_READ_WAIT=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_READ_WAIT=1),'persistent read wait preservation requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_READ_WAIT=1),'persistent read wait preservation requires GBA_FAST_WAIT'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_MAP_HELPER=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_MAP_HELPER=1),'persistent mapping helpers require'),
          (dict(GBA_DYNAREC_PERSISTENT_MAP_HELPER=2),'persistent mapping helpers require'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_MAP_HELPER=1,GBA_DYNAREC_PERSISTENT_PAGE_TABLE=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_PAGE_TABLE=1),'persistent page tables require'),
          (dict(GBA_DYNAREC_PERSISTENT_PAGE_TABLE=2),'persistent page tables require'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_INLINE_REFILL=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_INLINE_REFILL=1),'inline persistent refill requires'),
          (dict(GBA_DYNAREC_PERSISTENT_INLINE_REFILL=2),'inline persistent refill requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH=1),'direct persistent branches require'),
          (dict(GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH=2),'direct persistent branches require'),
          (dict(GBA_PPU_SCANLINE_FAST=1,GBA_PPU_SCANLINE_PALETTE_DEDUP=1),None),
          (dict(GBA_PPU_SCANLINE_PALETTE_DEDUP=1),'scanline palette deduplication requires'),
          (dict(GBA_PPU_SCANLINE_PALETTE_DEDUP=2),'scanline palette deduplication requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_TAIL=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_TAIL=1),'persistent tail reservation requires'),
          (dict(GBA_DYNAREC_PERSISTENT_TAIL=2),'persistent tail reservation requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_MULTI=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_MULTI=1),'persistent multiple transfers require'),
          (dict(GBA_DYNAREC_PERSISTENT_MULTI=2),'persistent multiple transfers require'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_PERSISTENT=1,GBA_DYNAREC_PERSISTENT_LITERALS=1),None),
          (dict(GBA_DYNAREC_PERSISTENT_LITERALS=1),'persistent literal loads require'),
          (dict(GBA_DYNAREC_PERSISTENT_LITERALS=2),'persistent literal loads require'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1),None),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_TLB=1),None),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_TRACE_INSNS=64),None),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_PRELINK=4),None),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_PRELINK=17),'invalid GBA turbo backend configuration'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_REGISTER_MAP=3),None),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_REGISTER_MAP=4),'invalid GBA turbo backend configuration'),
          (dict(GBA_DYNAREC_TURBO_REGISTER_MAP=3),'turbo register map requires'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_PERSISTENT=1),'mutually exclusive'),
          (dict(GBA_DYNAREC=0,GBA_DYNAREC_TURBO=1),'turbo execution requires dynarec'),
          (dict(GBA_DYNAREC=1,GBA_DYNAREC_TURBO_TLB=1),'turbo TLB requires turbo execution'),
          (dict(GBA_DYNAREC_TURBO_TRACE_FUSION=1),'turbo trace fusion requires turbo execution'),
          (dict(GBA_DYNAREC_TURBO_PRELINK=1),'turbo eager linking requires turbo execution'),
          (dict(approx,GBA_DYNAREC_PERSISTENT=1,GBA_PPU_SCANLINE_FAST=1),None),
          (dict(GBA_PPU_SCANLINE_FAST=1,GBA_DYNAREC=0),None)]
for options in ({'GBA_DYNAREC_PERSISTENT':2},{'GBA_DYNAREC_PERSISTENT':1,'GBA_DYNAREC':0},
                {'GBA_DYNAREC_PERSISTENT':1,'GBA_DYNAREC_CHAIN':1},
                {'GBA_DYNAREC_PERSISTENT':1,'GBA_DYNAREC_INTERIOR':1},
                {'GBA_DYNAREC_PERSISTENT':1,'GBA_DYNAREC_ROM_TRACE':1},
                {'GBA_DYNAREC_PERSISTENT':1,'GBA_DYNAREC_FASTCALL':1}):
    cases.append((dict(approx,**options),'persistent execution requires'))
cases += [(dict(approx,GBA_PPU_SCANLINE_FAST=2),'invalid GBA_PPU_SCANLINE_FAST'),
          (dict(GBA_PPU_SCANLINE_FAST=1,GBA_PPU_SCANLINE_IDENTITY=1),None),
          (dict(GBA_PPU_SCANLINE_IDENTITY=1),'identity effect scanlines require'),
          (dict(GBA_PPU_SCANLINE_FAST=1,GBA_PPU_SCANLINE_IDENTITY=2),'identity effect scanlines require'),
          (dict(approx,GBA_PPU_SCANLINE_FAST=1,GBA_DYNAREC_VIDEO_STORE=1),'scanline palette coherence requires')]
for capacity in (1,2):
    cases.append((dict(approx,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_LOCAL_ALU=1,GBA_DYNAREC_TURBO_LOCAL_REGS=capacity),None))
cases += [(dict(approx,GBA_DYNAREC_TURBO_LOCAL_ALU=1),'local ALU register requires Turbo'),
          (dict(approx,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_LOCAL_ALU=2),'local ALU register requires Turbo')]
for capacity in (0,3):
    cases.append((dict(approx,GBA_DYNAREC_TURBO=1,GBA_DYNAREC_TURBO_LOCAL_ALU=1,GBA_DYNAREC_TURBO_LOCAL_REGS=capacity),'local ALU capacity'))
rows = []
with tempfile.TemporaryDirectory(prefix='gba-native-options-') as directory:
    source = Path(directory) / 'options.c'
    source.write_text('#include "gba.h"\n')
    for options, expected in cases:
        command = [a.cc, '-mcpu=arm926ej-s', '-marm', '-std=c11', '-pedantic-errors',
                   '-fsyntax-only', '-I' + str(root / 'inc'), '-I' + str(root / 'port')]
        command += ['-D' + name + '=' + str(value) for name, value in options.items()]
        result = subprocess.run(command + [str(source)], capture_output=True, text=True)
        assert (result.returncode == 0) if expected is None else (result.returncode != 0 and expected in result.stderr), (options, result.stderr)
        assert 'requires a little endian ARMv5TE' not in result.stderr, result.stderr
        rows.append(dict(options=options, accepted=expected is None, expected_error=expected))
a.results.parent.mkdir(parents=True, exist_ok=True)
a.results.write_text(json.dumps(dict(passed=True, checks=rows), indent=2) + '\n')
print('ARM926 native option dependencies PASS:', len(rows))
