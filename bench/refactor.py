#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Paired production benchmarks against a frozen source tree; no test/profile probes."""
import argparse,hashlib,json,os,re,shutil,statistics,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--rom-directory',type=Path,required=True)
p.add_argument('--cc',default='D:/msys64/mingw64/bin/gcc.exe')
p.add_argument('--frames',type=int,default=600)
p.add_argument('--runs',type=int,default=5)
p.add_argument('--fast-core',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'test/out/refactor-speed';out.mkdir(parents=True,exist_ok=True)
os.environ['PATH']=str(Path(a.cc).parent)+os.pathsep+os.environ.get('PATH','')
variants={'565':['GBA_ENABLE_SOUND=1'], '32':['GBA_ENABLE_SOUND=1','GBA_COLOR_DEPTH=32'],
          'stream':['GBA_ENABLE_SOUND=1','GBA_ROM_STREAM=1','GBA_RAM_LACK=1'], 'silent':[]}
rows=[]
for name,definitions in variants.items():
    binaries=[]
    for label,core in [('baseline',a.baseline.resolve()),('current',root)]:
        exe=out/(label+'-'+name+'.exe')
        flags=['GBA_USE_FS=1','GBA_CUSTOM_TICKS=1','GBA_TEST_MODE=0','GBA_TEST_PROFILE=0']+definitions
        if a.fast_core: flags += ['GBA_FAST_CPU=1','GBA_FAST_WAIT=1','GBA_FAST_ALU=1','GBA_FAST_BLOCKS=1','GBA_VIDEO_CACHE=1','GBA_PORT_BATCH_EVENTS=1','GBA_PORT_FAST_RUN=1']
        command=[a.cc,'-std=c11','-O3','-I'+str(core/'inc'),'-I'+str(core/'src'),'-I'+str(core/'port')]
        command+=['-D'+flag for flag in flags]+[str(x) for x in sorted((core/'src').glob('*.c'))]
        port_directory=out/label;port_directory.mkdir(exist_ok=True)
        for filename in ('gba_default.c','gba_port.c'):
            (port_directory/filename).write_text((core/'port'/filename).read_text().replace('GBA_WEAK ',''))
        command += [str(port_directory/'gba_default.c'),str(port_directory/'gba_port.c'),str(root/'bench/fast.c'),'-o',str(exe)]
        subprocess.run(command,check=True);binaries.append(exe)
    for rom in sorted(a.rom_directory.glob('*.gba')):
        work=out/'roms';work.mkdir(exist_ok=True)
        local=work/rom.name;shutil.copyfile(rom,local)
        times=[[],[]];outputs=[[],[]]
        for batch in range(2):
            for run in range(a.runs):
                for index in ([0,1] if run%2==0 else [1,0]):
                    result=subprocess.check_output([str(binaries[index]),str(local),str(a.frames)],text=True)
                    assert 'test=0 profile=0' in result and 'fault=0' in result,result
                    times[index].append(int(re.search(r'us=(\d+)',result)[1]))
                    outputs[index].append(re.sub(r'us=\d+','us=TIME',result.strip()))
            assert len(set(outputs[0]+outputs[1]))==1,outputs
            median=[statistics.median(x[-a.runs:]) for x in times]
            regression=median[1]/median[0]-1
            if batch==0 and regression<=.05: break
        row=dict(configuration=name,rom=rom.name,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                 frames=a.frames,warmup=120,baseline_us=times[0],current_us=times[1],
                 median_baseline_us=statistics.median(times[0]),median_current_us=statistics.median(times[1]),
                 last_batch_change_percent=regression*100,batches=batch+1,output=outputs[1][-1],
                 regression=regression>.05 and batch==1)
        rows.append(row)
        print(name,rom.name,'change_percent=',round(regression*100,2),'batches=',batch+1,flush=True)
        (out/'results.json').write_text(json.dumps(dict(production_probes=False,fast_core=a.fast_core,rows=rows),indent=2)+'\n')
assert not any(x['regression'] for x in rows),'Repeated regressions above 5 percent'
print('Paired production benchmarks PASS',flush=True)
