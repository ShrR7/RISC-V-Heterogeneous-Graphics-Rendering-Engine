import hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/host/hp_animation'
BUILD.mkdir(parents=True,exist_ok=True)
paths=[ROOT/'tools/test_hp_animation.c',ROOT/'src/garden_animation.c',ROOT/'src/garden_assets.c',ROOT/'src/garden_motion.c',ROOT/'src/garden_heading_precision.c']
exe=BUILD/'test_hp_animation.exe'
cmd=['D:/mingw64/bin/gcc.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),*map(str,paths),'-o',str(exe)]
subprocess.run(cmd,check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);text=r.stdout+r.stderr
(BUILD/'test.log').write_text(text,encoding='utf-8');print(text,end='')
if r.returncode:raise SystemExit(r.returncode)
m=re.search(r'PASS hp_animation checks=(\d+) nonfatal_hp_steps=(\d+)',text)
assert m
bound=paths+[ROOT/'src/garden_animation.h',ROOT/'src/garden_motion.h',ROOT/'src/garden_heading_precision.h',ROOT/'src/garden_core.h',Path(__file__)]
result={'status':'PASS','host_only':True,'board_acceptance':False,'checks':int(m[1]),'nonfatal_hp_steps':int(m[2]),'command':cmd,'core_written':False,'contracts':['10-to1HP does not trigger death fragments/shake, even with a fresh big core effect','Existing grace marker alpha pulse and alpha-off fallback remain identical','1-to0HP retains genuine death position, eight particles and bounded uniform camera','Death pause/lifetime/restart and capacity canaries remain intact','Nonfatal HP damage preserves pilot interpolation; final zeroHP still resets it'],'source_sha256':{str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in bound}}
(BUILD/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
