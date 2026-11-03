#!/usr/bin/env python3
"""Fresh W revision2: wave-clear gate plus the hazard/ordnance board audit."""
import argparse, importlib.util, json
from pathlib import Path

def analyze(path):
    spec=importlib.util.spec_from_file_location('sortie_hazard',Path(__file__).with_name('summarize_hazard.py'))
    h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
    result=h.analyze(path)
    rows,partial,boots=h.parse(path.read_text(encoding='utf-8-sig',errors='replace'),True)
    ctrl=[r for r in rows if r['raw'].startswith('[WAVE_CTRL]')]
    expected={'earliest_boss_tick':1000,'gate':'all_planned_enemies_killed','leftovers_cleared':0,'natural_exit_preserved':0,'strict_kill_all':1,'scheduled':10}
    issues=[]
    if ctrl and any(any(r.get(k)!=value for k,value in expected.items()) for r in ctrl):issues.append('Wrong wave protocol declaration')
    samples=[r for r in rows if r['raw'].startswith('[WAVE]')]
    valid=[]
    for r in samples:
        if not all(isinstance(r.get(k),int) for k in ('tick','mode','alive','boss','spawned','killed')) or not 0<=r['alive']<=8 or r['boss'] not in (0,1):
            issues.append({'line':r['line'],'error':'Invalid wave sample'});continue
        if not 0<=r['killed']<=r['spawned']<=10 or (r['mode'] in (0,1) and r['alive']!=r['spawned']-r['killed']):issues.append({'line':r['line'],'error':'Wave spawn/kill/live ledger inconsistent'})
        if r['mode']==1 and (r['alive']!=0 or r['boss']!=1):issues.append({'line':r['line'],'error':'Boss and live wave enemy coexist'})
        valid.append(r)
    games=[r for r in rows if r['raw'].startswith('[GAME]') and r.get('mode')==1]
    for r in games:
        if r.get('enemies')!=0 or r.get('tick',0)<1000 or r.get('spawned')!=10 or r.get('killed')!=10:issues.append({'line':r['line'],'error':'Boss entered before wave-clear condition'})
    waits=[r for r in rows if r['raw'].startswith('[WAVE_WAIT]')]
    for r in waits:
        if not isinstance(r.get('tick'),int) or r['tick']<1000 or not isinstance(r.get('alive'),int) or r['alive']<1:issues.append({'line':r['line'],'error':'Invalid wait evidence'})
    gate={'status':'FAIL' if issues else 'PASS' if ctrl and valid and games else 'PENDING',
          'controls':ctrl,'issues':issues,'sample_count':len(valid),'actual_waits':waits,
          'actual_boss_entry':games[0] if games else None,
          'zero_remaining_enemy_samples_at_boss':bool(games) and all(r.get('enemies')==0 for r in games),
          'scope':'Real sampled stage states. Host tests exercise a surviving enemy at tick1000; ten scheduled enemies must actually be spawned and killed; live retreat is disabled.'}
    return {**result,'evidence_kind':'HAZARD_AND_WAVE_CLEAR_FRESH_BOARD_CAPTURE',
            'status':'FAIL' if result['status']=='FAIL' or gate['status']=='FAIL' else 'PASS' if result['status']==gate['status']=='PASS' else 'PENDING',
            'wave_gate':gate}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',required=True,type=Path);p.add_argument('--output',type=Path);p.add_argument('--require-strict-60',action='store_true');a=p.parse_args()
    result=analyze(a.log);text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if a.output:
        with a.output.open('x',encoding='utf-8') as stream:stream.write(text)
    print(text,end='')
    return 0 if result['status']=='PASS' and (not a.require_strict_60 or result['strict_60fps']['status']=='PASS') else 1

if __name__=='__main__':raise SystemExit(main())
