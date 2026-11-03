from pathlib import Path
import subprocess,time,json,sys,hashlib
sys.path.insert(0,r'D:/Efinity/2026.1/python311/Lib/site-packages')
import serial
V=Path(__file__).resolve().parents[1]
O=Path(sys.argv[1]);O.mkdir(parents=True,exist_ok=False)
gdb=r'D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gdb.exe'
script='''set confirm off
set arch riscv:rv32
file "{elf}"
target extended-remote 127.0.0.1:13333
set $c_s0 = *(unsigned int*)0xf8100050
set $c_px = *(unsigned int*)0xf8100054
set $c_ln = *(unsigned int*)0xf8100058
set $c_fu = *(unsigned int*)0xf8100070
set $c_st = *(unsigned int*)0xf8100060
set $c_s1 = *(unsigned int*)0xf8100050
printf "[DISPLAY_PROBE] px=%u part=%u full=%u stall=0x%x before=0x%x after=0x%x\\n",$c_px,$c_ln,$c_fu,$c_st,$c_s0,$c_s1
monitor resume
detach
quit
'''.format(elf=(V/'build/gardenStage9Game.elf').as_posix())
results=[]
def read(which):
 p=O/(which+'.gdb');p.write_text(script)
 t=time.monotonic();run=subprocess.run([gdb,'-batch','-x',str(p)],capture_output=True,text=True,timeout=20)
 text=run.stdout+'\n'+run.stderr;(O/(which+'.log')).write_text(text)
 print(text,flush=True);run.check_returncode()
 results.append(dict(phase=which,duration_s=time.monotonic()-t))
read('baseline_readclear')
t=time.monotonic()
with serial.Serial('COM11',115200,timeout=.08) as p,(O/'uart.log').open('wb') as f:
 while time.monotonic()-t<20:
  b=p.read(max(1,p.in_waiting))
  if b:f.write(b);f.flush()
read('steady_end_readclear')
assert (O/'uart.log').stat().st_size>1000,'CPU did not stream updates between endpoint reads'
(O/'summary.json').write_text(json.dumps(dict(source='board',steady_duration_s=time.monotonic()-t,reads=results,
 note='GDB briefly halts at endpoints and detaches/resumes; not the FPS run. Read-clear display counters only; no reset, control, or Flash writes.'),indent=2))

