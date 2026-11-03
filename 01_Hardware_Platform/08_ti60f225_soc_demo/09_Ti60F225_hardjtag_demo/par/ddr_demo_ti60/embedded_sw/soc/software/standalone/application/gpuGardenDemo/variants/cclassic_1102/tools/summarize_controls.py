#!/usr/bin/env python3
"""Read-only functional UiInputTest review. FPS and deferred judder are not gates.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

TOOLS = Path(__file__).resolve().parent
SCHEDULE = [(8, 'a'), (10, 'd'), (12, 'w'), (14, 's'), (16, 'p'), (18, 'p'),
            (20, 'r'), (23, 't'), (26, 't'), (30, 'b'), (38, 'c'), (43, 'c'),
            (48, 'v'), (51, 'n'), (54, 'v'), (57, 'n'), (60, 'n'), (63, 'n'),
            (66, 'n'), (69, 'n'), (72, 'c'), (76, 'v'), (80, 'v'), (84, 'c'),
            (88, 'f'), (91, 'f'), (94, 'p'), (96, 'p'), (99, 'r'), (104, 't'), (108, 't')]
FAULT_FIELDS = ('errors', 'front_reject', 'atlas_bounds', 'swap_timeout',
                'log_dropped', 'log_truncated', 'report_scheduler_overruns', 'missed')
FIELDS = re.compile(r'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def parsed(line, number):
    row = {'line': number, 'raw': line}
    for key, token in FIELDS.findall(line):
        try:
            row[key] = int(token, 16 if token.lower().startswith('0x') else 10)
        except ValueError:
            row[key] = token
    return row


def load_lines(path):
    raw = path.read_bytes()
    text = raw.decode('utf-8-sig', errors='replace')
    lines = text.splitlines()
    partial = bool(text and not text.endswith(('\n', '\r')))
    if partial:
        lines = lines[:-1]
    return lines, {'path': str(path.resolve()), 'sha256': digest(raw), 'bytes': len(raw),
                   'partial_tail_excluded': partial}


def audit(log, host_log, case_dir=None):
    lines, source = load_lines(Path(log))
    hosts, host_source = load_lines(Path(host_log))
    boots = [i for i, line in enumerate(lines) if line.startswith('GARDEN Ti60 cropped port ')]
    start = boots[-1] if boots else 0
    rows = [parsed(line, i + 1) for i, line in enumerate(lines) if i >= start]
    select = lambda tag: [r for r in rows if r['raw'].startswith('[' + tag + ']')]
    states, live, benches = select('INPUT_STATE'), select('LIVE'), select('BENCH')
    issues, warnings = [], []
    gates = {}

    def gate(name, ok, detail=None):
        gates[name] = bool(ok)
        if not ok:
            issues.append({'gate': name, 'detail': detail})

    gate('boot_banner', bool(boots))
    gate('ready', bool(select('READY')))
    gate('no_fatal', not any(re.search(r'\bFAIL\b|\bTRAP\b', r['raw']) for r in rows))
    alpha = select('ALPHA_PROBE')
    precision = select('PRECISION_PROBE')
    gate('alpha_35_verified', len(alpha) == 1 and all(alpha[0].get(k) == v for k, v in
         dict(rc=0, present=1, cases=35, commands=35, pixel_errors=0, source_errors=0, guard_errors=0).items()), alpha)
    gate('precision_40_verified', len(precision) == 1 and all(precision[0].get(k) == v for k, v in
         dict(rc=0, sprites=20, cases=40, commands=40, high_region_sources=20, opaque_cases=20,
              alpha_cases=20, pixel_errors=0, source_errors=0, guard_errors=0, restore_skipped=0).items()), precision)
    gate('color_key_verified', any('verified PASS' in r['raw'] for r in select('COLOR_KEY')))
    atlas, post_atlas = select('ATLAS'), select('ATLAS_POST_PROBE')
    gate('atlas_pre_post_crc', len(atlas) == len(post_atlas) == 1 and
         atlas[0].get('crc32') == atlas[0].get('expected') == post_atlas[0].get('crc32') == post_atlas[0].get('expected')
         and isinstance(atlas[0].get('crc32'), int))

    host_inputs = [(m.group(1), int(m.group(2))) for line in hosts
                   if (m := re.search(r'^\[HOST_INPUT\] (\S) at (\d+)s', line))]
    gate('host_schedule_complete', host_inputs == [(key, at) for at, key in SCHEDULE], host_inputs)
    gate('device_schedule_complete', [r.get('key') for r in states] == [key for _, key in SCHEDULE],
         [r.get('key') for r in states])
    expected = dict(cpu=0, alpha=1, panel_alpha=16, demo=1, paused=0, slow=0)
    effects, mismatches, pauses = [], [], []
    pause_start = None
    for index, row in enumerate(states):
        key = row.get('key')
        if key in ('a', 'd', 'w', 's'):
            expected['demo'] = 0
        elif key == 'f':
            expected['slow'] ^= 1
        elif key == 'p':
            expected['paused'] ^= 1
        elif key == 'r':
            expected.update(demo=1, paused=0)
        elif key == 't':
            expected['demo'] ^= 1
        elif key == 'c':
            expected['cpu'] ^= 1
        elif key == 'v':
            expected['alpha'] ^= 1
        elif key == 'n':
            expected['panel_alpha'] = 8 if expected['panel_alpha'] == 24 else expected['panel_alpha'] + 8
        difference = {k: {'expected': value, 'actual': row.get(k)} for k, value in expected.items() if row.get(k) != value}
        if key == 'r' and not (row.get('mode') == 0 and row.get('lives') == 3 and isinstance(row.get('tick'), int) and 0 <= row['tick'] <= 8):
            difference['restart'] = 'expected waves/lives3/tick0..8 after garden_init plus bounded catchup'
        if not all(isinstance(row.get(k), int) and lo <= row[k] <= hi for k, lo, hi in
                   (('player_x', 5000, 635000), ('player_y', 5000, 475000))):
            difference['player_bounds'] = 'missing/outside core movement bounds'
        if key == 'p':
            if row.get('paused') == 1:
                pause_start = row
            elif pause_start:
                delta = row.get('tick', -1) - pause_start.get('tick', -1)
                pauses.append({'pause_line': pause_start['line'], 'resume_line': row['line'],
                               'logic_tick_delta': delta, 'frozen_between_endpoints': 1 <= delta <= 8})
                pause_start = None
        effects.append({'index': index, 'line': row['line'], 'key': key, 'expected': expected.copy(),
                        'actual': {k: row.get(k) for k in expected}, 'differences': difference})
        if difference:
            mismatches.append(effects[-1])
    gate('actual_key_effects', bool(states) and not mismatches, mismatches)
    gate('pause_resume_freezes_core_ticks', len(pauses) == 2 and all(p['frozen_between_endpoints'] for p in pauses), pauses)
    # One INPUT_STATE is emitted after each key, not each rendered frame.
    # The following endpoint records establish the preceding held movement.
    movement = []
    for a, b, field, sign in ((0, 1, 'player_x', -1), (1, 2, 'player_x', 1),
                              (2, 3, 'player_y', -1), (3, 4, 'player_y', 1)):
        if len(states) > b and isinstance(states[a].get(field), int) and isinstance(states[b].get(field), int):
            delta = states[b][field] - states[a][field]
            movement.append({'key': SCHEDULE[a][1], 'from_line': states[a]['line'], 'to_line': states[b]['line'],
                             'axis': field, 'delta_raw': delta, 'expected_sign': sign, 'observed': delta * sign > 0})
    gate('wasd_endpoint_movement', len(movement) == 4 and all(m['observed'] for m in movement), movement)
    final = states[-1] if states else {}
    final_expected = dict(cpu=0, alpha=1, panel_alpha=16, demo=1, paused=0, slow=0)
    gate('final_gpu_alpha16_auto_unpaused_normal_speed', bool(final) and all(final.get(k) == v for k, v in final_expected.items()), final)

    counters = {}
    for field in FAULT_FIELDS:
        values = [r.get(field) for r in live]
        numeric = bool(values) and all(isinstance(v, int) and v >= 0 for v in values)
        counters[field] = {'first': values[0] if values else None, 'last': values[-1] if values else None,
                           'delta': values[-1] - values[0] if numeric else None,
                           'maximum': max(values) if numeric else None,
                           'monotonic': numeric and all(a <= b for a, b in zip(values, values[1:])),
                           'sample_count': len(values)}
    gate('live_fault_fields_present_and_zero', bool(live) and all(c['maximum'] == 0 and c['monotonic'] for c in counters.values()), counters)
    bench_results = [r for r in benches if r.get('backend') in ('CPU', 'GPU')]
    gate('benchmark_errors_zero', len(bench_results) >= 4 and all(r.get('errors') == 0 for r in bench_results), bench_results)
    gate('exercise_cpu_and_alpha_off', any(r.get('cpu') == 1 for r in states) and any(r.get('alpha') == 0 for r in states))
    gate('exercise_all_alpha_levels', {r.get('panel_alpha') for r in states} >= {8, 16, 24})

    module_path = TOOLS / 'analyze_display7.py'
    spec = importlib.util.spec_from_file_location('controls_display7', module_path)
    analyzer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(analyzer)
    display = analyzer.analyze(Path(log))
    pairs = display['pairs']
    running = [p for p in pairs if p['after_GAME_START_window']]
    start_pair = display.get('GAME_START_pair')
    last_pair = pairs[-1] if pairs else None
    totals = None
    if start_pair and last_pair:
        totals = [b - a for a, b in zip(start_pair['aux']['total'], last_pair['aux']['total'])]
    observed = display['runtime_window_observed_counts']
    valid_display = display['status'] == 'PAIRED' and bool(running) and display['GAME_START_boundary_verified']
    gate('display_measurement_present', valid_display, {k: display[k] for k in ('status', 'complete_pairs', 'issues', 'GAME_START_boundary_verified')})
    gate('display_observed_underruns_zero', valid_display and totals is not None and
         all(observed[i] == totals[i] == 0 for i in (0, 1, 4)) and
         not any(p['main']['sat'][i] or p['main']['ov'] & ((1 << i) | (1 << (5 + i))) or p['aux']['tov'] & ((1 << i) | (1 << (5 + i))) for p in running for i in (0, 1, 4)),
         {'counter_order': list(analyzer.NAMES), 'window_counts': observed, 'total_difference': totals})
    coverage_complete = valid_display and not display['counter_total_window_gaps'] and not display['incomplete_rids']
    if not coverage_complete:
        warnings.append('DISPLAY7 coverage has gaps/unpaired records; observed window sums and lifecycle differences are reported separately, never invented as zero.')
    partials = source['partial_tail_excluded'] or host_source['partial_tail_excluded']
    if partials:
        warnings.append('An incomplete trailing line was excluded; coverage does not extend through that fragment.')
    fps = [r['fps10'] for r in live if isinstance(r.get('fps10'), int)]
    functional_pass = all(gates.values())
    return dict(status=('FUNCTIONAL_PASS' if coverage_complete and not partials else 'FUNCTIONAL_PASS_WITH_COVERAGE_LIMITS') if functional_pass else 'FUNCTIONAL_FAIL',
        functional_pass=functional_pass, motion_issue='DEFERRED_BY_USER', fps_is_a_gate=False,
        fps10_range=[min(fps), max(fps)] if fps else None, source=source, host_source=host_source,
        case_dir=str(Path(case_dir).resolve()) if case_dir else None, boot_line=start + 1 if boots else None,
        earlier_boots_excluded=max(0, len(boots)-1), gates=gates, issues=issues, warnings=warnings,
        input_states=states, key_effects=effects, host_inputs=host_inputs, movement_endpoints=movement,
        pause_resume=pauses, final_state=final, live_counter_differences=counters, benchmark_results=bench_results,
        display=dict(status=display['status'], counter_order=list(analyzer.NAMES), complete_pairs=display['complete_pairs'],
            incomplete_rids=display['incomplete_rids'], issues=display['issues'], coverage_complete=coverage_complete,
            counter_total_window_gaps=display['counter_total_window_gaps'],
            runtime_window_observed_counts=observed, GAME_START_to_last_total_difference=totals,
            runtime_counter_lower_bound_fields=display['runtime_counter_lower_bound_fields'],
            runtime_counter_duration_seconds=display['runtime_counter_duration_seconds'],
            all_gpu_alpha16_informational_only=display['runtime_all_sampled_GPU_Alpha16']),
        analyzer_sha256=digest(module_path.read_bytes()), auditor_sha256=digest(Path(__file__).read_bytes()),
        limits=['Functional UART controls only; no physical button, PS2, sound, or visual HDMI acceptance.',
                'FPS is disclosed but is not a functional control gate; deferred judder is not reopened.',
                'CPU and Alpha-off are deliberate tests, not violations of an all-GPU-Alpha16 condition.',
                'WASD movement uses separated input endpoints, not per-frame velocities; clipping/respawn can prevent this evidence gate.',
                'A final state is the last emitted INPUT_STATE, not a later independent atomic device read.',
                'Benchmark/CPU intervals may create display sampling gaps; AR/R stall totals are not display underrun counts.',
                'Host scheduled markers prove commands were sent, not exact device or display-edge timing.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--host-log', type=Path, required=True)
    parser.add_argument('--case-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, help='New directory outside the original case; never overwritten')
    args = parser.parse_args()
    result = audit(args.log, args.host_log, args.case_dir)
    payload = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output_dir:
        output = args.output_dir.resolve()
        if output.exists():
            parser.error('--output-dir already exists; refusing to overwrite evidence')
        if args.case_dir and (output == args.case_dir.resolve() or args.case_dir.resolve() in output.parents):
            parser.error('--output-dir must be outside the original case')
        if output in args.log.resolve().parents or output in args.host_log.resolve().parents:
            parser.error('--output-dir must not contain original logs')
        output.mkdir(parents=True, exist_ok=False)
        (output / 'controls_summary.json').write_text(payload, encoding='utf-8')
    print(payload, end='')
    return 0 if result['functional_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
