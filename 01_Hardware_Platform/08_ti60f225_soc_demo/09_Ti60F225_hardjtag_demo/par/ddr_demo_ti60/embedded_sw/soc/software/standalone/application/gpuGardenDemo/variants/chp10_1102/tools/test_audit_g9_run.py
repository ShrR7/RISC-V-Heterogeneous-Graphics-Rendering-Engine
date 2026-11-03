"""Offline tamper and baseline tests for audit_g9_run; no hardware or evidence edits."""
import unittest
import audit_g9_run as audit


def fixture(path=1, baseline=17):
    lines=['[G9_INIT] initial_shown=32 initial_engine=33 initial_miss_px=%d initial_miss_lines=2 initial_reject=0 initial_skip=0'%baseline]
    if not path:lines+=['[G9_CTRL] enabled=0 target=0']
    lines+=['[FRAME_CTRL] async=0 verbose=1 profile=0','[G9_WORKLOAD] target=128 injected=1 normal_collision=1 normal_logic=1']
    for i in range(4):
        d={k:0 for k in audit.PERF_FIELDS}
        d.update(frame=(i+1)*100,target=128,path=path,hz=100000000,tick_lo=200000000,updates=100,logic_steps=80,shown_delta=120 if path else 0,engine_delta=120 if path else 0,actual_min=128,actual_max=128,migrated_min=128 if path else 0,migrated_max=128 if path else 0,migrated_sum=12800 if path else 0,legacy_sum=0 if path else 12800,desc_sum=12800 if path else 0,logic_us=2000,build_us=3000,render_us=4000,work_peak_us=11000)
        lines+=['[G9_PERF] '+' '.join('%s=%d'%v for v in d.items())]
        f=dict(frame=d['frame'],target=128,actual=128,enabled=path,migrated=128 if path else 0,legacy=0 if path else 128,desc=128 if path else 0,tiles=1 if path else 0,shown=32+(i+1)*d['shown_delta'],engine=33+(i+1)*d['engine_delta'],miss_px=baseline,miss_lines=2,skip=0,reject=0,errors=0,guard=0)
        lines+=['[G9_FRAME] '+' '.join('%s=%d'%v for v in f.items()),'[LIVE] frame=%d fps10=500 errors=0 swap_timeout=0 front_reject=0 atlas_bounds=0 log_dropped=0 log_truncated=0'%d['frame']]
    return '\n'.join(lines)+'\n'


class G9AuditTests(unittest.TestCase):
    def check_run(self, raw, valid, path=1):
        result=audit.audit_case(raw,'n128_path%d'%path,{'pool':2000})
        self.assertEqual(result['status'].startswith('PASS'),valid,result['issues'])
        return result

    def test_historical_init_pressure_is_not_new(self):
        result=self.check_run(fixture(),True)
        self.assertEqual(result['miss_new'],0)
        self.assertEqual(result['eligible_windows'],3)
        self.assertEqual(result['update_fps_mean'],50)
        self.assertEqual(result['selected_summary']['stage9_good_composition_fps'],60)

    def test_legacy_path_zero_stage9_shown_is_not_zero_game_fps(self):
        result=self.check_run(fixture(0),True,0)
        self.assertEqual(result['update_fps_mean'],50)
        self.assertFalse(result['selected_summary']['stage9_display_fps_applicable'])

    def test_new_fault_kept(self):
        self.check_run(fixture().replace('miss_px=17 miss_lines=2 skip=0','miss_px=18 miss_lines=2 skip=0',1),False)

    def test_log_drop_kept(self):
        self.check_run(fixture().replace('log_dropped=0','log_dropped=1',1),False)

    def test_fatal_after_good_windows_kept(self):
        self.check_run(fixture()+'[G9_FATAL] error=-8 raw_px=1297 raw_lines=3 reject=0\n',False)

    def test_fatal_before_any_window_kept_and_delta_from_init(self):
        raw=fixture().split('[G9_PERF]')[0]+'[G9_FATAL] error=-8 raw_px=1297 raw_lines=3 reject=0\nRESULT: GARDEN FAIL Stage9 handoff\n'
        result=self.check_run(raw,False)
        self.assertEqual(result['eligible_windows'],0)
        self.assertEqual(result['miss_new'],1280)
        self.assertEqual(result['lines_new'],1)
        self.assertEqual(result['last_error'],-8)

    def test_missing_frame_kept(self):
        self.check_run('\n'.join(x for x in fixture().splitlines() if not x.startswith('[G9_FRAME] frame=200 ')),False)

    def test_missing_perf_window_kept(self):
        self.check_run('\n'.join(x for x in fixture().splitlines() if not x.startswith('[G9_PERF] frame=200 ')),False)

    def test_two_stable_windows_insufficient(self):
        result=self.check_run('\n'.join(x for x in fixture().splitlines() if 'frame=400 ' not in x),False)
        self.assertEqual(result['status'],'INSUFFICIENT')

    def test_invalid_span_kept(self):
        self.check_run(fixture().replace('tick_lo=200000000','tick_lo=0',1),False)

    def test_mixed_first_window_excluded(self):
        result=self.check_run(fixture().replace('actual_min=128','actual_min=0',1),True)
        self.assertEqual(result['actual_count_range'],[128,128])

    def test_orphan_frame_new_fault_kept(self):
        self.check_run(fixture()+'[G9_FRAME] frame=401 miss_px=1297 miss_lines=3 reject=0 skip=0 errors=0 guard=0\n',False)


if __name__=='__main__':unittest.main(verbosity=2)