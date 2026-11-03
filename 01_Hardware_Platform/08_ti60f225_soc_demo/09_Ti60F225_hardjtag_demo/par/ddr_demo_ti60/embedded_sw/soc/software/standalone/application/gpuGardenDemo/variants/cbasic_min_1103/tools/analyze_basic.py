"""Read-only audit of BASIC_1103 board UART evidence.

Window and tier FPS use actual CLINT ticks and completed presentations. No
historical chart value, displayed FPS or host simulation is a board result.
"""
from pathlib import Path
from fractions import Fraction
import argparse
import csv
import hashlib
import json
import re

READY = '[READY] BASIC_1103'
IDENTITY = dict(initial_count=2093, target_fps=50, alpha=32, key=4, shared_cpu=1, flash_write=0)
COUNTERS = ('missing_px', 'missing_lines', 'reject', 'skip', 'errors', 'guard', 'front_reject',
            'log_dropped', 'log_truncated', 'disp_px', 'disp_line', 'disp_full')
FATAL = ('[FAIL]', '[TRAP]', '[FATAL]', '[BASIC_FATAL]', '[BASIC_FAILED]', '[BASIC_STOP]')


def fields(line):
    result = {}
    for key, value in re.findall(r'(\w+)=([^\s]+)', line):
        try:
            result[key] = int(value, 16) if value.lower().startswith('0x') else int(value)
        except ValueError:
            result[key] = value
    return result


def integer(value, low=0):
    return isinstance(value, int) and not isinstance(value, bool) and value >= low


def duration(row):
    if not integer(row.get('ticks'), 1) or not integer(row.get('hz'), 1):
        return None
    return Fraction(row['ticks'], row['hz'])


def rate(row, backend):
    span = duration(row)
    updates = row.get(backend + '_presented')
    return Fraction(updates, 1) / span if span is not None and integer(updates) else None


def count_of(row):
    return row.get('per_side', row.get('count'))


def analyze(raw, source=''):
    lines = raw.decode('ascii', errors='replace').splitlines()
    result = dict(schema=1, variant='cbasic_min_1103', source=source,
                  serial_sha256=hashlib.sha256(raw).hexdigest(), serial_bytes=len(raw),
                  status='UNTESTED', target_status='UNTESTED', issues=[], incomplete_reasons=[],
                  windows=[], assessments=[], configurations=[], changes=[], events=[], sequences=[],
                  visual_acceptance=False, timing_scope='formal START after preparation; each assessment at least15seconds',
                  fps_basis='sum(actual completed presentations) / sum(raw ticks/hz)',
                  shared_cpu=True, historical_results_used=False,
                  drawing_time_scope='cpu_draw_us, gpu_blit_us and copy_us are latest completed task/frame samples, not window means. CPU raster and HW legacy draw have different scopes. No all-frame drawing mean, maximum or P99 inferred.')
    starts = [i for i, line in enumerate(lines) if line.startswith(READY)]
    if not starts:
        result['reason'] = 'No fresh BASIC_1103 READY; no board result inferred.'
        return result
    first = starts[-1]
    previous = max((i for i in range(first) if lines[i].startswith('[READY]')), default=-1)
    result['discarded_prefix_lines'] = previous + 1
    result['ready'] = fields(lines[first])
    if any(result['ready'].get(k) != v for k, v in IDENTITY.items()):
        result['issues'].append('READY configuration mismatch')
    probes = {}
    for line in lines[previous + 1:first]:
        for tag in ('ALPHA_PROBE', 'PRECISION_PROBE', 'BASIC_PIXEL_PROBE'):
            if line.startswith('[' + tag + ']'):
                probes[tag] = fields(line)
    result['startup_probes'] = probes
    for name, cases in [('ALPHA_PROBE', 35), ('PRECISION_PROBE', 40)]:
        row = probes.get(name)
        if row is None:
            result['incomplete_reasons'].append('Fresh ' + name + ' absent')
        elif row.get('rc') != 0 or row.get('cases') != cases or any(row.get(k) != 0 for k in ('pixel_errors', 'source_errors', 'guard_errors')):
            result['issues'].append(name + ' did not explicitly pass the full probe')
    probe = probes.get('BASIC_PIXEL_PROBE')
    expected = dict(rc=0, errors=0, pixels=32, source_pixels=6912, key=4, key_matches=0,
                    alpha_enabled=0, copy_all=1, bake_black_at_setup=1)
    if probe is None:
        result['incomplete_reasons'].append('Fresh BASIC_PIXEL_PROBE absent')
    elif any(probe.get(k) != v for k, v in expected.items()) or not integer(probe.get('original_key_pixels'), 1):
        result['issues'].append('BASIC_PIXEL_PROBE does not prove opaque source copy with absent key')
    configs, formal, groups = {}, {}, {}
    def key_of(row, line_number):
        if not integer(row.get('epoch')) or not integer(row.get('assess')):
            result['issues'].append('Missing epoch/assess at line%d' % line_number)
            return None
        return row['epoch'], row['assess']
    def group(key):
        return groups.setdefault(key, dict(windows=[], assessment=None))
    for i in range(first + 1, len(lines)):
        line = lines[i]
        if line.startswith(FATAL):
            result['issues'].append('Firmware fatal/stop at line%d: %s' % (i + 1, line))
        match = re.match(r'^\[(BASIC[^]]*)\]', line)
        if not match:
            continue
        tag, row = match.group(1), fields(line)
        result['events'].append(dict(serial_line=i + 1, tag=tag, text=line))
        row = dict(serial_line=i + 1, **row)
        if tag == 'BASIC_CONFIG':
            epoch = row.get('epoch')
            if not integer(epoch) or epoch in configs:
                result['issues'].append('Invalid/repeated CONFIG epoch at line%d' % (i + 1))
                continue
            configs[epoch] = row
            result['configurations'].append(row)
            count = count_of(row)
            if not integer(count) or not 728 <= count <= 2093 or count % 91:
                result['issues'].append('Invalid object tier at line%d' % (i + 1))
            expected_config = dict(alpha=32, alpha_enabled=0, key=4, key_matches=0, copy_all=1,
                source_pixels=6912, rays=91, speed_q8=612, assess_seconds=15, target_hw_fps=50,
                min_count=728, step=91, shared_cpu=1, isolated_peak=0, health_includes_prep=1)
            if any(row.get(k) != v for k, v in expected_config.items()) or not integer(row.get('bake_key_to_black'), 1):
                result['issues'].append('CONFIG alpha/key/assessment rule mismatch at line%d' % (i + 1))
            if not integer(count) or row.get('total') != 2 * count:
                result['issues'].append('CONFIG total must equal twice per-side count at line%d' % (i + 1))
            continue
        if tag == 'BASIC_START':
            epoch = row.get('epoch')
            if not integer(epoch) or epoch in formal:
                result['issues'].append('Invalid/repeated START at line%d' % (i + 1))
            else:
                formal[epoch] = row
                if row.get('warm_hw_frames') != 2 or row.get('prepared_cpu_frames') != 1 or not integer(row.get('prep_ticks')):
                    result['issues'].append('START preparation/warmup scope invalid at line%d' % (i + 1))
            continue
        if tag == 'BASIC_SEQUENCE':
            if (not integer(row.get('epoch')) or not integer(row.get('frame'), 1)
                or row.get('backend') not in ('CPU', 'HW') or not integer(row.get('checksum'))
                or row.get('visible') != row.get('count')):
                result['issues'].append('Malformed sequence record at line%d' % (i + 1))
            else:
                result['sequences'].append(row)
            continue
        if tag == 'BASIC_COUNT_CHANGE':
            result['changes'].append(row)
            old, new = row.get('old_count'), row.get('new_count')
            if not integer(old) or not integer(new) or new != max(728, old - 91) or row.get('step') != 91 or row.get('drained') != 1:
                result['issues'].append('Invalid/undrained count change at line%d' % (i + 1))
            continue
        if tag not in ('BASIC', 'BASIC_ASSESS'):
            continue
        key = key_of(row, i + 1)
        if key is None:
            continue
        current = group(key)
        span = duration(row)
        cpu, hw = rate(row, 'cpu'), rate(row, 'hw')
        if span is None or cpu is None or hw is None:
            result['issues'].append('Invalid raw timing/presentation counts at line%d' % (i + 1))
        row.update(actual_seconds=float(span) if span is not None else None,
                   actual_cpu_fps=float(cpu) if cpu is not None else None,
                   actual_hw_fps=float(hw) if hw is not None else None)
        count = count_of(row)
        if not integer(count) or row.get('count') != count or row.get('total') != 2 * count:
            result['issues'].append('Window count/per-side/total mismatch at line%d' % (i + 1))
        config = configs.get(key[0])
        if config is None:
            result['incomplete_reasons'].append('CONFIG absent for epoch%d' % key[0])
        elif count != count_of(config):
            result['issues'].append('Window object count differs from CONFIG at line%d' % (i + 1))
        if row.get('cpu_visible') != count or row.get('hw_visible') != count:
            result['issues'].append('Actual CPU/HW visible count mismatch at line%d' % (i + 1))
        if any(row.get(k) != v for k, v in dict(alpha=32, key=4, alpha_enabled=0, key_matches=0, copy_all=1).items()):
            result['issues'].append('Window alpha/key/copy rule mismatch at line%d' % (i + 1))
        missing = [k for k in COUNTERS if not integer(row.get(k))]
        row['unreported_counters'] = ','.join(missing)
        if missing:
            result['incomplete_reasons'].append('UNREPORTED counters at line%d: %s' % (i + 1, ','.join(missing)))
        if any(integer(row.get(k), 1) for k in COUNTERS) or integer(row.get('errors'), 1):
            result['issues'].append('Nonzero health counter at line%d' % (i + 1))
        if tag == 'BASIC':
            if row.get('pass') != 2 or row.get('action') != 'WINDOW':
                result['issues'].append('Window incorrectly declares a final tier decision at line%d' % (i + 1))
            if current['assessment'] is not None:
                result['issues'].append('Window follows closed ASSESS at line%d' % (i + 1))
            current['windows'].append(row)
            result['windows'].append(row)
        else:
            if current['assessment'] is not None:
                result['issues'].append('Duplicate ASSESS at line%d' % (i + 1))
            current['assessment'] = row
    for (epoch, assess), current in groups.items():
        rows, done = current['windows'], current['assessment']
        if done is None:
            continue
        total_span = sum((duration(row) for row in rows if duration(row) is not None), Fraction())
        totals = {b: sum(row[b + '_presented'] for row in rows if integer(row.get(b + '_presented'))) for b in ('cpu', 'hw')}
        summary = dict(done)
        summary['windows'] = len(rows)
        summary['health_counters_zero'] = all(row.get(k) == 0 for row in [*rows, done] for k in COUNTERS)
        summary['window_totals_equal_ASSESS'] = (all(duration(row) is not None for row in rows)
            and total_span == duration(done) and all(totals[b] == done.get(b + '_presented') for b in totals))
        if not summary['window_totals_equal_ASSESS']:
            result['issues'].append('Window totals disagree with ASSESS for epoch%d assess%d' % (epoch, assess))
        if total_span < 15:
            result['incomplete_reasons'].append('Formal assessment shorter than15seconds for epoch%d assess%d' % (epoch, assess))
        if epoch not in formal:
            result['incomplete_reasons'].append('Formal START absent for epoch%d' % epoch)
        count = count_of(done)
        hw = rate(done, 'hw')
        if hw is not None and integer(count):
            meets = hw >= 50
            action = 'KEEP' if meets else 'REDUCE' if count > 728 else 'FLOOR'
            next_count = count if action != 'REDUCE' else max(728, count - 91)
            summary.update(target_met=meets, expected_action=action, expected_next_count=next_count)
            if done.get('pass') != int(meets) or done.get('action') != action or done.get('next_count') != next_count:
                result['issues'].append('Tier decision disagrees with raw hardware FPS for epoch%d assess%d' % (epoch, assess))
        result['assessments'].append(summary)
    pairs = {}
    for row in result['sequences']:
        pairs.setdefault((row['epoch'], row['frame']), {})[row['backend']] = row
    result['trajectory_pairs_matched'] = 0
    result['trajectory_pairs_pending'] = 0
    for key, pair in pairs.items():
        if set(pair) != {'CPU', 'HW'}:
            result['trajectory_pairs_pending'] += 1
        elif (pair['CPU'].get('checksum') != pair['HW'].get('checksum')
              or pair['CPU'].get('visible') != pair['HW'].get('visible')):
            result['issues'].append('CPU/HW per-update trajectory checksum mismatch for%s' % (key,))
        else:
            result['trajectory_pairs_matched'] += 1
    epoch_order = list(configs)
    if epoch_order and (epoch_order[0] != 0 or count_of(configs[epoch_order[0]]) != 2093):
        result['issues'].append('Fresh BASIC run must start at epoch0 with2093 objects per side')
    for before, after in zip(epoch_order, epoch_order[1:]):
        old, new = configs[before], configs[after]
        if (not integer(count_of(old)) or not integer(count_of(new)) or after != before + 1
                or count_of(new) != max(728, count_of(old) - 91)):
            result['issues'].append('Unexpected epoch/count transition%d->%d' % (before, after))
        matching = [r for r in result['changes'] if r.get('epoch') == before
                    and r.get('old_count') == count_of(old) and r.get('new_count') == count_of(new)
                    and old['serial_line'] < r['serial_line'] < new['serial_line']]
        if len(matching) != 1:
            result['incomplete_reasons'].append('Exactly one drained COUNT_CHANGE needed before epoch%d' % after)
        previous = [r for r in result['assessments'] if r['epoch'] == before]
        if not previous or previous[-1].get('expected_action') != 'REDUCE' or previous[-1].get('expected_next_count') != count_of(new):
            result['issues'].append('New epoch does not follow a measured reduction decision%d' % after)
    complete = [r for r in result['assessments'] if r['window_totals_equal_ASSESS'] and (r.get('actual_seconds') or 0) >= 15]
    if not complete:
        result['incomplete_reasons'].append('No complete raw-timed15second assessment')
    result['target_status'] = 'MET' if any(r.get('target_met') for r in complete) else 'NOT_MET' if complete else 'UNTESTED'
    passing = [r for r in complete if r.get('target_met') and r['health_counters_zero']]
    result['highest_measured_passing_count_each'] = max((count_of(r) for r in passing), default=None)
    result['highest_measured_passing_count_total'] = (2 * result['highest_measured_passing_count_each']
                                                    if result['highest_measured_passing_count_each'] is not None else None)
    result['issues'] = list(dict.fromkeys(result['issues']))
    result['incomplete_reasons'] = list(dict.fromkeys(result['incomplete_reasons']))
    if not result['windows']:
        result['status'] = 'FAIL' if result['issues'] else 'UNTESTED'
        result['reason'] = 'Fresh READY but no measured BASIC windows'
    else:
        result['status'] = 'FAIL' if result['issues'] else 'INCOMPLETE' if result['incomplete_reasons'] else 'PASS'
    return result


def save_report(report, output):
    output.mkdir(parents=True, exist_ok=True)
    for name in ('windows', 'assessments', 'configurations', 'changes', 'events', 'sequences'):
        rows = report[name]
        columns = list(dict.fromkeys(k for row in rows for k in row)) or ['serial_line', 'epoch', 'assess']
        with (output / (name + '.csv')).open('w', encoding='utf-8', newline='') as target:
            writer = csv.DictWriter(target, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)
    (output / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def fixture():
    rows = ['[READY] OLD_DEMO', '[BASIC] ticks=1 hz=1 hw_presented=999',
        '[ALPHA_PROBE] rc=0 cases=35 pixel_errors=0 source_errors=0 guard_errors=0',
        '[PRECISION_PROBE] rc=0 cases=40 pixel_errors=0 source_errors=0 guard_errors=0',
        '[BASIC_PIXEL_PROBE] rc=0 errors=0 pixels=32 source_pixels=6912 original_key_pixels=5142 key=0x4 key_matches=0 alpha_enabled=0 copy_all=1 bake_black_at_setup=1',
        READY + ' initial_count=2093 target_fps=50 alpha=32 key=0x4 shared_cpu=1 flash_write=0',
        '[BASIC_CONFIG] epoch=0 per_side=2093 total=4186 alpha=32 alpha_enabled=0 key=0x4 key_matches=0 copy_all=1 bake_key_to_black=5142 source_pixels=6912 rays=91 speed_q8=612 assess_seconds=15 target_hw_fps=50 min_count=728 step=91 shared_cpu=1 isolated_peak=0 health_includes_prep=1',
        '[BASIC_START] epoch=0 warm_hw_frames=2 prepared_cpu_frames=1 prep_ticks=10']
    common = 'epoch=0 assess=0 per_side=2093 count=2093 total=4186 cpu_visible=2093 hw_visible=2093 hz=100 alpha=32 key=0x4 alpha_enabled=0 key_matches=0 copy_all=1 '
    health = ' '.join(k + '=0' for k in COUNTERS)
    rows += ['[BASIC] ' + common + 'ticks=500 cpu_presented=5 hw_presented=300 cpu_fps1000=999999 hw_fps1000=1 pass=2 action=WINDOW ' + health] * 3
    rows += ['[BASIC_ASSESS] ' + common + 'ticks=1500 cpu_presented=15 hw_presented=900 pass=1 action=KEEP next_count=2093 ' + health]
    return '\n'.join(rows).encode('ascii')


def self_test():
    raw = fixture()
    report = analyze(raw)
    assert report['status'] == 'PASS' and report['target_status'] == 'MET', report
    assert report['assessments'][0]['actual_hw_fps'] == 60
    assert report['assessments'][0]['actual_cpu_fps'] == 1
    assert report['highest_measured_passing_count_each'] == 2093
    assert analyze(b'unrelated data')['status'] == 'UNTESTED'
    assert analyze(raw + b'\n' + READY.encode() + b' initial_count=2093 target_fps=50 alpha=32 key=0x4 shared_cpu=1 flash_write=0')['status'] == 'UNTESTED'
    assert analyze(raw.replace(b'front_reject=0', b'front_reject=1', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'guard=0', b'guard=broken', 1))['status'] == 'INCOMPLETE'
    assert analyze(raw.replace(b'ticks=1500', b'ticks=1499', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'cpu_visible=2093', b'cpu_visible=0', 1))['status'] == 'FAIL'
    assert analyze(raw + b'\n[BASIC_STOP] rc=-5')['status'] == 'FAIL'
    assert analyze(raw.replace(b'action=KEEP', b'action=REDUCE', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'[BASIC_START]', b'[OTHER]', 1))['status'] == 'INCOMPLETE'
    assert analyze(raw.replace(b'alpha_enabled=0', b'alpha_enabled=1', 1))['status'] == 'FAIL'
    assert analyze(raw.replace(b'pass=1', b'pass=0', 1))['status'] == 'FAIL'
    exactly50 = raw.replace(b'hw_presented=300', b'hw_presented=250').replace(b'hw_presented=900', b'hw_presented=750')
    assert analyze(exactly50)['status'] == 'PASS' and analyze(exactly50)['target_status'] == 'MET'
    below50 = raw.replace(b'hw_presented=300', b'hw_presented=245').replace(b'hw_presented=900', b'hw_presented=735').replace(b'pass=1 action=KEEP next_count=2093', b'pass=0 action=REDUCE next_count=2002')
    assert analyze(below50)['status'] == 'PASS' and analyze(below50)['target_status'] == 'NOT_MET'
    print(json.dumps(dict(status='PASS', checks=17, host_only=True, hardware_access=False)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--strict', action='store_true', help='Exit2 unless complete healthy evidence also meets the hardware50FPS target')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.log is None or args.out is None:
        parser.error('--log and --out are required')
    raw = args.log.read_bytes() if args.log.is_file() else b''
    report = analyze(raw, str(args.log.resolve()))
    save_report(report, args.out)
    print(json.dumps(dict(status=report['status'], target_status=report['target_status'],
                         windows=len(report['windows']), assessments=len(report['assessments']),
                         output=str(args.out.resolve())), ensure_ascii=False))
    if args.strict and (report['status'] != 'PASS' or report['target_status'] != 'MET'):
        raise SystemExit(2)


if __name__ == '__main__':
    main()
