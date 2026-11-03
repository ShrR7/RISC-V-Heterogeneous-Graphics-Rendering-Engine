#!/usr/bin/env python3
"""Compile and run production FX host contracts; no serial/JTAG access.

SPDX-License-Identifier: GPL-3.0-or-later
Hardware performance, FPGA pixels and board acceptance remain separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'void_art_20261013'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/fx'
    build.mkdir(parents=True, exist_ok=True)
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    baseline_atlas = (BASE / 'assets/atlas.bin').read_bytes()
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    baseline_manifest = json.loads((BASE / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(atlas) <= 237568 and sha(atlas) == manifest['sha256']
    assert atlas[:len(baseline_atlas)] == baseline_atlas, 'Frozen 266-slot atlas bytes changed'
    assert len(manifest['sprites']) == 280
    for current, previous in zip(manifest['sprites'][:266], baseline_manifest['sprites']):
        for field in ('id', 'name', 'offset', 'width', 'height', 'stride', 'anchor_x', 'anchor_y', 'bytes'):
            assert current[field] == previous[field], (previous['name'], field)
    end = 0
    for sprite in manifest['sprites']:
        assert sprite['offset'] % 4 == 0 and sprite['offset'] >= end
        assert sprite['stride'] == sprite['width'] * 2
        assert sprite['bytes'] == sprite['stride'] * sprite['height']
        end = sprite['offset'] + sprite['bytes']
        assert end <= len(atlas)
    assert 0x5C4000 + len(atlas) <= 0x5FE000
    for name in ('garden_core.c', 'garden_core.h'):
        assert (ROOT / 'src' / name).read_bytes() == (BASE / 'src' / name).read_bytes(), name
    exe = build / 'test_fx.exe'
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_fx.c'),
               str(ROOT / 'src/garden_fx.c'), str(ROOT / 'src/garden_core.c'),
               str(ROOT / 'src/garden_assets.c'), '-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    (build / 'test_fx.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, end='')
        raise SystemExit(result.returncode)
    match = re.search(r'host contract checks=(\d+)', result.stdout)
    assert match, 'No completed contract summary'
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'compiler_command': command, 'contract_checks': int(match.group(1)),
        'atlas_bytes': len(atlas), 'atlas_sha256': sha(atlas),
        'baseline_266_slots_byte_identical': True, 'all_descriptors_bounded': True,
        'asset_count_extended_to_280': True,
        'fixture_change': 'Only the expected total asset-count assertion extends274 to280; all legacy FX contracts retained.',
        'core_source_byte_identical': True, 'complete_core_snapshots_compared_steps': 12000,
        'checks': ['flash replaces without extra draw', 'white ColorKey hole masks',
                   'true old-position alpha6/12 trails', 'same-tick render idempotence',
                   'pause freezes, stationary converges', 'restart/mode/respawn/inactive/grace clears',
                   'two youngest kill rings, FX-off and alpha-off',
                   'all capacities0..241 canary bounds, essentials before decoration, panel slot',
                   '12000 steps including pause/restart preserve complete core snapshots'],
        'stdout_sha256': sha(result.stdout.encode('utf-8')),
        'source_sha256': {name: sha((ROOT / name).read_bytes())
                          for name in ('src/garden_core.c', 'src/garden_core.h', 'src/garden_fx.c', 'src/garden_fx.h',
                                       'src/garden_assets.c', 'src/garden_assets.h', 'tools/test_fx.c', 'tools/verify_fx_host.py')},
        'limits': ['Uses production FX/core with host compiler; no board pixels or measured rendering time.',
                   'This contract test does not execute the production GPU/MMIO renderer.'],
    }
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
