#!/usr/bin/env python3
"""Audit fresh V startup, real ordnance counters, and the full game loop."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

COUNTERS = ('launched', 'impacts', 'targets', 'bomb_damage', 'heals_spawned',
            'heals_collected', 'heals_restored', 'laser_warnings', 'laser_shots',
            'laser_hits', 'swept_hits')


def analyze(path):
    raw = path.read_bytes()
    text = raw.decode('utf-8-sig', errors='replace')
    lines = text.splitlines()
    if text and not text.endswith(('\n', '\r')):
        lines = lines[:-1]
    boots = [i for i, line in enumerate(lines) if line.startswith('GARDEN Ti60 cropped port ')]
    start = boots[-1] if boots else 0
    rows = []
    for i, line in enumerate(lines[start:], start + 1):
        row = {'line': i, 'raw': line}
        for key, value in re.findall(r'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)', line):
            try:
                row[key] = int(value, 16 if value.lower().startswith('0x') else 10)
            except ValueError:
                row[key] = value
        rows.append(row)
    spec = importlib.util.spec_from_file_location('ordnance_baseline_checks', Path(__file__).with_name('summarize_reward.py'))
    baseline = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(baseline)
    base = baseline.analyze([path])
    # The inherited parser pins the old 631954-byte U atlas. Validate this
    # variant's actual before/after CRC records against its own generation.
    generation_path = Path(__file__).resolve().parent.parent / 'assets/bullet_scale_generation.json'
    generation = json.loads((generation_path if generation_path.exists() else generation_path.with_name('ordnance_generation.json')).read_text(encoding='utf-8-sig'))
    expected_crc = int(generation['atlas_crc32'], 16)
    atlases = [r for r in rows if r['raw'].startswith(('[ATLAS]', '[ATLAS_POST_PROBE]'))]
    atlas_ok = (len(atlases) >= 2 and any(r['raw'].startswith('[ATLAS_POST_PROBE]') for r in atlases)
                and all(r.get('bytes') == generation['atlas_bytes'] and r.get('crc32') == r.get('expected') == expected_crc for r in atlases))
    startup = dict(base['startup'])
    startup['atlas_crc_before_and_after'] = atlas_ok
    inherited_issues = [r for r in base['live']['malformed_records']
                        if not (r.get('error') == 'observed startup check invalid' and r.get('check') == 'atlas CRC')]
    inherited_fail = bool(inherited_issues or base['fatal_records'] or base['rewards']['issues'] or
                          base['live']['counter_reversals'] or any(base['live']['counter_maxima'].values()) or
                          base['display']['status'] == 'FAIL')
    controls = [r for r in rows if r['raw'].startswith('[ORDNANCE_CTRL]')]
    shots = [r for r in rows if r['raw'].startswith('[ORDNANCE]')]
    rewards = [r for r in rows if r['raw'].startswith('[REWARD]')]
    problems = []
    expected = {'thrown': 1, 'bomb_damage': 600, 'blast_radius_px': 90, 'heal_lives': 1,
                'max_lives': 3, 'laser_warn_ticks': 40, 'laser_fire_ticks': 40,
                'laser_fade_ticks': 12, 'bullet_path_collision': 1, 'clear_enemy_bullets': 0}
    if controls and any(controls[-1].get(k) != v for k, v in expected.items()):
        problems.append('ORDNANCE_CTRL differs from this variant protocol')
    required = COUNTERS + ('tick', 'mode', 'flight', 'blast', 'laser', 'lives')
    previous = None
    paired_rewards = []
    for shot in shots:
        if not all(isinstance(shot.get(k), int) and shot[k] >= 0 for k in required):
            problems.append({'line': shot['line'], 'error': 'missing/negative ordnance field'})
            continue
        if shot['lives'] > 3 or shot['laser'] > 3 or shot['flight'] > 1:
            problems.append({'line': shot['line'], 'error': 'state bounds exceeded'})
        if (shot['heals_collected'] > shot['heals_spawned'] or
                shot['heals_restored'] > shot['heals_collected'] or
                shot['impacts'] > shot['launched'] or shot['launched'] > 2 or
                shot['laser_shots'] > shot['laser_warnings']):
            problems.append({'line': shot['line'], 'error': 'weapon/pickup conservation violated'})
        if previous and shot['tick'] >= previous['tick']:
            for k in COUNTERS:
                if shot[k] < previous[k]:
                    problems.append({'line': shot['line'], 'error': 'counter decreased within a run', 'field': k})
        elif previous and shot['mode'] != 0:
            problems.append({'line': shot['line'], 'error': 'tick reversal outside restart'})
        previous = shot
        reward = next((r for r in reversed(rewards) if r['line'] < shot['line']), None)
        if reward is None or reward.get('tick') != shot['tick']:
            problems.append({'line': shot['line'], 'error': 'missing same-tick REWARD pair'})
        else:
            paired_rewards.append(reward)
            if reward.get('cleared') != 0:
                problems.append({'line': reward['line'], 'error': 'offensive bomb unexpectedly clears enemy bullets'})
            if reward.get('used') != shot['launched'] or reward.get('bombs', -1) + shot['launched'] != 2:
                problems.append({'line': shot['line'], 'error': 'launches do not match finite inventory'})
    maxima = {k: max((r.get(k, 0) for r in shots), default=0) for k in COUNTERS}
    checks = {
        'fresh_variant_startup': bool(boots and controls and all(startup.values()) and not inherited_fail),
        'real_bomb_launch_and_impact': maxima['launched'] > 0 and maxima['impacts'] > 0 and maxima['targets'] > 0,
        'effective_bomb_damage_recorded': maxima['bomb_damage'] >= 600,
        'heal_drop_and_real_pickup': maxima['heals_spawned'] > 0 and maxima['heals_collected'] > 0,
        'laser_warning_and_fire': maxima['laser_warnings'] > 0 and maxima['laser_shots'] > 0,
        'actual_victory': bool(base['rewards']['victories']),
        'restart_best_and_stock_retained': bool(base['rewards']['terminal_restarts']) and all(
            r['retained_best_and_replenished'] for r in base['rewards']['terminal_restarts']),
    }
    status = 'FAIL' if problems or inherited_fail or (atlases and not atlas_ok) else 'PASS' if all(checks.values()) else 'PENDING'
    return {'schema': 1, 'evidence_kind': 'ORDNANCE_FRESH_BOARD_CAPTURE', 'status': status,
            'input': str(path.resolve()), 'raw_sha256': hashlib.sha256(raw).hexdigest().upper(),
            'selected_boot_line': start + 1 if boots else None,
            'checks': checks, 'issues': problems, 'ordnance_samples': len(shots),
            'observed_maxima': maxima, 'paired_reward_samples': len(paired_rewards),
            'controls': controls, 'startup': startup, 'live': base['live'],
            'atlas_validation': {'expected_bytes': generation['atlas_bytes'], 'expected_crc32': generation['atlas_crc32'], 'records': atlases, 'valid': atlas_ok},
            'inherited_non_atlas_issues': inherited_issues,
            'strict_60fps': base['strict_60fps'], 'display': base['display'],
            'victories': base['rewards']['victories'], 'restarts': base['rewards']['terminal_restarts'],
            'new600s_run': False, 'visual_acceptance': 'PENDING_USER_OBSERVATION',
            'limits': ['The inherited clear-bomb reward-closure criterion is intentionally not used.',
                       'Actual restored health may be zero if all collected packs meet full health; host tests cover the cap.',
                       'Laser/swept hit counters may be zero when the player avoids damage; focused host tests cover these paths.',
                       'Sparse reports show real counters, not exact effect frames or per-frame deadlines.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--require-strict-60', action='store_true')
    args = parser.parse_args()
    result = analyze(args.log)
    output = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as stream:
            stream.write(output)
    print(output, end='')
    return 0 if result['status'] == 'PASS' and (not args.require_strict_60 or result['strict_60fps']['status'] == 'PASS') else 1


if __name__ == '__main__':
    raise SystemExit(main())
