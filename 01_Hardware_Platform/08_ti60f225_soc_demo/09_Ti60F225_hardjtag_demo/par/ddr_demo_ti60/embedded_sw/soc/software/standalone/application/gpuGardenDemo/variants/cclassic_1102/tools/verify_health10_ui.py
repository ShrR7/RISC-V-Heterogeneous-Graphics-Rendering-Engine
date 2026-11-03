"""Compile the production HUD and fonts; preserve host-only ten-HP evidence."""
from pathlib import Path
import hashlib,json,subprocess
from PIL import Image,ImageDraw
V=Path(__file__).resolve().parents[1]
ROOT=next(p for p in V.parents if (p/'01_Hardware_Platform').is_dir())
OUT=V/'tools/validation/health10';OUT.mkdir(parents=True,exist_ok=True)
BUILD=ROOT/'06_Controller_Dependencies/health10_ui';BUILD.mkdir(parents=True,exist_ok=True)
cc='D:/mingw64/bin/gcc.exe';exe=BUILD/'test_health10_ui.exe'
command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(V/'src'),str(V/'tools/test_health10_ui.c'),
    str(V/'src/garden_ui.c'),str(V/'src/garden_assets.c'),'-o',str(exe)]
compiled=subprocess.run(command,capture_output=True,text=True)
(OUT/'ui_compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
if compiled.returncode:print(compiled.stderr)
compiled.check_returncode()
run=subprocess.run([str(exe),str(V/'assets/atlas.bin')],cwd=OUT,capture_output=True,text=True)
(OUT/'ui_host.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode()
sheet=Image.new('RGB',(256*5,260),(8,12,23));draw=ImageDraw.Draw(sheet)
for col,hp in enumerate((10,9,3,1,0)):
    with Image.open(OUT/f'hp_{hp:02}.ppm') as im:sheet.paste(im,(col*256,24))
    draw.text((col*256+12,6),f'HOST PREVIEW / HP {hp}',fill=(220,240,255))
sheet.save(OUT/'health10_ui_preview.png')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
result={'status':'PASS','host_only':True,'board_test_performed':False,'health_levels':[10,9,3,1,0],
    'physical_buffers':2,'bar_count':10,'bar_rectangle':{'x':56,'y':286,'width':18,'height':6,'stride':21},
    'numeric_clear_rectangle':[232,262,32,16],'checks':['real RGB565 fonts','HP 10 fits two glyphs',
    '10 to 9 clears second digit','exact lit/unlit counts','no writes to other HUD values or playfield',
    'cached output equals independent full HUD repaint','unchanged cached output issues no calls'],
    'ui_sha256':sha(V/'src/garden_ui.c'),'atlas_sha256':sha(V/'assets/atlas.bin'),
    'compiler_command':command,'log':'ui_host.log','preview':'health10_ui_preview.png'}
(OUT/'ui_host_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(run.stdout,end='');print(json.dumps(result,indent=2))
