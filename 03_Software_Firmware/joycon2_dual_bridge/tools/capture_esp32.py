"""Capture only the ESP32 diagnostic USB UART; never open the FPGA port."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
import time

try:
    import serial
except ModuleNotFoundError:
    # Efinity already bundles pure-Python pyserial; no global pip installation.
    vendor=Path('D:/Efinity/2026.1/python311/Lib/site-packages')
    if not (vendor/'serial/__init__.py').exists():
        raise
    sys.path.insert(0,str(vendor))
    import serial

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port',default='COM22')
    parser.add_argument('--seconds',type=float,default=30)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--stop-file',type=Path)
    args=parser.parse_args()
    if args.port.upper()=='COM11':
        raise SystemExit('COM11 is reserved for the FPGA; this tool accepts only the ESP32 UART')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    lines=[];states=[];connected=[];fatal=[]
    device=serial.Serial(port=None,baudrate=115200,timeout=0.2)
    device.dtr=False;device.rts=False;device.port=args.port
    device.open()
    start=time.monotonic();pending=bytearray()
    try:
        with args.out.open('x',encoding='utf-8') as out:
            out.write(f"# {datetime.now(timezone.utc).isoformat()} ESP32 {args.port} only\n")
            while time.monotonic()-start<args.seconds:
                if args.stop_file and args.stop_file.exists():
                    break
                chunk=device.read(max(1,device.in_waiting))
                pending.extend(chunk)
                while b'\n' in pending:
                    line,_,rest=pending.partition(b'\n');pending=bytearray(rest)
                    text=line.decode('utf-8',errors='replace').rstrip('\r')
                    out.write(text+'\n');out.flush();lines.append(text)
                    if text.startswith('[J2_STATE]'):
                        state={k:int(v,16 if k in ('btn','dpad','misc') else 10)
                               for k,v in re.findall(r'(\w+)=(-?[0-9a-f]+)',text)}
                        states.append(state)
                    if text.startswith(('[J2_ADVERTISEMENT]','[J2_CONNECTED]','[J2_DISCONNECTED]','[J2_INPUT]',
                                        '[J2_BAD_REPORT]','[J2_GATT_FAILED]','[J2_CONNECT_FAILED]','[J2_FATAL]',
                                        '[J2_BUTTONS]')):
                        print(text,flush=True)
                        connected.append(text)
                    if 'Guru Meditation' in text or '[J2_FATAL]' in text or 'abort()' in text:
                        fatal.append(text)
    finally:
        device.close()
    summary={
        'port':args.port,'seconds':time.monotonic()-start,'lines':len(lines),'states':len(states),
        'both_fresh_samples':sum(s.get('L')==1 and s.get('R')==1 for s in states),
        'left_fresh_samples':sum(s.get('L')==1 for s in states),
        'right_fresh_samples':sum(s.get('R')==1 for s in states),
        'movement_samples':sum(abs(s.get('LX',0))>=160 or abs(s.get('LY',0))>=160 for s in states),
        'left_selected_samples':sum(s.get('src')==1 for s in states),
        'right_selected_samples':sum(s.get('src')==2 for s in states),
        'button_changes':sum(line.startswith('[J2_BUTTONS]') for line in lines),
        'right_button_samples':sum(bool(s.get('btn',0)&3) for s in states),
        'pause_samples':sum(bool(s.get('misc',0)&4) for s in states),
        'focus_samples':sum(bool(s.get('btn',0)&0x30) for s in states),
        'last_state':states[-1] if states else None,'fatal':fatal,'events':connected,
        'scope':'ESP32 BLE/merged output only; does not prove FPGA reception or game response',
    }
    args.out.with_suffix('.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in summary.items() if k!='events'},ensure_ascii=False),flush=True)
    return 1 if fatal else 0

if __name__=='__main__':
    raise SystemExit(main())
