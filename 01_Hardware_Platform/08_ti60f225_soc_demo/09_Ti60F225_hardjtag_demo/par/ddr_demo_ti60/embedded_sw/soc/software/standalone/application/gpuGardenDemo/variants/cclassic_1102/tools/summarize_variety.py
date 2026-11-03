#!/usr/bin/env python3
"""Fresh board audit for nine new bullet materials, bounded random schedules and frame phases."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path

def module(name,file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def analyze(path):
    base=module('cruiser_sortie','summarize_sortie.py').analyze(path)
    h=module('cruiser_records','summarize_hazard.py');rows,partial,boots=h.parse(path.read_text(encoding='utf-8-sig',errors='replace'),True)
    controls=[r for r in rows if r['raw'].startswith('[ART_CTRL]')]
    generation=json.loads((Path(__file__).resolve().parents[1]/'assets/bullet_scale_generation.json').read_text(encoding='utf-8-sig'))
    expected={'materials':13,'bullet_styles':9,'enemy_projectiles_all_new':1,'sprite_count':859,'atlas_bytes':generation['atlas_bytes'],'player_bank':1,'actor_hp':'above','damage_numbers':1,
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
    random=audit_random(rows)
    return {**base,'random_schedule':random,'evidence_kind':'NINE_BULLET_MATERIALS_RANDOM_SCHEDULE_FRESH_CAPTURE',
            'status':'FAIL' if base['status']=='FAIL' or art_status=='FAIL' or random['status']=='FAIL' else 'PASS' if base['status']==art_status==random['status']=='PASS' else 'PENDING',
            'art':{'status':art_status,'checks':checks,'controls':controls,'issues':issues,
                   'phase_samples':len(valid),'phase_maxima':{k:max((r[k] for r in valid),default=0) for k in fields if k not in ('frames','steps')},
                   'work_over_60hz_period_observed':sum(r['work_over_period'] for r in valid),
                   'limits':'logic averages are per step; other averages per frame. Logging cost is explicitly one reporting window delayed; submit waiting is not a CPU deadline proof.'}}

def seed_successor(seed):
    seed ^= (seed << 13) & 0xffffffff
    seed ^= seed >> 17
    seed ^= (seed << 5) & 0xffffffff
    return seed & 0xffffffff


def audit_random(rows):
    controls=[r for r in rows if r['raw'].startswith('[RANDOM_CTRL]')]
    seeds=[r for r in rows if r['raw'].startswith('[RUN_SEED]')]
    patterns=[r for r in rows if r['raw'].startswith('[PATTERN]')]
    issues=[];runs={};seed_lines={};trains={};last_patterns={};complete=True
    expected={'enabled':1,'boss_shuffle':3,'repeat_adjacent':0,'timing_bounded':1,'warning_preserved':1}
    for row in controls:
        if any(row.get(k)!=v for k,v in expected.items()) or row.get('initial_source') not in ('fixed_override','CLINT_mix'):
            issues.append({'line':row['line'],'error':'random protocol mismatch'})
        if not isinstance(row.get('seed'),int) or not 0<row['seed']<=0xffffffff:
            issues.append({'line':row['line'],'error':'invalid initial control seed'})
    previous_seed=None
    for n,row in enumerate(seeds,1):
        if row.get('round')!=n or not isinstance(row.get('seed'),int) or not 0<row['seed']<=0xffffffff or not isinstance(row.get('tick'),int) or row['tick']<0:
            issues.append({'line':row['line'],'error':'invalid round/seed'});continue
        if row['seed'] in runs.values():issues.append({'line':row['line'],'error':'round seed repeated'})
        if previous_seed is not None and row['seed']!=seed_successor(previous_seed):
            issues.append({'line':row['line'],'error':'round seed did not follow core successor'})
        runs[n]=row['seed'];seed_lines[n]=row['line'];previous_seed=row['seed']
    if runs and any(row.get('seed')!=runs.get(1) for row in controls):
        issues.append({'error':'control seed disagrees with first round'})
    for row in patterns:
        fields=('round','seed','tick','attack','state','band','charge','rest','gap','remaining','windows','train','laser')
        if not all(isinstance(row.get(k),int) for k in fields):issues.append({'line':row['line'],'error':'missing pattern field'});continue
        band=row['band'];attack=row['attack'];gap0=6-band if attack==2 else 12-2*band
        if not(band in (0,1,2) and attack in (1,2,3) and row['state']==1 and
               20-4*band<=row['charge']<=28-4*band and 80-16*band<=row['rest']<=104-16*band and
               gap0<=row['gap']<=gap0+(2 if attack==2 else 4) and
               0<row['remaining']<={1:1,2:6,3:2}.get(attack,0) and row['train']>0 and
               row['windows']>0 and row['tick']>=0 and 0<=row['laser']<=3 and
               runs.get(row['round'])==row['seed'] and seed_lines.get(row['round'],row['line'])<row['line']):
            issues.append({'line':row['line'],'error':'pattern outside announced bounds'});continue
        prior_window=last_patterns.get(row['round'])
        if prior_window:
            if row['tick']<prior_window['tick'] or row['windows']<=prior_window['windows'] or row['train']<prior_window['train']:
                issues.append({'line':row['line'],'error':'pattern chronology reversed or duplicated'})
            if row['windows']!=prior_window['windows']+1:complete=False
        elif row['windows']!=1:complete=False
        key=(row['round'],row['train']);prior=trains.get(key)
        if prior and any(prior[k]!=row[k] for k in ('attack','band','charge','rest','gap')):
            issues.append({'line':row['line'],'error':'capacity re-charge changed announced train'})
        if prior_window and prior_window['train']==row['train'] and row['remaining']>prior_window['remaining']:
            issues.append({'line':row['line'],'error':'capacity re-charge replenished consumed pulses'})
        trains.setdefault(key,row);last_patterns[row['round']]=row
    sequences={};indexed={}
    for (run,train),row in sorted(trains.items()):
        sequences.setdefault(run,[]).append(row['attack']);indexed.setdefault(run,{})[train]=row['attack']
    for run,seq in indexed.items():
        if sorted(seq)!=list(range(1,max(seq)+1)):complete=False
        for train,attack in seq.items():
            if seq.get(train+1)==attack:issues.append({'round':run,'error':'adjacent full trains repeated attack'})
        for start in range(1,max(seq)-1,3):
            if all(i in seq for i in range(start,start+3)) and {seq[i] for i in range(start,start+3)}!={1,2,3}:
                issues.append({'round':run,'error':'completed bag did not contain all three attacks'})
    checks={'random_control':bool(controls),'fresh_seed_for_at_least_two_rounds':len(runs)>=2,
            'actual_patterns_in_at_least_two_rounds':sum(bool(s) for s in sequences.values())>=2,
            'three_boss_attack_types_observed':any(set(seq)=={1,2,3} for seq in sequences.values()),
            'contiguous_train_and_charge_observations':bool(trains) and complete}
    return {'status':'FAIL' if issues else 'PASS' if all(checks.values()) else 'PENDING',
            'checks':checks,'issues':issues,'round_seeds':runs,'actual_attack_sequences':sequences,
            'charge_windows_observed':len(patterns),'unique_trains':len(trains),
            'limits':'Sparse actual charge transitions; gaps remain PENDING and are never compacted into a complete shuffle bag. Host fixtures independently check enemy timing/type randomness, seed determinism and capacity invariants. Different rounds may randomly share a short attack prefix.'}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',required=True,type=Path);p.add_argument('--output',type=Path);p.add_argument('--require-strict-60',action='store_true');a=p.parse_args()
    result=analyze(a.log);text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if a.output:
        with a.output.open('x',encoding='utf-8') as f:f.write(text)
    print(text,end='');return 0 if result['status']=='PASS' and (not a.require_strict_60 or result['strict_60fps']['status']=='PASS') else 1
if __name__=='__main__':raise SystemExit(main())
