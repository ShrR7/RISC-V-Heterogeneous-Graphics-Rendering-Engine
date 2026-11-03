#!/usr/bin/env python3
"""Read-only closure audit of C motion cases and the explicitly retained runtime.

Default: JSON to stdout only. Recorded FPS/visual/user disposition are facts,
separate from hash integrity. No repository scripts or hardware tools execute.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX_FILES = 10000
MAX_JSON_BYTES = 32 * 1024 * 1024
ELF_NAME = 'neonMotionGardenDemo.elf'
REQUIRED = {'source_manifest.json', 'summary.json', 'build_manifest.json',
            'atlas.bin', ELF_NAME, 'raw_UART.log', 'native_display7.json'}


class AuditError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise AuditError(message)


def under(path, parent):
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def relative_file(base, name):
    require(isinstance(name, str) and 0 < len(name) <= 1024, 'Invalid relative path')
    require('\\' not in name and ':' not in name and not name.startswith('/'), f'Unsafe relative path: {name}')
    require(all(part not in ('', '.', '..') for part in name.split('/')), f'Unsafe relative path: {name}')
    path = base / name
    require(under(path, base), f'Path escapes its bounded directory: {name}')
    return path


def digest(value):
    require(isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{64}', value) is not None,
            'Invalid SHA256 value')
    return value.lower()


def sha(path):
    require(path.is_file(), f'Missing file: {path}')
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def read_json(path):
    require(path.is_file() and path.stat().st_size <= MAX_JSON_BYTES, f'Missing/oversized JSON: {path}')
    try:
        value = json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=no_duplicates)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise AuditError(f'Invalid JSON {path}: {error}') from error
    require(isinstance(value, dict), f'Expected JSON object: {path}')
    return value


def hash_map(value, context):
    require(isinstance(value, dict) and 0 < len(value) <= MAX_FILES, f'Invalid hash map: {context}')
    return {name: digest(h) for name, h in value.items()}


def audit_case(case):
    seal = read_json(relative_file(case, 'SHA256SUMS.json'))
    require(seal.get('closed_case') is True, f'Not a closed case: {case.name}')
    rows = seal.get('files')
    require(isinstance(rows, list) and 0 < len(rows) <= MAX_FILES, f'Invalid seal rows: {case.name}')
    indexed, folded = {}, set()
    for row in rows:
        require(isinstance(row, dict), 'Invalid seal row')
        name = row.get('path')
        path = relative_file(case, name)
        require(name != 'SHA256SUMS.json' and name.casefold() not in folded, f'Duplicate/self seal row: {name}')
        require(type(row.get('bytes')) is int and row['bytes'] >= 0, f'Invalid file length: {name}')
        require(path.is_file() and path.stat().st_size == row['bytes'], f'Missing/changed file length: {case.name}/{name}')
        require(sha(path) == digest(row.get('sha256')), f'Hash mismatch: {case.name}/{name}')
        indexed[name] = row
        folded.add(name.casefold())
    require(REQUIRED <= set(indexed), f'Required evidence absent from seal: {case.name}')
    actual = set()
    for path in case.rglob('*'):
        require(under(path, case), f'Case symlink escapes evidence: {path}')
        if path.is_file() and path != case / 'SHA256SUMS.json':
            actual.add(path.relative_to(case).as_posix())
            require(len(actual) <= MAX_FILES, 'Too many case files')
    require(actual == set(indexed), f'Case contains unsealed/missing files: {case.name}')
    m = read_json(case / 'source_manifest.json')
    summary = read_json(case / 'summary.json')
    build = read_json(case / 'build_manifest.json')
    elf_hash = sha(case / ELF_NAME)
    require(all(digest(x) == elf_hash for x in (m.get('ELF_sha256'), summary.get('ELF_sha256'), build.get('elf_sha256'))),
            f'ELF cross-binding mismatch: {case.name}')
    require(summary.get('case') == case.name, f'Case identity mismatch: {case.name}')
    atlas_hash = sha(case / 'atlas.bin')
    require(digest(m.get('atlas_sha256')) == atlas_hash == digest(build.get('atlas_sha256')), f'Atlas cross-binding mismatch: {case.name}')
    atlas_bytes = (case / 'atlas.bin').stat().st_size
    require(m.get('atlas_bytes') == atlas_bytes == build.get('atlas_bytes'), f'Atlas length mismatch: {case.name}')
    require(digest(summary.get('raw_sha256')) == sha(case / 'raw_UART.log'), f'UART binding mismatch: {case.name}')
    runtime = hash_map(m.get('runtime_source_sha256'), 'runtime')
    for name, h in runtime.items():
        require(name.startswith('src/'), f'Runtime source is not bounded to src: {name}')
        snapshot_name = 'source_snapshot/' + name
        snapshot = relative_file(case, snapshot_name)
        require(snapshot_name in indexed and sha(snapshot) == h, f'Runtime snapshot mismatch: {case.name}/{name}')
    unchanged = m.get('unchanged_P_source_sha256', {})
    require(isinstance(unchanged, dict), 'Invalid unchanged-source map')
    for name, h in unchanged.items():
        relative_file(case, 'source_snapshot/' + name)
        require(runtime.get(name) == digest(h), f'Unchanged-source claim disagrees with snapshot: {name}')
    reports = m.get('host_reports')
    require(isinstance(reports, dict) and reports, f'Missing host-report bindings: {case.name}')
    host_rows = []
    for key, info in reports.items():
        require(isinstance(key, str) and re.fullmatch(r'[a-z_]{1,64}', key) is not None and isinstance(info, dict), 'Invalid host-report record')
        # The original build path is descriptive only, never followed outside
        # the case. Still validate it as a bounded relative variant path.
        relative_file(case, info.get('path'))
        report_name = key + '_host_verification.json'
        require(report_name in indexed, f'Host report not sealed: {report_name}')
        report_path = relative_file(case, report_name)
        require(sha(report_path) == digest(info.get('sha256')), f'Host report binding mismatch: {case.name}/{key}')
        report = read_json(report_path)
        hashes = hash_map(report.get('source_sha256'), 'host report sources')
        for name, h in hashes.items():
            require(name.startswith(('src/', 'tools/')), f'Host source outside src/tools: {name}')
            snapshot_name = 'source_snapshot/' + name
            snapshot = relative_file(case, snapshot_name)
            require(snapshot_name in indexed and sha(snapshot) == h, f'Tested-source snapshot mismatch: {case.name}/{name}')
        host_rows.append({'name': key, 'recorded_status': report.get('status'), 'bound_sources': len(hashes)})
    fps = summary.get('fps10_range')
    require(isinstance(fps, list) and len(fps) == 2 and all(type(v) is int and 0 <= v <= 10000 for v in fps) and fps[0] <= fps[1], 'Invalid recorded FPS range')
    strict = summary.get('strict_60FPS_pass')
    require(type(strict) is bool and (not strict or fps[0] >= 600), 'Inconsistent recorded strict FPS result')
    counters = summary.get('display_PX_PART_FULL')
    require(isinstance(counters, list) and len(counters) == 3 and all(type(v) is int and v >= 0 for v in counters), 'Invalid recorded display counters')
    public = {'case': case.name, 'seal_sha256': sha(case / 'SHA256SUMS.json'), 'verified_files': len(indexed),
              'ELF_sha256': elf_hash, 'atlas_sha256': atlas_hash, 'runtime_snapshot_files': len(runtime),
              'host_reports': host_rows, 'recorded_fps10_range': fps, 'recorded_strict_60FPS_pass': strict,
              'recorded_display_PX_PART_FULL': counters,
              'recorded_counter_conservation': summary.get('all_paired_and_conserved'),
              'recorded_visual_status': summary.get('visual'), 'recorded_new600s_run': summary.get('new600s_run'),
              'recorded_Flash_written': summary.get('Flash_written')}
    return {'path': case, 'manifest': m, 'summary': summary, 'public': public}


def audit_delivery(variant_dir, case_dir=None):
    root = Path(variant_dir).resolve()
    evidence = relative_file(root, 'evidence')
    require(evidence.is_dir(), 'Missing evidence directory')
    # Controls have a different evidence schema and their own reviewer.
    case_paths = [p.parent for p in evidence.glob('motion_trial_*/SHA256SUMS.json')]
    require(0 < len(case_paths) <= 100, 'Expected a finite nonempty set of sealed motion_trial_* cases')
    cases = []
    for case in sorted(case_paths, key=lambda p: p.name):
        require(under(case, evidence), f'Case escapes evidence: {case}')
        cases.append(audit_case(case.resolve()))
    disposition = read_json(relative_file(root, 'motion_user_disposition.json'))
    retained = digest(disposition.get('retained_ELF_sha256'))
    explicit = None
    if case_dir is not None:
        path = Path(case_dir)
        path = (path if path.is_absolute() else root / path).resolve()
        require(under(path, evidence) and path != evidence.resolve(), 'Explicit case must be inside variant/evidence')
        require(path.name.startswith('motion_trial_'), 'Unsupported case kind: --case-dir accepts motion_trial_* only; review controls with summarize_controls.py')
        explicit = next((c for c in cases if c['path'] == path), None)
        require(explicit is not None, 'Explicit case is not a sealed immediate evidence case')
    matching = [c for c in cases if c['public']['ELF_sha256'] == retained]
    require(matching, 'No sealed case matches retained ELF from user disposition')
    if len(matching) > 1:
        require(explicit in matching, 'Ambiguous retained ELF across cases; use --case-dir to choose a matching case explicitly')
        current = explicit
    else:
        current = matching[0]
    selected = explicit or current
    manifest = current['manifest']
    for name, h in hash_map(manifest.get('runtime_source_sha256'), 'current runtime').items():
        require(sha(relative_file(root, name)) == h, f'Current runtime differs from retained sealed case: {name}')
    require(sha(relative_file(root, 'assets/atlas.bin')) == digest(manifest.get('atlas_sha256')), 'Current atlas differs from retained sealed case')
    current_elf = relative_file(root, 'build/' + ELF_NAME)
    build_present = current_elf.is_file()
    if build_present:
        require(sha(current_elf) == retained, 'Present current build ELF differs from retained sealed ELF')
    return {'integrity_status': 'PASS', 'default_read_only': True, 'hardware_access': False,
            'variant': root.name, 'verified_case_count': len(cases),
            'cases': [c['public'] for c in cases], 'selected_case': selected['path'].name,
            'case_selection': 'explicit --case-dir' if explicit else 'retained ELF exact match; no time/name ordering',
            'current_retained_case': current['path'].name, 'retained_ELF_sha256': retained,
            'current_runtime_sources_match': True, 'current_atlas_matches': True,
            'current_build_ELF_present': build_present, 'current_build_ELF_matches_if_present': True,
            'selected_recorded_strict_60FPS_pass': selected['public']['recorded_strict_60FPS_pass'],
            'user_disposition_status': disposition.get('status'), 'visual_acceptance_established': False,
            'recorded_visual_fix_accepted_in_disposition': disposition.get('visual_fix_accepted'),
            'recorded_root_cause_confirmed': disposition.get('root_cause_confirmed'),
            'limits': ['Hash/snapshot audit of recorded evidence; no repository script, compiler, FPGA or debugger executes.',
                       'Recorded FPS, counters and host statuses are bound facts, not newly measured performance/counter analysis.',
                       'DEFERRED_BY_USER and a valid seal do not establish visual acceptance or a root cause.',
                       'Current build ELF is optional for a source-only checkout; each sealed case ELF is mandatory and verified.',
                       'SHA256SUMS is an integrity manifest, not a cryptographic signature or independent proof of FPGA readback.']}


def check_output_dir(path):
    target = Path(path).resolve()
    require(not target.exists(), 'Output directory already exists; choose a new directory')
    require(not any(part.casefold() == 'evidence' for part in target.parts), 'Output directory must be outside every evidence directory')
    return target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant-dir', type=Path, default=ROOT)
    parser.add_argument('--case-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--require-strict-60', action='store_true')
    args = parser.parse_args(argv)
    try:
        output = check_output_dir(args.output_dir) if args.output_dir else None
        report = audit_delivery(args.variant_dir, args.case_dir)
        gate = not args.require_strict_60 or report['selected_recorded_strict_60FPS_pass']
        report['strict_60_required'] = args.require_strict_60
        report['requested_gate_pass'] = gate
        encoded = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
        if output:
            # No existing path is ever overwritten; a racing creator also fails.
            output.mkdir(parents=True, exist_ok=False)
            with (output / 'audit_motion_delivery.json').open('x', encoding='utf-8') as stream:
                stream.write(encoded)
        print(encoded, end='')
        return 0 if gate else 3
    except (AuditError, OSError, ValueError, TypeError, KeyError) as error:
        print(json.dumps({'integrity_status': 'FAIL', 'error': str(error)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
