"""Audit SIXLIVE_1103 UART evidence without opening hardware.

FPS is recomputed from raw CLINT ticks/hz and completed presentations, never
from the displayed fps1000. Synthetic self-tests are not board evidence.
"""
from pathlib import Path
from fractions import Fraction
import argparse
import csv
import hashlib
import json
import re

READY = '[READY] SIXLIVE_1103'
CASES = {1: (1, 0), 2: (0, 0), 3: (1, 1), 4: (0, 1), 5: (1, 2), 6: (0, 2)}
COUNTERS = ('missing_px', 'missing_line', 'reject', 'skip', 'errors', 'guard',
            'front_reject', 'log_dropped', 'log_truncated',
            'disp_px', 'disp_line', 'disp_full')
FATAL = ('[FAIL]', '[TRAP]', '[FATAL]', '[SIXL_FATAL]', '[SIXL_STOP]', '[SIXL_FAILED]')
COMMON_CRC = 0xfd0eceeb
ATLAS_CRC = 0x7d029d94
TILE_CRCS = ('0e2c2d10,584a8249,4ff2d9e7,e6ab80ef,aee9ae5b,eb223456,b891bb07,b0607057,'
             '7b6957b4,d99ba3e5,a826bd4b,35c56aba,cf42c32d,66a343c2,10027040,a4dc48fe,'
             'b3a9db44,626dec9f,4ee2faed,ffda89dd,124fc546,11fa5f30,6cb3b993,33432e97,'
             '3ba5084d,412f892d,7e80fff8')


def fields(line):
    result = {}
    for key, value in re.findall(r'(\w+)=([^\s]+)', line):
        try:
            result[key] = int(value, 16) if value.lower().startswith('0x') else int(value)
        except ValueError:
            result[key] = value
    return result


def number(value, minimum=0):
    return isinstance(value, int) and not isinstance(value, bool) and value >= minimum


def timing(row):
    if not all(number(row.get(k), 1) for k in ('ticks', 'hz')) or not number(row.get('presented')):
        return None
    return Fraction(row['ticks'], row['hz'])


def csv_write(path, rows, empty):
    columns = list(dict.fromkeys(k for row in rows for k in row)) or list(empty)
    with path.open('w', encoding='utf-8', newline='') as target:
        writer = csv.DictWriter(target, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def analyze(raw, source='', min_rounds=3):
    lines = raw.decode('ascii', errors='replace').splitlines()
    result = dict(schema=1, variant='csixlive_1103', source=source,
                  serial_sha256=hashlib.sha256(raw).hexdigest(), serial_bytes=len(raw),
                  status='UNTESTED', issues=[], incomplete_reasons=[],
                  windows=[], cases=[], modes=[], events=[], sequences=[], atlases=[],
                  min_completed_visits_per_case=min_rounds, visual_acceptance=False,
                  source_asymmetry=False, runtime_source_identical=True, comparable_features=True,
                  pixel_rules_differ=False,
                  interpretation='Independent CPU/HW cases use the same original pixels, objects, per-update trajectories and matching Key/Alpha rules. Frame FPS is end-to-end; partial CPU/GPU timers still have different scopes.',
                  fps_basis='sum(completed presentations) / sum(raw ticks/hz)',
                  drawing_time_scope='draw_us/clear_us are measured accumulated active ticks divided by presented frames, truncated to integer microseconds. CPU draw covers sprite raster; HW draw is legacy only. HW clear_us=0 means not separately measured. Stage9 cycles are separate concurrent scan samples. No P99 inferred.')
    ready_rows = [i for i, line in enumerate(lines) if line.startswith(READY)]
    if not ready_rows:
        result['reason'] = 'No fresh SIXLIVE_1103 READY. No hardware result was inferred.'
        return result
    ready_index = ready_rows[-1]
    previous_ready = max((i for i in range(ready_index) if lines[i].startswith('[READY]')), default=-1)
    result['discarded_prefix_lines'] = previous_ready + 1
    identity = fields(lines[ready_index])
    result['ready'] = identity
    if any(identity.get(k) != v for k, v in dict(cases=6, duration_ms=10000, count=2093, independent=1,
                cpu_effects='matched', metrics='live', flash_write=0).items()):
        result['issues'].append('READY does not identify six independent10-second2093-object cases')
    probes = {}
    for line in lines[previous_ready + 1:ready_index]:
        for tag in ('ALPHA_PROBE', 'PRECISION_PROBE', 'SIXL_PIXEL_PROBE', 'SIXL_CPU_PROBE'):
            if line.startswith('[' + tag + ']'):
                probes[tag] = fields(line)
    result['startup_probes'] = probes
    for name in ('ALPHA_PROBE', 'PRECISION_PROBE'):
        row = probes.get(name)
        if row is None:
            result['incomplete_reasons'].append('Fresh ' + name + ' absent')
        elif not (row.get('rc') == 0 and row.get('cases') == (35 if name == 'ALPHA_PROBE' else 40)
                  and all(row.get(k) == 0 for k in ('pixel_errors', 'source_errors', 'guard_errors'))):
            result['issues'].append(name + ' missing explicit success or reported errors')
    probe = probes.get('SIXL_PIXEL_PROBE')
    if probe is None:
        result['incomplete_reasons'].append('Fresh96-pixel SIXL_PIXEL_PROBE absent')
    elif any(probe.get(k) != v for k, v in dict(errors=0, pixels=96, modes=3, original_key_pixels=5142,
        original_black_pixels=4, source_pixels=6912, key=0xf81f, key_matches=5142,
        no_key=4, no_key_matches=0).items()):
        result['issues'].append('SIXL_PIXEL_PROBE missing explicit success or reported errors')
    probe = probes.get('SIXL_CPU_PROBE')
    if probe is None:
        result['incomplete_reasons'].append('Fresh96-pixel SIXL_CPU_PROBE absent')
    elif any(probe.get(k) != v for k, v in dict(errors=0, pixels=96, modes=3, includes_black=1,
        includes_f81f=1, comparator_sw=1, keyskip_tested=1, alpha16_tested=1, actual_raster=1).items()):
        result['issues'].append('SIXL_CPU_PROBE did not verify three matched CPU shaders')
    visits, atlases = {}, {}
    order = []
    def visit(row, line_number):
        if not number(row.get('cycle')) or row.get('case') not in CASES:
            result['issues'].append('Invalid cycle/case at line%d' % line_number)
            return None
        key = (row['cycle'], row['case'])
        if key not in visits:
            visits[key] = dict(cycle=key[0], case=key[1], windows=[], done=None, task=None, start=None,
                               announced=False, first_line=line_number)
            order.append(key)
        expected_backend, expected_mode = CASES[row['case']]
        if row.get('backend') != expected_backend or row.get('mode') != expected_mode:
            result['issues'].append('Case/backend/mode mismatch at line%d' % line_number)
        return visits[key]
    for i in range(ready_index + 1, len(lines)):
        line = lines[i]
        match = re.match(r'^\[([^]]+)\]', line)
        if not match:
            continue
        tag = match.group(1)
        if line.startswith(FATAL):
            result['issues'].append('Firmware fatal/stop at line%d: %s' % (i + 1, line))
        if not tag.startswith('SIXL') and tag not in ('DISP',):
            continue
        row = fields(line)
        result['events'].append(dict(serial_line=i + 1, tag=tag, text=line))
        if tag == 'SIXL_ATLAS':
            key = row.get('cycle'), row.get('case')
            if not number(key[0]) or key[1] not in CASES or row.get('backend') != CASES.get(key[1], (-1, -1))[0]:
                result['issues'].append('Malformed ATLAS identity at line%d' % (i + 1))
                continue
            if key in atlases:
                result['issues'].append('Repeated ATLAS record for%s' % (key,))
            atlases[key] = row
            result['atlases'].append(dict(serial_line=i + 1, **row))
            if (row.get('common_source_crc') != COMMON_CRC or row.get('atlas0_crc') != ATLAS_CRC
                or row.get('atlas1_crc') != ATLAS_CRC or row.get('tiles') != 27
                or str(row.get('tile_crcs', '')).lower() != TILE_CRCS):
                result['issues'].append('Actual common source/bank/tile CRC mismatch at line%d' % (i + 1))
            continue
        if tag not in ('SIXL_CASE', 'SIXL_TASK', 'SIXL_START', 'SIXL_SEQUENCE', 'SIXL_CASE_DONE', 'SIXL_WINDOW', 'SIXL_DONE'):
            continue
        current = visit(row, i + 1)
        if current is None:
            continue
        if tag == 'SIXL_CASE':
            if current['announced']:
                result['issues'].append('Duplicate SIXL_CASE at line%d' % (i + 1))
            current['announced'] = True
            current['announcement'] = row
            continue
        if tag == 'SIXL_CASE_DONE':
            if row.get('rc') != 0:
                result['issues'].append('Nonzero SIXL_CASE_DONE at line%d' % (i + 1))
            continue
        if tag == 'SIXL_SEQUENCE':
            if (row.get('visible') != 2093 or not number(row.get('checksum')) or row.get('frame') not in (1, 2)
                    or row.get('mode') not in (0, 1, 2) or row.get('backend') not in (0, 1)):
                result['issues'].append('Malformed trajectory checksum at line%d' % (i + 1))
                continue
            result['sequences'].append(dict(serial_line=i + 1, **row))
            continue
        if tag in ('SIXL_TASK', 'SIXL_START'):
            name = 'task' if tag == 'SIXL_TASK' else 'start'
            if current[name] is not None:
                result['issues'].append('Duplicate ' + tag + ' at line%d' % (i + 1))
            current[name] = row
            continue
        row = dict(serial_line=i + 1, **row)
        if row.get('count') != 2093:
            result['issues'].append('Object count mismatch at line%d' % (i + 1))
        if any(row.get(k) != 2093 for k in ('visible', 'visible_min', 'visible_max')):
            result['issues'].append('Visible count mismatch at line%d' % (i + 1))
        alpha, key = ((32, 0xf81f) if row.get('mode') == 0 else
                     (16, 4) if row.get('mode') == 1 else (16, 0xf81f))
        if row.get('alpha') != alpha or row.get('key') != key:
            result['issues'].append('Blend/key rule mismatch at line%d' % (i + 1))
        if row.get('comparator_sw') != row.get('backend') or row.get('common_source_crc') != COMMON_CRC:
            result['issues'].append('Software comparator/common source identity mismatch at line%d' % (i + 1))
        if row.get('blend_sw') != int(row.get('backend') == 1 and row.get('mode') in (1, 2)):
            result['issues'].append('Matched software blending state mismatch at line%d' % (i + 1))
        duration = timing(row)
        row['actual_seconds'] = float(duration) if duration is not None else None
        row['actual_fps'] = float(Fraction(row['presented'], 1) / duration) if duration is not None else None
        row['mean_frame_us_raw'] = float(duration * 1000000 / row['presented']) if duration is not None and row['presented'] else None
        if duration is None:
            result['issues'].append('Invalid raw ticks/hz/presented at line%d' % (i + 1))
        missing = [name for name in COUNTERS if not number(row.get(name))]
        row['unreported_counters'] = ','.join(missing)
        if missing:
            result['incomplete_reasons'].append('UNREPORTED counter(s) at line%d: %s' % (i + 1, ','.join(missing)))
        if any(number(row.get(name), 1) for name in COUNTERS):
            result['issues'].append('Nonzero health counter at line%d' % (i + 1))
        if 'rc' in row and row['rc'] != 0:
            result['issues'].append('Nonzero result at line%d' % (i + 1))
        if tag == 'SIXL_WINDOW':
            if current['done'] is not None:
                result['issues'].append('SIX window after DONE at line%d' % (i + 1))
            if current['windows']:
                before = current['windows'][-1]
                if any(number(row.get(k)) and number(before.get(k)) and row[k] < before[k] for k in COUNTERS):
                    result['issues'].append('Cumulative health counter decreased within case at line%d' % (i + 1))
            current['windows'].append(row)
            result['windows'].append(row)
        else:
            if current['done'] is not None:
                result['issues'].append('Duplicate SIXL_DONE at line%d' % (i + 1))
            current['done'] = row
    for index, key in enumerate(order):
        current = visits[key]
        windows, done = current['windows'], current['done']
        if index and key != ((order[index - 1][0] + 1, 1) if order[index - 1][1] == 6
                             else (order[index - 1][0], order[index - 1][1] + 1)):
            result['issues'].append('Case order skipped/repeated at cycle%d case%d' % key)
        if not current['announced']:
            result['incomplete_reasons'].append('SIXL_CASE announcement absent for%s' % (key,))
        task, start = current['task'], current['start']
        if current['windows'] and (task is None or start is None):
            result['incomplete_reasons'].append('SIXL_TASK/START absent for%s' % (key,))
        if task is not None and any(task.get(k) != v for k, v in
            dict(count=2093, tile='16x16', rays=91, speed_q8=612, distribution='fan180', logic=0, independent=1).items()):
            result['issues'].append('Fixed independent task configuration mismatch for%s' % (key,))
        if task is not None:
            backend, mode = CASES[key[1]]
            common = dict(comparator_sw=backend, blend_sw=int(bool(backend) and mode > 0), source_asymmetry=0,
                common_source_crc=COMMON_CRC, common_atlas_crc=ATLAS_CRC,
                pixel_rules_differ=0, comparable_features=1, converted_pixels=0,
                alpha_enabled=int(mode > 0), key_effect=int(mode != 1))
            if any(task.get(k) != v for k, v in common.items()):
                result['issues'].append('CPU/HW source/effect policy mismatch for%s' % (key,))
        if current['windows'] and key not in atlases:
            result['incomplete_reasons'].append('Actual common atlas CRC record absent for%s' % (key,))
        if start is not None and (start.get('count') != 2093 or start.get('warmup') != 3
                                 or not number(start.get('prep_ticks')) or not number(start.get('hz'), 1)):
            result['issues'].append('Invalid formal START/preparation scope for%s' % (key,))
        actual = [timing(w) for w in windows]
        seconds = sum((value for value in actual if value is not None), Fraction())
        updates = sum(w['presented'] for w in windows if number(w.get('presented')))
        summary = dict(cycle=key[0], case=key[1], backend=CASES[key[1]][0], mode=CASES[key[1]][1],
                       backend_name='CPU' if CASES[key[1]][0] else 'HARDWARE', count=2093,
                       completed=done is not None, windows=len(windows),
                       first_line=current['first_line'], formal_seconds=float(seconds),
                       presented=updates, actual_fps=float(Fraction(updates, 1) / seconds) if seconds else None,
                       mean_frame_us_raw=float(seconds * 1000000 / updates) if updates else None,
                       ticks_exact='+'.join('%d/%d' % (w['ticks'], w['hz']) for w in windows if timing(w) is not None),
                       at_least10s=seconds >= 10, window_totals_equal_DONE=None)
        if done is not None:
            summary.update(done_line=done['serial_line'], prep_ticks=done.get('prep_ticks'),
                           warmup=done.get('warmup'), health_includes_prep=done.get('health_includes_prep'))
            summary.update({k: done.get(k) for k in ('draw_us', 'clear_us', 'max_us', 's9_engine_cycles')})
            summary.update({'done_' + k: done.get(k) for k in COUNTERS})
            summary['window_totals_equal_DONE'] = bool(all(v is not None for v in actual)
                and seconds == timing(done) and updates == done.get('presented'))
            if not summary['window_totals_equal_DONE']:
                result['issues'].append('Window totals disagree with SIXL_DONE for%s' % (key,))
            if seconds < 10:
                result['incomplete_reasons'].append('Formal interval below10seconds for%s' % (key,))
            if done.get('warmup') != 3 or done.get('health_includes_prep') != 1 or not number(done.get('prep_ticks')):
                result['incomplete_reasons'].append('Preparation/warmup health scope not explicit for%s' % (key,))
            if windows and any(number(done.get(k)) and number(windows[-1].get(k))
                               and done[k] < windows[-1][k] for k in COUNTERS):
                result['issues'].append('DONE cumulative health decreased for%s' % (key,))
        result['cases'].append(summary)
    pairs = {}
    for row in result['sequences']:
        pairs.setdefault((row['cycle'], row['mode'], row['frame']), {})[row['backend']] = row
    result['sequence_pairs_matched'] = 0
    result['sequence_pairs_pending'] = 0
    for key, pair in pairs.items():
        if set(pair) != {0, 1}:
            result['sequence_pairs_pending'] += 1
        elif pair[0].get('checksum') != pair[1].get('checksum'):
            result['issues'].append('CPU/HW initial trajectory checksum mismatch for%s' % (key,))
        else:
            result['sequence_pairs_matched'] += 1
    completed_cycles = sorted({r['cycle'] for r in result['cases']
        if all(any(c['cycle'] == r['cycle'] and c['case'] == case and c['completed']
                   and c['at_least10s'] and c['window_totals_equal_DONE'] for c in result['cases'])
               for case in CASES)})
    selected_cycles = completed_cycles[:min_rounds]
    result['completed_cycles'] = completed_cycles
    result['aggregate_cycles'] = selected_cycles
    result['aggregate_scope'] = 'first%d complete six-case rounds; extra visits remain in raw CSV' % min_rounds
    if len(selected_cycles) < min_rounds:
        result['incomplete_reasons'].append('Need%d complete six-case rounds; have%d' % (min_rounds, len(selected_cycles)))
    for row in [*result['cases'], *result['windows']]:
        row['selected_for_aggregate'] = row['cycle'] in selected_cycles
    for cycle in selected_cycles:
        for mode in range(3):
            for frame in (1, 2):
                if set(pairs.get((cycle, mode, frame), {})) != {0, 1}:
                    result['incomplete_reasons'].append('CPU/HW initial trajectory pair absent for cycle%d mode%d frame%d' % (cycle, mode, frame))
    for case, (backend, mode) in CASES.items():
        complete = [r for r in result['cases'] if r['case'] == case and r['completed'] and r['at_least10s']]
        selected_visits = [r for r in complete if r['cycle'] in selected_cycles]
        complete_keys = {(r['cycle'], r['case']) for r in selected_visits}
        selected = [w for w in result['windows'] if (w['cycle'], w['case']) in complete_keys]
        duration = sum((timing(w) for w in selected if timing(w) is not None), Fraction())
        presented = sum(w['presented'] for w in selected if number(w.get('presented')))
        result['modes'].append(dict(case=case, backend=backend, mode=mode,
            backend_name='CPU' if backend else 'HARDWARE', count=2093,
            completed_visits=len(selected_visits), available_completed_visits=len(complete), formal_seconds=float(duration),
            presented=presented, actual_fps=float(Fraction(presented, 1) / duration) if duration else None,
            mean_frame_us_raw=float(duration * 1000000 / presented) if presented else None,
            max_frame_us=max((r['max_us'] for r in selected_visits if number(r.get('max_us'))), default=None)))
        if len(complete) < min_rounds:
            result['incomplete_reasons'].append('case%d has%d completed>=10s visits; need%d' % (case, len(complete), min_rounds))
    result['issues'] = list(dict.fromkeys(result['issues']))
    result['incomplete_reasons'] = list(dict.fromkeys(result['incomplete_reasons']))
    if not result['windows']:
        result['status'] = 'FAIL' if result['issues'] else 'UNTESTED'
        result['reason'] = 'No measured SIX windows after the latest READY'
    else:
        result['status'] = 'FAIL' if result['issues'] else 'INCOMPLETE' if result['incomplete_reasons'] else 'PASS'
    return result


def save_report(report, output):
    output.mkdir(parents=True, exist_ok=True)
    for name in ('windows', 'cases', 'modes', 'events', 'sequences', 'atlases'):
        csv_write(output / (name + '.csv'), report[name], ('serial_line', 'case', 'actual_fps'))
    (output / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def fixture(rounds=3):
    rows = ['[READY] OLD_DEMO', '[SIXL_WINDOW] presented=999 ticks=1 hz=1',
            '[ALPHA_PROBE] rc=0 cases=35 pixel_errors=0 source_errors=0 guard_errors=0',
            '[PRECISION_PROBE] rc=0 cases=40 pixel_errors=0 source_errors=0 guard_errors=0',
            '[SIXL_PIXEL_PROBE] errors=0 modes=3 pixels=96 original_key_pixels=5142 original_black_pixels=4 source_pixels=6912 key=0xf81f key_matches=5142 no_key=0x4 no_key_matches=0',
            '[SIXL_CPU_PROBE] errors=0 modes=3 pixels=96 includes_black=1 includes_f81f=1 comparator_sw=1 keyskip_tested=1 alpha16_tested=1 actual_raster=1',
            READY + ' cases=6 duration_ms=10000 count=2093 independent=1 cpu_effects=matched metrics=live flash_write=0']
    health = ' '.join(k + '=0' for k in COUNTERS)
    for cycle in range(rounds):
        for case, (backend, mode) in CASES.items():
            common = 'cycle=%d case=%d backend=%d mode=%d count=2093' % (cycle, case, backend, mode)
            rows.append('[SIXL_CASE] ' + common)
            policy = (' source_asymmetry=0 common_source_crc=0xfd0eceeb common_atlas_crc=0x7d029d94'
                      ' converted_pixels=0 comparable_features=1 pixel_rules_differ=0'
                      ' alpha_enabled=%d key_effect=%d comparator_sw=%d blend_sw=%d' %
                      (int(mode > 0), int(mode != 1), backend, int(bool(backend) and mode > 0)))
            rows.append('[SIXL_TASK] ' + common + ' tile=16x16 rays=91 speed_q8=612 distribution=fan180 logic=0 independent=1' + policy)
            rows.append('[SIXL_ATLAS] ' + common + ' common_source_crc=0xfd0eceeb atlas0_crc=0x7d029d94 atlas1_crc=0x7d029d94 tiles=27 tile_crcs=' + TILE_CRCS)
            for frame in (1, 2):
                rows.append('[SIXL_SEQUENCE] ' + common + ' frame=%d checksum=0x%x visible=2093' % (frame, 0x100 + mode * 16 + frame))
            rows.append('[SIXL_START] ' + common + ' prep_ticks=5 hz=100 warmup=3')
            blend = (' alpha=32 key=0xf81f' if mode == 0 else ' alpha=16 key=0x4' if mode == 1 else ' alpha=16 key=0xf81f')
            blend += ' comparator_sw=%d blend_sw=%d common_source_crc=0xfd0eceeb' % (backend, int(bool(backend) and mode > 0))
            for i in range(2):
                rows.append('[SIXL_WINDOW] ' + common + ' ticks=500 hz=100 presented=300 fps1000=123 visible=2093 visible_min=2093 visible_max=2093' + blend + ' ' + health)
            rows.append('[SIXL_DONE] ' + common + ' ticks=1000 hz=100 presented=600 prep_ticks=5 warmup=3 health_includes_prep=1 visible=2093 visible_min=2093 visible_max=2093' + blend + ' ' + health)
    return '\n'.join(rows).encode('ascii')


def self_test():
    raw = fixture()
    report = analyze(raw)
    assert report['status'] == 'PASS', report
    assert all(r['actual_fps'] == 60 for r in report['modes'])
    assert len(report['windows']) == 36 and len(report['cases']) == 18
    assert analyze(b'old unrelated data')['status'] == 'UNTESTED'
    assert analyze(raw + b'\n' + READY.encode() + b' cases=6 duration_ms=10000 count=2093 independent=1 cpu_effects=matched metrics=live flash_write=0')['status'] == 'UNTESTED'
    assert analyze(raw.replace(b'front_reject=0', b'front_reject=1', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'guard=0', b'guard=broken', 1))['status'] == 'INCOMPLETE'
    assert analyze(raw.replace(b'ticks=1000', b'ticks=999', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'visible=2093', b'visible=2002', 1))['status'] == 'FAIL'
    assert analyze(raw + b'\n[SIXL_FATAL] rc=-5')['status'] == 'FAIL'
    assert analyze(raw.replace(b'[SIXL_CASE]', b'[OTHER]', 1))['status'] == 'INCOMPLETE'
    assert analyze(raw.replace(b'pixels=96', b'pixels=95', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'health_includes_prep=1', b'health_includes_prep=0', 1))['status'] == 'INCOMPLETE'
    assert analyze(raw.replace(b'count=2093', b'count=2730', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'atlas1_crc=0x7d029d94', b'atlas1_crc=0x0', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'comparator_sw=0', b'comparator_sw=1', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'[SIXL_ATLAS]', b'[OLD_ATLAS]', 1))['status'] == 'INCOMPLETE'
    assert analyze(fixture(2))['status'] == 'INCOMPLETE'
    extra = fixture(4).splitlines()
    extra = [(line.replace(b'presented=300', b'presented=600') if line.startswith(b'[SIXL_WINDOW]') else
              line.replace(b'presented=600', b'presented=1200')) if b'cycle=3 ' in line else line for line in extra]
    assert all(r['actual_fps'] == 60 and r['completed_visits'] == 3 for r in analyze(b'\n'.join(extra))['modes'])
    assert analyze(raw.replace(b'actual_raster=1', b'actual_raster=0', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'converted_pixels=0', b'converted_pixels=5142', 1))['status'] == 'FAIL'
    print(json.dumps(dict(status='PASS', checks=21, host_only=True, hardware_access=False)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--min-rounds', type=int, default=3)
    parser.add_argument('--strict', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.log is None or args.out is None or args.min_rounds < 1:
        parser.error('--log and --out are required; --min-rounds must be>=1')
    raw = args.log.read_bytes() if args.log.is_file() else b''
    result = analyze(raw, str(args.log.resolve()), args.min_rounds)
    save_report(result, args.out)
    print(json.dumps(dict(status=result['status'], windows=len(result['windows']),
                          cases=len(result['cases']), output=str(args.out.resolve())), ensure_ascii=False))
    if args.strict and result['status'] != 'PASS':
        raise SystemExit(2)


if __name__ == '__main__':
    main()
