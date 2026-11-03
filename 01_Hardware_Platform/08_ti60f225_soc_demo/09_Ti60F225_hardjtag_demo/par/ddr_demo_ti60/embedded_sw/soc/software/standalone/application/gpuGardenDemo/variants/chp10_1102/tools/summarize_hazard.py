#!/usr/bin/env python3
"""W fresh-start gameplay audit. Missing board evidence stays PENDING."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

FIELDS=re.compile(r'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)')
HAZARD=('tick','mode','armed','forming','armed_contacts','forming_contacts','grace','respawn','player_x','player_y','lives')
CONTROL={'arm_ticks':6,'hitbox_half_px':9,'pivot':'white_core',
         'render_clock':'shared_player_and_bullets','forming':'small_dim','grace_preserved':1}

def module(name,filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    obj=importlib.util.module_from_spec(spec);spec.loader.exec_module(obj);return obj

def number(value):
    try:return int(value,16 if value.lower().startswith('0x') else 10)
    except ValueError:return value

def parse(text,select_boot=False):
    partial=bool(text and not text.endswith(('\n','\r')))
    lines=text.splitlines()
    if partial:lines=lines[:-1]
    boots=[i for i,line in enumerate(lines) if line.startswith('GARDEN Ti60 cropped port ')]
    start=boots[-1] if select_boot and boots else 0
    return [{'line':i+1,'raw':line,**{k:number(v) for k,v in FIELDS.findall(line)}}
            for i,line in enumerate(lines) if i>=start],partial,boots

def preceding(rows,record,prefix):
    return next((r for r in reversed(rows) if r['line']<record['line'] and r['raw'].startswith(prefix)),None)

def hazard_review(rows,require_control=True):
    controls=[r for r in rows if r['raw'].startswith('[HAZARD_CTRL]')]
    samples=[r for r in rows if r['raw'].startswith('[HAZARD]')]
    issues=[];pairs=[];resets=[];previous=None
    if controls and any(any(r.get(k)!=v for k,v in CONTROL.items()) for r in controls):
        issues.append({'error':'HAZARD_CTRL protocol differs','expected':CONTROL})
    for h in samples:
        if not all(isinstance(h.get(k),int) and h[k]>=0 for k in HAZARD):
            issues.append({'line':h['line'],'error':'missing/negative HAZARD integer'});continue
        if not(h['mode']<=3 and h['lives']<=3 and h['armed']+h['forming']<=192 and
               h['grace']<=180 and h['respawn']<=50 and 5000<=h['player_x']<=635000 and 5000<=h['player_y']<=520000):
            issues.append({'line':h['line'],'error':'HAZARD state bounds exceeded'})
        reward=preceding(rows,h,'[REWARD]');ordnance=preceding(rows,h,'[ORDNANCE]');live=preceding(rows,h,'[LIVE]')
        if not reward or not ordnance or any(r.get('tick')!=h['tick'] or r.get('mode')!=h['mode'] for r in (reward,ordnance)):
            issues.append({'line':h['line'],'error':'missing same-tick REWARD/ORDNANCE pair'});continue
        if not all(isinstance(reward.get(k),int) and reward[k]>=0 for k in ('hits','used','bombs')):
            issues.append({'line':h['line'],'error':'malformed paired REWARD counters'});continue
        if ordnance.get('lives')!=h['lives']:
            issues.append({'line':h['line'],'error':'same-tick lives mismatch'})
        if live and (previous is None or live['line']>previous['hazard']['line']) and live.get('eb')!=h['armed']+h['forming']:
            issues.append({'line':h['line'],'error':'LIVE eb differs from armed+forming'})
        current={'hazard':h,'reward':reward,'ordnance':ordnance,'live_line':live['line'] if live else None}
        if previous:
            old=previous['hazard']
            if h['tick']<old['tick']:
                if h['mode']!=0:issues.append({'line':h['line'],'error':'tick reversal outside restart'})
                resets.append({'line':h['line'],'tick':h['tick'],'contacts_zero_observed':h['armed_contacts']==h['forming_contacts']==0,
                               'reward_used':reward.get('used'),'bombs':reward.get('bombs'),'hits':reward.get('hits')})
            else:
                for key in ('armed_contacts','forming_contacts'):
                    if h[key]<old[key]:issues.append({'line':h['line'],'error':'contact counter decreased within run','field':key})
                if h['armed_contacts']>old['armed_contacts'] and reward.get('hits',-1)<=previous['reward'].get('hits',-1):
                    issues.append({'line':h['line'],'error':'unprotected armed contact increased without actual damage count'})
        pairs.append(current);previous=current
    valid_control=bool(controls) and all(all(r.get(k)==v for k,v in CONTROL.items()) for r in controls)
    complete=bool(pairs) and (not require_control or valid_control)
    return {'status':'FAIL' if issues else 'PASS' if complete else 'PENDING',
            'control_status':'PASS' if valid_control else 'FAIL' if controls else 'NOT_RERUN_ATTACH_ONLY' if not require_control else 'PENDING',
            'controls':controls,'samples':len(samples),'paired_samples':len(pairs),'issues':issues,
            'first':pairs[0] if pairs else None,'last':pairs[-1] if pairs else None,
            'contact_maxima':{k:max((h[k] for h in samples if isinstance(h.get(k),int)),default=0) for k in ('armed_contacts','forming_contacts')},
            'restarts':resets,'restart_zero_observed':bool(resets) and all(r['contacts_zero_observed'] for r in resets),
            'pairs':pairs,
            'scope':'Counters describe sampled real device state; zero auto-demo hits are valid. Contacts count bullet-tick endpoints, not one independent life loss each.'}

def analyze(path):
    raw=path.read_bytes();rows,partial,boots=parse(raw.decode('utf-8-sig',errors='replace'),True)
    base=module('hazard_ordnance','summarize_ordnance.py').analyze(path)
    hazard=hazard_review(rows)
    # A2s report can first observe the new run after its legal tick55 throw.
    # That stock1/used1 sample must not be treated as a failed stock reset.
    inherited=module('hazard_reward_legacy','summarize_reward.py').analyze([path])
    restarts=[]
    for old in base['restarts']:
        current=next((r for r in rows if r['line']==old['line']),{})
        shot=next((r for r in rows if r['raw'].startswith('[ORDNANCE]') and r.get('tick')==current.get('tick') and r['line']>current.get('line',0)),{})
        valid=bool(current.get('mode')==0 and isinstance(current.get('bombs'),int) and isinstance(current.get('used'),int) and
                   0<=current['used']<=2 and current['bombs']+current['used']==2 and current['used']==shot.get('launched') and
                   current.get('cleared')==0 and current.get('best',-1)>=max(old.get('previous_best',0),old.get('previous_score',0)))
        restarts.append({**old,'retained_best_and_finite_inventory':valid,
            'observed_bombs':current.get('bombs'),'observed_used':current.get('used'),
            'immediate_stock2_observed':current.get('bombs')==2 and current.get('used')==0,
            'scope':'First periodic new-run sample; finite ledger after any already launched real ordnance, not exact reset-frame inventory.'})
    resolved={r['line'] for r in restarts if r['retained_best_and_finite_inventory']}
    legacy_reward_issues=inherited['rewards']['issues']
    unexpected=[r for r in legacy_reward_issues if not(r.get('line') in resolved and r.get('error')=='restart failed retained-best/replenishment')]
    underlying=bool(base['issues'] or base['inherited_non_atlas_issues'] or unexpected or inherited['fatal_records'] or
                    base['live']['counter_reversals'] or any(base['live']['counter_maxima'].values()) or base['display']['status']=='FAIL' or
                    (base['atlas_validation']['records'] and not base['atlas_validation']['valid']))
    revised=dict(base['checks'])
    revised['fresh_variant_startup']=bool(boots and base['controls'] and all(base['startup'].values()) and not underlying)
    revised['restart_best_and_stock_retained']=bool(restarts) and all(r['retained_best_and_finite_inventory'] for r in restarts)
    base={**base,'legacy_summary_status':base['status'],'legacy_checks':base['checks'],'checks':revised,'restarts':restarts,
          'legacy_reward_restart_issues':legacy_reward_issues,'unexpected_reward_issues':unexpected,
          'status':'FAIL' if underlying else 'PASS' if all(revised.values()) else 'PENDING'}
    checks={'hazard_control':hazard['control_status']=='PASS','same_tick_hazard_samples':hazard['status']=='PASS',
            'runtime_live_samples':base['live']['samples']>0,'runtime_display_counters':base['display']['status']=='PASS',
            'restart_contact_counters_zero_observed':hazard['restart_zero_observed']}
    status='FAIL' if base['status']=='FAIL' or hazard['status']=='FAIL' else 'PASS' if base['status']=='PASS' and all(checks.values()) else 'PENDING'
    return {'schema':1,'evidence_kind':'HAZARD_FRESH_BOARD_CAPTURE','status':status,
            'input':str(path.resolve()),'raw_sha256':hashlib.sha256(raw).hexdigest().upper(),
            'selected_boot_line':boots[-1]+1 if boots else None,'partial_tail_excluded':partial,
            'checks':checks,'hazard':hazard,'ordnance':base,'strict_60fps':base['strict_60fps'],
            'new600s_run':False,'visual_acceptance':'PENDING_USER_OBSERVATION',
            'limits':['Auto demonstration need not take damage; actual enemy contact is tested by an independent attach capture.',
                      'This is not a600-second or every-frame deadline result. Sparse reports cannot prove every possible collision.',
                      'Protocol declarations alone do not prove atlas pivots or interpolation; independent pixel tests and board observation remain required.']}

def self_test():
    def sample(armed=2,forming=0,hits=0,contacts=0,tick=80):
        return f'[LIVE] eb={armed+forming}\n[REWARD] tick={tick} mode=0 hits={hits} used=0 bombs=2\n[ORDNANCE] tick={tick} mode=0 lives=3\n[HAZARD] tick={tick} mode=0 armed={armed} forming={forming} armed_contacts={contacts} forming_contacts=0 grace=0 respawn=0 player_x=320000 player_y=410000 lives=3\n'
    ctrl='[HAZARD_CTRL] arm_ticks=6 hitbox_half_px=9 pivot=white_core render_clock=shared_player_and_bullets forming=small_dim grace_preserved=1\n'
    tests=[]
    def check(name,condition):
        tests.append(name)
        if not condition:raise AssertionError(name)
    def audit(text,required=True):return hazard_review(parse(text)[0],required)
    check('missing is pending',audit('')['status']=='PENDING')
    check('zero auto hits valid',audit(ctrl+sample())['status']=='PASS')
    check('wrong arm declaration fails',audit(ctrl.replace('arm_ticks=6','arm_ticks=80')+sample())['status']=='FAIL')
    check('same tick mandatory',audit(ctrl+sample().replace('[ORDNANCE] tick=80','[ORDNANCE] tick=79'))['status']=='FAIL')
    check('LIVE current count pair',audit(ctrl+sample().replace('[LIVE] eb=2','[LIVE] eb=3'))['status']=='FAIL')
    check('contact needs real hit delta',audit(ctrl+sample()+sample(tick=160,contacts=1))['status']=='FAIL')
    check('real contact and hit advance',audit(ctrl+sample()+sample(tick=160,contacts=1,hits=1))['status']=='PASS')
    check('counter reversal fails',audit(ctrl+sample(contacts=1,hits=1)+sample(tick=160))['status']=='FAIL')
    check('restart reset recognized',audit(ctrl+sample(tick=1000,contacts=2,hits=2)+sample(tick=40))['restart_zero_observed'])
    check('attach declaration not invented',audit(sample(),False)['control_status']=='NOT_RERUN_ATTACH_ONLY')
    check('malformed contact returns FAIL',audit(ctrl+sample()+sample(tick=160).replace('armed_contacts=0','armed_contacts=oops'))['status']=='FAIL')
    return {'evidence_kind':'SYNTHETIC_PARSER_TEST_ONLY','status':'PASS','checks':tests}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path);p.add_argument('--output',type=Path)
    p.add_argument('--require-strict-60',action='store_true');p.add_argument('--self-test',action='store_true');args=p.parse_args()
    result=self_test() if args.self_test else analyze(args.log) if args.log else {'status':'PENDING','reason':'No actual W UART log provided','new600s_run':False}
    output=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if args.output:
        with args.output.open('x',encoding='utf-8') as stream:stream.write(output)
    print(output,end='')
    return 0 if result['status']=='PASS' and (not args.require_strict_60 or result.get('strict_60fps',{}).get('status')=='PASS') else 1
if __name__=='__main__':raise SystemExit(main())
