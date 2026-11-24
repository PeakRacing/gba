#!/usr/bin/env python3
# Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0
"""Compare fixed real-ROM replay with pinned e0f44c6, optimized core and LTO.

ROMs are local user inputs and are copied into temporary directories. No ROM
or battery/state files are downloaded, bundled, or written beside the inputs.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
p.add_argument('--rom-directory', type=Path, required=True)
p.add_argument('--frames', type=int, default=900)
p.add_argument('--results', type=Path, default=Path('test/out/optimization-replay.json'))
a = p.parse_args()
if not 300 <= a.frames <= 9000:
    p.error('--frames must be between 300 and 9000')
cc = shutil.which(a.cc)
if not cc:
    p.error('C11 compiler not found; set --cc or CC')
root = Path(__file__).resolve().parents[1]
ref = 'e0f44c6bec202fa15890f3c72909f6187fe236b7'
names = ('metroid_fusion', 'mario_advance', 'zelda_minish', 'pokemon_emerald')
roms = {name: (a.rom_directory / (name + '.gba')).resolve() for name in names}
for path in roms.values():
    if not path.is_file():
        p.error('Missing local ROM: ' + str(path))
env = dict(os.environ)
env['PATH'] = str(Path(cc).parent) + os.pathsep + env['PATH']
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
source = {str(x.relative_to(root)): sha(x) for folder in ('inc', 'src')
          for x in sorted((root / folder).glob('*')) if x.is_file()}
with tempfile.TemporaryDirectory(prefix='gba-replay-') as directory:
    t = Path(directory)
    old = t / 'reference'
    old.mkdir()
    archive = subprocess.check_output(['git', 'archive', '--format=tar', ref, 'inc', 'src', 'port'], cwd=root)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(old, filter='data')
    for name in ('gba_default.c', 'gba_port.c'):
        (t / name).write_text((root / 'port' / name).read_text(encoding='utf-8').replace('GBA_WEAK ', ''), encoding='utf-8')
    results = []
    for label, core, enabled, lto in [('reference', old, False, False),
                                       ('optimized', root, True, False), ('lto', root, True, True)]:
        exe = t / (label + '.exe')
        cmd = [cc, '-std=c11', '-pedantic-errors', '-O3',
               '-DGBA_CUSTOM_TICKS=1', '-DGBA_CUSTOM_FRAME=1', '-DGBA_CUSTOM_OUTPUT=1',
               '-I' + str(root / 'test/optimization'), '-I' + str(core / 'inc'), '-I' + str(core / 'src')]
        if lto:
            cmd.append('-flto')
        cmd += ['-D' + x + '=' + str(int(enabled)) for x in
                ('GBA_FAST_CPU', 'GBA_FAST_WAIT', 'GBA_FAST_ALU', 'GBA_FAST_BLOCKS',
                 'GBA_VIDEO_CACHE', 'GBA_PORT_BATCH_EVENTS', 'GBA_PORT_FAST_RUN')]
        cmd += [str(x) for x in sorted((core / 'src').glob('*.c'))]
        cmd += [str(root / 'test/optimization/replay.c'), str(t / 'gba_default.c'),
                str(t / 'gba_port.c'), '-o', str(exe)]
        subprocess.run(cmd, check=True, env=env)
        rows = []
        for name, rom in roms.items():
            work = t / (label + '-' + name)
            work.mkdir()
            local = work / 'game.gba'
            shutil.copyfile(rom, local)
            run = subprocess.run([str(exe), str(local), str(a.frames)], cwd=work,
                                 check=True, env=env, capture_output=True, timeout=600)
            output = run.stdout.decode('utf-8')
            rows.append(dict(game=name, rom_sha256=sha(rom), output=output,
                             states={x.name: sha(x) for x in sorted(work.glob('*.state'))}))
            print(label, name, output.strip(), flush=True)
        results.append(rows)
    assert results[0] == results[1] == results[2], 'Image/PCM/cycles/CPU/state mismatch'
for name, digest in source.items():
    assert sha(root / name) == digest, 'Source changed during replay'
a.results.parent.mkdir(parents=True, exist_ok=True)
a.results.write_text(json.dumps(dict(passed=True, reference_commit=ref, frames=a.frames,
                                    core_sha256=source, games=results[0]), indent=2) + '\n', encoding='utf-8')
print('Pinned reference / optimized / LTO replay identical PASS')
