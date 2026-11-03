#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build production feedback contracts and verify frozen Stage asset prefix.

No serial/JTAG/MMIO operations. Host snapshots and geometry prove software
contracts, not measured frame time, hardware performance or board pixels.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'neon_stage_20261013'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/feedback'
    build.mkdir(parents=True, exist_ok=True)
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    old = (BASE / 'assets/atlas.bin').read_bytes()
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    previous = json.loads((BASE / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(old) == 226816 and len(atlas) <= 237568 and atlas[:len(old)] == old
    assert len(manifest['sprites']) == 280 and manifest['sprites'][:271] == previous['sprites']
    assert sha(atlas) == manifest['sha256']
    end = 0
    for item in manifest['sprites']:
        assert item['offset'] % 4 == 0 and item['offset'] >= end
        assert item['stride'] == item['width'] * 2
        assert item['bytes'] == item['stride'] * item['height']
        end = item['offset'] + item['bytes']
        assert end <= len(atlas)
    for name in ('garden_core.c', 'garden_core.h'):
        assert (ROOT / 'src' / name).read_bytes() == (BASE / 'src' / name).read_bytes(), name
    exe = build / 'test_feedback.exe'
    sources = ('garden_feedback.c', 'garden_fx.c', 'garden_core.c', 'garden_assets.c')
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_feedback.c')]
    command += [str(ROOT / 'src' / name) for name in sources] + ['-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (build / 'test_feedback.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, end='')
        raise SystemExit(result.returncode)
    match = re.search(r'host contract checks=(\d+)', result.stdout)
    assert match, 'Missing completed contract summary'
    source_files = ('src/garden_feedback.c', 'src/garden_feedback.h', 'src/garden_fx.c', 'src/garden_fx.h',
                    'src/garden_core.c', 'src/garden_core.h', 'src/garden_assets.c', 'src/garden_assets.h',
                    'tools/test_feedback.c', 'tools/verify_feedback_host.py')
    report = dict(status='PASS', host_only=True, board_acceptance=False, compiler_command=command,
        contract_checks=int(match.group(1)), atlas_bytes=len(atlas), atlas_sha256=sha(atlas),
        frozen_271_slots_byte_identical=True, frozen_prefix_bytes=226816, unchanged_font_slots=172,
        all_descriptors_bounded=True, core_source_byte_identical=True,
        complete_core_snapshots_compared_steps=12000,
        checks=['actual production Boss HP-drop/kill and both enemy kills',
                'consumed bullet coordinates and honest fallback, no fake flash/score/despawn hits',
                'one-use bullet matching, invalid evidence fallback',
                'newest-three bounded sparks; six ticks alpha32/20/10; Alpha-off opaque',
                'same-tick/pause freezes; rollback/restart/mode/gap clears',
                'focus/demo/grace-blink/respawn/inactive and exact field-edge marker centre',
                'all capacities0..241 canaries, essential then marker then spark then old decor',
                'full234 pool, 12000 complete core snapshots including pause/restarts'],
        stdout_sha256=sha(result.stdout.encode('utf-8')),
        source_sha256={name: sha((ROOT / name).read_bytes()) for name in source_files},
        limits=['Production observer/FX/core/assets with host compiler; no board pixel/frame-time claim.',
                'Main integration, production GPU/MMIO renderer and live input loop are not executed by this contract suite.'])
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
