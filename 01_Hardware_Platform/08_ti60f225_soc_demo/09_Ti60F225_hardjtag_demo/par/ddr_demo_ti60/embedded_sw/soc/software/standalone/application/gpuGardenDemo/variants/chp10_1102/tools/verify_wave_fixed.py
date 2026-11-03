#!/usr/bin/env python3
"""Focused actual-core and actual-asset checks; no board operations."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PARENT=ROOT.parent/'neon_wave_light_20261022'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def without_comments(s):return re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
def erase_function(s,name,optional=False,whole=False):
 m=re.search(r'(?m)^static\s+void\s+'+name+r'\s*\([^;]*?\)\s*\{',s)
 if not m:
  if optional:return s
  raise AssertionError('missing function '+name)
 start=s.index('{',m.start());depth=1;end=start+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[:m.start()]+s[end:] if whole else s[:start]+'{ BODY_CHECKED_SEPARATELY }'+s[end:]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');a=p.parse_args()
 build=ROOT/'build/host/wave_fixed';build.mkdir(parents=True,exist_ok=True)
 parent_before={f.name:sha(f) for f in (PARENT/'src').iterdir() if f.is_file()}
 candidate_before={f.name:sha(f) for f in (ROOT/'src').iterdir() if f.is_file()}
 assert candidate_before.keys()==parent_before.keys(),'runtime file list changed'
 assert all(candidate_before[k]==v for k,v in parent_before.items() if k not in ['garden_core.c','main.c']),'unexpected runtime edit outside core/main metadata'
 old_main=(PARENT/'src/main.c').read_text();new_main=(ROOT/'src/main.c').read_text()
 assert new_main.count(' enemy_scope=type')==1,'missing or duplicated enemy-scope metadata'
 assert new_main.replace(' enemy_scope=type','')==old_main,'unexpected main edit beyond enemy-scope audit metadata'
 old=without_comments((PARENT/'src/garden_core.c').read_text());new=without_comments((ROOT/'src/garden_core.c').read_text())
 old=erase_function(old,'enemy_fire');new=erase_function(new,'enemy_fire')
 assert re.sub(r'\s+','',old)==re.sub(r'\s+','',new),'unexpected change outside enemy_fire'
 cmd=[a.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(ROOT/'src'),str(ROOT/'tools/test_wave_fixed.c'),str(ROOT/'tools/test_wave_fixed_parent.c'),str(ROOT/'src/garden_assets.c'),'-o',str(build/'test_wave_fixed.exe')]
 subprocess.run(cmd,check=True);r=subprocess.run([str(build/'test_wave_fixed.exe')],capture_output=True,text=True);out=r.stdout+r.stderr;(build/'test.log').write_text(out,encoding='utf-8');print(out,end='')
 if r.returncode:raise SystemExit(r.returncode)
 m=re.search(r'PASS wave_fixed checks=(\d+) boss_snapshots=(\d+) game_bytes=(\d+)',out)
 cycles=re.search(r'CYCLES seeds=32 ticks=3400 targets=2 closed=(\d+) shots=(\d+) projectiles=(\d+) reentries=(\d+)',out)
 direct=re.search(r'FIXED events=(\d+) bullets=(\d+)',out)
 if not (m and cycles and direct):raise SystemExit('focused test did not complete')
 assert parent_before=={f.name:sha(f) for f in (PARENT/'src').iterdir() if f.is_file()},'parent runtime changed'
 assert candidate_before=={f.name:sha(f) for f in (ROOT/'src').iterdir() if f.is_file()},'candidate runtime changed while testing'
 files=[ROOT/'src/garden_core.c',ROOT/'src/garden_core.h',ROOT/'src/garden_assets.c',ROOT/'src/garden_assets.h',ROOT/'src/main.c',ROOT/'tools/test_wave_fixed.c',ROOT/'tools/test_wave_fixed_parent.c',Path(__file__)]
 report={'status':'PASS','host_only':True,'board_acceptance':False,'checks':int(m[1]),'boss_identical_snapshots':int(m[2]),'game_bytes':int(m[3]),'direct_seeds':1024,'direct_model_count':2,'direct_entity_count':4,'direct_repeated_shots':8,'direct_events':int(direct[1]),'direct_projectiles':int(direct[2]),'all_angles_per_model':1024,'orientation_descriptors_per_model':64,'cycle_seeds':32,'cycle_ticks':3400,'cycle_entities':2,'complete_attack_windows':int(cycles[1]),'cycle_volleys':int(cycles[2]),'cycle_projectiles':int(cycles[3]),'cycle_reentries':int(cycles[4]),'compiler_command':cmd,'core_sha256':sha(ROOT/'src/garden_core.c'),'parent_core_sha256':sha(PARENT/'src/garden_core.c'),'source_sha256':{str(f.relative_to(ROOT)).replace('\\','/'):sha(f) for f in files},'parent_source_sha256':parent_before,'contracts':['Same small model always emits one aimed three-shot DART fan with colour1; same large model always emits one evenly spaced eight-shot DIAMOND ring with colour4.','1024 seeds x2 models x4 entities x8 successive firing events retain physical type, bank, count and shape while the RNG advances.','Actual garden_bullet_precision_id is linked and its family/colour bank checked for emitted bullets and all1024 headings per model; only64 orientation descriptors are used.','Actual run_enemies retains one volley in every complete150tick large-enemy attack window,40..64 initial/re-entry delay and120..160 recycle.','768 same-entering-state Boss snapshots are byte-identical to the low-density parent; all code outside enemy_fire remains unchanged.','Only main enemy-scope audit metadata differs outside core; parent runtime hashes are unchanged.'],'limits':['No board access or visual/FPS/display/DDR acceptance claim.','Schedule fixture drains bullets each step to count production without pool exhaustion; live occupancy is not measured.','Changed enemy firing changes full-game RNG history; Boss equivalence applies to identical entering state/seed, not identical full-game Boss order.']}
 (build/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
