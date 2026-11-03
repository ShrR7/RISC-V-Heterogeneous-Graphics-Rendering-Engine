#!/usr/bin/env python3
"""Fresh board audit for the five user-art cruiser and actual phase timing."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path

def module(name,file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def analyze(path):
    base=module('cruiser_sortie','summarize_sortie.py').analyze(path)
    h=module('cruiser_records','summarize_hazard.py');rows,partial,boots=h.parse(path.read_text(encoding='utf-8-sig',errors='replace'),True)
    controls=[r for r in rows if r['raw'].startswith('[ART_CTRL]')]
    expected={'materials':5,'sprite_count':859,'atlas_bytes':472732,'player_bank':1,'actor_hp':'above','damage_numbers':1,
              'laser_left_px':-42,'laser_right_px':43,'laser_y_px':19}
    issues=[]
    for row in controls:
        if any(row.get(k)!=v for k,v in expected.items()):issues.append({'line':row['line'],'error':'new art declaration disagrees with measured material geometry'})
    costs=[r for r in rows if r['raw'].startswith('[FRAME_COST]')]
    fields=('frames','steps','logic_avg_us','logic_peak_us','build_avg_us','build_peak_us','render_avg_us',
            'present_avg_us','debug_avg_us','log_avg_us','work_peak_us','work_over_period')
    valid=[]
    for row in costs:
        if not all(isinstance(row.get(k),int) and row[k]>=0 for k in fields) or row.get('frames',0)==0 or row.get('log_cost_one_window_delayed')!=1:
            issues.append({'line':row['line'],'error':'invalid frame phase record'});continue
        live=next((r for r in reversed(rows) if r['line']<row['line'] and r['raw'].startswith('[LIVE]')),None)
        if not live or live.get('render_us')!=row['render_avg_us']:issues.append({'line':row['line'],'error':'phase record not paired with LIVE render interval'})
        valid.append(row)
    checks={'actual_user_art_control':bool(controls),'actual_frame_phase_samples':bool(valid)}
    art_status='FAIL' if issues else 'PASS' if all(checks.values()) else 'PENDING'
    return {**base,'evidence_kind':'FIVE_USER_ART_FRESH_BOARD_CAPTURE',
            'status':'FAIL' if base['status']=='FAIL' or art_status=='FAIL' else 'PASS' if base['status']==art_status=='PASS' else 'PENDING',
            'art':{'status':art_status,'checks':checks,'controls':controls,'issues':issues,
                   'phase_samples':len(valid),'phase_maxima':{k:max((r[k] for r in valid),default=0) for k in fields if k not in ('frames','steps')},
                   'work_over_60hz_period_observed':sum(r['work_over_period'] for r in valid),
                   'limits':'logic averages are per step; other averages per frame. Logging cost is explicitly one reporting window delayed; submit waiting is not a CPU deadline proof.'}}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',required=True,type=Path);p.add_argument('--output',type=Path);p.add_argument('--require-strict-60',action='store_true');a=p.parse_args()
    result=analyze(a.log);text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if a.output:
        with a.output.open('x',encoding='utf-8') as f:f.write(text)
    print(text,end='');return 0 if result['status']=='PASS' and (not a.require_strict_60 or result['strict_60fps']['status']=='PASS') else 1
if __name__=='__main__':raise SystemExit(main())
