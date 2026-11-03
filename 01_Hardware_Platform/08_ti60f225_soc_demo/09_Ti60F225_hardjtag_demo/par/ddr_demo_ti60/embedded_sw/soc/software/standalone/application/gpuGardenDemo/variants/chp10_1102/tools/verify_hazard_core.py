#!/usr/bin/env python3
"""W-only host verification. No UART, JTAG, MMIO or board access."""
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
PINS={
 'tools/reference_hazard_activation/garden_core.c':'EF68441A916F7C7D30FD3DC618F07E433941DC81013A43332DFF29C142AC56CE',
 'tools/reference_hazard_activation/garden_core.h':'AA3D4CFC61388DE851C16820125E517A7CD7A1E2FF813AB12E060AFE91B45066',
 'tools/reference_ordnance_collision/garden_core.c':'F94324282912069E6278DDE447F95609BDE824E779DB30B8E994C15A8EC4924A',
 'tools/reference_ordnance_collision/garden_core.h':'FBBB3AC70B6BC44EDC8F8335091519448C8D8C63DB10AEC6C89EF2C354B472C2',
}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--cc',default=shutil.which('gcc') or r'D:\mingw64\bin\gcc.exe')
 p.add_argument('--out',type=Path,default=ROOT/'build/host/hazard_core')
 args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
 for name,value in PINS.items():
  if sha(ROOT/name)!=value:raise RuntimeError('Historical source changed: '+name)
 command=[args.cc,'-std=c11','-Wall','-Wextra','-Werror','-O2',
  str(ROOT/'tools/test_hazard_core.c'),str(ROOT/'tools/reference_ordnance_collision/bridge.c'),
  str(ROOT/'tools/reference_hazard_activation/bridge.c'),'-o',str(args.out/'test_hazard_core.exe')]
 compiled=subprocess.run(command,capture_output=True,text=True,timeout=60)
 (args.out/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
 if compiled.returncode:raise RuntimeError('Compile failed; see compile.log')
 run=subprocess.run([str(args.out/'test_hazard_core.exe')],capture_output=True,text=True,timeout=60)
 (args.out/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8')
 found=re.search(r'RESULT checks=(\d+) runs=(\d+) wins=(\d+) state_bytes=(\d+) event_max=(\d+)',run.stdout)
 metrics=dict(zip(('checks','runs','wins','state_bytes','event_max'),map(int,found.groups()))) if found else {}
 runs=[]
 for line in run.stdout.splitlines():
  if line.startswith('RUN '):runs.append(dict(re.findall(r'(\w+)=([^ ]+)',line)))
  elif line.startswith('RESTART '):
   row=dict(re.findall(r'(\w+)=([^ ]+)',line))
   next(item for item in runs if item['seed']==row['seed'])['restart_best']=int(row['best'])
 passed=run.returncode==0 and found is not None
 files=['src/garden_core.c','src/garden_core.h','tools/test_hazard_core.c','tools/verify_hazard_core.py',
  'tools/HAZARD_CORE_TESTS.txt','tools/reference_hazard_activation/bridge.c',
  'tools/reference_ordnance_collision/bridge.c',*PINS]
 report={'schema':'garden-hazard-host-v1','created_utc':datetime.now(timezone.utc).isoformat(),
  'status':'PASS' if passed else 'FAIL','scope':'HOST/SYNTHETIC fixtures and actual seeded core steps; no board/FPS acceptance',
  'command':command,'compile_exit':compiled.returncode,'test_exit':run.returncode,
  'metrics':metrics,'runs':runs,'bindings':{name:sha(ROOT/name) for name in files},
  'logs':{name:sha(args.out/name) for name in ('compile.log','host.log')},
  'baseline_reproduction':{'reference':'pinned V default seed12345','unprotected_unarmed_contact_ticks':53,
    'max_unarmed_age_ticks':170,'exact_contact_tick995':{'player':[232837,320657],
    'bullet':[233321,322469],'age':48,'old_wait':2,'old_grace':0,'old_respawn':0},
    'test':'real V baseline is rerun; exact contact coordinates then exercised against W collision, produces one normal life loss'},
  'rules':{'arming':'all normal bullets age0..5 forming, age6 onward dangerous; helper used by AI/graze/collision/cull; cord_wait is diagnostic mirror only',
    'first_armed':'current endpoint only; no retroactive damage during forming trajectory',
    'player_protection':'original grace80, death grace180 and respawn50 retained; demo shares damage code',
    'diagnostics':'hazard.armed/forming are current active counts; armed_contacts/forming_contacts accumulate unprotected original9px-square endpoint bullet-tick contacts before damage; simultaneous contacts do not imply several life losses',
    'pilot':'9 manual-speed actions axis6000/diagonal4242/still; bounded2/5/9/14tick forecasts of existing bullets, remaining acceleration/arming, laser, enemy bodies and boundaries; no future RNG or immunity',
    'score':'actual per-tick hit+kill+graze+pickup ledger and high score retention; no fixed result score',
    'ordnance':'V physical600damage/no-clear/no-bonus-grace retained; finite2stock; missed projectiles stay genuine misses',
    'laser':'V40warn/40fire/12fade and13px core retained; body/pod geometry anchored through sequence',
    'memory':'12B hazard diagnostics, no new per-projectile state or heap'},
  'limitations':['Host test does not verify W atlas pivots or motion synchronization; their independent renderer checks and board observation are required.',
    'Eight seeded victories demonstrate selected deterministic runs, not an invincible pilot or a general no-death guarantee.',
    'Pilot CPU timing must be measured on RV32 board; host runtime is not an FPS result.']}
 (args.out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'status':report['status'],**metrics,'default':runs[0] if runs else None,
   'report':str(args.out/'verification.json')},ensure_ascii=False))
 return 0 if passed else 1
if __name__=='__main__':
 try:sys.exit(main())
 except (RuntimeError,OSError,subprocess.TimeoutExpired) as exc:
  print('FAIL: '+str(exc),file=sys.stderr);sys.exit(1)
