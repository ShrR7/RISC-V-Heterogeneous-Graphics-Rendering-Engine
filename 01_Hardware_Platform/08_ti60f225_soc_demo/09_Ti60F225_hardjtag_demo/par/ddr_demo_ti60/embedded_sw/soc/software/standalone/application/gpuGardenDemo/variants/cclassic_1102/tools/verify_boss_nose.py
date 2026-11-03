#!/usr/bin/env python3
"""Real Boss birth/collision points and unchanged side-laser contract, host only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PARENT = ROOT.parent / 'cgame60h_1030'


def function(source, name):
    start = source.index('static void ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    output = ROOT / 'build/host/boss_nose'
    output.mkdir(parents=True, exist_ok=True)
    prior = (PARENT / 'src/garden_core.c').read_text(encoding='utf-8')
    current = (ROOT / 'src/garden_core.c').read_text(encoding='utf-8')
    # Freeze independent original generators while using the same primitive
    # allocator/trigonometry: only their physical birth coordinates may differ.
    reference = '\n'.join(function(prior, f'boss_attack{i}').replace(
        f'boss_attack{i}(', f'reference_boss_attack{i}(', 1) for i in (1, 2, 3))
    (output / 'boss_nose_reference.h').write_text(reference, encoding='utf-8')
    for name in ('laser_start', 'run_laser'):
        assert function(prior, name) == function(current, name), f'Unrelated side laser changed: {name}'
    executable = output / 'test_boss_nose.exe'
    names = ('garden_assets.c', 'garden_clear.c', 'garden_scene.c', 'garden_ordnance_view.c',
             'garden_feedback.c', 'garden_fx.c', 'garden_heading_precision.c')
    sources = [ROOT / 'src' / name for name in names]
    command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-I', str(output), '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_boss_nose.c'),
               *map(str, sources), '-o', str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    text = result.stdout + result.stderr
    (output / 'test.log').write_text(text, encoding='utf-8')
    print(text, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'PASS boss_nose checks=(\d+) generator_cases=(\d+) projectile_births=(\d+) laser_samples=(\d+)', text)
    if not match:
        raise SystemExit('Boss nose contract not completed')
    bound = sources + [ROOT / 'src/garden_core.c', ROOT / 'src/garden_core.h',
                       ROOT / 'src/garden_assets.h', ROOT / 'src/garden_scene.h',
                       ROOT / 'src/garden_ordnance_view.h', ROOT / 'tools/test_boss_nose.c',
                       Path(__file__), ROOT / 'assets/atlas.bin']
    report = {'status': 'PASS', 'host_only': True, 'board_acceptance': False,
              'checks': int(match[1]), 'generator_cases': int(match[2]),
              'projectile_births': int(match[3]), 'laser_samples': int(match[4]),
              'normal_boss_descriptor': {'width': 128, 'height': 132, 'anchor': [64, 66],
                                         'last_non_key_row': 130, 'last_non_key_x': [64, 65],
                                         'shot_origin_offset_pixels': [0, 64]},
              'side_laser_origin_offsets_pixels': [[-42, 19], [43, 19]],
              'side_laser_core_source_unchanged': True,
              'contracts': ['Every real ordinary Boss projectile starts at lower model nose',
                            'All three original generators preserve count, angle, velocity, acceleration, palette, PRNG and counters',
                            'Real collision checks consume the projectile at its relocated physical origin',
                            'Actual RGB565 lowest non-key row binds the nose macro to final asset geometry',
                            'Both visual gun pods and beam/collision origin remain aligned through40/40/12 warn/fire/fade'],
              'command': command,
              'reference_core_sha256': hashlib.sha256((PARENT / 'src/garden_core.c').read_bytes()).hexdigest().upper(),
              'source_sha256': {str(path.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest().upper() for path in bound}}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
