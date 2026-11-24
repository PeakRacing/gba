#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Independent brightness values and pixel differential against generic PPU."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',type=Path,default=Path('D:/msys64/mingw64/bin/gcc.exe'))
p.add_argument('--core-source',type=Path)
p.add_argument('--text-spans',action='store_true')
p.add_argument('--dependency-dedup',action='store_true')
p.add_argument('--results-dir',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];core=(a.core_source or root).resolve()
out=a.results_dir.resolve();out.mkdir(parents=True,exist_ok=False)
cc=a.cc;env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
(out/'gba_conf.h').write_text('#pragma once\n#define GBA_ENABLE_SOUND 0\n#define GBA_USE_FS 0\n#define GBA_ENABLE_SAVE_STATE 0\n')
(out/'main.c').write_bytes((root/'test/optimization/scanline_effects.c').read_bytes())
for name in ['gba_default.c','gba_port.c']:
 (out/name).write_text((core/'port'/name).read_text(encoding='utf-8').replace('GBA_WEAK ',''),encoding='utf-8')
sealed={str(f.relative_to(core)):hashlib.sha256(f.read_bytes()).hexdigest() for d in ('inc','src','port') for f in (core/d).iterdir() if f.suffix in ('.c','.h')}
rows=[]
for depth,swap,cache in [(16,0,0),(16,1,1),(32,0,1),(32,1,0)]:
 label=f'd{depth}-s{swap}-c{cache}';exe=out/(label+'.exe')
 command=[str(cc),'-std=c11','-pedantic-errors','-O3','-Wall','-Wextra','-Werror',
 '-DGBA_CUSTOM_TICKS=1','-DGBA_CUSTOM_FRAME=1','-DGBA_CUSTOM_OUTPUT=1',
 '-DGBA_PPU_SCANLINE_FAST=1','-DGBA_PPU_SCANLINE_EFFECTS=1','-DGBA_PPU_SCANLINE_IDENTITY=1',
 '-DGBA_TEST_RENDER_REFERENCE=1','-DGBA_TEST_SCANLINE_COUNTERS=1',
 '-DGBA_COLOR_DEPTH='+str(depth),'-DGBA_COLOR_SWAP='+str(swap),'-DGBA_VIDEO_CACHE='+str(cache),
 '-I'+str(out),'-I'+str(core/'inc'),'-I'+str(core/'src')]
 if a.text_spans:command+=['-DGBA_PPU_SCANLINE_SPANS=1']
 if a.dependency_dedup:command+=['-DGBA_PPU_SCANLINE_PALETTE_DEDUP=1','-DGBA_PPU_READ_DEDUP=1']
 command += [str(f) for f in sorted((core/'src').glob('*.c')) if f.name!='gba_ppu.c']
 command += [str(out/f) for f in ['main.c','gba_default.c','gba_port.c']]+['-o',str(exe)]
 with (out/(label+'.log')).open('w') as log:
  subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
  subprocess.run([str(exe)],check=True,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=180)
 assert all(hashlib.sha256((core/n).read_bytes()).hexdigest()==h for n,h in sealed.items()),'Source changed during test'
 text=(out/(label+'.log')).read_text();print(text.strip(),flush=True)
 rows.append(dict(label=label,passed=True,output=text,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()))
 (out/'results.json').write_text(json.dumps(dict(core_sha256=sealed,cases=rows),indent=2))
