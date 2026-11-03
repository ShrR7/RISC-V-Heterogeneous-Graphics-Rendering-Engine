#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-only genuine Boss timing and unchanged-generator scope verification."""
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


def function(source, name):
    match = re.search(r'^(?:static )?[^;\n]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', source, re.M)
    if not match:
        raise ValueError('Missing function ' + name)
    depth, end = 1, match.end()
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--baseline', type=Path, default=ROOT.parent / 'neon_combat_20261014')
    args = parser.parse_args()
    build = ROOT / 'build/host/rhythm_core'
    build.mkdir(parents=True, exist_ok=True)
    old = (args.baseline / 'src/garden_core.c').read_text(encoding='utf-8')
    current = (ROOT / 'src/garden_core.c').read_text(encoding='utf-8')
    unchanged = ('boss_attack1', 'boss_attack2', 'boss_attack3', 'spawn_enemy',
                 'run_waves', 'run_enemies', 'run_enemy_bullets', 'run_player_bullets',
                 'run_player', 'combat_reward', 'damage_player', 'check_player_collisions')
    hashes = {}
    for name in unchanged:
        original, now = function(old, name), function(current, name)
        if now != original:
            raise SystemExit('Out-of-scope core function changed: ' + name)
        hashes[name] = sha(now.encode('utf-8'))
    exe = build / 'test_rhythm_core.exe'
    source_names = ('garden_core.c', 'garden_scene.c', 'garden_feedback.c',
                    'garden_fx.c', 'garden_assets.c')
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_rhythm_core.c')]
    command += [str(ROOT / 'src' / name) for name in source_names] + ['-o', str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (build / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    if compiled.returncode:
        print(compiled.stdout + compiled.stderr, end='')
        raise SystemExit(compiled.returncode)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    log = result.stdout + result.stderr
    (build / 'test_rhythm_core.log').write_text(log, encoding='utf-8')
    print(log, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'rhythm core checks=(\d+) sizeof_game=(\d+) sizeof_boss=(\d+)', log)
    replay = re.search(r'seeds=65 wins=(\d+) losses=(\d+) restarts=(\d+) peak=(\d+) drops=(\d+) waits=(\d+) hits=(\d+) kills=(\d+) shots=(\d+)/(\d+)/(\d+) seed12345_score=(\d+) seed12345_victory_tick=(\d+)', log)
    if not match or not replay:
        raise SystemExit('Missing completed true timing/replay summary')
    metrics = dict(zip(('wins', 'losses', 'restarts', 'enemy_bullet_peak', 'dropped_enemy_bullets',
                        'capacity_wait_ticks', 'hits', 'kills', 'attack1_shots', 'attack2_shots',
                        'attack3_shots', 'seed12345_victory_score', 'seed12345_victory_tick'),
                       map(int, replay.groups())))
    timeline_matches = re.findall(r'SEED12345: cycle=(\d+) victory_tick=(\d+) score=(\d+) lives=(\d+) peak=(\d+) waits=(\d+) shots=(\d+)/(\d+)/(\d+) charge_windows=(\d+) bursts=(\d+) restart_tick=(\d+)', log)
    if len(timeline_matches) != 3:
        raise SystemExit('Missing three actual seed12345 victories')
    timeline_names = ('cycle', 'victory_tick', 'score', 'lives', 'peak', 'wait_ticks',
                      'attack1_shots', 'attack2_shots', 'attack3_shots',
                      'charge_windows', 'bursts', 'automatic_restart_tick')
    timeline = [dict(zip(timeline_names, map(int, row))) for row in timeline_matches]
    names = ('src/garden_core.c', 'src/garden_core.h', 'src/garden_scene.c',
             'src/garden_scene.h', 'tools/test_rhythm_core.c', 'tools/verify_rhythm_core.py')
    report = dict(status='PASS', host_only=True, board_acceptance=False,
                  completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  compiler_command=command, contract_checks=int(match.group(1)),
                  sizeof_game=int(match.group(2)), sizeof_boss=int(match.group(3)),
                  vulnerable_seeds=65, **metrics,
                  pulse_sizes=[64, 9, 36], pulses_per_train=[2, 6, 2],
                  physical_enemy_bullet_pool=192, boss_live_bullet_budget=128,
                  seed12345_completed_cycles=timeline,
                  hp_bands_percent=[70, 40], charge_ticks_by_band=[20, 16, 12],
                  recovery_ticks_by_band=[80, 64, 48], transit_ticks=40,
                  unchanged_function_sha256=hashes,
                  source_sha256={name: sha((ROOT / name).read_bytes()) for name in names},
                  test_log_sha256=sha(log.encode('utf-8')),
                  checks=['All3patterns x3bands exact future-step first emission and scene countdown',
                          'Full charge then finite64/9/36pulse trains, complete recovery and relocation',
                          'Whole-train admission plus adversarial competing producer safe defer',
                          '128live/192physical exact train boundaries, stale-count avoidance and full warning after budget recovery',
                          'Backpressure never emits an unannounced recovery pulse or consumes RNG',
                          'Pause/restart freezes or clears schedule and scene prediction',
                          '65vulnerable seeds, actual wins/losses/restarts and hit-score event ledger',
                          'Original generator geometry, enemy/player/collision/HP/scoring functions unchanged'],
                  limits=['Boss timing, RNG consumption order and resulting trajectories intentionally change.',
                          'This suite does not assert original complete state equivalence or board FPS/pixels.',
                          'Forecast describes reachable Boss states with no competing wave enemies.'])
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
