"""Board-only UART measurement of the immutable three-boss workload.

Run after board_run_fx_variant.ps1 has loaded cboss_hud_1103 and released COM11.
Only UART commands are sent; this tool does not reset/program either Flash.
"""
import argparse, csv, hashlib, json, re, sys, time
from pathlib import Path
sys.path.insert(0, r'D:/Efinity/2026.1/python311/Lib/site-packages')
import serial

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default='COM11')
    ap.add_argument('--scene',choices=('triple','t'),default='triple')
    ap.add_argument('--slots',default='21,23,25,26')
    ap.add_argument('--seconds',type=float,default=22)
    ap.add_argument('--hold',action='store_true',help='Leave the last mode running')
    ap.add_argument('--already-triple',action='store_true')
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args(); slots=[int(v) for v in args.slots.split(',')]
    if not all(8<=v<=33 for v in slots) or args.seconds<15:
        ap.error('Each stage needs >=15 seconds; legal slots 8..33 (91 bullets/slot)')
    args.out.mkdir(parents=True,exist_ok=False)
    data=bytearray(); events=[]; rows=[]; start=time.monotonic(); pending=''
    def event(kind,**kv):
        e=dict(kind=kind,host_s=time.monotonic()-start,**kv); events.append(e)
        print(json.dumps(e),flush=True)
    def fields(line):
        return {k:int(v,0) for k,v in re.findall(r'(\w+)=(0x[0-9a-fA-F]+|\d+)',line)}
    def read_for(port,duration):
        nonlocal pending
        end=time.monotonic()+duration
        while time.monotonic()<end:
            chunk=port.read(max(1,port.in_waiting))
            if not chunk: continue
            data.extend(chunk); raw.write(chunk);raw.flush()
            pending+=chunk.decode('ascii','replace')
            while '\n' in pending:
                line,pending=pending.split('\n',1)
                if '[PURE]' in line:
                    row=fields(line);row['host_s']=time.monotonic()-start
                    rows.append(row); print(line.strip(),flush=True)
                elif any(t in line for t in ('[TRIPLE','[PURE_START','[PURE_WARMUP','[PURE_FAIL','[PURE_EXIT','[PURE_PERIOD','[PURE_PREP','[T_LEFT','[PURE_LEFT')):
                    print(line.strip(),flush=True)
                if '[PURE_FAIL]' in line or '[FATAL]' in line:
                    raise RuntimeError('Device reported failure; stop and preserve first-fault snapshot')
    try:
        with (args.out/'uart.log').open('wb') as raw, serial.Serial(args.port,115200,timeout=.08,write_timeout=2) as port:
            read_for(port,1)
            if not args.already_triple:
                key=b'b' if args.scene=='triple' else b't'
                port.write(key);event('tx',ascii=key.decode(),action='enter_'+args.scene)
                read_for(port,4)
            for n in slots:
                port.write(('c%d\n'%n).encode());event('tx',ascii='c%d\\n'%n,expected_count=n*91)
                read_for(port,args.seconds)
            if not args.hold:
                key=b'b' if args.scene=='triple' else b't'
                port.write(key);event('tx',ascii=key.decode(),action='return_game')
                read_for(port,3)
    finally:
        (args.out/'events.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
        if rows:
            keys=sorted(set().union(*(r.keys() for r in rows)))
            with (args.out/'windows.csv').open('w',newline='',encoding='utf-8') as f:
                w=csv.DictWriter(f,keys);w.writeheader();w.writerows(rows)
        summary=[]
        for n in slots:
            selected=[r for r in rows if r.get('count')==91*n]
            if not selected: continue
            # Entry may already use this count. Keep the last commanded
            # generation only; never concatenate two steady runs across prep.
            generation=max(r['generation'] for r in selected)
            selected=[r for r in selected if r['generation']==generation]
            ticks=sum(r['tick_lo']+(r.get('tick_hi',0)<<32) for r in selected)
            hz=selected[0]['hz']; updates=sum(r['updates'] for r in selected)
            bad={k:max(r.get(k,0) for r in selected) for k in ('miss_px','miss_lines','reject','skip','errors','guard','log_dropped','log_truncated')}
            s=dict(count=n*91,generation=generation,windows=len(selected),duration_s=ticks/hz,updates=updates,
                   average_update_fps=updates*hz/ticks,min_visible=min(r['screen_min'] for r in selected),
                   max_visible=max(r['screen_max'] for r in selected),checks=bad)
            s['mean60_and_clean']=s['duration_s']>=15 and s['average_update_fps']>=60 and not any(bad.values())
            summary.append(s)
        mode=('three static bosses,60 degree fans,movable player' if args.scene=='triple' else 'original T,single point,180 degree fan,no player')
        result=dict(source='Ti60 real board UART',mode=mode+',seven16x16styles,no full game logic/collision/skills',scene=args.scene,
                    uart_sha256=hashlib.sha256(data).hexdigest(),results=summary)
        (args.out/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps(result,indent=2),flush=True)

if __name__=='__main__':main()
