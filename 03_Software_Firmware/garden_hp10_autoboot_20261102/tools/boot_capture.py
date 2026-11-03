"""Capture COM11 without resetting it, then audit a fresh CGB1 boot offline."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import time

BOOT=re.compile(r'^\[BOOT\] CGB1 stage2 relocated C00000\s*$')
GARDEN=re.compile(r'^GARDEN Ti60 cropped port\b')
READY=re.compile(r'^\[READY\]')


def stamp(): return datetime.now(timezone.utc).isoformat()
def write_json(path,value): path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def fields(line):
    return {k:int(v,0) if v.lower().startswith(('0x','-0x')) else int(v)
            for k,v in re.findall(r'\b(\w+)=(-?0[xX][0-9a-fA-F]+|-?\d+)\b',line)}


def matches(mode,line):
    return mode=='open' or (mode in ('boot','boot-or-garden') and BOOT.search(line)) or \
           (mode in ('garden','boot-or-garden') and GARDEN.search(line)) or \
           (mode=='ready' and READY.search(line))


class ReconnectingSerial:
    """Reopen only the exact caller-supplied port, recording every exception."""
    def __init__(self,factory,deadline,on_error,clock=time.monotonic,sleep=time.sleep):
        self.factory,self.deadline,self.on_error=factory,deadline,on_error
        self.clock,self.sleep=clock,sleep;self.current=None
    def close(self):
        if self.current is not None:
            try:self.current.close()
            except OSError:pass
            self.current=None
    def lost(self,error,phase):
        self.on_error(error,phase);self.close()
    def open(self):
        while self.current is None:
            if self.clock()>=self.deadline():raise TimeoutError('Serial reconnect deadline exceeded')
            try:self.current=self.factory()
            except OSError as error:
                self.on_error(error,'open');self.sleep(0.5)
    @property
    def in_waiting(self):
        self.open()
        try:return self.current.in_waiting
        except OSError as error:self.lost(error,'in_waiting');return 0
    def read(self,n):
        while True:
            self.open()
            try:return self.current.read(n)
            except OSError as error:self.lost(error,'read');self.sleep(0.5)
    def write(self,data):
        self.open()
        try:return self.current.write(data)
        except OSError as error:self.lost(error,'write');raise
    def flush(self):
        try:return self.current.flush()
        except OSError as error:self.lost(error,'flush');raise


def capture(args):
    try:
        import serial  # Imported only when the root explicitly invokes capture.
    except ImportError:
        sys.path.insert(0, r'D:\Efinity\2026.1\python311\Lib\site-packages')
        import serial
    out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=False)
    result=dict(status='STARTING',started_utc=stamp(),port=args.port,baud=args.baud,
        dtr=False,rts=False,flow_control=False,input_buffer_reset=False,
        seconds_after_anchor=args.seconds,start_after=args.start_after,
        wait_timeout=args.wait_timeout,toggle_seconds_after_fresh_ready=sorted(args.toggle_at),
        label=args.label,hardware_access=True,reconnect_enabled=args.reconnect,
        serial_exceptions=[],post_anchor_gap=False)
    write_json(out/'capture.json',result)
    ser=None;opened=time.monotonic();started=None;ready_at=None;received=0;line_offset=0
    pending=b'';last_progress=opened;commands=[];start_line=None;send_index=0
    def factory():
        s=serial.Serial(port=None,baudrate=args.baud,timeout=0.1,xonxoff=False,rtscts=False,dsrdtr=False)
        s.dtr=False;s.rts=False;s.port=args.port
        try:s.open()
        except Exception:
            s.close();raise
        return s
    def reconnect_error(error,phase):
        nonlocal pending,line_offset
        event=dict(utc=stamp(),phase=phase,error=repr(error),byte_offset=received,
                   seconds_since_open=time.monotonic()-opened,after_anchor=started is not None)
        result['serial_exceptions'].append(event)
        if started is not None:result['post_anchor_gap']=True
        pending=b'';line_offset=received
        with (out/'reconnects.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps(event)+'\n')
        write_json(out/'capture.json',result)
        if len(result['serial_exceptions']) in (1,2,4,8,16,32,64,128,256,512):
            print('CAPTURE_RECONNECT '+json.dumps(event),flush=True)
    try:
        if args.reconnect:
            ser=ReconnectingSerial(factory,lambda:(started+args.seconds if started is not None else opened+args.wait_timeout),reconnect_error)
            ser.open()
        else:ser=factory()
        if args.start_after=='open':started=opened
        result['opened_utc']=stamp();result['status']='WAITING_FOR_ANCHOR'
        write_json(out/'capture.json',result)
        print('CAPTURE_OPEN '+str(out),flush=True)
        with (out/'serial.bin').open('wb') as raw,(out/'serial.log').open('w',encoding='utf-8',newline='') as text, \
             (out/'events.jsonl').open('w',encoding='utf-8') as events, \
             (out/'commands.jsonl').open('w',encoding='utf-8') as command_log:
            while True:
                now=time.monotonic()
                if started is None and now-opened>=args.wait_timeout:
                    raise TimeoutError('No fresh requested startup anchor before wait timeout')
                if started is not None and now-started>=args.seconds:break
                data=ser.read(min(max(ser.in_waiting,1),65536))
                now=time.monotonic()
                if data:
                    raw.write(data);raw.flush();text.write(data.decode('ascii','replace'));text.flush()
                    received+=len(data);pending+=data
                    while b'\n' in pending:
                        bline,pending=pending.split(b'\n',1)
                        line=bline.decode('ascii','replace').strip('\r\n')
                        event=dict(utc=stamp(),seconds_since_open=now-opened,byte_offset=line_offset,line=line)
                        if started is None and matches(args.start_after,line):
                            started=now;start_line=line
                            result.update(status='CAPTURING',anchor_line=line,anchor_byte_offset=line_offset,
                                          anchor_seconds_since_open=now-opened,anchor_utc=stamp())
                            write_json(out/'capture.json',result)
                            print('CAPTURE_ANCHOR '+line,flush=True)
                        if started is not None and ready_at is None and READY.search(line):
                            ready_at=now
                            result.update(fresh_ready_seconds_since_open=now-opened,fresh_ready_utc=stamp())
                            write_json(out/'capture.json',result)
                            print('CAPTURE_READY '+line,flush=True)
                        events.write(json.dumps(event,ensure_ascii=True)+'\n');events.flush()
                        line_offset+=len(bline)+1
                if ready_at is not None and not result['post_anchor_gap']:
                    schedule=sorted(args.toggle_at)
                    while send_index<len(schedule) and now-ready_at>=schedule[send_index]:
                        ser.write(b't');ser.flush()
                        command=dict(utc=stamp(),bytes_hex='74',text='t',scheduled_after_ready=schedule[send_index],
                                     actual_after_ready=now-ready_at,seconds_since_open=now-opened,
                                     purpose='toggle T test; first enters, second returns')
                        commands.append(command);command_log.write(json.dumps(command)+'\n');command_log.flush()
                        print('CAPTURE_COMMAND '+json.dumps(command),flush=True);send_index+=1
                if now-last_progress>=10:
                    print(json.dumps({'capture_bytes':received,'anchor_seconds':None if started is None else round(now-started,2),
                                      'ready_seen':ready_at is not None,'toggles_sent':len(commands)}),flush=True)
                    last_progress=now
            result['status']='CAPTURE_COMPLETE'
    except KeyboardInterrupt:
        result.update(status='INTERRUPTED',error='KeyboardInterrupt')
    except Exception as exc:
        result.update(status='FAILED',error=repr(exc))
    finally:
        if ser is not None:ser.close()
        result.update(completed_utc=stamp(),port_closed=True,received_bytes=received,
                      commands=commands,anchor_elapsed_seconds=None if started is None else time.monotonic()-started,
                      all_requested_toggles_sent=len(commands)==len(args.toggle_at))
        if (out/'serial.bin').exists():result['raw_sha256']=sha(out/'serial.bin')
        write_json(out/'capture.json',result)
    print(json.dumps({'status':result['status'],'out':str(out),'bytes':received,'port_closed':True}),flush=True)
    return 0 if result['status']=='CAPTURE_COMPLETE' else 1


def audit_log(raw:bytes,package_manifest=None,min_live=3,min_game_seconds=15,require_t_return=False,require_relock=False):
    lines=raw.decode('ascii','replace').splitlines();errors=[]
    starts=[i for i,line in enumerate(lines) if BOOT.search(line)]
    if not starts:return dict(status='FAIL',errors=['Fresh CGB1 relocated BOOT marker missing'],hardware_access=False)
    start=starts[-1];selected=lines[start:]
    def require(condition,message):
        if not condition:errors.append(message)
    def indices(pattern):return [i for i,l in enumerate(selected) if re.search(pattern,l)]
    require(not any('\ufffd' in l for l in selected),'Non-ASCII/corrupt UART bytes in selected boot')
    fatal=[l for l in selected if re.search(r'\[(?:BOOT_FAIL|G9_FATAL|PURE_FAIL|ALPHA_PROBE_FAIL)\]|RESULT:.*FAIL',l)]
    require(not fatal,'Fatal marker in selected boot')
    ddr=indices(r'^\[BOOT\] DDR probe PASS\s*$')
    compat=indices(r'^\[BOOT\] header/compat PASS\s*$')
    jumps=indices(r'^\[BOOT\] all sections PASS; jumping 00001000\s*$')
    jedec=[fields(l).get('JEDEC') for l in selected if l.startswith('[BOOT] JEDEC=')]
    require(jedec==[0xEF6017],'Expected one measured EF6017 JEDEC report')
    require(len(ddr)==len(compat)==len(jumps)==1,'Missing or repeated DDR/header/jump success marker')
    if ddr and compat and jumps:require(0<ddr[0]<compat[0]<jumps[0],'Boot success markers out of order')
    sections=[];pending=None
    for i,line in enumerate(selected):
        if line.startswith('[BOOT] section='):
            require(pending is None,'Section began before previous section CRC');pending=(i,fields(line))
        m=re.match(r'^\[BOOT\] DDR CRC PASS (?:0x)?([0-9A-Fa-f]{8})\s*$',line)
        if m:
            require(pending is not None,'CRC PASS without section record')
            if pending is not None:
                sections.append(dict(line=pending[0],**pending[1],crc32=int(m[1],16),crc_line=i));pending=None
    require(pending is None and len(sections)==4,'Expected four complete section/DDR CRC records')
    require([s.get('section') for s in sections]==[1,2,3,4],'Section order/kinds mismatch')
    if jumps:require(all(s['crc_line']<jumps[0] for s in sections),'Section CRC after game jump')
    if package_manifest is not None:
        entries=package_manifest['entries']
        require(len(entries)==4,'Frozen package manifest must contain four sections')
        for actual,expected in zip(sections,entries):
            crc=expected['crc32'];crc=int(crc,0) if isinstance(crc,str) else crc
            require(all(actual.get(k)==v for k,v in {'section':expected['kind'],'src':expected['flash_offset'],
                    'dst':expected['ddr_address'],'bytes':expected['bytes'],'crc32':crc}.items()),'Section differs from frozen package manifest')
    garden=[i for i,l in enumerate(selected) if GARDEN.search(l)]
    require(len(garden)==1,'Expected one new game banner after selected BOOT')
    if jumps and garden:require(jumps[0]<garden[0],'Game banner occurred before loader jump')
    game=selected[garden[0]:] if garden else []
    def tagged(tag):return [(i,fields(l),l) for i,l in enumerate(game) if l.startswith('['+tag+']')]
    alpha=tagged('ALPHA_PROBE');precision=tagged('PRECISION_PROBE');ready=tagged('READY')
    require(len(alpha)==len(precision)==len(ready)==1,'Missing/repeated Alpha/precision/READY startup results')
    if alpha:
        p=alpha[0][1];require(all(p.get(k)==v for k,v in {'rc':0,'present':1,'cases':35,'pixel_errors':0,'source_errors':0,'guard_errors':0}.items()),'Alpha35 verification failed/incomplete')
    if precision:
        p=precision[0][1];require(all(p.get(k)==v for k,v in {'rc':0,'cases':40,'pixel_errors':0,'source_errors':0,'guard_errors':0}.items()),'Material40 verification failed/incomplete')
    key=[i for i,l in enumerate(game) if l=='[COLOR_KEY] source pixels and transparent holes verified PASS']
    require(len(key)==1,'Color Key pixel verification missing')
    if alpha and precision and key and ready:require(alpha[0][0]<precision[0][0]<key[0]<ready[0][0],'Game verification markers out of order')
    relock=indices(r'^\[BOOT\] display relock PASS\s*$')
    display=[fields(l) for l in game if l.startswith('[DISPLAY7]')]
    if require_relock:
        require(len(relock)==1,'New loader display relock PASS missing/repeated')
        if compat and relock and sections:require(compat[0]<relock[0]<sections[0]['line'],'Display initialization order mismatch')
        require(bool(display),'DISPLAY7 controller/mirror evidence missing')
        require(all(p.get('ctrl')==4 and p.get('q',0)&0x400 for p in display),'DISPLAY7 CTRL4/Q.bit10 verification failed')
    for _,p,_ in tagged('ATLAS')+tagged('ATLAS_POST_PROBE'):
        require(p.get('crc32')==p.get('expected') and p.get('crc32') is not None,'Atlas CRC mismatch')
    require(len(tagged('ATLAS'))==len(tagged('ATLAS_POST_PROBE'))==1,'Missing/repeated atlas CRC evidence')
    live=[p for i,p,l in tagged('LIVE') if ready and i>ready[0][0]]
    require(len(live)>=min_live,'Not enough fresh LIVE records after READY')
    live_health=('errors','swap_timeout','front_reject','atlas_bounds','missed','log_dropped','log_truncated')
    for p in live:require(all(p.get(k)==0 for k in live_health),'LIVE health error/missing field')
    perf=[p for i,p,l in tagged('G9_PERF') if ready and i>ready[0][0]]
    pure=[p for i,p,l in tagged('PURE') if ready and i>ready[0][0]]
    for p in perf+pure:
        keys=('miss_px','miss_lines','reject','skip','guard')
        require(all(p.get(k)==0 for k in keys),'G9/PURE display health error/missing field')
    for p in pure:require(all(p.get(k)==0 for k in ('errors','log_dropped','log_truncated')),'PURE errors/log losses')
    elapsed=sum(((p.get('tick_hi',0)<<32)|p.get('tick_lo',0))/p['hz'] for p in perf if p.get('hz',0)>0)
    updates=sum(p.get('updates',0) for p in perf)
    require(elapsed>=min_game_seconds,'Fresh game G9_PERF time shorter than required interval')
    resumed=tagged('PURE_GAME_RESUMED');exits=tagged('PURE_EXIT');pure_starts=tagged('PURE_START')
    if require_t_return:
        require(bool(pure and exits and resumed),'T entry/report/exit/return evidence incomplete')
        if resumed:
            require(resumed[-1][1].get('game_unchanged')==1,'T return game hash/state changed')
            require(any(i>resumed[-1][0] for i,p,l in tagged('LIVE')),'No LIVE after T return')
        for i,p,l in exits:require(p.get('errors')==0 and p.get('game_steps')==0,'T exit errors/game stepped')
    return dict(status='FAIL' if errors else 'PASS',hardware_access=False,errors=sorted(set(errors)),
        selected_last_boot_line=start+1,excluded_prefix_lines=start,total_boot_markers=len(starts),
        fatal_lines=fatal,sections=sections,game_banner_count=len(garden),live_windows=len(live),
        live_fps10_min_reference=min((p.get('fps10',0) for p in live),default=None),
        live_fps10_max_reference=max((p.get('fps10',0) for p in live),default=None),
        game_perf_windows=len(perf),game_perf_seconds=elapsed,game_perf_updates=updates,
        game_mean_fps=updates/elapsed if elapsed else None,pure_windows=len(pure),
        pure_returns=len(resumed),all_required_health_zero=not any('health' in e or 'errors/' in e for e in errors),
        display_relock_markers=len(relock),display_ctrl4_mirror10=bool(display) and all(p.get('ctrl')==4 and p.get('q',0)&0x400 for p in display),
        qualification='Fresh selected boot only. No claim of cold power-on unless capture label and operator record establish it; no 600-second claim.')


def audit(args):
    out=Path(args.capture).resolve();raw_path=out/'serial.bin'
    meta=json.loads((out/'capture.json').read_text(encoding='utf-8-sig')) if (out/'capture.json').exists() else {}
    package=json.loads(Path(args.package_manifest).read_text(encoding='utf-8-sig')) if args.package_manifest else None
    raw=raw_path.read_bytes()
    # A disconnect may cut a line. Preserve raw exactly, but parse sessions with
    # explicit boundaries from recorded exception offsets; never join fragments.
    offsets=sorted({p['byte_offset'] for p in meta.get('serial_exceptions',[]) if 0<p['byte_offset']<len(raw)},reverse=True)
    framed=raw
    for off in offsets:framed=framed[:off]+b'\n'+framed[off:]
    result=audit_log(framed,package,args.min_live,args.min_game_seconds,args.require_t_return,args.require_relock)
    result.update(created_utc=stamp(),raw_sha256=sha(raw_path),capture=meta)
    if meta and meta.get('status')!='CAPTURE_COMPLETE':
        result['status']='FAIL';result['errors'].append('Capture did not complete cleanly')
    if meta.get('raw_sha256') and meta['raw_sha256']!=result['raw_sha256']:
        result['status']='FAIL';result['errors'].append('Capture raw hash changed')
    if meta.get('post_anchor_gap'):
        result['status']='FAIL';result['errors'].append('Serial disconnected after startup anchor; continuous capture not established')
    write_json(out/'boot_audit.json',result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('capture','sections')},ensure_ascii=True))
    return 0 if result['status']=='PASS' else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='operation',required=True)
    p=sub.add_parser('capture');p.add_argument('--port',default='COM11');p.add_argument('--baud',type=int,default=115200)
    p.add_argument('--seconds',type=float,default=90);p.add_argument('--wait-timeout',type=float,default=180)
    p.add_argument('--start-after',choices=('open','boot','garden','boot-or-garden','ready'),default='boot')
    p.add_argument('--toggle-at',type=float,action='append',default=[],help='Send t N seconds after fresh READY; repeat for return')
    p.add_argument('--reconnect',action='store_true',help='Retry the same COM port before startup; record post-start gaps and fail continuous audit')
    p.add_argument('--out',required=True);p.add_argument('--label',default='unspecified_boot_trigger')
    p=sub.add_parser('audit');p.add_argument('--capture',required=True);p.add_argument('--package-manifest')
    p.add_argument('--min-live',type=int,default=3);p.add_argument('--min-game-seconds',type=float,default=15)
    p.add_argument('--require-t-return',action='store_true')
    p.add_argument('--require-relock',action='store_true')
    args=parser.parse_args()
    if args.operation=='capture':
        if args.seconds<=0 or args.wait_timeout<=0 or any(n<0 for n in args.toggle_at):parser.error('Durations must be positive; toggle times nonnegative')
        return capture(args)
    if args.min_live<1 or args.min_game_seconds<0:parser.error('Invalid audit requirements')
    return audit(args)


if __name__=='__main__':raise SystemExit(main())
