#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Compare actual OAM candidate lists, not a replica of the indexed selector."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--results-dir',type=Path,required=True)
p.add_argument('--core-source',type=Path)
p.add_argument('--cc',type=Path,default=Path('D:/msys64/mingw64/bin/gcc.exe'))
a=p.parse_args();root=Path(__file__).resolve().parents[1];core=(a.core_source or root).resolve()
out=a.results_dir.resolve();out.mkdir(parents=True,exist_ok=False)
identity=lambda:{str(x.relative_to(core)):hashlib.sha256(x.read_bytes()).hexdigest()
                 for directory in ('inc','src','port') for x in sorted((core/directory).iterdir()) if x.suffix in ('.c','.h')}
sealed=identity();rows=[];env=dict(os.environ);env['PATH']=str(a.cc.parent)+os.pathsep+env['PATH']
(out/'gba_conf.h').write_text('#pragma once\n')
(out/'main.c').write_bytes((root/'test/optimization/obj_index.c').read_bytes())
for name in ('gba_default.c','gba_port.c'):
    (out/name).write_text((core/'port'/name).read_text(encoding='utf-8').replace('GBA_WEAK ',''),encoding='utf-8')
for cache,fast in ((c,f) for c in (0,1) for f in (0,1)):
    label='cache'+str(cache)+'-fast'+str(fast);exe=out/(label+'.exe')
    cmd=[str(a.cc),'-std=c11','-O3','-Wall','-Wextra','-Werror','-pedantic-errors',
         '-DGBA_PPU_OBJ_INDEX=1','-DGBA_PPU_OAM_DIRTY=1','-DGBA_TEST_OAM_DIRTY_PROOF=1','-DGBA_TEST_OBJ_INDEX_PROOF=1','-DGBA_TEST_RENDER_REFERENCE=1',
         '-DGBA_CUSTOM_TICKS=1','-DGBA_CUSTOM_FRAME=1','-DGBA_CUSTOM_OUTPUT=1',
         '-DGBA_VIDEO_CACHE='+str(cache),'-DGBA_FAST_CPU='+str(fast),
         '-I'+str(out),'-I'+str(core/'inc'),'-I'+str(core/'src')]
    cmd += [str(x) for x in sorted((core/'src').glob('*.c')) if x.name!='gba_ppu.c']
    cmd += [str(out/x) for x in ('main.c','gba_default.c','gba_port.c')]+['-o',str(exe)]
    with (out/(label+'.log')).open('w') as log:
        subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
        run=subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,env=env,timeout=180)
    assert run.returncode==0,(label,run.returncode)
    output=(out/(label+'.log')).read_text();print(label,output.strip(),flush=True)
    rows.append(dict(label=label,passed=True,command=cmd,output=output))
assert identity()==sealed,'Source changed during verification'
(out/'results.json').write_text(json.dumps(dict(passed=True,source_sha256=sealed,
    harness_sha256=hashlib.sha256((out/'main.c').read_bytes()).hexdigest(),rows=rows),indent=2)+'\n')
