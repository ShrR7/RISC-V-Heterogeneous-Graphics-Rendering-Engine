#!/usr/bin/env python3
"""Finite real-case and small-fixture contracts for audit_motion_delivery.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
sys.dont_write_bytecode = True
TOOL = Path(__file__).with_name('audit_motion_delivery.py')
spec = importlib.util.spec_from_file_location('motion_delivery_auditor_under_test', TOOL)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
ROOT = TOOL.parents[1]
TEST_TEMP_ROOT = ROOT / 'build/host/audit_fixture_tmp'


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def seal(case):
    rows = []
    for f in sorted(case.rglob('*')):
        if f.is_file() and f != case / 'SHA256SUMS.json':
            rows.append({'path': f.relative_to(case).as_posix(), 'bytes': f.stat().st_size,
                         'sha256': digest(f.read_bytes())})
    write_json(case / 'SHA256SUMS.json', {'closed_case': True, 'files': rows})


def fixture(root, name='motion_trial_z_current', elf=b'fixture retained ELF', source=b'fixture source\n', strict=False):
    root.mkdir(parents=True, exist_ok=True)
    case = root / 'evidence' / name
    case.mkdir(parents=True)
    files = {'atlas.bin': b'RGB565 fixture', 'raw_UART.log': b'fixture raw UART\n',
             'neonMotionGardenDemo.elf': elf, 'source_snapshot/src/a.c': source}
    for name, data in files.items():
        path = case / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (root / 'src').mkdir(exist_ok=True)
    (root / 'assets').mkdir(exist_ok=True)
    (root / 'build').mkdir(exist_ok=True)
    (root / 'src/a.c').write_bytes(source)
    (root / 'assets/atlas.bin').write_bytes(files['atlas.bin'])
    (root / 'build/neonMotionGardenDemo.elf').write_bytes(elf)
    report = {'status': 'PASS', 'source_sha256': {'src/a.c': digest(source)}}
    write_json(case / 'motion_host_verification.json', report)
    manifest = {'variant': root.name, 'ELF_sha256': digest(elf), 'atlas_sha256': digest(files['atlas.bin']),
                'atlas_bytes': len(files['atlas.bin']), 'runtime_source_sha256': {'src/a.c': digest(source)},
                'unchanged_P_source_sha256': {'src/a.c': digest(source)},
                'host_reports': {'motion': {'path': 'build/host/motion/verification.json',
                                          'sha256': digest((case / 'motion_host_verification.json').read_bytes())}}}
    write_json(case / 'source_manifest.json', manifest)
    write_json(case / 'build_manifest.json', {'elf_sha256': digest(elf), 'atlas_sha256': digest(files['atlas.bin']),
               'atlas_bytes': len(files['atlas.bin']), 'elf': 'C:/descriptive/only/do-not-open.elf'})
    write_json(case / 'native_display7.json', {'fixture': True})
    write_json(case / 'summary.json', {'case': case.name, 'ELF_sha256': digest(elf), 'raw_sha256': digest(files['raw_UART.log']),
               'fps10_range': [601, 601] if strict else [374, 601], 'strict_60FPS_pass': strict,
               'display_PX_PART_FULL': [0, 0, 0], 'all_paired_and_conserved': True,
               'visual': 'PENDING', 'new600s_run': False, 'Flash_written': False})
    write_json(root / 'motion_user_disposition.json', {'status': 'DEFERRED_BY_USER',
               'retained_ELF_sha256': digest(elf), 'visual_fix_accepted': False, 'root_cause_confirmed': False})
    seal(case)
    return case


class DeliveryAuditTests(unittest.TestCase):
    def setUp(self):
        TEST_TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='audit_', dir=TEST_TEMP_ROOT)
        self.assertTrue(audit.under(Path(self.temp.name), TEST_TEMP_ROOT))
        self.root = Path(self.temp.name) / 'variant'
        self.case = fixture(self.root)

    def tearDown(self):
        # Verify the resolved target before the standard recursive temp cleanup.
        self.assertTrue(audit.under(Path(self.temp.name), TEST_TEMP_ROOT))
        self.temp.cleanup()

    def reject(self, action=None):
        if action:
            action()
        with self.assertRaises(audit.AuditError):
            audit.audit_delivery(self.root)

    def mutate_manifest(self, fn):
        path = self.case / 'source_manifest.json'
        value = json.loads(path.read_text())
        fn(value)
        write_json(path, value)
        seal(self.case)

    def call(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = audit.main(['--variant-dir', str(self.root), *map(str, args)])
        return code, out.getvalue(), err.getvalue()

    def test_01_real_six_cases_and_current_4839_bindings(self):
        before = {p.relative_to(ROOT).as_posix(): (p.stat().st_size, p.stat().st_mtime_ns)
                  for p in ROOT.rglob('*') if p.is_file() and (any(part.startswith('motion_trial_') for part in p.parts) or p.parent.name == 'src')}
        result = audit.audit_delivery(ROOT)
        self.assertEqual(result['verified_case_count'], 6)
        self.assertEqual(sum(c['verified_files'] for c in result['cases']), 408)
        self.assertEqual(result['selected_case'], 'motion_trial_20261015_100705')
        self.assertEqual(result['retained_ELF_sha256'], '4839b630b480da7b7a14bffe5634d71bb8b504a2528239c1c397125bd8774642')
        self.assertTrue(result['current_runtime_sources_match'] and result['current_atlas_matches'])
        self.assertFalse(result['selected_recorded_strict_60FPS_pass'])
        self.assertEqual(result['user_disposition_status'], 'DEFERRED_BY_USER')
        self.assertFalse(result['visual_acceptance_established'])
        after = {p.relative_to(ROOT).as_posix(): (p.stat().st_size, p.stat().st_mtime_ns)
                 for p in ROOT.rglob('*') if p.is_file() and (any(part.startswith('motion_trial_') for part in p.parts) or p.parent.name == 'src')}
        self.assertEqual(before, after)

    def test_02_default_stdout_only_and_strict_separate(self):
        before = {p.relative_to(self.root) for p in self.root.rglob('*')}
        code, out, err = self.call()
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads(out)['integrity_status'], 'PASS')
        code, out, err = self.call('--require-strict-60')
        self.assertEqual(code, 3, err)
        self.assertEqual(json.loads(out)['integrity_status'], 'PASS')
        self.assertFalse(json.loads(out)['requested_gate_pass'])
        self.assertEqual(before, {p.relative_to(self.root) for p in self.root.rglob('*')})

    def test_03_true_strict_gate(self):
        summary = json.loads((self.case / 'summary.json').read_text())
        summary.update(fps10_range=[601, 601], strict_60FPS_pass=True)
        write_json(self.case / 'summary.json', summary)
        seal(self.case)
        self.assertEqual(self.call('--require-strict-60')[0], 0)

    def test_04_tampered_sealed_file(self):
        (self.case / 'raw_UART.log').write_bytes(b'tampered')
        self.reject()

    def test_05_missing_file(self):
        (self.case / 'atlas.bin').unlink()
        self.reject()

    def test_06_unsealed_extra_file(self):
        (self.case / 'extra.txt').write_text('not in seal')
        self.reject()

    def test_07_current_runtime_mismatch(self):
        (self.root / 'src/a.c').write_text('different current source')
        self.reject()

    def test_08_current_atlas_mismatch(self):
        (self.root / 'assets/atlas.bin').write_bytes(b'wrong atlas')
        self.reject()

    def test_09_optional_build_missing_but_present_wrong_rejected(self):
        elf = self.root / 'build/neonMotionGardenDemo.elf'
        elf.unlink()
        self.assertFalse(audit.audit_delivery(self.root)['current_build_ELF_present'])
        elf.write_bytes(b'wrong current ELF')
        self.reject()

    def test_10_manifest_path_escapes(self):
        bad_paths = ('../outside', '/absolute', 'C:/drive', 'src/../../outside', 'src\\outside', 'src//a.c', 'src/./a.c')
        for bad in bad_paths:
            with self.subTest(path=bad):
                with self.assertRaises(audit.AuditError):
                    audit.relative_file(self.case, bad)
        self.mutate_manifest(lambda m: m['host_reports']['motion'].update(path='../escape'))
        self.reject()

    def test_11_seal_path_escape_and_duplicate(self):
        value = json.loads((self.case / 'SHA256SUMS.json').read_text())
        value['files'][0]['path'] = '../outside'
        write_json(self.case / 'SHA256SUMS.json', value)
        self.reject()
        seal(self.case)
        value = json.loads((self.case / 'SHA256SUMS.json').read_text())
        value['files'].append(value['files'][0].copy())
        write_json(self.case / 'SHA256SUMS.json', value)
        self.reject()

    def test_12_runtime_manifest_escape_and_snapshot_mismatch(self):
        self.mutate_manifest(lambda m: m['runtime_source_sha256'].update({'src/../outside': '0' * 64}))
        self.reject()

    def test_13_tested_snapshot_hash_mismatch(self):
        (self.case / 'source_snapshot/src/a.c').write_bytes(b'different snapshot')
        seal(self.case)
        self.reject()

    def test_14_host_report_identity_mismatch(self):
        write_json(self.case / 'motion_host_verification.json', {'source_sha256': {'src/a.c': '0' * 64}})
        seal(self.case)
        self.reject()

    def test_15_duplicate_json_keys(self):
        (self.case / 'summary.json').write_text('{"case": "a", "case": "b"}')
        seal(self.case)
        self.reject()

    def test_16_resolved_symlink_escape_guard(self):
        original = Path.resolve
        def escaped(path, *args, **kwargs):
            if path.name == 'raw_UART.log':
                return Path(self.temp.name) / 'outside.log'
            return original(path, *args, **kwargs)
        with mock.patch.object(Path, 'resolve', escaped):
            self.reject()

    def test_17_export_new_dir_outside_evidence_only(self):
        target = Path(self.temp.name) / 'new_report'
        self.assertEqual(self.call('--output-dir', target)[0], 0)
        result = target / 'audit_motion_delivery.json'
        self.assertTrue(result.is_file())
        original = result.read_bytes()
        self.assertEqual(self.call('--output-dir', target)[0], 2)
        self.assertEqual(result.read_bytes(), original)
        forbidden = self.root / 'evidence/new_report'
        self.assertEqual(self.call('--output-dir', forbidden)[0], 2)
        self.assertFalse(forbidden.exists())
        self.assertEqual(self.call('--output-dir', self.case)[0], 2)

    def test_18_ambiguous_retained_elf_requires_explicit_case(self):
        second = fixture(self.root, 'motion_trial_a_duplicate')
        self.reject()
        result = audit.audit_delivery(self.root, second)
        self.assertEqual(result['selected_case'], second.name)
        self.assertEqual(result['current_retained_case'], second.name)

    def test_19_historical_explicit_selection_keeps_current_binding(self):
        old = fixture(self.root, 'motion_trial_zz_old', elf=b'old ELF', source=b'old source')
        # Restore current source/disposition; old case remains sealed and can be
        # explicitly reviewed without pretending it matches current runtime.
        (self.root / 'src/a.c').write_bytes(b'fixture source\n')
        (self.root / 'build/neonMotionGardenDemo.elf').write_bytes(b'fixture retained ELF')
        write_json(self.root / 'motion_user_disposition.json', {'status': 'DEFERRED_BY_USER',
                   'retained_ELF_sha256': digest(b'fixture retained ELF'), 'visual_fix_accepted': True})
        result = audit.audit_delivery(self.root, old.relative_to(self.root))
        self.assertEqual(result['selected_case'], old.name)
        self.assertEqual(result['current_retained_case'], self.case.name)
        self.assertFalse(result['visual_acceptance_established'])
        self.assertTrue(result['recorded_visual_fix_accepted_in_disposition'])

    def test_20_case_outside_evidence_and_unknown_retained_elf(self):
        with self.assertRaises(audit.AuditError):
            audit.audit_delivery(self.root, Path(self.temp.name))
        write_json(self.root / 'motion_user_disposition.json', {'retained_ELF_sha256': '0' * 64})
        self.reject()


    def test_21_sealed_controls_success_and_failure_are_not_motion_cases(self):
        success = fixture(self.root, 'controls_success_fixture')
        failure = self.root / 'evidence/controls_failure_fixture'
        write_json(failure / 'execution_receipt.json', {'attempt': 'fixture'})
        write_json(failure / 'summary.json', {'status': 'FAIL', 'reason': 'fixture entry failure'})
        seal(failure)
        result = audit.audit_delivery(self.root)
        self.assertEqual(result['verified_case_count'], 1)
        self.assertEqual(result['selected_case'], self.case.name)
        self.assertEqual([c['case'] for c in result['cases']], [self.case.name])
        self.assertTrue((success / 'SHA256SUMS.json').is_file())
        self.assertTrue((failure / 'SHA256SUMS.json').is_file())

    def test_22_explicit_controls_are_clearly_unsupported(self):
        success = fixture(self.root, 'controls_success_fixture')
        failure = self.root / 'evidence/controls_failure_fixture'
        write_json(failure / 'summary.json', {'status': 'FAIL'})
        seal(failure)
        for case in (success, failure):
            with self.subTest(case=case.name):
                code, out, err = self.call('--case-dir', case.relative_to(self.root))
                self.assertEqual(code, 2)
                self.assertEqual(out, '')
                error = json.loads(err)['error']
                self.assertIn('Unsupported case kind', error)
                self.assertIn('motion_trial_* only', error)
                self.assertIn('summarize_controls.py', error)


if __name__ == '__main__':
    unittest.main(verbosity=2)
