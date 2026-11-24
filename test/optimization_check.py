#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Non-trace differential coverage for the portable optimization paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
p.add_argument('--results', type=Path, default=Path('test/out/optimizations.json'))
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
cc = shutil.which(a.cc)
if not cc:
    p.error('C11 compiler not found; set --cc or CC')
env = dict(os.environ)
env['PATH'] = str(Path(cc).parent) + os.pathsep + env['PATH']
source = {str(x.relative_to(root)): hashlib.sha256(x.read_bytes()).hexdigest()
          for folder in ('inc', 'src') for x in sorted((root / folder).glob('*')) if x.is_file()}
base = ['GBA_FAST_CPU=1', 'GBA_FAST_WAIT=1']
cases = [('registers', [base + ['GBA_FAST_ALU=0'], base + ['GBA_FAST_ALU=1'],
                        base + ['GBA_FAST_ALU=1', 'GBA_FAST_BLOCKS=1']]),
         ('edges', [base + ['GBA_FAST_ALU=0'], base + ['GBA_FAST_ALU=1'],
                    base + ['GBA_FAST_ALU=1', 'GBA_FAST_BLOCKS=1']]),
         ('video', [['GBA_VIDEO_CACHE=0'], ['GBA_VIDEO_CACHE=1']])]
rows = []
with tempfile.TemporaryDirectory(prefix='gba-optimizations-', ignore_cleanup_errors=True) as directory:
    t = Path(directory)
    # COFF weak libc defaults do not resolve consistently across MinGW objects.
    for name in ('gba_default.c', 'gba_port.c'):
        (t / name).write_text((root / 'port' / name).read_text(encoding='utf-8').replace('GBA_WEAK ', ''), encoding='utf-8')
    for name, variants in cases:
        outputs = []
        for i, definitions in enumerate(variants):
            exe = t / (name + str(i) + '.exe')
            command = [cc, '-std=c11', '-pedantic-errors', '-O3',
                       '-DGBA_CUSTOM_TICKS=1', '-DGBA_CUSTOM_FRAME=1', '-DGBA_CUSTOM_OUTPUT=1',
                       '-I' + str(root / 'test/optimization'), '-I' + str(root / 'inc'), '-I' + str(root / 'src')]
            command += ['-D' + x for x in definitions]
            command += [str(x) for x in sorted((root / 'src').glob('*.c'))]
            command += [str(root / 'test/optimization' / (name + '.c')),
                        str(t / 'gba_default.c'), str(t / 'gba_port.c'), '-o', str(exe)]
            subprocess.run(command, check=True, env=env)
            result = subprocess.run([str(exe)], check=True, env=env, capture_output=True, timeout=180)
            outputs.append(result.stdout.decode('utf-8'))
            print(name, i, outputs[-1].strip(), flush=True)
        assert len(set(outputs)) == 1, (name, outputs)
        rows.append(dict(name=name, variants=len(variants), equal=True, output=outputs[0]))
for name, digest in source.items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, 'Source changed during test'
a.results.parent.mkdir(parents=True, exist_ok=True)
a.results.write_text(json.dumps(dict(passed=True, strict_c11=True, core_sha256=source, cases=rows), indent=2) + '\n', encoding='utf-8')
print('Non-trace ALU/blocks, IRQ/DMA, fetch edges and scanline cache equivalence PASS')
