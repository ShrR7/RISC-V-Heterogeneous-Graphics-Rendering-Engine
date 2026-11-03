#!/usr/bin/env python3
"""Coherent firing appearance and pinned Y mechanics: host-only, no board access."""
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
BASELINE=ROOT.parent/'neon_variety_20261016'
PINS={'src/garden_core.c':'3BAF46CF6800CCCD3E51336242EDEFBF1152D041AD649A5512F38049B3BDF965',
      'src/garden_core.h':'0AFB38E790CF18AE1B10722FE9A05A94E9FE0828BF8B5677201D350460539641'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--cc',default=shutil.which('gcc') or r'D:\mingw64\bin\gcc.exe')
 p.add_argument('--out',type=Path,default=ROOT/'build/host/coherent_patterns')
 a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 for name,expected in PINS.items():
  if sha(BASELINE/name)!=expected:raise RuntimeError('Pinned accepted Y source changed: '+name)
 bridge=a.out/'baseline_bridge.c'
 bridge.write_text('#include <stddef.h>\n#include <string.h>\n'
  '#define garden_init baseline_garden_init\n#define garden_step baseline_garden_step\n'
  '#define garden_boss_fire_countdown baseline_garden_boss_fire_countdown\n'
  '#include '+json.dumps((BASELINE/'src/garden_core.c').as_posix())+'\n'
  'static GardenGame reference_game;\n'
  'void baseline_init(uint32_t seed){baseline_garden_init(&reference_game,seed);}\n'
  'void baseline_step(uint16_t buttons){baseline_garden_step(&reference_game,buttons);}\n'
  'size_t baseline_boss_size(void){return sizeof(GardenBoss);}\n'
  'size_t baseline_snapshot(void *out,size_t capacity){\n'
  ' GardenGame n=reference_game;if(capacity<sizeof(n))return sizeof(n);\n'
  ' for(unsigned i=0U;i<GARDEN_MAX_ENEMY_BULLETS;i++)n.enemy_bullets[i].colour=0U;\n'
  ' memset((unsigned char *)&n.boss+offsetof(GardenBoss,attack_bag)+1U,0,sizeof(GardenBoss)-offsetof(GardenBoss,attack_bag)-1U);\n'
  ' memcpy(out,&n,sizeof(n));return sizeof(n);}\n',encoding='utf-8')
 command=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-O2',str(ROOT/'tools/test_coherent_patterns.c'),str(bridge),'-o',str(a.out/'test_coherent_patterns.exe')]
 compile=subprocess.run(command,capture_output=True,text=True,timeout=60)
 (a.out/'compile.log').write_text(compile.stdout+compile.stderr,encoding='utf-8')
 if compile.returncode:raise RuntimeError('Compile failed; see '+str(a.out/'compile.log'))
 run=subprocess.run([str(a.out/'test_coherent_patterns.exe')],capture_output=True,text=True,timeout=120)
 (a.out/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8')
 found=re.search(r'RESULT checks=(\d+) enemy_events=(\d+) boss_trains=(\d+) replay_runs=(\d+) replay_steps=(\d+) state_bytes=(\d+)',run.stdout)
 metrics=dict(zip(('checks','enemy_events','boss_trains','replay_runs','replay_steps','state_bytes'),map(int,found.groups()))) if found else {}
 passed=run.returncode==0 and found is not None
 report={'schema':'garden-coherent-appearance-host-v1','created_utc':datetime.now(timezone.utc).isoformat(),
  'status':'PASS' if passed else 'FAIL','scope':'HOST only: consistent appearance and seeded mechanical equivalence; not board/FPS/visual acceptance',
  'command':command,'compile_exit':compile.returncode,'test_exit':run.returncode,'metrics':metrics,
  'bindings':{name:sha(ROOT/name) for name in ('src/garden_core.c','src/garden_core.h','tools/test_coherent_patterns.c','tools/verify_coherent_patterns.py','tools/COHERENT_PATTERNS.txt')},
  'baseline':{'variant':BASELINE.name,'source_sha256':PINS},
  'logs':{name:sha(a.out/name) for name in ('compile.log','host.log','baseline_bridge.c')},
  'contracts':{'enemy':'one type+colour for a single fire event, all six patterns/all five banks remain available; partial allocator behavior retained',
   'boss':'one type+colour for entire attack train,64/54/72 shots, initial and mid-train WAIT never rerolls, charge windows and laser unchanged',
   'mechanics':'eight full games, automatic/manual restart, pause, manual movement and bomb compare every byte to accepted Y except bullet colour and former Boss tail padding',
   'memory':'GardenGame9528B; new one-byte volley_colour occupies existing GardenBoss padding'},
  'limitations':['Coexisting projectiles from different past firing events may have different appearance.',
   'The same bank may be chosen in consecutive attacks; randomness does not imply mandatory alternation.',
   'No asset, FPGA, serial, display-underflow or frame-rate evidence is produced by this host test.']}
 (a.out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'status':report['status'],**metrics,'report':str(a.out/'verification.json')},ensure_ascii=False))
 return 0 if passed else 1
if __name__=='__main__':
 try:sys.exit(main())
 except (RuntimeError,OSError,subprocess.TimeoutExpired) as e:print('FAIL: '+str(e),file=sys.stderr);sys.exit(1)
