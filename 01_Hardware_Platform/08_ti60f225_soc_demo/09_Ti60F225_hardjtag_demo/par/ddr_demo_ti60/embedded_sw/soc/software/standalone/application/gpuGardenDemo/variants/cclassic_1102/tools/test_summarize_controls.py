#!/usr/bin/env python3
"""Small synthetic parser fixtures; these are not board observations.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('controls', HERE / 'summarize_controls.py')
controls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(controls)


def display_pair(rid, phase, begin, end, total_ar, sum_ar, backend='GPU', alpha=1):
    values = f'0/0/{sum_ar}/0/0'
    main = (f'[DISPLAY7] phase={phase} present=1 rid={rid} frame={rid} tick=0 mode=0 attack=0 score=0 lives=3 '
            f'backend={backend} alpha={alpha} panel=16 fb=0 ctrl=4 t={begin}..{end} n=1 gb=1 span=1 '
            f'sum={values} sat=0/0/0/0/0 max={values} last={values} rawstall={sum_ar} '
            'row=0 s=0/0 sor=0 q=1024 qor=1024 pred=0/0 sm=0 ov=0')
    aux = (f'[DISPLAY7_AUX] rid={rid} total=0/0/{total_ar}/0/0 total_sat=0/0/0/0/0 tov=0 '
           f'event=0/0/0/0/0 eraw=0 erow=0 et={begin}..{end} es=0/0 eq=1024 ev=0 ep=0 '
           f'estate=0/0/0/0 game_start=1002 game_elapsed={max(0,end-1002)} refused=0 '
           'snapshot_not_runtime=1 pred_not_D=1 clear_all_written=0')
    return [main, aux]


def fixture():
    rows = ['GARDEN Ti60 cropped port fixture / HOST SYNTHETIC',
            '[ATLAS] crc32=0x6486f38e expected=0x6486f38e',
            '[ALPHA_PROBE] rc=0 present=1 cases=35 commands=35 pixel_errors=0 source_errors=0 guard_errors=0',
            '[PRECISION_PROBE] rc=0 sprites=20 cases=40 commands=40 high_region_sources=20 opaque_cases=20 alpha_cases=20 pixel_errors=0 source_errors=0 guard_errors=0 restore_skipped=0',
            '[ATLAS_POST_PROBE] crc32=0x6486f38e expected=0x6486f38e',
            '[COLOR_KEY] source pixels and transparent holes verified PASS',
            '[BENCH] backend=GPU errors=0', '[BENCH] backend=CPU errors=0']
    rows += display_pair(1, 'GAME_START', 1000, 1001, 0, 0)
    rows += ['[READY] HOST SYNTHETIC FIXTURE, not an observation']
    state = dict(cpu=0, alpha=1, panel_alpha=16, demo=1, paused=0, slow=0,
                 mode=0, lives=3, tick=0, player_x=320000, player_y=410000)
    previous = 0
    for index, (at, key) in enumerate(controls.SCHEDULE):
        state['tick'] += 0 if state['paused'] else (at-previous)*40
        if key in ('a', 'd', 'w', 's'):
            state['demo'] = 0
        elif key in ('c', 'v', 't', 'f'):
            field = dict(c='cpu', v='alpha', t='demo', f='slow')[key]
            state[field] ^= 1
        elif key == 'p':
            state['paused'] ^= 1
            if not state['paused']:
                state['tick'] += 1
        elif key == 'r':
            state.update(demo=1, paused=0, tick=0, mode=0, lives=3)
        elif key == 'n':
            state['panel_alpha'] = 8 if state['panel_alpha']==24 else state['panel_alpha']+8
        if index < 5:
            state['player_x'], state['player_y'] = [(314000,410000),(248000,410000),
                (320000,404000),(320000,338000),(320000,410000)][index]
        if key == 'b':
            rows += ['[BENCH] backend=GPU errors=0', '[BENCH] backend=CPU errors=0']
        rows.append('[INPUT_STATE] key=' + key + ' ' + ' '.join(f'{k}={v}' for k,v in state.items()))
        previous = at
    faults = ' '.join(f'{k}=0' for k in controls.FAULT_FIELDS)
    rows += ['[LIVE] fps10=200 backend=CPU alpha=0 '+faults,
             '[LIVE] fps10=590 backend=GPU alpha=1 '+faults]
    rows += display_pair(2, 'GARDEN', 2000, 2001, 3, 3, 'CPU', 0)
    hosts = [f'[HOST_INPUT] {key} at {at}s' for at,key in controls.SCHEDULE]
    return rows, hosts


class Contract(unittest.TestCase):
    def setUp(self):
        self.fixture_root = HERE.parent / 'build/host/controls_fixtures'
        self.fixture_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='garden_controls_', dir=self.fixture_root)
        self.directory = Path(self.temp.name)
        self.log, self.host = self.directory/'raw.log', self.directory/'host.log'
        self.rows, self.hosts = fixture()

    def tearDown(self):
        self.assertEqual(self.directory.resolve().parent, self.fixture_root.resolve())
        self.temp.cleanup()

    def review(self):
        self.log.write_text('\n'.join(self.rows)+'\n', encoding='utf-8')
        self.host.write_text('\n'.join(self.hosts)+'\n', encoding='utf-8')
        before = [p.read_bytes() for p in (self.log,self.host)]
        result = controls.audit(self.log,self.host,self.directory)
        self.assertEqual(before,[p.read_bytes() for p in (self.log,self.host)])
        return result

    def test_valid_cpu_alpha_off_low_fps_are_allowed(self):
        result = self.review()
        self.assertEqual(result['status'],'FUNCTIONAL_PASS')
        self.assertEqual(result['fps10_range'],[200,590])
        self.assertFalse(result['fps_is_a_gate'])
        self.assertEqual(result['motion_issue'],'DEFERRED_BY_USER')
        self.assertFalse(result['display']['all_gpu_alpha16_informational_only'])
        self.assertEqual(result['display']['GAME_START_to_last_total_difference'],[0,0,3,0,0])

    def test_actual_toggle_mismatch_fails(self):
        index=next(i for i,r in enumerate(self.rows) if r.startswith('[INPUT_STATE] key=c'))
        self.rows[index]=self.rows[index].replace('cpu=1','cpu=0')
        self.assertFalse(self.review()['gates']['actual_key_effects'])

    def test_missing_key_and_sent_marker_fail(self):
        self.rows=[r for r in self.rows if not r.startswith('[INPUT_STATE] key=w')]
        self.hosts.pop(2)
        result=self.review()
        self.assertFalse(result['gates']['device_schedule_complete'])
        self.assertFalse(result['gates']['host_schedule_complete'])

    def test_startup_each_probe_is_required(self):
        original=copy.copy(self.rows)
        for tag in ('[ALPHA_PROBE]','[PRECISION_PROBE]','[COLOR_KEY]','[ATLAS_POST_PROBE]'):
            with self.subTest(tag=tag):
                self.rows=[r for r in original if not r.startswith(tag)]
                self.assertFalse(self.review()['functional_pass'])

    def test_fatal_and_counter_growth_are_not_ignored(self):
        self.rows.append('RESULT: GARDEN FAIL fixture')
        index=max(i for i,r in enumerate(self.rows) if r.startswith('[LIVE]'))
        self.rows[index]=self.rows[index].replace('errors=0','errors=2')
        result=self.review()
        self.assertFalse(result['gates']['no_fatal'])
        self.assertEqual(result['live_counter_differences']['errors']['delta'],2)
        self.assertFalse(result['functional_pass'])

    def test_final_restore_mismatch_fails(self):
        index=max(i for i,r in enumerate(self.rows) if r.startswith('[INPUT_STATE]'))
        self.rows[index]=self.rows[index].replace('demo=1','demo=0')
        self.assertFalse(self.review()['gates']['final_gpu_alpha16_auto_unpaused_normal_speed'])

    def test_pause_tick_drift_is_detected(self):
        indices=[i for i,r in enumerate(self.rows) if r.startswith('[INPUT_STATE] key=p')]
        self.rows[indices[1]]=__import__('re').sub(r'tick=\d+', 'tick=99999', self.rows[indices[1]])
        self.assertFalse(self.review()['gates']['pause_resume_freezes_core_ticks'])

    def test_display_gap_retained_not_zeroed(self):
        self.rows[-1]=self.rows[-1].replace('total=0/0/3/0/0','total=0/0/5/0/0')
        result=self.review()
        self.assertEqual(result['status'],'FUNCTIONAL_PASS_WITH_COVERAGE_LIMITS')
        self.assertEqual(result['display']['GAME_START_to_last_total_difference'],[0,0,5,0,0])
        self.assertEqual(result['display']['counter_total_window_gaps'][0]['uncovered_counts'],[0,0,2,0,0])

    def test_display_incomplete_pair_disclosed(self):
        self.rows.append('[DISPLAY7] phase=SECOND rid=3 present=1')
        result=self.review()
        self.assertEqual(result['status'],'FUNCTIONAL_PASS_WITH_COVERAGE_LIMITS')
        self.assertEqual(result['display']['incomplete_rids'],[3])

    def test_empty_log_never_passes(self):
        self.rows=[];self.hosts=[]
        self.assertFalse(self.review()['functional_pass'])

    def test_output_refuses_existing_case_and_default_is_readonly(self):
        self.review()
        command=[sys.executable,'-B',str(HERE/'summarize_controls.py'),'--log',str(self.log),
                 '--host-log',str(self.host),'--case-dir',str(self.directory)]
        before=sorted(p.name for p in self.directory.iterdir())
        result=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(json.loads(result.stdout)['functional_pass'])
        self.assertEqual(before,sorted(p.name for p in self.directory.iterdir()))
        refused=subprocess.run(command+['--output-dir',str(self.directory/'summary')],capture_output=True,text=True)
        self.assertNotEqual(refused.returncode,0)
        self.assertFalse((self.directory/'summary').exists())

    def test_cli_functional_failure_exits_one_without_changing_json_gates(self):
        self.rows.append('RESULT: GARDEN FAIL fixture')
        expected=self.review()
        before=[p.read_bytes() for p in (self.log,self.host)]
        command=[sys.executable,'-B',str(HERE/'summarize_controls.py'),'--log',str(self.log),
                 '--host-log',str(self.host),'--case-dir',str(self.directory)]
        result=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(result.returncode,1,result.stderr)
        actual=json.loads(result.stdout)
        self.assertEqual(actual['status'],expected['status'])
        self.assertEqual(actual['gates'],expected['gates'])
        self.assertEqual(actual['issues'],expected['issues'])
        self.assertEqual(before,[p.read_bytes() for p in (self.log,self.host)])


if __name__=='__main__':
    unittest.main()
