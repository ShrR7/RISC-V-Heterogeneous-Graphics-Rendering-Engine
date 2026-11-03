import json,time,sys,hashlib
from pathlib import Path
sys.path.insert(0,r'D:/Efinity/2026.1/python311/Lib/site-packages')
import serial
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
start=time.monotonic();raw=bytearray();events=[];partial='';lines=[]
def drain(p,secs,f):
 global partial
 end=time.monotonic()+secs
 while time.monotonic()<end:
  b=p.read(max(1,p.in_waiting))
  if not b:continue
  raw.extend(b);f.write(b);f.flush();partial+=b.decode('ascii','replace')
  while '\n' in partial:
   s,partial=partial.split('\n',1);lines.append(s)
   if any(x in s for x in ('[IDLE_CTRL]','[INPUT_STATE]','[PURE_GAME_RESUMED]','[TRIPLE','[PURE]','[PURE_FAIL]')):print(s,flush=True)
def tx(p,b,meaning):
 p.write(b);e=dict(host_s=time.monotonic()-start,tx=b.decode(),action=meaning);events.append(e);print(e,flush=True)
with serial.Serial('COM11',115200,timeout=.08) as p,(out/'uart.log').open('wb') as f:
 tx(p,b'r','restart normal');drain(p,.5,f)
 tx(p,b'u','UART selects manual');drain(p,.5,f)
 tx(p,b'p','pause so no death before10s');drain(p,11.5,f)
 tx(p,b'p','resume after idle takeover');drain(p,2,f)
 tx(p,b'b','leave final2730 threeboss visible');drain(p,9,f)
result=dict(source='real COM11',controller_injected=False,events=events,uart_sha256=hashlib.sha256(raw).hexdigest(),
 idle_records=[s for s in lines if '[IDLE_CTRL]' in s],input_records=[s for s in lines if '[INPUT_STATE]' in s],
 final_triple_ready=[s for s in lines if '[TRIPLE_READY]' in s],elapsed_s=time.monotonic()-start)
(out/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))

