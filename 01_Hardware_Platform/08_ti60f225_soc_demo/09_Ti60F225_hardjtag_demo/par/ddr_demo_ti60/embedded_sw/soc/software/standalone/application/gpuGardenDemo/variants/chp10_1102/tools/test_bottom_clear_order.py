"""Host-only extraction/runner for the real old/new lower-strip order."""
from pathlib import Path
import hashlib
import subprocess
import shutil

V=Path(__file__).resolve().parents[1]
OLD=V.parent/'cgame60h_1030'
ROOT=next(p for p in V.parents if (p/'01_Hardware_Platform').is_dir())
OUT=ROOT/'06_Controller_Dependencies/host_bottom_clear_order'
OUT.mkdir(parents=True,exist_ok=True)
GCC=Path('D:/mingw64/bin/gcc.exe')

def extract(s,signature):
    start=s.index(signature)
    brace=s.index('{',start)
    level=1
    end=brace+1
    while level:
        if s[end]=='{':level+=1
        if s[end]=='}':level-=1
        end+=1
    return s[start:end]

baseline=(OLD/'src/pure_bullet_test.c').read_text(encoding='utf-8-sig')
candidate=(V/'src/pure_bullet_test.c').read_text(encoding='utf-8-sig')
functions=[extract(candidate,'static unsigned route_cut('),
           extract(baseline,'static int draw_legacy(').replace('draw_legacy(', 'baseline_draw_legacy(',1),
           extract(candidate,'static int draw_legacy(').replace('draw_legacy(', 'candidate_draw_legacy(',1),
           extract(candidate,'static int begin_bottom_clear(').replace('asm volatile("fence iorw,iorw" ::: "memory");','host_fence();'),
           extract(candidate,'static int finish_bottom_clear(')]
(OUT/'bottom_clear_extracted.inc').write_text('\n\n'.join(functions),encoding='utf-8')
flags=['-std=c11','-Wall','-Wextra','-Werror','-O2','-I'+str(V/'src'),'-I'+str(OUT)]
model=OUT/'candidate_model.o'
subprocess.run([str(GCC),*flags,'-DPURE_BULLET_HOST_TEST',
    '-Dpure_bullet_test_model_init=candidate_model_init','-Dpure_bullet_test_model_build=candidate_model_build',
    '-c',str(V/'src/pure_bullet_test.c'),'-o',str(model)],check=True)
exe=OUT/'test_bottom_clear_order.exe'
subprocess.run([str(GCC),*flags,str(V/'tools/test_bottom_clear_order.c'),str(V/'src/garden_ui.c'),str(model),'-o',str(exe)],check=True)
shutil.copyfile(V/'assets/pure_scatter16.bin',OUT/'scatter16.bin')
result=subprocess.run([str(exe),'scatter16.bin'],
    cwd=OUT,capture_output=True,text=True)
text='Host only; extracted actual source hashes:\n'+f'H source {hashlib.sha256(baseline.encode()).hexdigest()}\nCandidate source {hashlib.sha256(candidate.encode()).hexdigest()}\n'+result.stdout+result.stderr
(V/'build/bottom_clear_order_test.txt').write_text(text,encoding='utf-8')
print(text,end='')
result.check_returncode()
