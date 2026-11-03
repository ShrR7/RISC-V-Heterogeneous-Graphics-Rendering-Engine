#!/usr/bin/env python3
"""Offline W real-contact audit. Attach-only coverage; no hardware access."""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
_spec=importlib.util.spec_from_file_location('contact_hazard_parser',Path(__file__).with_name('summarize_hazard.py'))
hazard=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(hazard)
FIELDS=hazard.FIELDS
FAULTS=("errors","swap_timeout","front_reject","atlas_bounds","missed","log_dropped","log_truncated")
def sha(raw):
    return hashlib.sha256(raw).hexdigest().upper()


def number(token):
    try:
        return int(token, 16 if token.lower().startswith("0x") else 10)
    except ValueError:
        return token


def complete_lines(text):
    partial = bool(text and not text.endswith(("\n", "\r")))
    lines = text.splitlines()
    return (lines[:-1] if partial else lines), partial


def parse_rows(text):
    lines, partial = complete_lines(text)
    return [{"line": i + 1, "raw": line, **{k: number(v) for k, v in FIELDS.findall(line)}} for i, line in enumerate(lines)], partial


def combined_status(items):
    return "FAIL" if "FAIL" in items else "PENDING" if not items or "PENDING" in items else "PASS"


class MemoryLog:
    def __init__(self, text):
        self.data = text.encode("utf-8")

    def read_bytes(self):
        return self.data

    def resolve(self):
        return "<attach-capture-in-memory>"


def display_review(text):
    """Use paired windows and first-to-last lifecycle deltas, not runtime=0.

    An attach capture does not include the device's GAME_START report. Its
    first total/window discrepancy is prior capture history, not lost data in
    this capture. Later discrepancies remain real coverage gaps.
    """
    try:
        spec = importlib.util.spec_from_file_location("bomb_controls_display7", Path(__file__).with_name("analyze_display7.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        native = module.analyze([MemoryLog(text)])
    except (OSError, AttributeError, KeyError, ValueError, TypeError) as error:
        return {"status": "FAIL", "parse_error": str(error), "runtime_counts_without_GAME_START": None}
    pairs = native["pairs"]
    parsed_rows, _ = parse_rows(text)
    boundary_incomplete, internal_incomplete = [], []
    first_line = min(pairs[0][part]["line"] for part in ("main", "aux")) if pairs else None
    last_line = max(pairs[-1][part]["line"] for part in ("main", "aux")) if pairs else None
    for rid in native["incomplete_rids"]:
        lines = [r["line"] for r in parsed_rows if r.get("rid") == rid and r["raw"].startswith(("[DISPLAY7]", "[DISPLAY7_AUX]"))]
        record = {"rid": rid, "lines": lines}
        if lines and first_line is not None and (max(lines) < first_line or min(lines) > last_line):
            boundary_incomplete.append(record)
        else:
            internal_incomplete.append(record)
    deltas, reversals = [], []
    for before, after in zip(pairs, pairs[1:]):
        delta = [a - b for a, b in zip(after["aux"]["total"], before["aux"]["total"])]
        sat_delta = [a - b for a, b in zip(after["aux"]["total_sat"], before["aux"]["total_sat"])]
        record = {"previous_rid": before["rid"], "rid": after["rid"], "counts": delta, "saturation_incidence": sat_delta}
        deltas.append(record)
        if any(n < 0 for n in delta + sat_delta):
            reversals.append(record)
    total_delta = [a - b for a, b in zip(pairs[-1]["aux"]["total"], pairs[0]["aux"]["total"])] if len(pairs) >= 2 else None
    sat_delta = [a - b for a, b in zip(pairs[-1]["aux"]["total_sat"], pairs[0]["aux"]["total_sat"])] if len(pairs) >= 2 else None
    measured = pairs[1:]
    window_sum = [sum(p["main"]["sum"][i] for p in measured) for i in range(5)]
    initial_history = [g for g in native["counter_total_window_gaps"] if g.get("prior_capture_history")]
    gaps = [g for g in native["counter_total_window_gaps"] if not g.get("prior_capture_history")]
    overflow = [{"rid": p["rid"], "window": p["main"]["ov"], "total": p["aux"]["tov"]} for p in measured if p["main"]["ov"] or p["aux"]["tov"]]
    lower = [native["counter_order"][i] for i in range(5) if (sat_delta is not None and sat_delta[i] > 0) or any(p["main"]["ov"] & ((1 << i) | (1 << (5+i))) or p["aux"]["tov"] & ((1 << i) | (1 << (5+i))) for p in measured)]
    px = [total_delta[i] for i in (0, 1, 4)] if total_delta is not None else None
    refused_delta = pairs[-1]["aux"]["refused"] - pairs[0]["aux"]["refused"] if len(pairs) >= 2 else None
    malformed = bool(native["issues"] or reversals or native["status"] == "FORMAT_FAIL")
    failures = bool(malformed or (px is not None and any(n > 0 for n in px)) or (refused_delta is not None and refused_delta != 0))
    complete = bool(len(pairs) >= 2 and not gaps and not internal_incomplete and not any(k in lower for k in ("PX", "PART", "FULL")))
    return {
        "status": "FAIL" if failures else "PASS" if complete else "PENDING",
        "native_format_status": native["status"], "counter_order": list(native["counter_order"]),
        "complete_pairs": len(pairs), "incomplete_rids": native["incomplete_rids"],
        "boundary_incomplete_pairs_excluded_from_interval": boundary_incomplete,
        "internal_incomplete_pairs": internal_incomplete,
        "issues": native["issues"], "initial_prior_capture_history": initial_history,
        "capture_internal_window_total_gaps": gaps, "total_reversals_not_coerced_to_zero": reversals,
        "first_pair": pairs[0] if pairs else None, "last_pair": pairs[-1] if pairs else None,
        "all_reported_window_counts_including_first": native["window_observed_counts"],
        "capture_first_to_last_total_delta": total_delta,
        "capture_first_to_last_window_counts_excluding_first": window_sum if measured else None,
        "capture_first_to_last_px_part_full": px,
        "capture_saturation_delta": sat_delta, "lower_bound_fields": lower,
        "overflow_pairs": overflow, "refused_delta": refused_delta, "total_deltas": deltas,
        "max_sample_gap_ms": max((p["gap_bound_ms"] for p in measured), default=None),
        "capture_counter_duration_seconds": (pairs[-1]["main"]["t"][1] - pairs[0]["main"]["t"][1]) / 100000000 if len(pairs) >= 2 else None,
        "GAME_START_report_observed": native["GAME_START_boundary_verified"],
        "runtime_counts_without_GAME_START": native["runtime_window_observed_counts"] if native["GAME_START_boundary_verified"] else None,
        "AR_R_wait_is_not_a_display_fault": True,
        "scope": "Attach capture: lifecycle total difference from first complete pair to last complete pair. The first reported window may precede attachment and is excluded from this interval; no GAME_START means no boot-to-now runtime claim.",
        "limits": ["Saturation is a lower bound; AR/R waits are not failed pixels.", "Sequential register samples and host timestamps are not video edges or exact pixel displacement.", "Pairing/zero counters do not certify visual appearance or unobserved tails."],
    }


def host_review(text,rows):
    if text is None:return {'status':'PENDING','reason':'HOST capture not supplied'}
    lines,partial=complete_lines(text);events=[];issues=[];previous=-1.0
    for i,line in enumerate(lines,1):
        if not line.strip():continue
        try:
            e=json.loads(line)
            if not isinstance(e,dict):raise ValueError('HOST event must be object')
            if not isinstance(e.get('data'),dict):raise ValueError('HOST event data must be object')
            seconds=e.get('seconds')
            if not isinstance(seconds,(int,float)) or isinstance(seconds,bool) or not math.isfinite(seconds) or seconds<previous:
                raise ValueError('invalid/regressing monotonic seconds')
            stamp=datetime.fromisoformat(e.get('utc','').replace('Z','+00:00'))
            if stamp.tzinfo is None:raise ValueError('UTC timestamp lacks timezone')
            previous=seconds;events.append({**e,'host_line':i})
        except (ValueError,TypeError,AttributeError) as exc:issues.append({'host_line':i,'error':str(exc)})
    sends=[e for e in events if e.get('event')=='send'];acks=[e for e in events if e.get('event')=='ack']
    keys=[e.get('data',{}).get('key') for e in sends]
    if keys and keys!=['r','t','r']:issues.append({'error':'unexpected HOST key sequence','keys':keys})
    for send,ack in zip(sends,acks):
        data=ack.get('data',{});line=data.get('line');fields=data.get('fields',{})
        if not isinstance(fields,dict):issues.append({'host_line':ack['host_line'],'error':'HOST ack fields must be object'});continue
        device=next((r for r in rows if r['line']==line and r['raw'].startswith('[INPUT_STATE]')),None)
        if (device is None or data.get('kind')!='INPUT_STATE' or fields.get('key')!=send.get('data',{}).get('key') or
            ack['seconds']<send['seconds'] or not isinstance(send.get('data',{}).get('after_raw_line'),int) or
            line<=send['data']['after_raw_line'] or any(device.get(k)!=v for k,v in fields.items())):
            issues.append({'host_line':ack['host_line'],'error':'HOST acknowledgement does not bind actual later device INPUT_STATE'})
        if send.get('data',{}).get('single_raw_character') is not True:
            issues.append({'host_line':send['host_line'],'error':'single-byte write declaration missing'})
    failures=[e for e in events if e.get('event')=='failure']
    if failures:issues.append({'error':'HOST recorded failure','records':failures})
    starts=[e for e in events if e.get('event')=='start'];restored=[e for e in events if e.get('event')=='restored']
    complete=bool(keys==['r','t','r'] and len(acks)==3 and starts and restored and
                  starts[0].get('data',{}).get('no_reload') is True and restored[-1].get('data',{}).get('verified') is True)
    return {'status':'FAIL' if issues else 'PASS' if complete else 'PENDING','issues':issues,
            'events':len(events),'send_keys':keys,'acknowledgements':len(acks),'partial_tail_excluded':partial,
            'start':starts[0] if starts else None,'restored':restored[-1] if restored else None,
            'scope':'HOST times and writes document control sequence; device fields below independently establish effects.'}


def receipt_review(receipt,raw,host):
    if receipt is None:return {'status':'PENDING','reason':'Receipt not supplied','identity':'NOT_DEVICE_READBACK'}
    if not isinstance(receipt,dict):return {'status':'FAIL','issues':['Receipt must be object'],'identity':'NOT_DEVICE_READBACK'}
    issues=[]
    for field,content in (('raw_sha256',raw),('host_sha256',host)):
        if content is None or receipt.get(field,'').upper()!=sha(content):issues.append({'field':field,'error':'capture hash mismatch/missing'})
    expected={'status':'DEVICE_CONTACT_STEPS_COMPLETE_AUDIT_REQUIRED','restored':True,'serial_released':True,
              'FPGA_reconfigured':False,'Flash_written':False,'real_contact_proof_observed':True}
    for key,value in expected.items():
        if receipt.get(key)!=value:issues.append({'field':key,'error':'receipt incomplete or contradictory'})
    if receipt.get('failure'):issues.append({'error':'receipt records failure','value':receipt['failure']})
    elf=receipt.get('elf_sha256','')
    if not isinstance(elf,str) or not re.fullmatch('[0-9A-Fa-f]{64}',elf):issues.append({'error':'invalid ELF provenance hash'})
    return {'status':'FAIL' if issues else 'PASS','issues':issues,'claims':receipt,
            'identity':'HOST_PROVENANCE_NOT_DEVICE_READBACK','startup_probes':'NOT_RERUN_ATTACH_ONLY'}


def analyze_text(text,host_text=None,receipt=None,synthetic=False,raw_bytes=None,host_bytes=None):
    rows,partial,boots=hazard.parse(text);issues=[];inputs=[r for r in rows if r['raw'].startswith('[INPUT_STATE]')]
    fatal=[r for r in rows if re.search(r'\bFAIL\b|\bTRAP\b',r['raw'])]
    if boots:issues.append({'error':'unexpected fresh boot inside attach-only control capture'})
    observed=[r.get('key') for r in inputs]
    if inputs and observed!=['r','t','r']:issues.append({'error':'unexpected device INPUT_STATE sequence','keys':observed})
    setup=inputs[0] if len(inputs)>=1 else None;manual=inputs[1] if len(inputs)>=2 else None;restored=inputs[2] if len(inputs)>=3 else None
    expected={'mode':0,'paused':0,'demo':1,'cpu':0,'alpha':1,'panel_alpha':16,'slow':0,'lives':3,'bombs':2}
    def retry_ok(r):return bool(r and isinstance(r.get('tick'),int) and 0<=r['tick']<=7 and all(r.get(k)==v for k,v in expected.items()))
    for name,r in (('initial_retry',setup),('restored_retry',restored)):
        if r and not retry_ok(r):issues.append({'line':r['line'],'error':name+' state contradicts GPU/Alpha16 default'})
    manual_ok=bool(manual and manual.get('demo')==0 and manual.get('paused')==0 and
                   manual.get('cpu')==0 and manual.get('alpha')==1 and manual.get('panel_alpha')==16 and manual.get('slow')==0)
    if manual and not manual_ok:issues.append({'line':manual['line'],'error':'manual phase did not disable demo in expected mode'})
    selected=[r for r in rows if setup and r['line']>=setup['line']]
    hazards=hazard.hazard_review(selected,require_control=False)
    if hazards['status']=='FAIL':issues.extend(hazards['issues'])
    manual_pairs=[p for p in hazards['pairs'] if manual and p['hazard']['line']>manual['line'] and
                  (restored is None or p['hazard']['line']<restored['line'])]
    proof=[]
    for p in hazards['pairs']:
        reward,ordnance=p['reward'],p['ordnance']
        required=('bombs','used','cleared','hits','best')
        if not all(isinstance(reward.get(k),int) and reward[k]>=0 for k in required):
            issues.append({'line':reward['line'],'error':'missing/negative finite-inventory reward field'});continue
        if reward['bombs']+reward['used']!=2 or reward['used']!=ordnance.get('launched') or reward['cleared']!=0:
            issues.append({'line':reward['line'],'error':'offensive bomb stock/use/no-clear ledger inconsistent'})
    for p in manual_pairs:
        r,o,h=p['reward'],p['ordnance'],p['hazard']
        if not isinstance(o.get('swept_hits'),int) or not isinstance(o.get('heals_restored'),int):
            issues.append({'line':o['line'],'error':'missing damage/heal observables'});continue
        if h['lives']!=3-r.get('hits',0)+o['heals_restored']:
            issues.append({'line':h['line'],'error':'real life/hit/heal ledger inconsistent'})
        if r.get('hits',0)>=2 and (h['armed_contacts']>=1 or o['swept_hits']>=1):proof.append(p)
    after=[p for p in hazards['pairs'] if restored and p['hazard']['line']>restored['line']]
    restored_samples=bool(retry_ok(restored) and after and all(isinstance(p['reward'].get('used'),int) and
        0<=p['reward']['used']<=2 and p['reward'].get('bombs',-1)+p['reward']['used']==2 and
        p['reward']['used']==p['ordnance'].get('launched') and p['reward'].get('hits')==0 and
        p['hazard']['armed_contacts']==0 and p['hazard']['forming_contacts']==0 for p in after))
    best_before=max((p['reward']['best'] for p in manual_pairs if isinstance(p['reward'].get('best'),int)),default=None)
    best_ok=bool(after and best_before is not None and all(p['reward'].get('best',-1)>=best_before for p in after))
    live=[r for r in rows if r['raw'].startswith('[LIVE]')];valid=[]
    for r in live:
        if not all(isinstance(r.get(k),int) and r[k]>=0 for k in FAULTS+('fps10','frame','device_ms')):
            issues.append({'line':r['line'],'error':'malformed LIVE fields'});continue
        valid.append(r)
    baseline={k:valid[0][k] for k in FAULTS} if valid else {}
    deltas={k:valid[-1][k]-valid[0][k] for k in FAULTS} if len(valid)>=2 else {}
    reversals=[{'line':b['line'],'field':k,'before':a[k],'after':b[k]} for a,b in zip(valid,valid[1:]) for k in FAULTS if b[k]<a[k]]
    guards={'status':'FAIL' if reversals or any(v!=0 for v in deltas.values()) else 'PASS' if len(valid)>=2 else 'PENDING',
            'samples':len(valid),'first_counter_baseline':baseline,'historic_nonzero_baseline':{k:v for k,v in baseline.items() if v},
            'last_minus_first':deltas,'counter_reversals_not_coerced_to_zero':reversals,
            'first':valid[0] if valid else None,'last':valid[-1] if valid else None,
            'scope':'Attach cumulative counters, first-to-last LIVE only; initial nonzero history is not a new fault.'}
    fps=[r['fps10'] for r in valid if r.get('backend')=='GPU']
    strict={'status':'PASS' if fps and min(fps)>=600 else 'FAIL' if fps else 'PENDING','threshold_fps10':600,
            'observed_GPU_interval_fps_min':min(fps)/10 if fps else None,'observed_GPU_interval_fps_max':max(fps)/10 if fps else None,
            'samples':len(fps),'scope':'Sampled GPU LIVE averages only; not per-frame latency or600second acceptance'}
    checks={'initial_default_retry':retry_ok(setup),'manual_mode':manual_ok,'device_two_hits_and_same_tick_armed_contact_or_sweep':bool(proof),
            'restored_default_retry':retry_ok(restored),'restored_stock_hits_contacts_reset':restored_samples,'best_retained':best_ok}
    effects='FAIL' if issues or fatal else 'PASS' if all(checks.values()) else 'PENDING'
    display=display_review(text);host=host_review(host_text,rows)
    provenance=receipt_review(receipt,raw_bytes if raw_bytes is not None else text.encode(),
                             host_bytes if host_bytes is not None else host_text.encode() if host_text is not None else None)
    combined=combined_status([effects,guards['status'],display['status'],host['status'],provenance['status']])
    return {'schema':1,'evidence_kind':'SYNTHETIC_PARSER_TEST_ONLY' if synthetic else 'HAZARD_ATTACH_REAL_CONTACT_AUDIT',
            'status':combined,'device_contact_status':effects,'checks':checks,'device_inputs':inputs,
            'proof_samples':proof,'manual_samples':len(manual_pairs),'restored_samples':after,
            'hazard':hazards,'live_guards':guards,'display':display,'strict_60fps':strict,'host':host,'receipt':provenance,
            'issues':issues,'fatal_records':fatal,'partial_tail_excluded':partial,'new600s_run':False,
            'startup':{'status':'NOT_RERUN_ATTACH_ONLY','boot_lines_observed':boots,
                       'Alpha35_precision40_CRC_ColorKey':'NOT_RETESTED','identity':'Receipt is inherited HOST provenance, not device image readback'},
            'visual_acceptance':'PENDING_USER_OBSERVATION',
            'limits':['Two accumulated real damage events plus same-report armed endpoint contact or sweep evidence do not identify every causal frame or prove every collision.',
                      'The restore INPUT_STATE immediately verifies stock2; later periodic reports may already show a legal automatic throw after tick55, so only the finite ledger and reset hit/contact counters are required there.',
                      'HOST proof markers are not accepted as device damage. Sparse RAW device reports and real life/hit ledger establish effects.',
                      'Display interval excludes the first history-containing pair, incomplete capture edges and unobserved tails; AR/R stalls remain informational.']}


def self_test():
    lines=[]
    def display(rid,total):
        end=rid*100000000;start=end-10
        lines.append(f'[DISPLAY7] phase=SECOND present=1 rid={rid} frame={rid*60} tick=100 mode=0 backend=GPU alpha=1 panel=16 ctrl=4 q=1024 qor=1024 sm=0 ov=0 n=60 gb=1666667 span=10 rawstall=655360 t={start}..{end} sum=0/0/0/10/0 sat=0/0/0/0/0 max=0/0/0/10/0 last=0/0/0/10/0')
        lines.append(f'[DISPLAY7_AUX] rid={rid} total=0/0/0/{total}/0 total_sat=0/0/0/0/0 tov=0 event=0/0/0/0/0 et={start}..{end} game_start=1 game_elapsed={end-1} refused=0 ev=0 ep=0 snapshot_not_runtime=1 pred_not_D=1 clear_all_written=0')
    def ack(key,tick=0,demo=1):
        lines.append(f'[INPUT_STATE] key={key} tick={tick} mode=0 paused=0 demo={demo} cpu=0 alpha=1 panel_alpha=16 slow=0 player_x=320000 player_y=410000 lives=3 bombs=2')
    def report(tick,hits,contacts,idx,demo=0):
        life=3-hits
        lines.append(f'[LIVE] secs={idx*2} frame={idx*120} fps10=601 render_us=4000 worst_us=12000 backend=GPU eb=2 errors=7 swap_timeout=0 front_reject=0 atlas_bounds=0 missed=0 demo={demo} alpha=1 device_ms={idx*2000} log_dropped=3 log_truncated=0')
        lines.append(f'[REWARD] tick={tick} mode=0 phase=0 bombs=2 used=0 cleared=0 graze=0 cores=0 spawned=0 lost=0 score=0 best=2041 run_ticks={tick} hits={hits}')
        lines.append(f'[ORDNANCE] tick={tick} mode=0 launched=0 impacts=0 targets=0 bomb_damage=0 heals_spawned=0 heals_collected=0 heals_restored=0 laser_warnings=0 laser_shots=0 laser_hits=0 swept_hits=0 flight=0 blast=0 laser=0 lives={life}')
        lines.append(f'[HAZARD] tick={tick} mode=0 armed=2 forming=0 armed_contacts={contacts} forming_contacts=0 grace=0 respawn=0 player_x=320000 player_y=410000 lives={life}')
    display(100,1000);ack('r');ack('t',1,0);report(80,0,0,1);report(240,1,1,2);report(420,2,2,3);ack('r');report(40,0,0,4,1);display(101,1010);display(102,1020)
    text='\n'.join(lines)+'\n';rows=hazard.parse(text)[0];acks=[r for r in rows if r['raw'].startswith('[INPUT_STATE]')]
    events=[{'event':'start','seconds':0.0,'utc':'2026-10-16T08:00:00+00:00','data':{'no_reload':True,'identity':'HOST_PROVENANCE_NOT_DEVICE_READBACK','ELF_SHA256':'0'*64}}]
    for i,row in enumerate(acks):
        events.extend([{'event':'send','seconds':1.0+i*4,'utc':'2026-10-16T08:00:00+00:00','data':{'key':row['key'],'after_raw_line':row['line']-1,'single_raw_character':True}},
                       {'event':'ack','seconds':1.1+i*4,'utc':'2026-10-16T08:00:00+00:00','data':{'line':row['line'],'kind':'INPUT_STATE','fields':{k:v for k,v in row.items() if k not in ('raw','line')}}}])
    events.append({'event':'restored','seconds':12.0,'utc':'2026-10-16T08:00:00+00:00','data':{'verified':True}})
    host='\n'.join(json.dumps(e) for e in events)+'\n'
    receipt={'status':'DEVICE_CONTACT_STEPS_COMPLETE_AUDIT_REQUIRED','restored':True,'serial_released':True,'FPGA_reconfigured':False,'Flash_written':False,
             'real_contact_proof_observed':True,'raw_sha256':sha(text.encode()),'host_sha256':sha(host.encode()),'elf_sha256':'0'*64,'failure':None}
    def audit(raw=text,hlog=host,rec=None):
        binding=dict(receipt) if rec is None else rec
        if rec is None:binding['raw_sha256']=sha(raw.encode());binding['host_sha256']=sha(hlog.encode())
        return analyze_text(raw,hlog,binding,synthetic=True)
    checks=[]
    def check(name,ok):
        checks.append(name)
        if not ok:raise AssertionError(name)
    r=audit();check('complete device controls',r['status']=='PASS')
    check('historical LIVE baseline retained',r['live_guards']['historic_nonzero_baseline']=={'errors':7,'log_dropped':3})
    check('attach runtime not invented',r['display']['runtime_counts_without_GAME_START'] is None)
    check('display first history excluded',r['display']['capture_first_to_last_px_part_full']==[0,0,0] and bool(r['display']['initial_prior_capture_history']))
    check('missing proof stays pending',audit(raw=text.replace('hits=2','hits=1').replace('lives=1','lives=2').replace('armed_contacts=2','armed_contacts=1'))['status']=='PENDING')
    check('same tick mismatch fails',audit(raw=text.replace('[HAZARD] tick=420','[HAZARD] tick=421'))['status']=='FAIL')
    check('finite inventory failure detected',audit(raw=text.replace('bombs=2 used=0','bombs=1 used=0'))['status']=='FAIL')
    check('new LIVE error is not history',audit(raw=text.replace('errors=7','errors=8',1))['status']=='FAIL')
    check('capture hash mismatch fails',audit(rec={**receipt,'raw_sha256':'F'*64})['status']=='FAIL')
    check('HOST marker cannot supply device hits',analyze_text(text.replace('hits=2','hits=1').replace('lives=1','lives=2').replace('armed_contacts=2','armed_contacts=1'),host_text=host,synthetic=True)['device_contact_status']=='PENDING')
    check('empty evidence pending',analyze_text('',synthetic=True)['status']=='PENDING')
    return {'evidence_kind':'SYNTHETIC_PARSER_TEST_ONLY','status':'PASS','checks':checks}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path);p.add_argument('--host-log',type=Path)
    p.add_argument('--receipt',type=Path);p.add_argument('--output',type=Path);p.add_argument('--require-strict-60',action='store_true')
    p.add_argument('--self-test',action='store_true');args=p.parse_args()
    if args.self_test:result=self_test()
    elif not args.log:result={'status':'PENDING','reason':'No actual W attach RAW log supplied','new600s_run':False}
    else:
        raw=args.log.read_bytes();host_raw=args.host_log.read_bytes() if args.host_log else None
        receipt_raw=args.receipt.read_bytes() if args.receipt else None
        result=analyze_text(raw.decode('utf-8-sig',errors='replace'),host_raw.decode('utf-8-sig',errors='replace') if host_raw else None,
                            json.loads(receipt_raw.decode('utf-8-sig')) if receipt_raw else None,raw_bytes=raw,host_bytes=host_raw)
        result['inputs']={kind:{'path':str(path.resolve()),'bytes':len(data),'sha256':sha(data)}
            for kind,path,data in (('raw',args.log,raw),('host',args.host_log,host_raw),('receipt',args.receipt,receipt_raw)) if path is not None}
    text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if args.output:
        with args.output.open('x',encoding='utf-8') as stream:stream.write(text)
    print(text,end='')
    return 0 if result['status']=='PASS' and (not args.require_strict_60 or result.get('strict_60fps',{}).get('status')=='PASS') else 1


if __name__=='__main__':raise SystemExit(main())


