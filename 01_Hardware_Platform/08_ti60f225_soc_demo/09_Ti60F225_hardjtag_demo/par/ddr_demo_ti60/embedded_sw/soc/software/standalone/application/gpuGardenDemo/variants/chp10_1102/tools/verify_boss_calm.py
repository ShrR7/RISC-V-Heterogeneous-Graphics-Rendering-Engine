#!/usr/bin/env python3
"""Focused cadence verification of actual production cores; no hardware."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PARENT=ROOT.parent/'neon_wave_fixed_20261022'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def no_comments(s):return re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
def erase(s,name):
 m=re.search(r'(?m)^static\s+void\s+'+name+r'\s*\([^;]*?\)\s*\{',s);assert m,'missing '+name
 start=s.index('{',m.start());depth=1;end=start+1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[:start]+'{ BODY_CHECKED_SEPARATELY }'+s[end:]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');args=p.parse_args()
 build=ROOT/'build/host/boss_calm';build.mkdir(parents=True,exist_ok=True)
 before={f.name:sha(f) for f in (ROOT/'src').iterdir() if f.is_file()};parent_before={f.name:sha(f) for f in (PARENT/'src').iterdir() if f.is_file()}
 assert before.keys()==parent_before.keys(),'runtime file list changed'
 assert all(before[k]==v for k,v in parent_before.items() if k!='garden_core.c'),'unexpected runtime edit outside core'
 old=no_comments((PARENT/'src/garden_core.c').read_text());new=no_comments((ROOT/'src/garden_core.c').read_text())
 assert re.sub(r'\s+','',erase(old,'run_boss'))==re.sub(r'\s+','',erase(new,'run_boss')),'code outside run_boss changed'
 guard='if(g->rhythm.bursts_completed%3U)b->target_x=b->x;'
 assert re.sub(r'\s+','',new).count(guard)==1,'expected single3-group target guard missing/duplicated'
 assert re.sub(r'\s+','',new).replace(guard,'')==re.sub(r'\s+','',old),'core body changed beyond target guard'
 cmd=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(ROOT/'src'),str(ROOT/'tools/test_boss_calm.c'),str(ROOT/'tools/test_boss_calm_parent.c'),'-o',str(build/'test_boss_calm.exe')]
 subprocess.run(cmd,check=True);r=subprocess.run([str(build/'test_boss_calm.exe')],capture_output=True,text=True);out=r.stdout+r.stderr;(build/'test.log').write_text(out,encoding='utf-8');print(out,end='')
 if r.returncode:raise SystemExit(r.returncode)
 m=re.search(r'PASS boss_calm checks=(\d+) boss_snapshots=(\d+) wave_steps=(\d+) game_bytes=(\d+)',out);c=re.search(r'CADENCE rest_cases=(\d+) sequence_steps=(\d+) holds=(\d+) moves=(\d+) natural_cross_rest=(\d+) laser_warn=(\d+) laser_fire=(\d+) laser_fade=(\d+) wait_cases=(\d+)',out)
 assert m and c,'focused test did not complete'
 assert before=={f.name:sha(f) for f in (ROOT/'src').iterdir() if f.is_file()},'candidate source changed during test'
 assert parent_before=={f.name:sha(f) for f in (PARENT/'src').iterdir() if f.is_file()},'parent source changed during test'
 files=[ROOT/'src/garden_core.c',ROOT/'src/garden_core.h',ROOT/'src/main.c',ROOT/'tools/test_boss_calm.c',ROOT/'tools/test_boss_calm_parent.c',Path(__file__)]
 report={'status':'PASS','host_only':True,'board_acceptance':False,'checks':int(m[1]),'boss_identical_snapshots':int(m[2]),'wave_identical_steps':int(m[3]),'game_bytes':int(m[4]),'direct_rest_cases':int(c[1]),'sequence_steps':int(c[2]),'held_slots':int(c[3]),'moving_slots':int(c[4]),'natural_laser_rest_crossings':int(c[5]),'laser_warn_steps':int(c[6]),'laser_fire_steps':int(c[7]),'laser_fade_steps':int(c[8]),'wait_cases':int(c[9]),'compiler_command':cmd,'core_sha256':sha(ROOT/'src/garden_core.c'),'parent_core_sha256':sha(PARENT/'src/garden_core.c'),'source_sha256':{str(f.relative_to(ROOT)).replace('\\','/'):sha(f) for f in files},'parent_source_sha256':parent_before,'contracts':['Every first/second completed train retains original32..56tick transit slot but targets current x; every third retains original random destination.','Paired actual run_laser/run_boss sequences cover16seeds x3HPbands x3000steps, same100tick entrance, identical PRNG/timers/train order/ledger and projectile properties except horizontal translation.','WARN/FIRE/FADE remain anchored across REST and retain40/40/12tick duration; natural and constructed low-HP crossings covered.','Capacity WAIT retains announced train and all timing values without reroll/counting another train; pause/unpause behavior matches parent.','768 same-input Boss generator snapshots and19200 actual fixed-wave steps are byte-identical.','Strict source boundary permits only one3-group target guard in core; unchanged header means no new fields.'],'limits':['Host-only; no visual/FPS/display/DDR acceptance claim.','Sequence fixture drains bullets each tick to isolate cadence/PRNG from live-pool occupancy and player combat. Dedicated WAIT fixtures separately cover capacity blocking.','Different Boss positions intentionally change world-space projectile origins and can change actual pilot/combat outcomes.','The change does not proactively remove preparation intervals. This is not a claim of identical full-game frame timing: position-dependent bullet lifetimes and capacity WAIT can change later random-history timing during real play.']}
 (build/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
