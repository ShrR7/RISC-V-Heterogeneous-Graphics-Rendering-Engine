#!/usr/bin/env python3
"""Focused normal-game batch protocol tests; synthetic evidence only."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('batch_game_under_test', Path(__file__).with_name('summarize_batch_game.py'))
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
RECORDS = AUDIT.module('batch_game_test_records', 'summarize_hazard.py')
BOOT = 'GARDEN Ti60 cropped port synthetic batch game'
CTRL = '[BATCH_CTRL] enabled=1 list=0x0067d000 capacity=256 order=consecutive_only toggle=J'


def valid_lines():
    return [BOOT, '[BENCH] backend=GPU batch=1 commands=999', CTRL,
            '[WAVE] tick=80 mode=0 alive=10',
            '[BATCH] tick=80 mode=0 enabled=1 submissions=1000 batches=100 instances=300 fill=40 blit=60',
            '[WAVE] tick=160 mode=1 alive=0',
            '[BATCH] tick=160 mode=1 enabled=1 submissions=1200 batches=120 instances=360 fill=45 blit=75']


def audit(lines):
    return AUDIT.audit_batch_game(RECORDS.parse('\n'.join(lines) + '\n', True)[0])


def change(prefix, before, after, lines=None):
    lines = list(valid_lines() if lines is None else lines)
    index = next(i for i, line in enumerate(lines) if line.startswith(prefix))
    lines[index] = lines[index].replace(before, after)
    return lines


def aggregate(lines=None, inherited='PASS', strict='PASS', terminated=True):
    text = '\n'.join(valid_lines() if lines is None else lines) + ('\n' if terminated else '')
    base = {'status': inherited, 'strict_60fps': {'status': strict},
            'ordnance': {'display': {'status': 'PASS'}, 'live': {'counter_maxima': {'errors': 0}}},
            'visual_acceptance': 'PENDING_USER_OBSERVATION', 'sentinel': ['preserve every old guard']}
    original = AUDIT.module
    def module(name, filename):
        return SimpleNamespace(analyze=lambda _: copy.deepcopy(base)) if filename == 'summarize_coherent.py' else original(name, filename)
    with patch.object(AUDIT, 'module', module), patch.object(Path, 'read_text', return_value=text):
        return AUDIT.analyze(Path('synthetic.log')), base


class BatchGameTests(unittest.TestCase):
    def test_valid_normal_game_delta(self):
        result = audit(valid_lines())
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(result['normal_game_delta'], {'submissions': 200, 'batches': 20, 'instances': 60, 'fill': 5, 'blit': 15})

    def test_startup_bench_and_one_sample_insufficient(self):
        self.assertEqual(audit(valid_lines()[:5])['status'], 'PENDING')

    def test_no_growth_not_promoted_by_positive_initial_totals(self):
        lines = valid_lines()
        lines[-1] = lines[4].replace('tick=80 mode=0', 'tick=160 mode=1')
        self.assertEqual(audit(lines)['status'], 'PENDING')

    def test_only_fill_growth_not_blit_coverage(self):
        lines = valid_lines()
        lines[-1] = lines[-1].replace('fill=45 blit=75', 'fill=60 blit=60')
        self.assertEqual(audit(lines)['status'], 'PENDING')

    def test_counter_decrease_fails_even_across_restart(self):
        lines = valid_lines()
        lines[-2] = lines[-2].replace('tick=160 mode=1', 'tick=10 mode=0')
        lines[-1] = lines[-1].replace('tick=160 mode=1', 'tick=10 mode=0').replace('submissions=1200', 'submissions=900')
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_actual_restart_retains_cumulative_ledger(self):
        lines = [line.replace('tick=160 mode=1', 'tick=10 mode=0') for line in valid_lines()]
        self.assertEqual(audit(lines)['status'], 'PASS')

    def test_same_tick_mode_wave_required(self):
        for before, after in [('tick=80', 'tick=79'), ('mode=0', 'mode=1')]:
            self.assertEqual(audit(change('[WAVE]', before, after))['status'], 'FAIL')

    def test_wave_cannot_be_reused(self):
        lines = valid_lines()
        lines.pop(5)
        lines[-1] = lines[-1].replace('tick=160 mode=1', 'tick=80 mode=0')
        self.assertEqual(audit(lines)['status'], 'FAIL')

    def test_control_contract(self):
        for before, after in [('enabled=1', 'enabled=0'), ('list=0x0067d000', 'list=0x0067c000'),
                              ('capacity=256', 'capacity=1024'), ('order=consecutive_only', 'order=reordered')]:
            with self.subTest(field=before):
                self.assertEqual(audit(change('[BATCH_CTRL]', before, after))['status'], 'FAIL')

    def test_control_missing_pending(self):
        self.assertEqual(audit([r for r in valid_lines() if not r.startswith('[BATCH_CTRL]')])['status'], 'PENDING')

    def test_duplicate_control_fails(self):
        self.assertEqual(audit(valid_lines() + [CTRL])['status'], 'FAIL')

    def test_invalid_cumulative_ledger(self):
        for before, after in [('submissions=1000', 'submissions=99'), ('instances=300', 'instances=199'),
                              ('fill=40', 'fill=41'), ('enabled=1', 'enabled=0'), ('blit=60', 'blit=-1')]:
            with self.subTest(field=before):
                self.assertEqual(audit(change('[BATCH]', before, after))['status'], 'FAIL')

    def test_delta_ledger_must_also_be_valid(self):
        self.assertEqual(audit(change('[BATCH] tick=160', 'instances=360', 'instances=320'))['status'], 'FAIL')

    def test_duplicate_field_not_hidden(self):
        self.assertEqual(audit(change('[BATCH]', 'enabled=1', 'enabled=0 enabled=1'))['status'], 'FAIL')

    def test_missing_field_fails(self):
        self.assertEqual(audit(change('[BATCH]', ' blit=60', ''))['status'], 'FAIL')

    def test_strict60_exception_preserved(self):
        result, base = aggregate(strict='FAIL')
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(result['strict_60fps'], base['strict_60fps'])
        self.assertEqual(result['ordnance'], base['ordnance'])
        self.assertEqual(result['sentinel'], base['sentinel'])
        self.assertEqual(result['visual_acceptance'], 'PENDING_USER_OBSERVATION')

    def test_coherent_failure_and_pending_preserved(self):
        for status in ('FAIL', 'PENDING'):
            self.assertEqual(aggregate(inherited=status)[0]['status'], status)

    def test_last_boot_does_not_reuse_old_pass(self):
        result, _ = aggregate(valid_lines() + [BOOT, CTRL])
        self.assertEqual(result['status'], 'PENDING')
        self.assertEqual(result['batch_game']['samples_observed'], 0)

    def test_partial_tail_excluded(self):
        result, _ = aggregate(valid_lines() + ['[BATCH] enabled=broken'], terminated=False)
        self.assertEqual(result['status'], 'PASS')
        self.assertTrue(result['batch_partial_tail_excluded'])


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BatchGameTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    variant = Path(__file__).resolve().parents[1]
    output = variant / 'build/host/batch_game_audit'
    output.mkdir(parents=True, exist_ok=True)
    bound = [Path(__file__), Path(__file__).with_name('summarize_batch_game.py')]
    report = {'schema': 1, 'status': 'PASS' if result.wasSuccessful() else 'FAIL',
              'evidence_kind': 'SYNTHETIC_PROTOCOL_TESTS_NOT_BOARD_ACCEPTANCE',
              'tests_run': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
              'source_sha256': {str(p.relative_to(variant)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in bound}}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
