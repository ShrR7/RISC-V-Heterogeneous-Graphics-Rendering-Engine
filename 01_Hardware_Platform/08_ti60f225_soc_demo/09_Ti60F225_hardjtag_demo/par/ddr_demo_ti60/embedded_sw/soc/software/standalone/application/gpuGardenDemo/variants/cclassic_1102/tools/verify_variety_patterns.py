#!/usr/bin/env python3
"""Host-only bounded random schedule/replay checks; no hardware or FPS claim."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--cc',default=shutil.which('gcc') or r'D:\mingw64\bin\gcc.exe')
 p.add_argument('--out',type=Path,default=ROOT/'build/host/variety_patterns')
 a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 command=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-O2',str(ROOT/'tools/test_variety_patterns.c'),'-o',str(a.out/'test_variety_patterns.exe')]
 compile=subprocess.run(command,capture_output=True,text=True,timeout=60)
 (a.out/'compile.log').write_text(compile.stdout+compile.stderr,encoding='utf-8')
 if compile.returncode:raise RuntimeError('Compile failed; see '+str(a.out/'compile.log'))
 run=subprocess.run([str(a.out/'test_variety_patterns.exe')],capture_output=True,text=True,timeout=120)
 (a.out/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8')
 found=re.search(r'RESULT checks=(\d+) runs=(\d+) wins=(\d+) state_bytes=(\d+)',run.stdout)
 metrics=dict(zip(('checks','runs','wins','state_bytes'),map(int,found.groups()))) if found else {}
 runs=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('RUN ')]
 passed=run.returncode==0 and found is not None
 report={'schema':'garden-variety-patterns-host-v1','created_utc':datetime.now(timezone.utc).isoformat(),
  'status':'PASS' if passed else 'FAIL','scope':'HOST/SYNTHETIC and actual seeded games; not board/FPS acceptance',
  'command':command,'compile_exit':compile.returncode,'test_exit':run.returncode,'metrics':metrics,'runs':runs,
  'bindings':{name:sha(ROOT/name) for name in ('src/garden_core.c','src/garden_core.h','tools/test_variety_patterns.c','tools/verify_variety_patterns.py','tools/VARIETY_PATTERNS.txt')},
  'logs':{name:sha(a.out/name) for name in ('compile.log','host.log')},
  'contracts':{'boss':'3-item shuffle bag, no adjacent repeat, all3 types each complete bag, existing64/9/36 pulses and80-live complete train reservation',
   'time':'charge old HP-band minimum +0..8 ticks, rest +0..24, gap +0..2 or+0..4; transit32..56; WAIT freezes draw/type',
   'enemy':'3 small styles1/3/3 shots and3 large18/12/15 shots; first8..24/12..32, repeat36..60/80..112 ticks; all5 appearance banks',
   'restart':'nonzero xorshift successor(initial_seed), independent of run duration; same first seed and input reproduce every byte',
   'unchanged':'40Hz,6tick arming/collision,600physical bomb,healing,strict10actualspawns+10kills,asymmetric laser mounts,40warning/40fire/12fade'},
  'limitations':['Randomness is deterministic gameplay variety, not cryptographic randomness.',
    'Sixteen seeded games are sampled balance evidence, not a guarantee of every seed winning.',
    'Host runtime does not measure RV32 performance; atlas, rendering and board acceptance are separate.']}
 (a.out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'status':report['status'],**metrics,'report':str(a.out/'verification.json')},ensure_ascii=False))
 return 0 if passed else 1
if __name__=='__main__':
 try:sys.exit(main())
 except (RuntimeError,OSError,subprocess.TimeoutExpired) as e:print('FAIL: '+str(e),file=sys.stderr);sys.exit(1)
