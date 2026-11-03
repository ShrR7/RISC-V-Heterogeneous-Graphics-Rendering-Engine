#!/usr/bin/env python3
"""Audit fresh combat scoring/feedback short smoke; no hardware access.

SPDX-License-Identifier: GPL-3.0-or-later
Closed captures are immutable; outputs must be new. This is not600s acceptance.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,zlib

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parent/'neon_scene_20261014'
CASE=ROOT/'evidence/combat_smoke_20261014'
def sha(data):return hashlib.sha256(data).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def put(path,obj):path.write_bytes((json.dumps(obj,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
def historical(name,fallback):
    p=CASE/name
    return p if p.is_file() else fallback
def identity(path):return {'path':str(path.resolve()),'bytes':path.stat().st_size,'sha256':sha(path.read_bytes())}
def monotonic(rows,keys):
    return bool(rows) and all(isinstance(r.get(k),int) and r[k]>=0 and (i==0 or r[k]>=rows[i-1][k]) for i,r in enumerate(rows) for k in keys)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,default=historical('combat_smoke_20261014.log',ROOT/'logs/combat_smoke_20261014_retry1.log'))
    parser.add_argument('--console',type=Path,default=historical('capture_completion_tail.txt',ROOT/'logs/capture_completion_tail.txt'))
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--visual',type=Path)
    args=parser.parse_args();out=args.output_dir.resolve()
    if out.exists() or out==CASE.resolve() or CASE.resolve() in out.parents:parser.error('Use a new output directory outside the closed case.')
    raw=args.log.read_bytes();start=raw.rfind(b'GARDEN Ti60 cropped port ')
    if start<0:parser.error('No current application boot banner.')
    suffix=raw[start:];out.mkdir(parents=True);(out/'raw_capture.log').write_bytes(raw);(out/'current_boot.log').write_bytes(suffix)
    tool=ROOT/'tools/analyze_display7.py';spec=importlib.util.spec_from_file_location('combat_display7',tool);native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    display=native.analyze(out/'current_boot.log');put(out/'native_display7.json',display)
    text=suffix.decode('utf-8-sig',errors='replace');partial=bool(text and not text.endswith('\n'));lines=text.splitlines()
    if partial:lines=lines[:-1]
    rows=[{'line':i+1,'raw':line,**{k:native.value(v) for k,v in native.FIELDS.findall(line)}} for i,line in enumerate(lines)]
    def selected(prefix):return [r for r in rows if r['raw'].startswith(prefix)]
    live,combat,scene,feedback,stage=map(selected,('[LIVE]','[COMBAT]','[SCENE]','[FEEDBACK]','[STAGE]'))
    alpha=display['native_alpha'];atlas=(ROOT/'assets/atlas.bin').read_bytes();manifest=read(ROOT/'assets/manifest.json');runtime=read(ROOT/'assets/runtime_checksums.json')
    build=read(historical('build_manifest.json',ROOT/'build/build_manifest.json'));elf=identity(ROOT/'build/neonCombatGardenDemo.elf')
    paths={
        'core':historical('combat_core_host_verification.json',ROOT/'build/host/combat_core/verification.json'),
        'combat':historical('combat_host_verification.json',ROOT/'build/host/combat/verification.json'),
        'renderer':historical('score_renderer_host_verification.json',ROOT/'build/host/score_renderer/verification.json'),
        'legacy':historical('combat_legacy_host_verification.json',ROOT/'build/host/combat_legacy/verification.json'),
        'HUD':historical('feedback_hud_host_verification.json',ROOT/'build/host/feedback_hud/verification.json'),
        'preview':historical('combat_preview_verification.json',ROOT/'build/host/combat_preview/preview_verification.json')}
    reports={k:read(p) for k,p in paths.items()}
    def bound(report):
        source=report.get('source_sha256',{})
        return report.get('status')=='PASS' and bool(source) and all(sha((ROOT/n).read_bytes())==h.lower() for n,h in source.items())
    pairs=[p for p in display['pairs'] if p['after_GAME_START_window']];counts=display['runtime_window_observed_counts']
    zero_fields=('errors','swap_timeout','front_reject','atlas_bounds','missed','dropped','log_dropped','log_truncated')
    wins,restarts,previous=[],[],None
    for r in display['native_game_events']:
        mode=r.get('mode')
        if mode==2 and previous!=2:wins.append(r)
        if previous==2 and mode==0:restarts.append(r)
        previous=mode
    checks={
        'exact_latest_boot_suffix':raw[start:]==suffix and start>=0,
        'display_records_paired':display['status']=='PAIRED' and not display['issues'] and not display['incomplete_rids'] and not display['duplicates'],
        'display_counter_conservation':not display['counter_total_window_gaps'],
        'GAME_START_boundary_verified':display['GAME_START_boundary_verified'],
        'runtime_display_PX_PART_FULL_zero':bool(pairs) and all(counts[i]==0 for i in (0,1,4)) and all(p['main'][k][i]==0 for p in pairs for k in ('sum','sat','max','last') for i in (0,1,4)),
        'GPU_Alpha_and_relock':display['runtime_all_sampled_ON'] and display['runtime_all_sampled_GPU_Alpha16'],
        'no_counter_overflow_or_refused_reports':not display['runtime_overflow_pairs'] and display['refused_max']==0,
        'Alpha35_cases_pass':len(alpha)==1 and alpha[0].get('rc')==0 and alpha[0].get('cases')==35 and alpha[0].get('commands')==35 and all(alpha[0].get(k)==0 for k in ('pixel_errors','source_errors','guard_errors')),
        'ColorKey_pass':len(display['native_color_key'])==1 and 'verified PASS' in display['native_color_key'][0]['raw'],
        'runtime_errors_zero':bool(live) and all(r.get(k)==0 for r in live for k in zero_fields) and not display['native_log_nonzero'] and not display['native_faults'] and not display['fatal'],
        'FPS_at_least60':bool(live) and all(r.get('fps10',0)>=600 for r in live),
        'LIVE_GPU_Alpha_on':bool(live) and all(r.get('backend')=='GPU' and r.get('alpha_supported')==1 and r.get('alpha')==1 for r in live),
        'two_actual_1959_victories_and_auto_restarts':len(wins)>=2 and len(restarts)>=2 and all(r.get('score')==1959 for r in wins) and not selected('[INPUT]'),
        'combat_reports_complete':bool(combat) and len(combat)==len(live),
        'combat_enabled':bool(combat) and all(r.get('enabled')==1 for r in combat),
        'combat_effects_and_score_triggered':bool(combat) and all(combat[-1].get(k,0)>0 for k in ('events','kills','score_observed','impact_frames','kill_frames','popup_frames','flash_frames','score_hud_calls')),
        'combat_lifetime_monotonic':monotonic(combat,('events','kills','score_observed','impact_frames','kill_frames','popup_frames','flash_frames','popups_merged','peak','essential_drop','decor_drop','score_hud_calls')),
        'combat_no_rejected_lost_or_dropped_core_events':bool(combat) and all(r.get(k)==0 for r in combat for k in ('event_rejected','event_gaps','event_dropped','essential_drop')),
        'actual_score_matches_hit_and_old_kill_awards':bool(combat) and all(r.get('score')==r.get('hit_points',-1)+r.get('kill_points',-1) and r.get('epoch_hits')==r.get('hit_points') for r in combat),
        'within240_draws_and_GPU_frame_budget':bool(combat) and all(0<=r.get('peak',-1)<=240 and r.get('gpu_over_budget')==0 for r in combat),
        'scene_and_stage_retained':bool(scene) and bool(stage) and all(r.get('enabled')==1 for r in scene+stage) and stage[-1].get('victories',0)>=2 and stage[-1].get('restarts',0)>=2,
        'marker_and_label_state':bool(feedback) and all(r.get('panel_visible')==0 for r in feedback) and feedback[-1].get('marker_frames',0)>0,
        'matching_actual_ELF':elf['sha256']==build['elf_sha256'].lower(),
        'unchanged_atlas_matches_build_manifest':len(atlas)==236954 and zlib.crc32(atlas)&0xffffffff==0x2A64AD91 and sha(atlas)==build['atlas_sha256'].lower()==manifest['sha256'].lower() and atlas==(BASE/'assets/atlas.bin').read_bytes(),
        'atlas_runtime_checksums_match':len(runtime['files'])==13 and all((ROOT/n).stat().st_size==r['bytes'] and sha((ROOT/n).read_bytes())==r['sha256'].lower() for n,r in runtime['files'].items()),
        'atlas_before_reserved_scratch':0x5c4000+len(atlas)<=0x5fe000,
        'capture_completion':bool(re.search(r'Captured\s+210s\s+->',args.console.read_text(encoding='utf-8-sig'))),
        'independent_old_core_physics_comparison':bound(reports['core']) and reports['core'].get('prefix_compared_steps',0)>=24000,
        'combat_host_contract_current':bound(reports['combat']),
        'production_score_renderer_host_contract_current':bound(reports['renderer']),
        'retained_legacy_contracts_current':bound(reports['legacy']),
        'retained_HUD_contracts_current':bound(reports['HUD']),
        'actual_combined_preview_current':bound(reports['preview']),
    }
    for name,report in reports.items():
        for group in ('integration_source_sha256','tool_sha256'):
            if group in report:checks[f'{name}_{group}_bound']=all(sha((ROOT/n).read_bytes())==h.lower() for n,h in report[group].items())
    protected=('garden_fx.c','garden_fx.h','garden_feedback.c','garden_feedback.h','garden_scene.c','garden_scene.h','garden_stage.c','garden_stage.h','gpu_driver.c','gpu_driver.h','gpu_display.c','gpu_display.h','void_display_counter.c')
    checks['previous_FX_scene_stage_and_GPU_code_unchanged']=all((ROOT/'src'/n).read_bytes()==(BASE/'src'/n).read_bytes() for n in protected)
    binding_path=CASE/'input_manifest.json'
    if binding_path.is_file():
        binding=read(binding_path);checks['closed_case_capture_and_ELF_bound']=sha(raw)==binding['captured_raw_UART_sha256'] and elf['sha256']==binding['captured_ELF_sha256'].lower() and sha(atlas)==binding['captured_atlas_sha256'].lower() and all(sha((ROOT/n).read_bytes())==h.lower() for n,h in binding['runtime_source_sha256'].items())
    report={'variant':'neon_combat_20261014','test_kind':'210-second authorized hit+1 feedback smoke',
        'machine_smoke_pass':all(checks.values()),'guards':checks,'failed_guards':[k for k,v in checks.items() if not v],
        'current_boot_source':{'raw':identity(args.log),'start':start,'suffix_bytes':len(suffix),'suffix_sha256':sha(suffix)},'partial_tail_excluded':partial,
        'runtime_counter_device_seconds':display['runtime_counter_duration_seconds'],'runtime_samples':display['runtime_samples'],
        'observed_counts':counts,'counter_saturation_incidence':display['runtime_window_saturation_incidence'],
        'counter_lower_bound_fields':display['runtime_counter_lower_bound_fields'],
        'victories':len(wins),'restarts':len(restarts),'victory_events':wins,'restart_events':restarts,
        'last_LIVE':live[-1] if live else None,'last_COMBAT':combat[-1] if combat else None,
        'last_SCENE':scene[-1] if scene else None,'last_STAGE':stage[-1] if stage else None,'last_FEEDBACK':feedback[-1] if feedback else None,
        'host_verifications':{n:identity(p) for n,p in paths.items()},'ELF':elf,'atlas':{'bytes':len(atlas),'sha256':sha(atlas),'crc32':'2A64AD91'},
        'visual_acceptance':'pending user observation','full_600s_acceptance_complete':False,
        'limits':['Newscorerule explicitly authorized: actualpositiveHPdamaginghit+1, existing20/80/1000killrewards retained.',
                  'Old-vs-new physics proof normalizes onlyscore; currentcore is not falsely claimed byte-identical.',
                  'Formal capture is210s; complete device-window coverage is separately reported.',
                  'AR/R saturation is a lowerbound; startup snapshots are not screen displacement.',
                  'Proceduralplus is blackscaledfill; actualfontdigits use destination-awareAlpha.',
                  'Oldsmallimpacts are suppressed while strongcombatfeedback is enabled; explicitcombatcounters establish currenttriggers.',
                  'Zeroinstrumentcounts cannot replace visual observation; preview images/HUD samples are not board captures.']}
    if args.visual:
        visual=read(args.visual);report['visual_observation']=visual
        matched=visual.get('source')=='user observation' and bool(visual.get('answer')) and str(visual.get('applies_to_elf_sha256','')).lower()==elf['sha256'] and str(visual.get('applies_to_atlas_sha256','')).lower()==sha(atlas)
        report['visual_acceptance']='HASH_BOUND_USER_OBSERVATION_RECORDED' if matched else 'VISUAL_IDENTITY_NOT_MATCHED'
    put(out/'smoke_summary.json',report)
    print(json.dumps({k:report[k] for k in ('machine_smoke_pass','failed_guards','runtime_counter_device_seconds','runtime_samples','observed_counts','victories','restarts','last_COMBAT','last_LIVE','visual_acceptance')},ensure_ascii=False))
    return 0 if report['machine_smoke_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
