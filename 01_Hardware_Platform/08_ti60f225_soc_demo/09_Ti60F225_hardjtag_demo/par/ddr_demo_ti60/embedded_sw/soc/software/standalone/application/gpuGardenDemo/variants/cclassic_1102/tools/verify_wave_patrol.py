#!/usr/bin/env python3
"""Focused production-core wave patrol verification; no hardware or old report edits."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PARENT = ROOT.parent / 'neon_boss_calm_20261022'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

def without_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)

def erase(text, name, whole=False):
    match = re.search(r'(?m)^static\s+void\s+' + name + r'\s*\([^;]*?\)\s*\{', text)
    assert match, 'missing function ' + name
    start = text.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[:match.start()] + text[end:] if whole else text[:start] + '{ BODY_CHECKED }' + text[end:]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/wave_patrol'
    build.mkdir(parents=True, exist_ok=True)
    before = {p.name: sha(p) for p in (ROOT / 'src').iterdir() if p.is_file()}
    parent_before = {p.name: sha(p) for p in (PARENT / 'src').iterdir() if p.is_file()}
    assert before.keys() == parent_before.keys(), 'runtime file set changed'
    assert all(before[k] == value for k, value in parent_before.items() if k != 'garden_core.c'), 'unexpected runtime change outside core'
    old = without_comments((PARENT / 'src/garden_core.c').read_text(encoding='utf-8'))
    new = without_comments((ROOT / 'src/garden_core.c').read_text(encoding='utf-8'))
    old = erase(erase(old, 'return_wave_enemy', whole=True), 'run_enemies')
    new_erased = erase(new, 'run_enemies')
    assert re.sub(r'\s+', '', old) == re.sub(r'\s+', '', new_erased), 'unexpected core change outside removed top-return helper and enemy loop'
    assert 'return_wave_enemy' not in new, 'top re-entry helper/calls must be absent'
    cmd = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_wave_patrol.c'),
           str(ROOT / 'tools/test_wave_patrol_parent.c'), str(ROOT / 'src/garden_assets.c'),
           '-o', str(build / 'test_wave_patrol.exe')]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(build / 'test_wave_patrol.exe')], capture_output=True, text=True)
    output = run.stdout + run.stderr
    (build / 'test.log').write_text(output, encoding='utf-8')
    print(output, end='')
    if run.returncode:
        raise SystemExit(run.returncode)
    result = re.search(r'PASS wave_patrol checks=(\d+) game_bytes=(\d+)', output)
    patrol = re.search(r'PATROL long_steps=(\d+) moves=(\d+) windows=(\d+) large_volleys=(\d+) age_saturated_steps=(\d+) move_saturated_steps=(\d+)', output)
    controls = re.search(r'CONTROL direct_events=(\d+) prefix_steps=(\d+) boss_snapshots=(\d+) boss_steps=(\d+) stage_cases=(\d+)', output)
    assert result and patrol and controls, 'focused fixture did not complete'
    assert before == {p.name: sha(p) for p in (ROOT / 'src').iterdir() if p.is_file()}, 'candidate runtime changed during test'
    assert parent_before == {p.name: sha(p) for p in (PARENT / 'src').iterdir() if p.is_file()}, 'parent runtime changed during test'
    bound_files = [ROOT / ('src/' + name) for name in ['garden_core.c', 'garden_core.h', 'garden_assets.c', 'garden_assets.h', 'main.c']]
    bound_files += [ROOT / ('tools/' + name) for name in ['test_wave_patrol.c', 'test_wave_patrol_parent.c', 'verify_wave_patrol.py']]
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'checks': int(result[1]), 'game_bytes': int(result[2]),
        'long_steps': int(patrol[1]), 'complete_moves': int(patrol[2]),
        'complete_attack_windows': int(patrol[3]), 'large_ring_volleys': int(patrol[4]),
        'age_saturated_steps': int(patrol[5]), 'move_saturated_steps': int(patrol[6]),
        'direct_fire_events': int(controls[1]), 'identical_entrance_prefix_steps': int(controls[2]),
        'boss_identical_snapshots': int(controls[3]), 'boss_identical_sequence_steps': int(controls[4]),
        'strict_clear_and_control_cases': int(controls[5]),
        'cycle_seeds': 8, 'cycle_models': 2, 'cycle_ticks': 80000,
        'compiler_command': cmd,
        'core_sha256': sha(ROOT / 'src/garden_core.c'),
        'parent_core_sha256': sha(PARENT / 'src/garden_core.c'),
        'source_sha256': {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p) for p in bound_files},
        'runtime_source_sha256': before, 'parent_source_sha256': parent_before,
        'contracts': [
            'Both enemy models retain the same active slot, already-reduced HP and type through 8seeds x80000steps each; no lifetime removal or top re-entry even beyond1600ticks and uint16 age saturation.',
            'Every40step patrol alternates horizontal side, stays in upper arena and start-target rectangle, follows cubic smoothstep within0.1px, lands exactly and stops; vx/vy retain actual per-step velocities for pilot avoidance.',
            'Existing entrance/braking/first attack/rest prefix remains byte-identical to parent before first patrol.',
            'Every complete large150tick attack window emits exactly one fixed eight-shot DIAMOND ring; small90tick windows retain aimed DART triples, initial delays and cooldowns.',
            '4096same-input direct firing events and their PRNG/whole-state results are byte-identical; actual directional asset mapper and atlas bounds checked.',
            '384Boss generator snapshots and48000paired real Boss/laser steps across three HP bands are byte-identical to calm parent, preserving lower movement cadence and laser behavior for identical entering states.',
            'Real run_waves cannot pass the final live target, partial damage, inconsistent active count or missing scheduled task; final real kill alone admits unchanged100tick Boss entrance.',
            'Pause freezes movement/age/timer; resume continues it; explicit restart resets wave state and preserves best score.',
            'Strict source boundary permits only removed return_wave_enemy and modified run_enemies; all other runtime source/header/assets remain byte-identical.'
        ],
        'limits': [
            'Host-only; no visual, FPS, display, DDR, full-game or600second acceptance.',
            'Long schedule fixture drains enemy bullets after each step to avoid pool-drop censorship; emitted production is measured, not live screen occupancy.',
            'Patrol target RNG draws intentionally change later whole-game random history and combat outcomes; Boss equality is for same entering state/seed, not same complete-run order.',
            'Normal legitimate entrance/patrol states are exercised; corrupted arbitrary phase4 fields are not a supported external state interface.'
        ]
    }
    (build / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'checks': report['checks'], 'core_sha256': report['core_sha256'], 'report_sha256': sha(build / 'verification.json')}))
if __name__ == '__main__':
    main()

