#!/usr/bin/env python3
"""Audit paired, full-pixel single/batch UART benchmarks without hiding samples."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
from statistics import median

SCENES = ('fill_grid', 'bullets_same128', 'bullets_mixed128', 'alpha_overlap', 'cache_fallback')
SCENE_ITEMS = dict(zip(SCENES, (128, 128, 128, 48, 8)))
FRAME_PIXELS = 1280 * 720
FIELDS = re.compile(r'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)')
TAG = re.compile(r'^\[(BATCH_BENCH_CTRL|BATCH_SAMPLE|BATCH_DIFF|BATCH_BENCH_DONE|BATCH_PROBE|BATCH_PROBE_END|BATCH_FATAL)\](?:\s|$)')
CONTRACT = {'version': '1', 'parity': 'AB_BA', 'compare': 'full_pixels',
            'timing': 'submit_wait_only', 'init': 'outside_timing',
            'hash': 'outside_timing', 'present': 'excluded'}


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def records(text):
    partial = bool(text and not text.endswith(('\r', '\n')))
    lines = text.splitlines()
    if partial:
        lines.pop()
    boots = [i for i, line in enumerate(lines) if line.startswith('GARDEN Ti60 cropped port ')]
    start = boots[-1] if boots else 0
    rows = []
    for index in range(start, len(lines)):
        match = TAG.match(lines[index])
        if match:
            pairs = FIELDS.findall(lines[index])
            rows.append({'line': index + 1, 'raw': lines[index], 'tag': match[1],
                         'values': dict(pairs), 'duplicates': sorted({k for k, _ in pairs if sum(a == k for a, _ in pairs) > 1})})
    return rows, partial, boots[-1] + 1 if boots else None


def integer(value):
    if not isinstance(value, str) or not re.fullmatch(r'(?:0x[0-9a-fA-F]+|[0-9]+)', value):
        return None
    return int(value, 16 if value.startswith('0x') else 10)


def crc(value):
    if not isinstance(value, str) or not re.fullmatch(r'(?:0x)?[0-9a-fA-F]{1,8}', value):
        return None
    return int(value.removeprefix('0x'), 16)


def audit_benchmark(rows):
    issues, missing = [], []
    controls = [r for r in rows if r['tag'] == 'BATCH_BENCH_CTRL']
    done = [r for r in rows if r['tag'] == 'BATCH_BENCH_DONE']
    samples = [r for r in rows if r['tag'] == 'BATCH_SAMPLE']
    differences = [r for r in rows if r['tag'] == 'BATCH_DIFF']
    probes = [r for r in rows if r['tag'] == 'BATCH_PROBE']
    integrity = [r for r in rows if r['tag'] == 'BATCH_PROBE_END']
    if any(r['tag'] == 'BATCH_FATAL' for r in rows):
        issues.append({'error': 'probe reported fatal error'})
    for row in rows:
        if row['duplicates']:
            issues.append({'line': row['line'], 'error': 'repeated field', 'fields': row['duplicates']})
    if len(controls) > 1 or len(done) > 1:
        issues.append({'error': 'multiple benchmark controls or terminal records'})
    if len(probes) > 1 or len(integrity) > 1:
        issues.append({'error': 'multiple probe metadata or integrity records'})
    if not probes:
        missing.append('probe memory map and greater-than-4KiB source coverage')
    else:
        p = probes[0]['values']
        expected = {'ref': 0x00800000, 'list': 0x0067C000, 'render_list': 0x0067D000}
        if any(integer(p.get(k)) != value for k, value in expected.items()) or \
           integer(p.get('front')) not in (0x00200000, 0x00400000) or \
           (integer(p.get('fallback_source_bytes')) or 0) <= 4096 or crc(p.get('atlas')) != 0xCEE719EF:
            issues.append({'line': probes[0]['line'], 'error': 'probe memory map, atlas, or cache-fallback coverage differs'})
        if any(r['line'] < probes[0]['line'] for r in samples + differences):
            issues.append({'error': 'probe metadata follow measured samples'})
    if not integrity:
        missing.append('front, atlas, and scratch integrity end record')
    else:
        p = integrity[0]['values']
        if integer(p.get('front_unchanged')) != 1 or integer(p.get('atlas_unchanged')) != 1 or integer(p.get('guard')) != 0:
            issues.append({'line': integrity[0]['line'], 'error': 'front framebuffer, atlas, or scratch integrity failed'})
        if any(r['line'] > integrity[0]['line'] for r in samples + differences):
            issues.append({'error': 'integrity end record precedes measured samples'})
    control = controls[0]['values'] if controls else {}
    rounds = integer(control.get('rounds'))
    valid_rounds = rounds is not None and 4 <= rounds <= 64 and rounds % 2 == 0
    declared_scenes = control.get('scenes', '').split(',')
    if controls:
        if any(control.get(k) != v for k, v in CONTRACT.items()) or not valid_rounds:
            issues.append({'error': 'benchmark timing/fairness contract differs', 'observed': control})
        if len(set(declared_scenes)) != len(declared_scenes) or set(declared_scenes) != set(SCENES):
            issues.append({'error': 'benchmark scene contract differs', 'observed': declared_scenes})
        if any(r['line'] < controls[0]['line'] for r in samples + differences + done):
            issues.append({'error': 'benchmark records precede startup control'})
    else:
        missing.append('benchmark control')
    if done:
        if any(r['line'] > done[0]['line'] for r in rows):
            issues.append({'error': 'benchmark data appear after terminal record'})
        terminal = done[0]['values']
        if integer(terminal.get('samples')) != len(samples) or integer(terminal.get('pairs')) != len(differences):
            issues.append({'error': 'terminal counts differ from captured records'})
        if integer(terminal.get('failures')) != 0:
            issues.append({'error': 'benchmark reported failure'})
    else:
        missing.append('benchmark terminal record')

    by_pair, diff_by_pair = {}, {}
    for row in samples + differences:
        values = row['values']
        scene, number = values.get('scene'), integer(values.get('round'))
        if scene not in SCENES or number is None or valid_rounds and number >= rounds:
            issues.append({'line': row['line'], 'error': 'unknown scene or invalid round'})
            continue
        key = scene, number
        if row['tag'] == 'BATCH_DIFF':
            if key in diff_by_pair:
                issues.append({'line': row['line'], 'error': 'duplicate full-pixel comparison'})
            diff_by_pair[key] = row
            if integer(values.get('diff')) != 0 or integer(values.get('guard')) != 0:
                issues.append({'line': row['line'], 'error': 'pixel difference or framebuffer guard changed'})
            if integer(values.get('compared')) is None or integer(values.get('compared')) <= 0 or crc(values.get('initial_crc')) is None:
                issues.append({'line': row['line'], 'error': 'missing full comparison extent or initial hash'})
            continue
        if values.get('path') not in ('single', 'batch') or values.get('order') != ('AB' if number % 2 == 0 else 'BA'):
            issues.append({'line': row['line'], 'error': 'invalid path or alternating AB/BA schedule'})
        for field in ('us', 'commands', 'items', 'pixels'):
            if integer(values.get(field)) is None or integer(values.get(field)) <= 0:
                issues.append({'line': row['line'], 'error': 'missing positive sample field', 'field': field})
        if integer(values.get('errors')) != 0 or crc(values.get('crc')) is None or crc(values.get('initial_crc')) is None:
            issues.append({'line': row['line'], 'error': 'GPU errors or missing/invalid hash'})
        if integer(values.get('items')) != SCENE_ITEMS[scene]:
            issues.append({'line': row['line'], 'error': 'scene has wrong primitive count'})
        if integer(values.get('pixels')) != FRAME_PIXELS:
            issues.append({'line': row['line'], 'error': 'sample does not cover complete 1280x720 framebuffer'})
        pair = by_pair.setdefault(key, {})
        if values.get('path') in pair:
            issues.append({'line': row['line'], 'error': 'duplicate timing sample'})
        pair[values.get('path')] = row

    pairs = []
    if valid_rounds:
        expected = {(scene, number) for scene in SCENES for number in range(rounds)}
        for key in sorted(expected):
            pair, difference = by_pair.get(key, {}), diff_by_pair.get(key)
            if set(pair) != {'single', 'batch'} or difference is None:
                missing.append({'scene': key[0], 'round': key[1], 'evidence': 'both timings plus full comparison'})
                continue
            a, b, d = pair['single'], pair['batch'], difference
            av, bv, dv = a['values'], b['values'], d['values']
            expected_first = a if key[1] % 2 == 0 else b
            expected_last = b if key[1] % 2 == 0 else a
            if not expected_first['line'] < expected_last['line'] < d['line']:
                issues.append({'scene': key[0], 'round': key[1], 'error': 'actual sample order or comparison placement differs'})
            if any(integer(av.get(field)) != integer(bv.get(field)) for field in ('items', 'pixels')):
                issues.append({'scene': key[0], 'round': key[1], 'error': 'different primitive count or framebuffer extent'})
            if integer(dv.get('compared')) != integer(av.get('pixels')):
                issues.append({'scene': key[0], 'round': key[1], 'error': 'CPU comparison did not cover full framebuffer'})
            if len({crc(av.get('initial_crc')), crc(bv.get('initial_crc')), crc(dv.get('initial_crc'))}) != 1:
                issues.append({'scene': key[0], 'round': key[1], 'error': 'different initial framebuffer hashes'})
            if crc(av.get('crc')) != crc(bv.get('crc')):
                issues.append({'scene': key[0], 'round': key[1], 'error': 'single/batch output hash differs'})
            au, bu = integer(av.get('us')), integer(bv.get('us'))
            pairs.append({'scene': key[0], 'round': key[1], 'order': av.get('order'),
                          'single_us': au, 'batch_us': bu, 'speedup': au / bu if au is not None and bu else None,
                          'single_commands': integer(av.get('commands')), 'batch_commands': integer(bv.get('commands')),
                          'items': integer(av.get('items')), 'pixels': integer(av.get('pixels')),
                          'single_line': a['line'], 'batch_line': b['line'], 'comparison_line': d['line']})
    scene_summaries = {}
    for scene in SCENES:
        selected = [p for p in pairs if p['scene'] == scene]
        valid = [p for p in selected if p['single_us'] and p['batch_us']]
        if not valid:
            continue
        def summary(name):
            values = [p[name + '_us'] for p in valid]
            return {'samples': len(values), 'first_us': values[0], 'min_us': min(values), 'median_us': median(values),
                    'worst_us': max(values), 'total_us': sum(values), 'all_us': values}
        sc = sum(p['single_commands'] or 0 for p in valid)
        bc = sum(p['batch_commands'] or 0 for p in valid)
        scene_summaries[scene] = {'single': summary('single'), 'batch': summary('batch'),
                                  'paired_speedup_all': [p['speedup'] for p in valid],
                                  'paired_speedup_median': median(p['speedup'] for p in valid),
                                  'total_time_speedup': sum(p['single_us'] for p in valid) / sum(p['batch_us'] for p in valid),
                                  'single_commands_total': sc, 'batch_commands_total': bc,
                                  'command_reduction_fraction': (sc - bc) / sc if sc else None,
                                  'AB_pairs': sum(p['order'] == 'AB' for p in valid),
                                  'BA_pairs': sum(p['order'] == 'BA' for p in valid)}
    return {'status': 'FAIL' if issues else 'PENDING' if missing else 'PASS', 'issues': issues, 'missing': missing,
            'controls': controls, 'probe_metadata': probes, 'probe_integrity': integrity,
            'terminal': done, 'rounds': rounds, 'required_scenes': list(SCENES),
            'samples_observed': len(samples), 'comparisons_observed': len(differences), 'pairs_observed': len(pairs),
            'samples_all': samples, 'comparisons_all': differences, 'pairs_all': pairs, 'scenes': scene_summaries,
            'limits': ['Performance is descriptive; a slower valid batch sample is retained and does not become a correctness failure.',
                       'UART cannot independently prove timer placement, primitive content, command counters, or cache fallback. Source-bound host tests and benchmark implementation inspection remain required.',
                       'Full-pixel comparison and hashes describe reported scratch framebuffer results, not every displayed frame.',
                       'Presentation remains 60 Hz; render-time savings are distinct from frame-rate gains. No 600-second run or bus fault injection is implied.']}


def analyze(path, require_game=True):
    path = Path(path)
    raw = path.read_bytes()
    rows, partial, boot = records(raw.decode('utf-8-sig', errors='replace'))
    benchmark = audit_benchmark(rows)
    game = module('batch_coherent_base', 'summarize_coherent.py').analyze(path) if require_game else None
    statuses = [benchmark['status']] + ([game['status']] if game else [])
    status = 'FAIL' if 'FAIL' in statuses else 'PASS' if all(s == 'PASS' for s in statuses) else 'PENDING'
    return {'schema': 1, 'evidence_kind': 'RTL12_SINGLE_BATCH_PAIRED_PERFORMANCE', 'status': status,
            'input': str(path.resolve()), 'raw_sha256': hashlib.sha256(raw).hexdigest().upper(),
            'selected_boot_line': boot, 'partial_tail_excluded': partial, 'benchmark': benchmark,
            'gameplay': game, 'strict_60fps': game.get('strict_60fps') if game else None,
            'new600s_run': False, 'visual_acceptance': 'PENDING_USER_OBSERVATION'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--benchmark-only', action='store_true')
    parser.add_argument('--require-strict-60', action='store_true')
    args = parser.parse_args()
    result = analyze(args.log, not args.benchmark_only)
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as stream:
            stream.write(text)
    print(text, end='')
    return 0 if result['status'] == 'PASS' and (not args.require_strict_60 or
                    (result.get('strict_60fps') or {}).get('status') == 'PASS') else 1


if __name__ == '__main__':
    raise SystemExit(main())
