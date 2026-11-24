from pathlib import Path
import hashlib,json,os,subprocess
import argparse,tempfile
p=argparse.ArgumentParser(description='Lazy/eager mixer actual state and PCM differential')
p.add_argument('--results-dir',type=Path,required=True)
p.add_argument('--cc',type=Path,default=Path('D:/msys64/mingw64/bin/gcc.exe'))
a=p.parse_args();out=a.results_dir.resolve();out.mkdir(parents=True,exist_ok=False)
core=Path(__file__).resolve().parents[1];cc=a.cc
(out/'compare.c').write_bytes((core/'test/optimization/apu_lazy.c').read_bytes())
(out/'gba_apu_before.c').write_bytes((core/'src/gba_apu.c').read_bytes())
env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
(out/'gba_conf.h').write_text('#pragma once\n#define GBA_ENABLE_SOUND 1\n#define GBA_USE_FS 0\n#define GBA_ENABLE_SAVE_STATE 0\n')
for name in ('gba_port.c','gba_default.c'):(out/name).write_text((core/'port'/name).read_text(encoding='utf-8').replace('GBA_WEAK ',''),encoding='utf-8')
common=[str(cc),'-std=c11','-O3','-Wall','-Wextra','-Werror','-DGBA_CUSTOM_TICKS=1','-DGBA_CUSTOM_FRAME=1','-DGBA_CUSTOM_OUTPUT=1','-I'+str(out),'-I'+str(core/'inc'),'-I'+str(core/'src')]
subprocess.run(common+['-DGBA_APU_LAZY_MIX=0','-Dgba_apu_advance=gba_apu_advance_reference','-Dgba_apu_write=gba_apu_write_reference','-Dgba_apu_timer=gba_apu_timer_reference','-c',str(out/'gba_apu_before.c'),'-o',str(out/'reference.o')],check=True,env=env)
command=common+['-DGBA_APU_LAZY_MIX=1']+[str(p) for p in (core/'src').glob('*.c')]+[str(out/p) for p in ('compare.c','gba_default.c','gba_port.c','reference.o')]+['-o',str(out/'compare.exe')]
with (out/'compare.log').open('w') as log:
    subprocess.run(command,check=True,env=env,stdout=log,stderr=subprocess.STDOUT)
    subprocess.run([str(out/'compare.exe')],check=True,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
text=(out/'compare.log').read_text();print(text)
(out/'results.json').write_text(json.dumps(dict(passed=True,output=text,exe_sha256=hashlib.sha256((out/'compare.exe').read_bytes()).hexdigest(),source_sha256={str(p.relative_to(core)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('inc','src','port') for p in (core/folder).glob('*') if p.suffix in ('.c','.h')}),indent=2)+'\n')
