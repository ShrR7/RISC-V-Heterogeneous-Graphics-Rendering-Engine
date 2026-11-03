#!/usr/bin/env python3
"""Targeted actual-core wave balance checks. No board/driver/renderer tests."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PARENT=ROOT.parent/'neon_batch_lazy_20261021'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def without_comments(s):return re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
def erase_function(s,name):
 m=re.search(r'\b'+name+r'\s*\([^;]*?\)\s*\{',s)
 if not m:raise AssertionError('missing function '+name)
 start=s.index('{',m.start());depth=1;end=start+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[:start]+'{ BODY_CHECKED_SEPARATELY }'+s[end:]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');a=p.parse_args()
 build=ROOT/'build/host/wave_density';build.mkdir(parents=True,exist_ok=True)
 before={str(f.relative_to(PARENT)).replace('\\','/'):sha(f) for f in (PARENT/'src').iterdir() if f.is_file()}
 assert (ROOT/'src/garden_core.h').read_bytes()==(PARENT/'src/garden_core.h').read_bytes(),'core header changed'
 old=without_comments((PARENT/'src/garden_core.c').read_text());new=without_comments((ROOT/'src/garden_core.c').read_text())
 for name in ['stopper_fire','enemy_fire','run_enemies']:old=erase_function(old,name);new=erase_function(new,name)
 assert re.sub(r'\s+','',old)==re.sub(r'\s+','',new),'unexpected change outside three wave firing/scheduling functions'
 cmd=[a.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/'tools/test_wave_density.c'),str(ROOT/'tools/test_wave_density_parent.c'),'-o',str(build/'test_wave_density.exe')]
 subprocess.run(cmd,check=True);r=subprocess.run([str(build/'test_wave_density.exe')],capture_output=True,text=True);out=r.stdout+r.stderr;(build/'test.log').write_text(out);print(out,end='')
 if r.returncode:raise SystemExit(r.returncode)
 m=re.search(r'PASS wave_density checks=(\d+) direct_seeds=(\d+) cycle_seeds=(\d+) small_steps=(\d+) boss_snapshots=(\d+) game_bytes=(\d+)',out)
 if not m:raise SystemExit('No completed focused core contract')
 assert before=={str(f.relative_to(PARENT)).replace('\\','/'):sha(f) for f in (PARENT/'src').iterdir() if f.is_file()},'parent runtime changed'
 report={'status':'PASS','host_only':True,'board_acceptance':False,'checks':int(m[1]),'direct_seeds':int(m[2]),'cycle_seeds':int(m[3]),'small_enemy_identical_steps':int(m[4]),'boss_identical_snapshots':int(m[5]),'game_bytes':int(m[6]),'compiler_command':cmd,'core_sha256':sha(ROOT/'src/garden_core.c'),'parent_core_sha256':sha(PARENT/'src/garden_core.c'),'source_sha256':{str(f.relative_to(ROOT)).replace('\\','/'):sha(f) for f in [ROOT/'src/garden_core.c',ROOT/'src/garden_core.h',ROOT/'tools/test_wave_density.c',ROOT/'tools/test_wave_density_parent.c',Path(__file__)]},'parent_source_sha256':{'src/garden_core.c':sha(PARENT/'src/garden_core.c'),'src/garden_core.h':sha(PARENT/'src/garden_core.h')},'contracts':['4096seeds cover all three9/8/6 large volleys, uniform event style/color and120..160tick cooldown','Two persistent STOPPER targets across32seeds x3400real run_enemies steps: every full150tick attack window new1/parent2 volleys,40..64tick initial/re-entry delay, cumulative emission reduction without full-pool censorship','Small direct events and38400real movement/firing steps byte-identical with frozen parent','768 Boss full-state snapshots cover three generators and all four status branches','Strict code-body comparison proves all code outside the three changed wave functions unchanged; core header identical','Parent runtime source hashes preserved'],'limits':['No hardware access, no visual/FPS/DDR/display acceptance claim.','Schedule density fixture drains projectiles every tick to avoid pool-drop censorship; cumulative emissions are measured, not live screen occupancy.','Wave changes intentionally change gameplay and RNG history over a full run; unchanged Boss generator equivalence is for identical entering state/seed, not identical full-game attack order.']}
 (build/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
if __name__=='__main__':main()
