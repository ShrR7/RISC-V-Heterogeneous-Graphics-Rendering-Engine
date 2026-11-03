#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real production scene40/60Hz contracts; no board or MMIO claims."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--baseline', type=Path, default=ROOT.parent/'neon_rhythm_20261014')
    args = parser.parse_args()
    build = ROOT/'build/host/animation_scene'
    build.mkdir(parents=True, exist_ok=True)
    untouched = ('src/garden_core.c', 'src/garden_core.h', 'src/garden_assets.c',
                 'src/garden_assets.h', 'assets/atlas.bin')
    unchanged = {}
    for name in untouched:
        data = (ROOT/name).read_bytes()
        if data != (args.baseline/name).read_bytes():
            raise SystemExit('Refusing core/atlas/geometry change: '+name)
        unchanged[name] = sha(data)
    source_names = ('garden_scene.c', 'garden_feedback.c', 'garden_fx.c',
                    'garden_core.c', 'garden_assets.c')
    exe = build/'test_animation_scene.exe'
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT/'src'), str(ROOT/'tools/test_animation_scene.c')]
    command += [str(ROOT/'src'/name) for name in source_names]+['-o', str(exe)]
    result = subprocess.run(command, capture_output=True, text=True)
    (build/'compile.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout+result.stderr, end='')
        raise SystemExit(result.returncode)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    log = result.stdout+result.stderr
    (build/'test_animation_scene.log').write_text(log, encoding='utf-8')
    print(log, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'animation scene checks=(\d+) scene_bytes=(\d+)', log)
    trajectory = re.search(r'native_steps=(\d+) actual_shots=(\d+) charge_samples=(\d+) rest_samples=(\d+) burst_samples=(\d+)', log)
    if not match or not trajectory:
        raise SystemExit('Missing completed production animation summary')
    metrics = dict(zip(('native_game_steps', 'actual_shots', 'charge_render_samples',
                        'recovery_render_samples', 'burst_render_samples'), map(int, trajectory.groups())))
    files = ('src/garden_scene.c', 'src/garden_scene.h', 'src/garden_fx.c',
             'src/garden_feedback.c', 'src/garden_feedback.h', 'src/garden_fx.h',
             'tools/test_animation_scene.c', 'tools/verify_animation_scene.py')
    report = dict(status='PASS', host_only=True, board_acceptance=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        compiler_command=command, contract_checks=int(match.group(1)),
        scene_state_bytes=int(match.group(2)), **metrics,
        unchanged_core_atlas_and_geometry_sha256=unchanged,
        source_sha256={name: sha((ROOT/name).read_bytes()) for name in files},
        test_log_sha256=sha(log.encode('utf-8')),
        shell_open_q8_range=[0, 256], logic_hz=40, tested_display_hz=[40, 60],
        closure_ticks=16, wing_follow_ticks=[2, 3], wing_lag_limit_px=6,
        body_core_muzzle_true_anchor=True, existing_background_draws=8,
        background_speeds_px_per_second=[20, 60], background_alpha=[24, 26],
        new_atlas_bytes=0, additional_sprite_layers=0,
        checks=['Real core phase20/16/12charge: monotonic smoothstep40/60Hz and whole BURST hold',
                'Real REST monotonic16tick closure, small genuine-shot recoil and local reactor breathing',
                '2/3tick wing history, fractional interpolation and6px cosmetic lag clamp',
                'Pause freezes pose/fraction/history; restart/disable clear stale geometry',
                'Pure shared exact shell bbox, true body/core/cue anchors and material not white',
                '0..241capacities, canaries,234essentials and foreground marker priority',
                'Existing8background sprites show20/60px/s, no atlas/largeAlpha allocation',
                '18000native core snapshots byte identical; all actual shot counters observed'],
        limits=['Boss whole-white flash removal here coversFX path; combat path is verified separately.',
                'Cosmetic wing movement does not change gameplay collision boxes or target centre.',
                'Small reactor now uses existing625px sprite Alpha; real FPS needs board validation.',
                'No new board acceptance or600second run is established by these host checks.'])
    (build/'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
