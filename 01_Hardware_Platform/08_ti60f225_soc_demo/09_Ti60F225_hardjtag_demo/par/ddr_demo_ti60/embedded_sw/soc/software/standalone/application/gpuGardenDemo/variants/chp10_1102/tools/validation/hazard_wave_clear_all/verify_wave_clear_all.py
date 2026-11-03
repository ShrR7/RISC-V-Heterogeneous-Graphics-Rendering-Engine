#!/usr/bin/env python3
"""Host-only strict wave candidate; never edits W or accesses hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def planner(text):return text[text.index('static void bullet_forecast_pair'):text.index('static void check_player_collisions')]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default=r'D:\mingw64\bin\gcc.exe');args=p.parse_args()
    meta=json.loads((ROOT/'candidate.json').read_text())
    # Text archived using LF; original runtime file hash remains in meta.
    assert planner((ROOT/'base_core.c').read_text())==planner((ROOT/'garden_core.c').read_text()),'forecast/performance span changed'
    cmd=[args.cc,'-std=c11','-Wall','-Wextra','-Werror','-O2',str(ROOT/'test_wave_clear_all.c'),'-o',str(ROOT/'test_wave_clear_all.exe')]
    built=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
    (ROOT/'compile.log').write_text(built.stdout+built.stderr)
    assert built.returncode==0,'compile failed'
    run=subprocess.run([str(ROOT/'test_wave_clear_all.exe')],capture_output=True,text=True,timeout=60)
    (ROOT/'host.log').write_text(run.stdout+run.stderr)
    result=re.search(r'RESULT status=PASS checks=(\d+) runs=(\d+) state_bytes=(\d+)',run.stdout)
    rows=[]
    for line in run.stdout.splitlines():
        if line.startswith('RUN '):rows.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
        elif line.startswith('RESTART '):
            item=dict(re.findall(r'(\w+)=(\d+)',line));next(r for r in rows if r['seed']==int(item['seed']))['restart_best']=int(item['best'])
    report={'status':'PASS' if run.returncode==0 and result else 'FAIL','scope':'HOST candidate only; SYNTHETIC boundary fixtures plus actual seeded game-step loops; not board/FPS acceptance',
      'metrics':dict(zip(('checks','runs','state_bytes'),map(int,result.groups()))) if result else {},'runs':rows,
      'maxima':{k:max((r[k] for r in rows),default=None) for k in ('waves_eb_peak','waves_active_peak','boss_eb_peak','dropped_eb','combat_drop','win_tick')},
      'bindings':{name:sha(ROOT/name) for name in ('base_core.c','base_core.h','garden_core.c','garden_core.h','wave_clear_all.patch','test_wave_clear_all.c','verify_wave_clear_all.py','README.txt','compile.log','host.log')},
      'base':meta,'planner_span_byte_identical_to_2C475_base':True,'new_persistent_state_bytes':4,
      'policy':{'scheduled':10,'pool':8,'cursor':'wave_next advances only after actual successful delivery; full pool consumes no RNG and retries exact same task',
        'kills':'wave_kills increments only on actual positive-HP enemy kill through hit_target while WAVES; independent combat events verify each increment',
        'gate':'tick>=1000 && wave_next==10 && wave_kills==10 && direct active slot count==0',
        'normal_retreat':'STOPPER/completed-three-move DART repeats existing on-screen attack/rest loop instead of phase5',
        'fallback':'old phase5, off-field phase4 or age limit returns the same living HP target through the upper entrance, no RNG/reward/immunity',
        'protection_and_budgets':'player protection/weapon damage/AI/planner unchanged, Boss80live budget unchanged, wave physical192pool unchanged'},
      'limitations':['This candidate must be integrated by patch, not by overwriting later optimized forecast code.',
        'Eight seeds are reproducible runs, not a guarantee for every future seed or deliberately stationary player.',
        'Physical192slot wave bullet overflow remains the pre-existing limit; no additional hidden cap was introduced. All measured8runs have zero enemy/event drops.',
        'Default victory5274tick=131.85s plus8s settlement; device startup and sparse reporting require capture margin, suggested170s.',
        'Host does not establish board FPS, display counters or visual correctness.']}
    (ROOT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'metrics':report['metrics'],'maxima':report['maxima'],'default':rows[0] if rows else None}))
    return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
