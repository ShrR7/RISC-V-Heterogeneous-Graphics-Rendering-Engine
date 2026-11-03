#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Production scene/feedback/FX/core host contracts, without MMIO or board I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'neon_feedback_20261014'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/scene'
    build.mkdir(parents=True, exist_ok=True)
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    old = (BASE / 'assets/atlas.bin').read_bytes()
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    previous = json.loads((BASE / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(old) == 228602 and len(atlas) <= 237568 and atlas[:len(old)] == old
    assert len(manifest['sprites']) == 280 and manifest['sprites'][:274] == previous['sprites']
    assert sha(atlas) == manifest['sha256']
    end = 0
    for item in manifest['sprites']:
        assert item['offset'] % 4 == 0 and item['offset'] >= end
        assert item['stride'] == item['width'] * 2
        assert item['bytes'] == item['stride'] * item['height']
        end = item['offset'] + item['bytes']
        assert end <= len(atlas)
    frozen = ('garden_core.c', 'garden_core.h', 'garden_fx.c', 'garden_fx.h',
              'garden_feedback.c', 'garden_feedback.h')
    for name in frozen:
        assert (ROOT / 'src' / name).read_bytes() == (BASE / 'src' / name).read_bytes(), name
    exe = build / 'test_scene.exe'
    sources = ('garden_scene.c', 'garden_feedback.c', 'garden_fx.c', 'garden_core.c', 'garden_assets.c')
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_scene.c')]
    command += [str(ROOT / 'src' / name) for name in sources] + ['-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (build / 'test_scene.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, end='')
        raise SystemExit(result.returncode)
    match = re.search(r'scene host contract checks=(\d+)', result.stdout)
    assert match, 'Missing completed scene contract summary'
    source_files = tuple('src/' + name for name in frozen) + (
        'src/garden_scene.c', 'src/garden_scene.h', 'src/garden_assets.c', 'src/garden_assets.h',
        'tools/test_scene.c', 'tools/verify_scene_host.py')
    report = dict(status='PASS', host_only=True, board_acceptance=False,
        compiler_command=command, contract_checks=int(match.group(1)),
        atlas_bytes=len(atlas), atlas_sha256=sha(atlas), frozen_274_slots_byte_identical=True,
        frozen_prefix_bytes=228602, unchanged_font_slots=172, all_descriptors_bounded=True,
        core_feedback_fx_source_byte_identical=True, complete_core_snapshots_compared_steps=12000,
        checks=['882 forecasts versus actual future core steps, all3patterns and phase/transit timing',
                'cold100tick entrance including final move return and first12tick anticipation',
                'actual shot counter deltas, actual attack changes, visual-only halfHP stages',
                'same-tick/pause, disabled legacy equivalence, observationgap and restart clear',
                'all capacities0..241/canaries/null, full234essential pool and marker/spark priority',
                'paired wings/core/cue above olddecor, backgrounds last, partialtwo-layer admission',
                'paintorder core immediatelyafterBoss beforeprojectiles, visiblemuzzle outsidebody',
                'two different deterministic scroll speeds, large ticks defined, Alphaoff opaque',
                '12000 full game snapshots bit-identical including RNG/HP/score/attacks'],
        stdout_sha256=sha(result.stdout.encode('utf-8')),
        source_sha256={name: sha((ROOT / name).read_bytes()) for name in source_files},
        limits=['Host production-module tests do not establish board pixels, FPS or GPU/DDR load.',
                'Main integration, GPU renderer/dirty history and live input loop are outside this suite.'])
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
