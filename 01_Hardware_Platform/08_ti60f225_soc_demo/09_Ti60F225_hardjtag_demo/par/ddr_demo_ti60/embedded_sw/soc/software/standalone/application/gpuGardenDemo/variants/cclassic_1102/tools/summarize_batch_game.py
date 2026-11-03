#!/usr/bin/env python3
"""Normal-game RTL12 batch use, layered on the complete coherent game audit."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

COUNTERS = ('submissions', 'batches', 'instances', 'fill', 'blit')
CONTROL = {'enabled': 1, 'list': 0x0067D000, 'capacity': 256, 'order': 'consecutive_only'}


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def unique(row, fields):
    return all(len(re.findall(r'\b' + re.escape(field) + '=', row['raw'])) == 1 for field in fields)


def audit_batch_game(rows):
    controls = [r for r in rows if r['raw'].startswith('[BATCH_CTRL]')]
    samples = [r for r in rows if r['raw'].startswith('[BATCH]')]
    waves = [r for r in rows if r['raw'].startswith('[WAVE]')]
    issues, valid = [], []
    if len(controls) > 1:
        issues.append({'error': 'multiple batch startup controls'})
    for control in controls:
        if not unique(control, CONTROL) or any(control.get(k) != v for k, v in CONTROL.items()):
            issues.append({'line': control['line'], 'error': 'batch startup control differs'})
        if any(r['line'] < control['line'] for r in samples):
            issues.append({'line': control['line'], 'error': 'batch control follows gameplay samples'})
    prior = None
    for row in samples:
        fields = ('tick', 'mode', 'enabled') + COUNTERS
        if not unique(row, fields) or not all(type(row.get(k)) is int and 0 <= row[k] <= 0xFFFFFFFF for k in fields):
            issues.append({'line': row['line'], 'error': 'missing, repeated, negative or invalid batch field'})
            continue
        if row['mode'] > 3 or row['enabled'] != 1:
            issues.append({'line': row['line'], 'error': 'invalid game mode or batch rendering disabled'})
        if row['submissions'] < row['batches'] or row['instances'] < 2 * row['batches'] or row['fill'] + row['blit'] != row['batches']:
            issues.append({'line': row['line'], 'error': 'batch cumulative ledger inconsistent'})
        wave = next((w for w in reversed(waves) if w['line'] < row['line']), None)
        if wave is None or not unique(wave, ('tick', 'mode')) or \
           wave.get('tick') != row['tick'] or wave.get('mode') != row['mode'] or \
           prior is not None and wave['line'] <= prior['line']:
            issues.append({'line': row['line'], 'error': 'missing fresh same-tick and mode WAVE sample'})
        if prior:
            delta = {k: row[k] - prior[k] for k in COUNTERS}
            if any(value < 0 for value in delta.values()):
                issues.append({'line': row['line'], 'error': 'cumulative batch counter decreased', 'delta': delta})
            elif delta['submissions'] < delta['batches'] or delta['instances'] < 2 * delta['batches'] or delta['fill'] + delta['blit'] != delta['batches']:
                issues.append({'line': row['line'], 'error': 'batch interval ledger inconsistent', 'delta': delta})
        valid.append(row)
        prior = row
    delta = {k: valid[-1][k] - valid[0][k] for k in COUNTERS} if len(valid) > 1 else None
    checks = {'one_batch_startup_control': len(controls) == 1,
              'at_least_two_gameplay_batch_samples': len(valid) >= 2,
              'all_samples_valid': bool(samples) and len(valid) == len(samples),
              'normal_game_batch_growth': bool(delta and delta['batches'] > 0),
              'normal_game_blit_batch_growth': bool(delta and delta['blit'] > 0)}
    return {'status': 'FAIL' if issues else 'PASS' if all(checks.values()) else 'PENDING',
            'checks': checks, 'issues': issues, 'controls': controls,
            'samples_observed': len(samples), 'valid_samples': len(valid),
            'samples_all': samples, 'first': valid[0] if valid else None,
            'last': valid[-1] if valid else None, 'normal_game_delta': delta,
            'scope': 'Only changes between normal periodic BATCH samples count as observed game activity. Startup BENCH totals and one positive cumulative sample cannot satisfy coverage. Counters describe commands and instances, not independent GPU bus measurements or image equivalence.'}


def analyze(path):
    path = Path(path)
    base = module('batch_game_coherent', 'summarize_coherent.py').analyze(path)
    records = module('batch_game_records', 'summarize_hazard.py')
    rows, partial, boots = records.parse(path.read_text(encoding='utf-8-sig', errors='replace'), True)
    batch = audit_batch_game(rows)
    statuses = base['status'], batch['status']
    return {**base, 'status': 'FAIL' if 'FAIL' in statuses else 'PASS' if statuses == ('PASS', 'PASS') else 'PENDING',
            'evidence_kind': 'RTL12_BATCH_NORMAL_GAME_COHERENT_FRESH_CAPTURE',
            'inherited_coherent_status': base['status'], 'batch_game': batch,
            'batch_selected_boot_line': boots[-1] + 1 if boots else None,
            'batch_partial_tail_excluded': partial}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--require-strict-60', action='store_true')
    args = parser.parse_args()
    result = analyze(args.log)
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as stream:
            stream.write(text)
    print(text, end='')
    return 0 if result['status'] == 'PASS' and (not args.require_strict_60 or result['strict_60fps']['status'] == 'PASS') else 1


if __name__ == '__main__':
    raise SystemExit(main())
