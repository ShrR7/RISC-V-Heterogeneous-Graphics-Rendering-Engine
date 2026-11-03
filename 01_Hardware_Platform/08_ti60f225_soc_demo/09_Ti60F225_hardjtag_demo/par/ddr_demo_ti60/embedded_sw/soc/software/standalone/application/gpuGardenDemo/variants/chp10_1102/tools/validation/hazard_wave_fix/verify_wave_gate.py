#!/usr/bin/env python3
"""Offline candidate-only wave gate tests. W runtime is never modified."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def forecast(text):
    return text[text.index('static void bullet_forecast_pair'):text.index('static void check_player_collisions')]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default=r'D:\mingw64\bin\gcc.exe');args=p.parse_args()
    meta=json.loads((ROOT/'candidate.json').read_text());base=ROOT/'base_core.c';candidate=ROOT/'garden_core.c'
    assert sha(base)==meta['base_core_sha256'],'base source changed'
    assert sha(ROOT/'garden_core.h')==meta['base_header_sha256'],'header changed'
    assert forecast(base.read_text())==forecast(candidate.read_text()),'performance planner changed'
    command=[args.cc,'-std=c11','-Wall','-Wextra','-Werror','-O2',str(ROOT/'test_wave_gate.c'),'-o',str(ROOT/'test_wave_gate.exe')]
    compile_run=subprocess.run(command,capture_output=True,text=True,timeout=60)
    (ROOT/'compile.log').write_text(compile_run.stdout+compile_run.stderr)
    assert compile_run.returncode==0,'compile failed'
    run=subprocess.run([str(ROOT/'test_wave_gate.exe')],capture_output=True,text=True,timeout=60)
    (ROOT/'host.log').write_text(run.stdout+run.stderr)
    result=re.search(r'RESULT status=PASS checks=(\d+) runs=(\d+) state_bytes=(\d+)',run.stdout)
    rows=[]
    for line in run.stdout.splitlines():
        if line.startswith('RUN '):rows.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
        elif line.startswith('RESTART '):
            data=dict(re.findall(r'(\w+)=(\d+)',line));next(r for r in rows if r['seed']==int(data['seed']))['restart_best']=int(data['best'])
    report={'status':'PASS' if run.returncode==0 and result else 'FAIL','scope':'HOST candidate only; no W runtime or hardware change',
      'metrics':dict(zip(('checks','runs','state_bytes'),map(int,result.groups()))) if result else {},'runs':rows,
      'bindings':{name:sha(ROOT/name) for name in ('base_core.c','garden_core.c','garden_core.h','wave_gate.patch','test_wave_gate.c','verify_wave_gate.py','compile.log','host.log')},
      'base':meta,'forecast_span_byte_identical':True,
      'policy':'At tick>=1000 enter Boss only when directly scanned active enemy slots are zero. start_boss defensively returns without any side effect if a target is active. No force-clear of enemies.',
      'limitations':['Normal phase5 retreat and age>1600despawn are intentionally retained per root scope; this is not a strict kill-all roster.',
        'Original10scheduled spawns and8slot drop semantics are unchanged. These8runs each observe10spawns, not a guarantee under a deliberately unkillable full pool.',
        'Default final wave target naturally retreats at1211tick; Boss enters1212. Two of10wave enemies naturally exit, eight are killed. Never label those two as kills.',
        'Default victory5675tick=141.875s, plus8s settlement; initial140s capture length is insufficient for this new full loop.',
        'No board FPS, display or visual acceptance is inferred from host results.']}
    (ROOT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'metrics':report['metrics'],'default':rows[0] if rows else None}))
    return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
