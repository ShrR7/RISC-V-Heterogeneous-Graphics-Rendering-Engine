"""Independently audit G9 raw UART and dynamic comparison selection. Offline only.
Usage: python tools/audit_g9_run.py (--run RUN_DIR | --independent CASE_DIR) --output NEW_JSON
No hardware, network, git, runtime changes or evidence rewriting are performed.
"""
import argparse,hashlib,json,math,re,statistics
from pathlib import Path
PERF_FIELDS='frame target path hz tick_lo tick_hi updates logic_steps shown_delta engine_delta miss_px miss_lines reject skip actual_min actual_max migrated_min migrated_max migrated_sum legacy_sum desc_sum logic_us build_us render_us work_peak_us missed_logic config_max_us guard'.split()
COUNTERS={'shown':'shown_delta','engine':'engine_delta','miss_px':'miss_px','miss_lines':'miss_lines','reject':'reject','skip':'skip'}
INIT_COUNTERS={'shown':'initial_shown','engine':'initial_engine','miss_px':'initial_miss_px','miss_lines':'initial_miss_lines','reject':'initial_reject','skip':'initial_skip'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest().upper()
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))

def textfile(p):
    b=Path(p).read_bytes()
    return b.decode('utf-16' if b.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig',errors='strict')
def records(text):
    """Find embedded tags too: a boot INIT can follow an old UART partial line."""
    marks=list(re.finditer(r'\[([A-Z][A-Z0-9_]+)\]',text));out=[]
    for i,m in enumerate(marks):
        end=marks[i+1].start() if i+1<len(marks) else len(text)
        body=text[m.end():end];line=body.splitlines()[0] if body.splitlines() else ''
        d={}
        for k,val in re.findall(r'(\w+)=([^\s]+)',line):
            try:d[k]=int(val,16 if val.lower().startswith('0x') else 10)
            except ValueError:d[k]=val
        out.append({'tag':m[1],'data':d,'offset':m.start(),'text':line})
    return out

def select_windows(rows,suite):
    """Replay collector warm1+valid3. Never select transition/tail/mismatches."""
    seen={};selected=[];issues=[]
    for r in rows:
        case=r.get('case','');reason='transition_or_tail';index=None
        match=re.fullmatch(r'n(128|512|1024|1500|2000)_path([01])',case)
        if match:
            expected=(int(match[1]),int(match[2]))
            if (r.get('target'),r.get('path'))!=expected:reason='target_or_path_mismatch'
            else:
                n=seen.get(case,0);seen[case]=n+1
                index=n;reason='warmup_first_matching' if n==0 else ('eligible' if n<=3 else 'extra_after_three')
        elif case=='natural':
            if r.get('target')!=0:reason='injected_content_in_natural_capture'
            else:
                n=seen.get(case,0);seen[case]=n+1;index=n
                reason='warmup_first_matching' if n==0 else 'eligible'
        if 'eligible_index' in r and r['eligible_index']!=index:issues.append('host eligible_index differs from independent replay frame='+str(r.get('frame')))
        if 'eligible_reason' in r and r['eligible_reason']!=reason:issues.append('host eligible_reason differs frame='+str(r.get('frame')))
        selected.append({'frame':r.get('frame'),'case':case,'target':r.get('target'),'path':r.get('path'),'eligible_index':index,'reason':reason,'eligible':reason=='eligible'})
    if suite=='capacity':
        for count in (128,512,1024,1500,2000):
            for path in (0,1):
                case='n%d_path%d'%(count,path);n=sum(s['eligible'] and s['case']==case for s in selected)
                if n!=3:issues.append('expected3 selected windows for '+case+', got'+str(n))
    elif suite=='natural' and not any(s['eligible'] and s['case']=='natural' for s in selected):issues.append('no quantitative natural windows after warmup')
    return selected,issues

def summarize(rows):
    if not rows:return {}
    ticks=sum((r['tick_hi']<<32)|r['tick_lo'] for r in rows);hz=rows[0]['hz'];updates=sum(r['updates'] for r in rows)
    return {'windows':len(rows),'frames':[r['frame'] for r in rows],'seconds':ticks/hz,
      'update_fps':sum(r['updates'] for r in rows)*hz/ticks,
      'logic_hz':sum(r['logic_steps'] for r in rows)*hz/ticks,
      'stage9_good_composition_fps':sum(r['shown_delta'] for r in rows)*hz/ticks,
      'stage9_display_fps_applicable':any(r['path']==1 for r in rows),
      'per_window_update_fps':[r['updates']*r['hz']/((r['tick_hi']<<32)|r['tick_lo']) for r in rows],
      'actual_min':min(r['actual_min'] for r in rows),'actual_max':max(r['actual_max'] for r in rows),
      'migrated_min':min(r['migrated_min'] for r in rows),'migrated_max':max(r['migrated_max'] for r in rows),
      'migrated_average_per_update':sum(r['migrated_sum'] for r in rows)/updates if updates else 0,
      'legacy_average_per_update':sum(r['legacy_sum'] for r in rows)/updates if updates else 0,
      'tile_descriptors_average_per_update':sum(r['desc_sum'] for r in rows)/updates if updates else 0,
      'mean_logic_us_per_step':sum(r['logic_us']*r['logic_steps'] for r in rows)/sum(r['logic_steps'] for r in rows) if sum(r['logic_steps'] for r in rows) else 0,
      'mean_build_us_per_update':sum(r['build_us']*r['updates'] for r in rows)/updates if updates else 0,
      'mean_render_us_per_update':sum(r['render_us']*r['updates'] for r in rows)/updates if updates else 0,
      'new_miss_px':sum(r['miss_px'] for r in rows),'new_miss_lines':sum(r['miss_lines'] for r in rows),'new_reject':sum(r['reject'] for r in rows),'new_skip':sum(r['skip'] for r in rows),
      'missed_logic_cumulative_max':max(r['missed_logic'] for r in rows),
      'each_window_mean_update_fps_at_least60':all(r['updates']*r['hz']>=60*((r['tick_hi']<<32)|r['tick_lo']) for r in rows),
      'strict_per_frame60':'NOT_PROVEN_BY_INTERVAL_MEANS'}

def audit(run):
    run=Path(run).resolve();errors=[];warnings=[];checks=0;inputs={};parts={}
    def check(ok,reason):
        nonlocal checks
        checks+=1
        if not ok:errors.append(reason)
    def read(path,kind='json'):
        path=Path(path);inputs[str(path)]=sha(path)
        if kind=='json':return load(path)
        if kind=='jsonl':return [json.loads(x) for x in path.read_text(encoding='utf-8-sig').splitlines() if x.strip()]
        b=path.read_bytes();check(b'\xef\xbf\xbd' not in b,'replacement UTF8 bytes in '+str(path));return b.decode('utf-8-sig',errors='strict')
    try:
        state=read(run/'progress.json');rows=read(run/'windows.jsonl','jsonl');commands=read(run/'commands.jsonl','jsonl');raw=read(run/'serial.raw','text')
        check(state.get('status')=='COMPLETE','collector not COMPLETE: '+str(state.get('status'))+' '+str(state.get('error','')))
        check(state.get('pending') is None,'collector retains pending case')
        check(state.get('windows')==len(rows),'progress windows count differs')
        candidate_path=Path(state['candidate_path']);candidate=read(candidate_path)
        check(sha(candidate_path)==state['candidate_sha256'].upper(),'candidate manifest hash differs')
        check(candidate['elf_sha256'].upper()==state['elf_sha256'].upper(),'candidate/run ELF identity differs')
        check(candidate['bit_md5'].upper()==state['bit_md5'].upper(),'candidate/run bit identity differs')
        variant=Path(candidate['variant']);source_dir=Path(state.get('source_snapshot_dir',variant/'src'))
        for rel,expected in candidate['runtime_files'].items():
            f=source_dir/Path(rel).name if rel.startswith('src/') else variant/rel
            check(f.is_file() and sha(f)==expected.upper(),'source binding differs: '+rel)
        elf=Path(state.get('elf_path',variant/'build/gardenStage9Game.elf'))
        check(elf.is_file() and sha(elf)==state['elf_sha256'].upper(),'actual ELF file hash differs: '+str(elf))
        for rel,key in [('assets/atlas.bin','atlas_sha256'),('assets/stage9_tiles.bin','tile_library_sha256'),('tools/board_run_fx_variant.ps1','loader_sha256')]:
            check((variant/rel).is_file() and sha(variant/rel)==candidate[key].upper(),'candidate payload/loader differs: '+rel)
        bit=Path(state.get('bit_path',variant/'delivery/ddr_demo_ti60_rtl14.bit'))
        check(bit.is_file() and hashlib.md5(bit.read_bytes()).hexdigest().upper()==state['bit_md5'].upper(),'actual candidate bit hash differs')
        parts['binding']={'candidate':str(candidate_path),'candidate_sha256':sha(candidate_path),'elf':str(elf),'elf_sha256':sha(elf),'atlas_sha256':candidate['atlas_sha256'],'tile_library_sha256':candidate['tile_library_sha256'],'bit':str(bit),'bit_md5':state['bit_md5'],'bit_sha256':sha(bit),'source_snapshot_dir':str(source_dir),'scope':'file and declared launch identity; no new device readback performed'}
        startup='';startup_records=[]
        if state.get('startup_log'):
            startup=read(Path(state['startup_log']),'text');startup_records=records(startup)
            init_positions=[i for i,r in enumerate(startup_records) if r['tag']=='G9_INIT']
            check(bool(init_positions),'startup G9_INIT missing')
            if init_positions:startup_records=startup_records[init_positions[-1]:];startup=startup[startup_records[0]['offset']:]
        else:check(False,'startup_log binding absent; old pressure baseline not verified')
        rr=records(raw);perfs=[r['data'] for r in rr if r['tag']=='G9_PERF'];frames={};lives={}
        for r in rr:
            if r['tag'] in ('G9_FRAME','LIVE'):
                container=frames if r['tag']=='G9_FRAME' else lives;f=r['data'].get('frame');check(f not in container,'duplicate '+r['tag']+' frame '+str(f));container[f]=r['data']
        check(bool(perfs),'no raw G9_PERF received; an empty completed collector is not acceptance')
        check(len(perfs)==len(rows),'raw G9_PERF count differs from windows.jsonl (missing/unprocessed record)')
        run_init=[r for r in rr if r['tag']=='G9_INIT'];check(len(run_init)<=1,'multiple CPU init records inside run')
        if run_init and perfs:check(run_init[0]['offset']<next(r['offset'] for r in rr if r['tag']=='G9_PERF'),'CPU reset occurred after performance capture began')
        initial=(run_init[0]['data'] if run_init else (startup_records[0]['data'] if startup_records and startup_records[0]['tag']=='G9_INIT' else {}))
        parts['initial_raw']=initial
        for k in INIT_COUNTERS.values():check(isinstance(initial.get(k),int),'initial raw counter missing '+k)
        for text,label in [(raw,'run'),(startup,'startup after INIT')]:
            for marker in ('[G9_FATAL]','[TRAP]','GARDEN FAIL','RESULT: FAIL'):
                check(marker not in text,label+' fatal marker '+marker)
        predecessor=None;prior_missed=None;derived=[]
        for i,(r,d) in enumerate(zip(rows,perfs)):
            for k in PERF_FIELDS:check(k in d and isinstance(d[k],int),'missing/noninteger raw PERF field '+k+' index'+str(i))
            for k,val in d.items():check(r.get(k)==val,'JSON/raw PERF mismatch frame'+str(d.get('frame'))+'.'+k)
            if not all(k in d and isinstance(d[k],int) for k in PERF_FIELDS):continue
            span=(d['tick_hi']<<32)|d['tick_lo'];check(span>0 and d['hz']>0 and d['updates']>0,'invalid ticks/hz/updates frame'+str(d['frame']))
            if span<=0 or d['hz']<=0 or d['updates']<=0:continue
            calc={'actual_update_fps':d['updates']*d['hz']/span,'actual_logic_hz':d['logic_steps']*d['hz']/span,'actual_good_display_fps':d['shown_delta']*d['hz']/span}
            for k,val in calc.items():check(isinstance(r.get(k),(float,int)) and math.isclose(r[k],val,rel_tol=1e-12,abs_tol=1e-9),'host arithmetic mismatch '+k+' frame'+str(d['frame']))
            health=all(d[k]==0 for k in ('miss_px','miss_lines','reject','skip','guard'));check(r.get('healthy')==health,'host healthy mismatch frame'+str(d['frame']))
            check(health,'new display/reject/skip/guard fault frame'+str(d['frame']))
            check(0<=d['actual_min']<=d['actual_max']<=candidate['pool'],'invalid actual pool range frame'+str(d['frame']))
            check(0<=d['migrated_min']<=d['migrated_max']<=d['actual_max'],'invalid migration range frame'+str(d['frame']))
            check(d['migrated_min']*d['updates']<=d['migrated_sum']<=d['migrated_max']*d['updates'],'migration sum outside min/max frame'+str(d['frame']))
            check(d['migrated_sum']<=d['desc_sum']<=2048*d['updates'],'tile descriptor sum invalid frame'+str(d['frame']))
            check(d['legacy_sum']+d['migrated_sum']<=d['actual_max']*d['updates'],'routed sum exceeds live max frame'+str(d['frame']))
            if d['path']==0:check(d['migrated_sum']==0 and d['desc_sum']==0,'legacy path contains reported Stage9 migration')
            check(d['path'] in (0,1),'bad path value');check(d['config_max_us']<=3000,'driver config budget exceeded frame'+str(d['frame']))
            if predecessor is not None:check(d['frame']-predecessor==d['updates'],'PERF window missing/frame counter jump at'+str(d['frame']))
            predecessor=d['frame']
            if prior_missed is not None:check(d['missed_logic']>=prior_missed,'missed_logic counter decreased')
            prior_missed=d['missed_logic']
            f=frames.get(d['frame']);check(f is not None,'G9_FRAME missing for PERF frame'+str(d['frame']))
            if f:
                check(f.get('target')==d['target'] and f.get('enabled')==d['path'],'FRAME target/path mismatch')
                check(f.get('guard')==0 and f.get('errors')==0,'G9_FRAME driver/guard errors frame'+str(d['frame']))
                check(0<=f.get('tiles',999)<=32 and 0<=f.get('desc',9999)<=2048,'FRAME atlas/desc capacity violated')
                check(f.get('migrated',0)<=f.get('desc',0),'FRAME tile count fewer than migrated bullets')
                check(f.get('migrated',0)+f.get('legacy',0)<=f.get('actual',0),'FRAME routed enemy count exceeds actual')
                for key in COUNTERS:check(0<=f.get(key,-1)<0xFFFFFFFF,'counter invalid or saturated '+key)
                if i:
                    pf=frames.get(perfs[i-1]['frame'])
                    if pf:
                        for key,delta in COUNTERS.items():
                            check(f[key]>=pf[key],'raw counter decreased '+key)
                            check(((f[key]-pf[key])&0xFFFFFFFF)==d[delta],'raw counter/PERF delta mismatch '+key+' frame'+str(d['frame']))
                elif all(k in initial for k in INIT_COUNTERS.values()):
                    inferred={key:(f[key]-d[delta])&0xFFFFFFFF for key,delta in COUNTERS.items()}
                    parts['first_window_inferred_previous_raw']=inferred
                    for key in ('miss_px','miss_lines','reject','skip'):
                        check(inferred[key]==initial[INIT_COUNTERS[key]],'new fault before first captured PERF (not historical baseline): '+key)
            live=lives.get(d['frame']);check(live is not None,'LIVE missing for PERF frame'+str(d['frame']))
            if live:
                for k in ('errors','swap_timeout','front_reject','atlas_bounds','log_dropped','log_truncated'):
                    check(live.get(k)==0,'LIVE fault '+k+' frame'+str(d['frame']))
                check(live.get('fps10')==d['updates']*d['hz']*10//span,'LIVE FPS does not match G9 actual updates')
            derived.append({'frame':d['frame'],**calc,'scope':'natural' if d['target']==0 else 'explicit_injected_dynamic_gameplay','active_min':d['actual_min'],'active_max':d['actual_max'],'migrated_average':d['migrated_sum']/d['updates'],'legacy_average':d['legacy_sum']/d['updates'],'tile_desc_average':d['desc_sum']/d['updates']})
        for r in rr:
            if r['tag']=='LIVE':
                for k in ('errors','swap_timeout','front_reject','atlas_bounds','log_dropped','log_truncated'):
                    check(r['data'].get(k)==0,'any-run LIVE fault '+k)
            if r['tag']=='G9_FRAME':check(r['data'].get('errors')==0 and r['data'].get('guard')==0,'any-run G9_FRAME fault')
            if r['tag']=='DISPLAY7' and r['data'].get('phase')=='SECOND' and r['data'].get('present')==1:
                val=r['data'].get('sum','');pieces=str(val).split('/')
                try:check(len(pieces)==5 and all(int(pieces[k],16)==0 for k in (0,1,4)),'legacy display PX/PART/FULL new fault')
                except ValueError:check(False,'invalid legacy display counter tuple')
        sent=[c['command'] for c in commands];digits=[int(c) for c in sent if c in '012345' and len(c)==1];ack=[r['data'].get('target') for r in rr if r['tag']=='G9_WORKLOAD'];targetmap=[0,128,512,1024,1500,2000]
        check(ack==[targetmap[k] for k in digits],'workload command/ack sequence mismatch')
        toggles=[r['data'].get('enabled') for r in rr if r['tag']=='G9_CTRL'];check(len(toggles)==sent.count('k'),'path command/ack count mismatch')
        selections,selissues=select_windows(rows,state['suite']);errors.extend(selissues);checks+=len(selections)
        groups={}
        for s in selections:
            if s['eligible']:
                key=s['case'] if s['case']!='natural' else 'natural_path'+str(s['path']);groups.setdefault(key,[]).append(next(r for r in rows if r['frame']==s['frame']))
        summaries={k:summarize(v) for k,v in groups.items()}
        if any(x['missed_logic_cumulative_max']>0 for x in summaries.values()):warnings.append('missed_logic cumulative nonzero: retain logic drop/performance limitation')
        if any(not x['each_window_mean_update_fps_at_least60'] for x in summaries.values()):warnings.append('one or more selected windows do not meet60 actual game updates per second')
        scene=[{'tag':r['tag'],**r['data']} for r in rr if r['tag'] in ('GAME','REWARD','WAVE','G9_WORKLOAD','G9_CTRL')]
        modes=[x['mode'] for x in scene if 'mode' in x];parts['scene']={'events':scene,'modes_observed':sorted(set(modes)),'victory_observed':2 in modes,'restart_after_victory_observed':any(modes[i]==0 and 2 in modes[:i] for i in range(len(modes)))}
        parts.update({'suite':state['suite'],'collector_status':state['status'],'windows':len(rows),'selections':selections,'derived_windows':derived,'selected_summaries':summaries,'all_window_counters':{k:sum(r.get(k,0) for r in rows) for k in ('updates','logic_steps','shown_delta','engine_delta','miss_px','miss_lines','skip','reject')},'unselected_windows_preserved':sum(not s['eligible'] for s in selections)})
    except Exception as e:errors.append('audit input/processing error: '+type(e).__name__+': '+str(e))
    return {'schema':1,'status':'FAIL' if errors else ('PASS_EVIDENCE_WITH_PERFORMANCE_LIMITATIONS' if warnings else 'PASS_EVIDENCE'), 'checks':checks,'issues':errors,'warnings':warnings,'hardware_access':False,'runtime_modified':False,'input_sha256':inputs,'auditor_sha256':sha(__file__),'run':str(run),**parts,'limits':['actual update FPS is separate from frozen-input repeat/display FPS','path0 Stage9 shown=0 is not screen0FPS','tile descriptors are not bullet counts','warmup/transition/tail windows are retained but excluded from quantitative case comparison','natural level and explicitly injected stress are separate scopes','interval means do not establish every-frame strict60 deadline','no new600-second or complete gameplay acceptance is inferred']}

def audit_case(raw, case, candidate):
    """Audit one fresh ELF start; every counter delta is relative to its own INIT."""
    errors=[];warnings=[];checks=0
    def check(ok,msg):
        nonlocal checks
        checks+=1
        if not ok:errors.append(msg)
    rr=records(raw);inits=[r for r in rr if r['tag']=='G9_INIT']
    check(len(inits)==1,'expected one G9_INIT in fresh case, got '+str(len(inits)))
    prefix_records=0
    if inits:
        prefix_records=sum(r['offset']<inits[0]['offset'] for r in rr)
        rr=[r for r in rr if r['offset']>=inits[0]['offset']]
        raw=raw[inits[0]['offset']:]
    initial=inits[0]['data'] if inits else {}
    for k in INIT_COUNTERS.values():check(isinstance(initial.get(k),int),'missing INIT raw '+k)
    for marker in ('[G9_FATAL]','[TRAP]','GARDEN FAIL','RESULT: FAIL'):
        check(marker not in raw,'fatal marker '+marker)
    match=re.fullmatch(r'n(128|512|1024|1500|2000)_path([01])',case)
    check(bool(match),'invalid independent case name')
    expected=(int(match[1]),int(match[2])) if match else (None,None)
    perfs=[r['data'] for r in rr if r['tag']=='G9_PERF'];check(bool(perfs),'no G9_PERF received')
    frames={};lives={}
    for r in rr:
        if r['tag'] in ('G9_FRAME','LIVE'):
            dst=frames if r['tag']=='G9_FRAME' else lives;frame=r['data'].get('frame')
            check(frame not in dst,'duplicate '+r['tag']+' frame '+str(frame));dst[frame]=r['data']
        if r['tag']=='G9_FRAME':
            check(r['data'].get('errors')==0 and r['data'].get('guard')==0,'raw G9_FRAME driver/guard fault')
            for key in ('miss_px','miss_lines','reject','skip'):
                check(r['data'].get(key)==initial.get(INIT_COUNTERS[key]),'any raw FRAME new fault beyond its INIT baseline '+key+' frame '+str(r['data'].get('frame')))
        if r['tag']=='LIVE':
            for k in ('errors','swap_timeout','front_reject','atlas_bounds','log_dropped','log_truncated'):
                check(r['data'].get(k)==0,'raw LIVE fault '+k+' frame '+str(r['data'].get('frame')))
        if r['tag']=='DISPLAY7' and r['data'].get('phase')=='SECOND' and r['data'].get('present')==1:
            try:
                pieces=str(r['data'].get('sum','')).split('/')
                check(len(pieces)==5 and all(int(pieces[k],16)==0 for k in (0,1,4)),'new legacy PX/PART/FULL fault at frame '+str(r['data'].get('frame')))
            except ValueError:check(False,'invalid legacy counter tuple')
    workloads=[r['data'] for r in rr if r['tag']=='G9_WORKLOAD']
    check(len(workloads)==1 and workloads[0].get('target')==expected[0],'workload command ACK missing/different')
    ctrls=[r['data'] for r in rr if r['tag']=='G9_CTRL']
    if expected[1]==0:check(len(ctrls)==1 and ctrls[0].get('enabled')==0,'legacy path disable ACK missing/different')
    else:check(not ctrls,'unexpected path toggle in fresh Stage9 case')
    check(any(r['tag']=='FRAME_CTRL' and r['data'].get('async')==0 for r in rr),'SYNC logging ACK not observed')
    rows=[];derived=[];previous=None;previous_raw=None
    for i,d in enumerate(perfs):
        good=all(k in d and isinstance(d[k],int) for k in PERF_FIELDS)
        check(good,'PERF schema missing/noninteger index '+str(i))
        if not good:continue
        d={**d,'case':case};rows.append(d)
        span=(d['tick_hi']<<32)|d['tick_lo'];valid=span>0 and d['hz']>0 and d['updates']>0
        check(valid,'invalid ticks/hz/updates frame '+str(d['frame']))
        if not valid:continue
        check(d['path'] in (0,1),'invalid path value')
        health=all(d[k]==0 for k in ('miss_px','miss_lines','reject','skip','guard'))
        check(health,'new miss/reject/skip/guard fault frame '+str(d['frame']))
        check(0<=d['actual_min']<=d['actual_max']<=candidate['pool'],'actual object range invalid')
        check(0<=d['migrated_min']<=d['migrated_max']<=d['actual_max'],'migrated range invalid')
        check(d['migrated_min']*d['updates']<=d['migrated_sum']<=d['migrated_max']*d['updates'],'migrated total outside window bounds')
        check(d['migrated_sum']<=d['desc_sum']<=2048*d['updates'],'descriptor count invalid')
        check(d['migrated_sum']+d['legacy_sum']<=d['actual_max']*d['updates'],'route object count exceeds live maximum')
        if d['path']==0:check(d['migrated_sum']==0 and d['desc_sum']==0,'legacy-only window reports migration')
        check(d['config_max_us']<=3000,'configuration deadline exceeded')
        if previous is not None:
            check(d['frame']-previous['frame']==d['updates'],'missing PERF/window frame sequence')
            check(d['missed_logic']>=previous['missed_logic'],'cumulative missed logic decreased')
        f=frames.get(d['frame']);live=lives.get(d['frame'])
        check(f is not None,'missing G9_FRAME for PERF frame '+str(d['frame']))
        check(live is not None,'missing LIVE for PERF frame '+str(d['frame']))
        if f:
            check(f.get('target')==d['target'] and f.get('enabled')==d['path'],'FRAME target/path does not match PERF')
            check(0<=f.get('tiles',999)<=32 and 0<=f.get('desc',9999)<=2048,'FRAME capacity violated')
            check(f.get('migrated',9999)<=f.get('desc',-1),'FRAME migrated/descriptor mismatch')
            check(f.get('migrated',0)+f.get('legacy',0)<=f.get('actual',0),'FRAME routed objects exceed actual')
            for key in COUNTERS:check(isinstance(f.get(key),int) and 0<=f[key]<0xffffffff,'invalid/saturated raw counter '+key)
            if all(isinstance(f.get(key),int) for key in COUNTERS):
                if previous_raw:
                    for key,delta in COUNTERS.items():
                        check(f[key]>=previous_raw[key],'raw counter decreased '+key)
                        check(f[key]-previous_raw[key]==d[delta],'raw FRAME/PERF delta differs '+key)
                elif all(k in initial for k in INIT_COUNTERS.values()):
                    for key in ('miss_px','miss_lines','reject','skip'):
                        check(f[key]-d[COUNTERS[key]]==initial[INIT_COUNTERS[key]],'new fault before first PERF, not INIT historical baseline: '+key)
                previous_raw=f
        if live:check(live.get('fps10')==d['updates']*d['hz']*10//span,'LIVE fps10 arithmetic differs from 64-bit PERF ticks')
        derived.append({'frame':d['frame'],'actual_update_fps':d['updates']*d['hz']/span,'actual_logic_hz':d['logic_steps']*d['hz']/span,'repeat_good_composition_fps':d['shown_delta']*d['hz']/span,'ticks_64':span,'span_seconds':span/d['hz'],'migrated_average':d['migrated_sum']/d['updates'],'legacy_average':d['legacy_sum']/d['updates'],'tile_desc_average':d['desc_sum']/d['updates']})
        previous=d
    selections,_=select_windows(rows,'independent');selected=[r for r,s in zip(rows,selections) if s['eligible']]
    check(len(selected)==3,'expected at least three complete post-warm windows; selected '+str(len(selected)))
    if selected and any(r['missed_logic'] for r in selected):warnings.append('logic catch-up was capped: cumulative missed_logic is retained')
    if selected and not summarize(selected)['each_window_mean_update_fps_at_least60']:warnings.append('actual game update FPS below60; repeat good-composition FPS is not a replacement')
    events=[{'tag':r['tag'],**r['data']} for r in rr if r['tag'] in ('GAME','REWARD','WAVE','G9_WORKLOAD','G9_CTRL','FRAME_CTRL')]
    summary=summarize(selected)
    def window_range(key):
        vals=[r[key]/r['updates'] for r in selected]
        return [min(vals),max(vals)] if vals else []
    fatals=[{'data':r['data'],'raw':r['text']} for r in rr if r['tag']=='G9_FATAL']
    observed_raw={key:[r['data'][key] for r in rr if r['tag']=='G9_FRAME' and isinstance(r['data'].get(key),int)] for key in ('miss_px','miss_lines','reject','skip')}
    for item in fatals:
        d=item['data']
        for rawkey,key in [('raw_px','miss_px'),('raw_lines','miss_lines'),('reject','reject')]:
            if isinstance(d.get(rawkey),int):observed_raw[key].append(d[rawkey])
    rawnew={key:max([initial.get(INIT_COUNTERS[key],0)]+vals)-initial.get(INIT_COUNTERS[key],0) for key,vals in observed_raw.items()}
    flat={'fatal_details':fatals,'last_error':fatals[-1]['data'].get('error') if fatals else None,'failure_lines':[line for line in raw.splitlines() if any(mark in line for mark in ('[G9_FATAL]','[TRAP]','GARDEN FAIL','RESULT: FAIL'))],'target':expected[0],'path':expected[1],'eligible_windows':len(selected),'failures':list(errors),'update_fps_range':[min(summary['per_window_update_fps']),max(summary['per_window_update_fps'])] if summary else [],'update_fps_mean':summary.get('update_fps'),'migrated_per_update_range':window_range('migrated_sum'),'migrated_per_update_mean':summary.get('migrated_average_per_update'),'legacy_per_update_range':window_range('legacy_sum'),'legacy_per_update_mean':summary.get('legacy_average_per_update'),'actual_count_range':[summary['actual_min'],summary['actual_max']] if summary else [],'logic_us':summary.get('mean_logic_us_per_step'),'build_us':summary.get('mean_build_us_per_update'),'render_us':summary.get('mean_render_us_per_update'),'miss_new':rawnew['miss_px'],'lines_new':rawnew['miss_lines'],'reject_new':rawnew['reject'],'skip_new':rawnew['skip'],'reported_window_miss_new':sum(d.get('miss_px',0) for d in perfs),'reported_window_lines_new':sum(d.get('miss_lines',0) for d in perfs),'failed_windows':[d.get('frame') for d in perfs if any(d.get(k,0) for k in ('miss_px','miss_lines','reject','skip','guard'))]}
    insufficient_only=errors and all(x.startswith(('expected at least three','no G9_PERF received')) for x in errors)
    return {**flat,'case':case,'status':'INSUFFICIENT' if insufficient_only else 'FAIL' if errors else 'PASS_EVIDENCE_WITH_PERFORMANCE_LIMITATIONS' if warnings else 'PASS_EVIDENCE','checks':checks,'issues':errors,'warnings':warnings,'scope':'explicit_injected_dynamic_gameplay_with_synchronous_UART_reports','initial_raw':initial,'ignored_pre_INIT_records':prefix_records,'all_perf_windows':perfs,'derived_windows':derived,'selection':selections,'selected_summary':summarize(selected),'events':events,'records':{tag:sum(r['tag']==tag for r in rr) for tag in ('G9_INIT','G9_WORKLOAD','G9_PERF','G9_FRAME','LIVE','G9_FATAL')}}

def audit_independent(directory):
    directory=Path(directory).resolve();errors=[];warnings=[];checks=0;inputs={};result={'cases':{}}
    def check(ok,msg):
        nonlocal checks
        checks+=1
        if not ok:errors.append(msg)
    def read(p,kind='json'):
        p=Path(p);inputs[str(p)]=sha(p)
        return load(p) if kind=='json' else textfile(p)
    try:
        task=directory.parent;state=read(directory/'progress.json');result['collector_state']=state
        check(state.get('status')=='COMPLETE','independent collector not COMPLETE: '+str(state.get('status'))+' '+str(state.get('error','')))
        manifest=task/'candidate_inputs_v2.json';candidate=read(manifest);variant=Path(candidate['variant'])
        source_dir=task/'source_v2';loader_receipt=read(task/'independent_case_loader.json')
        for rel,expected in candidate['runtime_files'].items():
            path=source_dir/Path(rel).name if rel.startswith('src/') else variant/rel
            check(path.is_file() and sha(path)==expected.upper(),'frozen source binding differs: '+rel)
        payloads={'elf':variant/'build/gardenStage9Game.elf','atlas':variant/'assets/atlas.bin','tiles':variant/'assets/stage9_tiles.bin','bit':variant/'delivery/ddr_demo_ti60_rtl14.bit','loader':Path(loader_receipt['loader'])}
        for name,key in [('elf','elf_sha256'),('atlas','atlas_sha256'),('tiles','tile_library_sha256')]:
            check(payloads[name].is_file() and sha(payloads[name])==candidate[key].upper(),name+' candidate hash mismatch')
        check(hashlib.md5(payloads['bit'].read_bytes()).hexdigest().upper()==candidate['bit_md5'].upper(),'bit MD5 mismatch')
        check(sha(payloads['loader'])==loader_receipt['sha256'].upper(),'independent loader receipt hash mismatch')
        check(loader_receipt.get('runtime_unchanged') is True,'independent loader receipt does not declare unchanged runtime')
        result['binding']={'candidate':str(manifest),'candidate_sha256':sha(manifest),'frozen_sources':str(source_dir),'runtime_candidate_unchanged':True,'loader_override_receipt':loader_receipt,'payloads':{k:{'path':str(v),'sha256':sha(v)} for k,v in payloads.items()},'scope':'offline file identity plus declared launch receipt; not a new device readback'}
        runner=task.parent/'run_g9_independent_cases.ps1'
        if runner.is_file():read(runner,'text')
        completed=state.get('completed',[]);completed={x['case']:x for x in completed};result['incomplete_cases']=[]
        for n in (128,512,1024,1500,2000):
            for path in (0,1):
                case='n%d_path%d'%(n,path);log=directory/(case+'.log');host=directory/(case+'.host.txt')
                receipt=completed.get(case);expected_input=('km' if path==0 else 'm')+str((128,512,1024,1500,2000).index(n)+1)
                if receipt:
                    check(receipt.get('target')==n and receipt.get('path')==path and receipt.get('input')==expected_input,'case launch declaration differs '+case)
                    check(receipt.get('sync_logging') is True and receipt.get('loader_exit')==0,'case loader/SYNC declaration failed '+case)
                else:check(False,'case not completed in collector receipt '+case);result['incomplete_cases'].append(case)
                if host.is_file():
                    hosttext=read(host,'text');check('Captured 32s ->' in hosttext,'capture completion marker absent '+case)
                else:check(False,'case host transcript missing '+case)
                if not log.is_file():
                    check(False,'case raw capture missing '+case)
                    result['cases'][case]={'case':case,'target':n,'path':path,'status':'INSUFFICIENT','eligible_windows':0,'failures':['raw capture missing'],'issues':['raw capture missing'],'selected_summary':{}}
                    continue
                raw=read(log,'text');check('�' not in raw,'replacement character in raw capture '+case)
                part=audit_case(raw,case,candidate);checks+=part['checks'];result['cases'][case]=part
                errors.extend(case+': '+x for x in part['issues']);warnings.extend(case+': '+x for x in part['warnings'])
        result['comparisons']={}
        for n in (128,512,1024,1500,2000):
            a=result['cases'].get('n%d_path0'%n,{});b=result['cases'].get('n%d_path1'%n,{})
            sa=a.get('selected_summary',{});sb=b.get('selected_summary',{})
            if sa and sb:
                result['comparisons'][str(n)]={'path0_update_fps':sa['update_fps'],'path1_update_fps':sb['update_fps'],'update_fps_ratio_path1_over_path0':sb['update_fps']/sa['update_fps'],'both_evidence_healthy':a.get('status','').startswith('PASS') and b.get('status','').startswith('PASS'),'path1_migrated_average':sb['migrated_average_per_update'],'path1_legacy_average':sb['legacy_average_per_update'],'path1_tile_descriptors_average':sb['tile_descriptors_average_per_update'],'path1_repeat_good_composition_fps':sb['stage9_good_composition_fps']}
        restore=directory/'restore.json'
        if restore.is_file():
            result['restore']=read(restore);check(result['restore'].get('loader_exit')==0,'final original game restore failed')
        else:warnings.append('final original game restore receipt not present at audit time')
    except Exception as e:errors.append('audit input/processing error: '+type(e).__name__+': '+str(e))
    return {'schema':2,'status':'FAIL' if errors else 'PASS_EVIDENCE_WITH_PERFORMANCE_LIMITATIONS' if warnings else 'PASS_EVIDENCE','checks':checks,'issues':errors,'warnings':warnings,'hardware_access':False,'runtime_modified':False,'directory':str(directory),'input_sha256':inputs,'auditor_sha256':sha(__file__),**result,'limits':['synchronous UART reports including original DISPLAY7 formatting are included; this diagnostic gameplay comparison must not be divided by prior pure static capacity benchmark FPS','good frozen-input repeat composition at60Hz does not mean dynamic input updates at60FPS','initial raw counters are preserved per independent ELF launch and never relabeled as new run faults','one warm window and transition/mismatch windows are retained but excluded from the selected first three windows','all extra windows remain subject to health/fatal/log-drop checks','tile descriptors, migrated bullets and legacy fallback bullets are separate units','window means cannot establish strict every-frame60FPS or a new600-second acceptance']}

def main():
    p=argparse.ArgumentParser(description=__doc__);group=p.add_mutually_exclusive_group(required=True);group.add_argument('--run',type=Path);group.add_argument('--independent',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    result=audit(a.run) if a.run else audit_independent(a.independent);a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2,ensure_ascii=False);f.write('\n')
    print(json.dumps({'status':result['status'],'checks':result['checks'],'issues':result['issues'],'warnings':result['warnings'],'output':str(a.output)},ensure_ascii=False))
    raise SystemExit(1 if result['issues'] else 0)
if __name__=='__main__':main()
