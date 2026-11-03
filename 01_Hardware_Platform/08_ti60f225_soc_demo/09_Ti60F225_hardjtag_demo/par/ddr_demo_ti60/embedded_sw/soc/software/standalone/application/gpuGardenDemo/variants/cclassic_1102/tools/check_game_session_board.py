"""UART-only real-board interaction check, starting in three-boss mode.
No Flash, debugger writes, fabricated controller input or forced game state.
"""
import hashlib,json,sys,time
from pathlib import Path
sys.path.insert(0,r'D:/Efinity/2026.1/python311/Lib/site-packages')
import serial
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
schedule=[(0,b'b','leave three bosses'),(2,b'r','restart normal game'),
          (3,b'u','select manual via UART'),(6,b'q','throw enlarged bomb'),
          (10,b'q','throw enlarged bomb'),(116,b't','enter original T'),
          (136,b't','return from T'),(140,b'r','manual restart'),(153,b'', 'end')]
start=time.monotonic(); events=[]; data=bytearray(); lines=[]; partial=''
with (out/'uart.log').open('wb') as raw,serial.Serial('COM11',115200,timeout=.08,write_timeout=2) as port:
    step=0
    while step<len(schedule):
        now=time.monotonic()-start
        if now>=schedule[step][0]:
            _,command,action=schedule[step]
            if command:port.write(command)
            event=dict(host_s=now,ascii=command.decode(),action=action);events.append(event)
            print(json.dumps(event),flush=True);step+=1
        chunk=port.read(max(1,port.in_waiting))
        if not chunk:continue
        raw.write(chunk);raw.flush();data.extend(chunk);partial+=chunk.decode('ascii','replace')
        while '\n' in partial:
            line,partial=partial.split('\n',1);lines.append(line)
            if any(t in line for t in ('[GAME]','[IDLE_CTRL]','[GAMEOVER_IDLE]','[RUN_SEED]','[INPUT_STATE]','[PURE_GAME_RESUMED]','[STAGE]','[ORDNANCE]','[LIVE]','[PURE_FAIL]','[FATAL]')):
                print(line.strip(),flush=True)
            if '[FATAL]' in line or '[PURE_FAIL]' in line:raise RuntimeError('Device failure; preserve snapshot')
(out/'events.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
result=dict(source='COM11 real board',uart_sha256=hashlib.sha256(data).hexdigest(),
            actual_host_s=time.monotonic()-start,controller_injected=False,
            idle_auto_observed=any('[IDLE_CTRL] demo=1' in s for s in lines),
            natural_gameover_observed=any('[GAME]' in s and 'mode=3 ' in s for s in lines),
            gameover_15s_restart_observed=any('[GAMEOVER_IDLE]' in s for s in lines),
            return_events=[s for s in lines if '[PURE_GAME_RESUMED]' in s],
            warnings=[s for s in lines if '[STAGE]' in s and 'warning' in s.lower()],
            note='UART u selects manual; subsequent real-time idle takeover is observed. Real Joy-Con motion is a separate check.')
(out/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2),flush=True)
