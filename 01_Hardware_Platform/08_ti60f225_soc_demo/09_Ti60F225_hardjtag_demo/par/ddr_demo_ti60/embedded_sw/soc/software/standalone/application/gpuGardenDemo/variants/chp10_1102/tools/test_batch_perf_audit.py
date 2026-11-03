#!/usr/bin/env python3
"""Synthetic parser tests only: no assertion that a board benchmark ran."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('batch_perf_under_test', Path(__file__).with_name('summarize_batch_perf.py'))
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
BOOT = 'GARDEN Ti60 cropped port batch synthetic fixture'
CTRL = '[BATCH_BENCH_CTRL] version=1 rounds=6 parity=AB_BA compare=full_pixels timing=submit_wait_only init=outside_timing hash=outside_timing present=excluded scenes=' + ','.join(AUDIT.SCENES)


def valid_lines():
    lines = [BOOT, CTRL, '[BATCH_PROBE] atlas=0xCEE719EF ref=0x00800000 list=0x0067C000 render_list=0x0067D000 fallback_source_bytes=33792 front=0x00200000']
    for scene in AUDIT.SCENES:
        for round_ in range(6):
            order = 'AB' if round_ % 2 == 0 else 'BA'
            paths = ('single', 'batch') if order == 'AB' else ('batch', 'single')
            for path in paths:
                duration = (1000 if path == 'single' else 400) + round_
                commands = 128 if path == 'single' else 1
                lines.append(f'[BATCH_SAMPLE] scene={scene} round={round_} order={order} path={path} us={duration} commands={commands} items={AUDIT.SCENE_ITEMS[scene]} pixels=921600 crc=0xBEADCAFE initial_crc=0x1234ABCD errors=0')
            lines.append(f'[BATCH_DIFF] scene={scene} round={round_} compared=921600 diff=0 initial_crc=0x1234ABCD guard=0')
    return lines + ['[BATCH_PROBE_END] front_unchanged=1 atlas_unchanged=1 guard=0', '[BATCH_BENCH_DONE] samples=60 pairs=30 failures=0']


def audit(lines):
    return AUDIT.audit_benchmark(AUDIT.records('\n'.join(lines) + '\n')[0])


def change(lines, prefix, before, after):
    result = list(lines)
    index = next(i for i, line in enumerate(result) if line.startswith(prefix))
    result[index] = result[index].replace(before, after)
    return result


def position(lines, prefix):
    return next(i for i, line in enumerate(lines) if line.startswith(prefix))


class BatchPerfTests(unittest.TestCase):
    def test_complete_pairs(self):
        result = audit(valid_lines())
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(result['samples_observed'], 60)
        self.assertEqual(result['pairs_observed'], 30)
        for value in result['scenes'].values():
            self.assertEqual(value['AB_pairs'], 3)
            self.assertEqual(value['BA_pairs'], 3)
            self.assertEqual(value['single']['all_us'], [1000, 1001, 1002, 1003, 1004, 1005])
            self.assertEqual(value['batch']['first_us'], 400)
            self.assertEqual(value['batch']['worst_us'], 405)

    def test_missing_is_pending(self):
        self.assertEqual(audit([])['status'], 'PENDING')

    def test_no_terminal_pending(self):
        self.assertEqual(audit(valid_lines()[:-1])['status'], 'PENDING')

    def test_truncated_pair_pending(self):
        self.assertEqual(audit(valid_lines()[:4])['status'], 'PENDING')

    def test_declared_complete_missing_sample_fails(self):
        lines = valid_lines()
        lines.pop(position(lines, '[BATCH_SAMPLE]'))
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_first_and_worst_not_discarded(self):
        lines = change(valid_lines(), '[BATCH_SAMPLE]', 'us=1000', 'us=9900')
        result = audit(lines)
        self.assertEqual(result['status'], 'PASS')
        value = result['scenes']['fill_grid']['single']
        self.assertEqual(value['first_us'], 9900)
        self.assertEqual(value['worst_us'], 9900)
        self.assertEqual(value['all_us'][0], 9900)

    def test_slower_batch_is_valid_performance_result(self):
        lines = [line.replace('us=400', 'us=9900') if 'path=batch ' in line else line for line in valid_lines()]
        result = audit(lines)
        self.assertEqual(result['status'], 'PASS')
        self.assertLess(result['scenes']['fill_grid']['paired_speedup_all'][0], 1)

    def test_fairness_declarations_required(self):
        for before, after in [('present=excluded', 'present=included'), ('hash=outside_timing', 'hash=inside_timing'),
                              ('compare=full_pixels', 'compare=hash_only'), ('rounds=6', 'rounds=3'),
                              ('init=outside_timing', 'init=inside_timing')]:
            with self.subTest(field=before):
                self.assertEqual(audit(change(valid_lines(), '[BATCH_BENCH_CTRL]', before, after))['status'], 'FAIL')

    def test_scene_contract_required(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_BENCH_CTRL]', ',cache_fallback', ''))['status'], 'FAIL')

    def test_odd_rounds_rejected(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_BENCH_CTRL]', 'rounds=6', 'rounds=7'))['status'], 'FAIL')

    def test_counts_and_extents_positive(self):
        for before, after in [('us=1000', 'us=0'), ('us=1000', 'us=-1'), ('commands=128', 'commands=0'),
                              ('items=128', 'items=127'), ('pixels=921600', 'pixels=921599')]:
            with self.subTest(field=before):
                self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', before, after))['status'], 'FAIL')

    def test_full_pixel_diff_even_if_hash_matches_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_DIFF]', 'diff=0', 'diff=1'))['status'], 'FAIL')

    def test_guard_corruption_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_DIFF]', 'guard=0', 'guard=1'))['status'], 'FAIL')

    def test_incomplete_comparison_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_DIFF]', 'compared=921600', 'compared=921599'))['status'], 'FAIL')

    def test_initial_framebuffer_difference_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'initial_crc=0x1234ABCD', 'initial_crc=0x1234ABCE'))['status'], 'FAIL')

    def test_output_hash_difference_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'crc=0xBEADCAFE', 'crc=0xBEADCAFF'))['status'], 'FAIL')

    def test_gpu_error_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'errors=0', 'errors=1'))['status'], 'FAIL')

    def test_duplicate_fields_rejected(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'errors=0', 'errors=1 errors=0'))['status'], 'FAIL')

    def test_invalid_crc_rejected(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'crc=0xBEADCAFE', 'crc=garbage'))['status'], 'FAIL')

    def test_repeated_sample_rejected(self):
        lines = valid_lines()
        pos = position(lines, '[BATCH_SAMPLE]')
        lines.insert(pos, lines[pos])
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_repeated_diff_rejected(self):
        lines = valid_lines()
        pos = position(lines, '[BATCH_DIFF]')
        lines.insert(pos, lines[pos])
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_actual_AB_BA_order_required(self):
        lines = valid_lines()
        pos = position(lines, '[BATCH_SAMPLE]')
        lines[pos], lines[pos+1] = lines[pos+1], lines[pos]
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_wrong_order_label_fails(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'order=AB', 'order=BA'))['status'], 'FAIL')

    def test_compare_must_follow_both_samples(self):
        lines = valid_lines()
        pos = position(lines, '[BATCH_DIFF]')
        lines[pos-1], lines[pos] = lines[pos], lines[pos-1]
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_unknown_scene_rejected(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_SAMPLE]', 'scene=fill_grid', 'scene=other'))['status'], 'FAIL')

    def test_terminal_failure_rejected(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_BENCH_DONE]', 'failures=0', 'failures=1'))['status'], 'FAIL')

    def test_data_after_terminal_rejected(self):
        lines = valid_lines()
        lines.append(lines[position(lines, '[BATCH_SAMPLE]')])
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_control_must_be_first(self):
        lines = valid_lines()
        pos = position(lines, '[BATCH_SAMPLE]')
        lines[1], lines[pos] = lines[pos], lines[1]
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_duplicate_controls_rejected(self):
        lines = valid_lines()
        lines.insert(2, CTRL)
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_source_must_exceed_cache(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_PROBE]', 'fallback_source_bytes=33792', 'fallback_source_bytes=4096'))['status'], 'FAIL')

    def test_memory_map_must_match(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_PROBE]', 'ref=0x00800000', 'ref=0x00200000'))['status'], 'FAIL')

    def test_atlas_identity_required(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_PROBE]', 'atlas=0xCEE719EF', 'atlas=0xCEE719EE'))['status'], 'FAIL')

    def test_front_or_atlas_changes_fail(self):
        for field in ('front_unchanged', 'atlas_unchanged'):
            self.assertEqual(audit(change(valid_lines(), '[BATCH_PROBE_END]', field+'=1', field+'=0'))['status'], 'FAIL')

    def test_integrity_guard_failure(self):
        self.assertEqual(audit(change(valid_lines(), '[BATCH_PROBE_END]', 'guard=0', 'guard=1'))['status'], 'FAIL')

    def test_probe_fatal_not_hidden(self):
        lines = valid_lines()
        lines.insert(3, '[BATCH_FATAL] trap mcause=0x2 mepc=0x1000')
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_missing_integrity_is_pending(self):
        self.assertEqual(audit([r for r in valid_lines() if not r.startswith('[BATCH_PROBE_END]')])['status'], 'PENDING')

    def test_last_boot_selects_no_stale_pass(self):
        lines = valid_lines() + [BOOT, CTRL]
        self.assertEqual(audit(lines)['status'], 'PENDING')

    def test_partial_tail_excluded(self):
        rows, partial, _ = AUDIT.records('\n'.join(valid_lines()))
        self.assertTrue(partial)
        self.assertEqual(AUDIT.audit_benchmark(rows)['status'], 'PENDING')

    def test_benchmark_only_public_api(self):
        path = Path('synthetic_no_board.log')
        with patch.object(Path, 'read_bytes', return_value=('\n'.join(valid_lines()) + '\n').encode()):
            result = AUDIT.analyze(path, require_game=False)
        self.assertEqual(result['status'], 'PASS')
        self.assertIsNone(result['gameplay'])
        self.assertIsNone(result['strict_60fps'])
        self.assertFalse(result['new600s_run'])

    def test_gameplay_and_strict_statuses_retained(self):
        base = {'status': 'PASS', 'strict_60fps': {'status': 'FAIL', 'first_fps': 59.6}, 'sentinel': ['old guards']}
        path = Path('synthetic_no_board.log')
        with patch.object(Path, 'read_bytes', return_value=('\n'.join(valid_lines()) + '\n').encode()), \
             patch.object(AUDIT, 'module', return_value=SimpleNamespace(analyze=lambda _: copy.deepcopy(base))):
            result = AUDIT.analyze(path)
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(result['strict_60fps'], base['strict_60fps'])
        self.assertEqual(result['gameplay'], base)

    def test_gameplay_failure_not_hidden(self):
        path = Path('synthetic_no_board.log')
        with patch.object(Path, 'read_bytes', return_value=('\n'.join(valid_lines()) + '\n').encode()), \
             patch.object(AUDIT, 'module', return_value=SimpleNamespace(analyze=lambda _: {'status': 'FAIL'})):
            self.assertEqual(AUDIT.analyze(path)['status'], 'FAIL')


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BatchPerfTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    variant = Path(__file__).resolve().parents[1]
    destination = variant / 'build/host/batch_perf_audit'
    destination.mkdir(parents=True, exist_ok=True)
    bound = [Path(__file__), Path(__file__).with_name('summarize_batch_perf.py')]
    report = {'schema': 1, 'status': 'PASS' if result.wasSuccessful() else 'FAIL',
              'evidence_kind': 'SYNTHETIC_PARSER_TESTS_NOT_BOARD_RESULTS', 'tests_run': result.testsRun,
              'failures': len(result.failures), 'errors': len(result.errors),
              'source_sha256': {str(path.relative_to(variant)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest().upper() for path in bound}}
    (destination / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
