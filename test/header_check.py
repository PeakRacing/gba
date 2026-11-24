#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Check independent public C/C++ headers and configuration rejection paths."""
import argparse,json,subprocess,tempfile,os
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='D:/msys64/mingw64/bin/gcc.exe')
p.add_argument('--cxx',default='D:/msys64/mingw64/bin/g++.exe')
p.add_argument('--results',type=Path)
a=p.parse_args()
os.environ["PATH"]=str(Path(a.cc).parent)+os.pathsep+os.environ.get("PATH", "")
root=Path(__file__).resolve().parents[1]
rows=[]
configs=[[],['GBA_FAST_CORE=1','GBA_PPU_OBJ_INDEX=1','GBA_PPU_OAM_DIRTY=1'],['GBA_USE_FS=1','GBA_ENABLE_SAVE_STATE=1','GBA_ENABLE_SOUND=1','GBA_ROM_STREAM=1','GBA_RAM_LACK=1','GBA_COLOR_DEPTH=32'],['GBA_PORT_BATCH_EVENTS=1','GBA_PORT_FAST_RUN=1']]
configs += [['GBA_PPU_SCANLINE_FAST=1'],['GBA_PPU_SCANLINE_FAST=1','GBA_RAM_LACK=1','GBA_COLOR_DEPTH=32']]
configs += [['GBA_PPU_SCANLINE_FAST=1','GBA_PPU_SCANLINE_SPANS=1','GBA_APU_LAZY_MIX=1','GBA_ENABLE_SOUND=1']]
with tempfile.TemporaryDirectory(prefix='gba-headers-') as directory:
    for cpp in (False,True):
        source=Path(directory)/('check.cpp' if cpp else 'check.c')
        for header in sorted((root/'inc').glob('*.h')):
            source.write_text('#include "'+header.name+'"\nGBA_ALIGN(32) static unsigned char aligned[32];\nGBA_FORCE_INLINE int identity(int x) { return x; }\nint main(void) { return identity(aligned[0]); }\n')
            for definitions in configs:
                command=[a.cxx if cpp else a.cc,'-std=c++11' if cpp else '-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-fsyntax-only','-I'+str(root/'inc'),'-I'+str(root/'port')]
                command+=['-D'+x for x in definitions]+[str(source)]
                result=subprocess.run(command,capture_output=True,text=True)
                if result.returncode: raise RuntimeError(header.name+"\n"+result.stderr)
                rows.append(dict(header=header.name,cpp=cpp,definitions=definitions,passed=True))
    for definitions in [['GBA_DYNAREC=1'],['GBA_DYNAREC=2'],['GBA_FAST_ALU=1','GBA_FAST_CPU=0'],['GBA_ENABLE_SAVE_STATE=1','GBA_USE_FS=0']]+[['GBA_PORT_'+name+'=1','GBA_'+name+'=0'] for name in ('BATCH_EVENTS','FAST_RUN','FAST_IO','IDLE_LOOPS')]+[[name+'=2'] for name in ('GBA_PPU_OAM_DIRTY','GBA_PPU_OBJ_INDEX','GBA_DYNAREC_HARD_CACHE','GBA_PPU_FRONT_CULL','GBA_DYNAREC_TRACE_CONTEXT','GBA_DYNAREC_TRACE_LINKS','GBA_DYNAREC_VIDEO_STORE','GBA_DYNAREC_FASTCALL','GBA_DYNAREC_ROM_TRACE','GBA_PPU_SORTED_BLEND','GBA_PPU_OBJ_SPANS','GBA_PPU_TILE_DIRECT','GBA_PPU_ORDERED','GBA_PPU_READ_DEDUP','GBA_DYNAREC_MULTI_INLINE','GBA_DYNAREC_MULTI_PC','GBA_DYNAREC_RAM_INLINE','GBA_DYNAREC_ROM_PREFIX','GBA_DYNAREC_ROM_CHAIN','GBA_DYNAREC_STORE_INLINE','GBA_DYNAREC_PARTIAL_REGS','GBA_DYNAREC_WIDE_ROM','GBA_DYNAREC_LONG_LOOPS','GBA_DYNAREC_COARSE_CHAIN','GBA_DYNAREC_LINK_CACHE','GBA_DYNAREC_TINY_BLOCKS','GBA_DYNAREC_BL_PREFIX','GBA_DYNAREC_REFILL_MATCH','GBA_DYNAREC_BULK_PREFILL','GBA_DYNAREC_LINE_INVALIDATE','GBA_DYNAREC_TURBO')]+[[name+'=1','GBA_DYNAREC=0'] for name in ('GBA_DYNAREC_MULTI_INLINE','GBA_DYNAREC_MULTI_PC','GBA_DYNAREC_RAM_INLINE','GBA_DYNAREC_ROM_PREFIX','GBA_DYNAREC_ROM_CHAIN')]+[['GBA_DYNAREC_VIDEO_STORE=1','GBA_DYNAREC=0'],['GBA_DYNAREC_VIDEO_STORE=1','GBA_DYNAREC_FAST_BUS=0'],['GBA_DYNAREC_VIDEO_STORE=1','GBA_DYNAREC_EVENT_SLIP=63'],['GBA_DYNAREC_FASTCALL=1','GBA_DYNAREC=0'],['GBA_DYNAREC_FASTCALL=1','GBA_DYNAREC_CHAIN=1'],['GBA_DYNAREC_FASTCALL=1','GBA_DYNAREC_INTERIOR=1'],['GBA_DYNAREC_FASTCALL=1','GBA_DYNAREC_ROM_PREFIX=1'],['GBA_DYNAREC_ROM_TRACE=1','GBA_DYNAREC_FAST_BUS=0'],['GBA_DYNAREC_ROM_TRACE=1','GBA_DYNAREC_WIDE_ROM=0'],['GBA_DYNAREC_ROM_TRACE=1','GBA_DYNAREC_EVENT_SLIP=255'],['GBA_DYNAREC_ROM_TRACE=1','GBA_DYNAREC_INTERIOR=1'],['GBA_DYNAREC_ROM_TRACE=1','GBA_DYNAREC_ROM_PREFIX=1'],['GBA_DYNAREC_FIXED_MAP=3'],['GBA_DYNAREC_FIXED_MAP=1','GBA_DYNAREC=0'],['GBA_DYNAREC_LINE_INVALIDATE=1','GBA_DYNAREC=0'],['GBA_DYNAREC_BULK_PREFILL=1','GBA_DYNAREC_FAST_BUS=0'],['GBA_DYNAREC_BULK_PREFILL=1','GBA_DYNAREC_EVENT_SLIP=63'],['GBA_DYNAREC_REFILL_MATCH=1','GBA_DYNAREC_COARSE_CHAIN=0'],['GBA_DYNAREC_LINK_CACHE=1','GBA_DYNAREC_COARSE_CHAIN=0'],['GBA_DYNAREC_TINY_BLOCKS=1','GBA_DYNAREC_COARSE_CHAIN=0'],['GBA_DYNAREC_BL_PREFIX=1','GBA_DYNAREC=0'],['GBA_DYNAREC_LONG_LOOPS=1','GBA_DYNAREC_WIDE_ROM=0'],['GBA_DYNAREC_MULTI_PC=1','GBA_DYNAREC_MULTI_INLINE=0'],['GBA_DYNAREC_COARSE_CHAIN=1'],['GBA_DYNAREC_STORE_INLINE=1','GBA_DYNAREC_MULTI_INLINE=0'],['GBA_DYNAREC_WIDE_ROM=1','GBA_DYNAREC_FAST_BUS=0'],['GBA_DYNAREC_RECENT_BITS=5'],['GBA_DYNAREC_RECENT_BITS=11'],['GBA_DYNAREC_RECENT_BITS=9','GBA_DYNAREC_DATA_SIZE=262144'],['GBA_DYNAREC_FAST_BUS=1'],['GBA_DYNAREC_FAST_BUS=2'],['GBA_DYNAREC_EVENT_SLIP=-1'],['GBA_DYNAREC_EVENT_SLIP=2049'],['GBA_DYNAREC_EVENT_SLIP=1024','GBA_DYNAREC=0'],['GBA_DYNAREC_TURBO=1','GBA_DYNAREC_PERSISTENT=1'],['GBA_DYNAREC_TURBO_TLB=1'],['GBA_DYNAREC_TURBO_PRELINK=1'],['GBA_DYNAREC_TURBO_LIGHT=1'],['GBA_DYNAREC_PAGE_INDEX=1'],['GBA_PPU_SCANLINE_EFFECTS=1'],['GBA_PPU_SCANLINE_SPANS=1'],['GBA_PPU_SCANLINE_SPANS=2'],['GBA_APU_LAZY_MIX=2'],['GBA_DYNAREC_TURBO_LOCAL_ALU=1'],['GBA_DYNAREC_TURBO_LOCAL_ALU=2']]:
        source.write_text('#include "gba.h"\n')
        command=[a.cxx,'-std=c++11','-fsyntax-only','-I'+str(root/'inc'),'-I'+str(root/'port')]+['-D'+x for x in definitions]+[str(source)]
        result=subprocess.run(command,capture_output=True)
        assert result.returncode!=0,definitions
        rows.append(dict(rejected=definitions,passed=True))
out=a.results or root/'test/out/header-check.json';out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(dict(passed=True,checks=rows),indent=2)+'\n')
print('Public C/C++ headers and configuration checks PASS:',len(rows))
