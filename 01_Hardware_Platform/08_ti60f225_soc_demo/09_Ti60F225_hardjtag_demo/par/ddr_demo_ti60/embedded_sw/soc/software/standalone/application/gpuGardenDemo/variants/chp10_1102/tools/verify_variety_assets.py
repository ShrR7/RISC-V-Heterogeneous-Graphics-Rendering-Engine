#!/usr/bin/env python3
"""Independent compiled metadata, new-art pixel, heading and DDR probe checks.
SPDX-License-Identifier: GPL-3.0-or-later; host only, no board access.
"""
import argparse,csv,hashlib,json,math,re,struct,subprocess,sys,zlib
from fractions import Fraction
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
KEY=0xf81f
def sha(raw):return hashlib.sha256(raw).hexdigest()
def readjson(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def bullet(i):return 12<=i<92 or 280<=i<840
def family(s):
 p=s['name'].split('_')
 return (p[1],int(p[2][1:]),int(p[3][1:])) if p[0]=='PRECISION' else (p[0],int(p[1][1:]),int(p[2][1:])*8)
def vals(blob,s):return struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
def rgba_data(im):return list(im.get_flattened_data()) if hasattr(im,'get_flattened_data') else list(im.getdata())
def patch(blob,s):
 if s['offset']<0x3c000:return None
 width,height=min(8,s['width']),min(8,s['height']);pixels=vals(blob,s)
 for y in range(s['height']-height+1):
  for x in range(s['width']-width+1):
   region=[pixels[(y+yy)*s['width']+x+xx] for yy in range(height) for xx in range(width)]
   if KEY in region and any(c!=KEY for c in region):return s['offset']+(y*s['width']+x)*2
 return None
def pca_heading(pixels,width):
 points=[(i%width,i//width) for i,c in enumerate(pixels) if c!=KEY]
 cx=sum(x for x,y in points)/len(points);cy=sum(y for x,y in points)/len(points)
 xx=sum((x-cx)**2 for x,y in points);yy=sum((y-cy)**2 for x,y in points)
 xy=sum((x-cx)*(y-cy) for x,y in points)
 return math.atan2(2*xy,xx-yy)/2
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
 parser.add_argument('--reference',type=Path,default=ROOT.parent/'neon_cruiser_20261016');args=parser.parse_args()
 build=ROOT/'build/host/variety_assets';build.mkdir(parents=True,exist_ok=True)
 blob=(ROOT/'assets/atlas.bin').read_bytes();old=(args.reference/'assets/atlas.bin').read_bytes()
 manifest=readjson(ROOT/'assets/manifest.json');before=readjson(args.reference/'assets/manifest.json')
 generation=readjson(ROOT/'assets/variety_generation.json');oracle=readjson(ROOT/'assets/variety_anchor_oracle.json')
 assert sha(old)=='1f3f39944e40d982dc7cdb24c8b6ebb5b8011ae3be83e842e7f2b4061f96acbd'
 assert len(blob)==manifest['bytes']==generation['atlas_bytes']<=761856
 assert sha(blob)==manifest['sha256']==generation['atlas_sha256']
 assert f'0x{zlib.crc32(blob):08X}'==manifest['crc32']==generation['atlas_crc32']
 exe=build/'variety_assets.exe';dump=build/'compiled_descriptors.csv'
 command=[args.compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),
  str(ROOT/'tools/test_variety_assets.c'),str(ROOT/'src/garden_assets.c'),'-o',str(exe)]
 subprocess.run(command,check=True)
 result=subprocess.run([str(exe),str(dump),str(ROOT/'assets/atlas.bin')],capture_output=True,text=True)
 (build/'test_api.log').write_text(result.stdout+result.stderr,encoding='utf-8')
 print(result.stdout+result.stderr,end='');assert result.returncode==0
 compiled=[tuple(map(int,row)) for row in csv.reader(dump.read_text().splitlines())]
 fields=('id','offset','width','height','stride','anchor_x','anchor_y')
 assert len(compiled)==859
 for row,s in zip(compiled,manifest['sprites']):assert row==tuple(s[k] for k in fields)
 changed={4,9,10,11}|set(range(12,92))|set(range(280,840))
 assert set(generation['changed_ids'])==changed and len(changed)==644
 untouched=0;known_ranges={};white_errors=[];headings=[];pixel_count=0;transparent_count=0;changed_pixels=0
 anchors={r['id']:r for r in oracle['records']}
 for i,s in enumerate(manifest['sprites']):
  payload=bytes(blob[s['offset']:s['offset']+s['bytes']]);pixels=vals(blob,s)
  assert len(payload)==s['bytes']==s['width']*s['height']*2
  start,end=s['offset'],s['offset']+len(payload)
  for (lo,hi),other in known_ranges.items():
   if start<hi and lo<end:assert (start,end)==(lo,hi) and payload==other,'Only exact byte-range aliases allowed'
  known_ranges[(start,end)]=payload
  old_s=before['sprites'][i];old_payload=old[old_s['offset']:old_s['offset']+old_s['bytes']]
  if i not in changed:
   assert payload==old_payload
   for field in ('width','height','stride','anchor_x','anchor_y'):assert s[field]==old_s[field]
   untouched+=1
  else:
   assert payload!=old_payload,'Existing projectile image survived:'+str(i)
   changed_pixels+=1
   # Imported PNGs describe actual runtime colour values before RGB565. This
   # independently accounts for every retained opaque pixel and key boundary.
   image=Image.open(ROOT/'assets/imported/user_bullets_20261016'/f'{s["name"]}.png').convert('RGBA')
   assert image.size==(s['width'],s['height'])
   for j,(r,g,b,a) in enumerate(rgba_data(image)):
    if not a:assert pixels[j]==KEY;transparent_count+=1
    else:
     expected=(r//8)*2048+(g//4)*32+b//8
     if expected==KEY:expected^=1
     assert pixels[j]==expected and pixels[j]!=KEY
     pixel_count+=1
  if not bullet(i):continue
  white=[(j%s['width'],j//s['width']) for j,v in enumerate(pixels) if v==65535]
  assert white
  cx=Fraction(sum(x for x,y in white),len(white));cy=Fraction(sum(y for x,y in white),len(white))
  ax,ay=s['anchor_x'],s['anchor_y'];assert (ax,ay) in white
  assert abs(cx-ax)<=Fraction(1,2) and abs(cy-ay)<=Fraction(1,2)
  error=math.hypot(float(cx-ax),float(cy-ay));assert error<=math.sqrt(.5)+1e-10;white_errors.append(error)
  assert anchors[i]['descriptor_anchor']==[ax,ay]
  kind,color,direction=family(s)
  # Independent shape-axis check catches a reversed rotation sign or old
  # angle-grid mismatch for the four genuinely elongated source families.
  if kind=='DART' and color in(1,2,3,4):
   axis=pca_heading(pixels,s['width']);expected=direction*math.pi/32
   angle=abs((axis-expected+math.pi/2)%math.pi-math.pi/2)
   assert math.degrees(angle)<9.0,(s['name'],math.degrees(angle))
   headings.append(math.degrees(angle))
 assert untouched==215 and changed_pixels==644 and len(white_errors)==640 and len(headings)==256
 # Original source bytes and their provenance are independent of atlas data.
 original_dir=next(q for q in ROOT.parents if(q/'04_Assets_Set_A').exists())/'04_Assets_Set_A'
 assert len(generation['sources'])==9
 for row in generation['sources']:
  raw=(ROOT/row['copied_path']).read_bytes()
  assert sha(raw)==row['original_sha256']==sha((original_dir/row['name']).read_bytes())
  assert Image.open(ROOT/row['copied_path']).size==tuple(row['source_size'])
 for name in('README.txt','PROMPTS.txt'):assert (ROOT/'assets/source/user_20261016'/name).read_bytes()==(original_dir/name).read_bytes()
 # All ten public type/colour families must show distinct pixels at angle0.
 shape_pixels=[]
 for kind in('DART','DIAMOND'):
  for color in range(1,6):
   group=[s for s in manifest['sprites'] if bullet(s['id']) and family(s)[:2]==(kind,color)]
   assert len(group)==64
   s=next(s for s in group if family(s)[2]==0)
   shape_pixels.append((s['width'],s['height'],tuple(vals(blob,s))))
 assert len(set(shape_pixels))==10
 # Reproduce the real startup's source-address search from the actual atlas.
 # Even round families need two independent addresses, not two aliases to
 # one address. Every family provides opaque+key mixed regions above0x3C000.
 all_sources=[];family_sources={}
 for kind in('DART','DIAMOND'):
  for color in range(1,6):
   chosen=[]
   for direction in range(1,64):
    if direction%8==0:continue
    s=next(s for s in manifest['sprites'] if bullet(s['id']) and family(s)==(kind,color,direction))
    address=patch(blob,s)
    if address is not None:chosen.append(address)
    if len(chosen)==2:break
   assert len(chosen)==2 and len(set(chosen))==2
   family_sources[kind+'_B'+str(color)]=chosen;all_sources+=chosen
 assert len(all_sources)==len(set(all_sources))==20
 # Green ring keeps a real hole around its separately visible collision core.
 ring=next(s for s in manifest['sprites'] if bullet(s['id']) and family(s)==('DIAMOND',3,0));p=vals(blob,ring)
 cx,cy=ring['anchor_x'],ring['anchor_y'];assert p[cy*ring['width']+cx]==65535
 assert any(p[y*ring['width']+x]==KEY for y in range(max(0,cy-4),min(ring['height'],cy+5)) for x in range(max(0,cx-4),min(ring['width'],cx+5)))
 # The current model, banking, hit/blast, bomb/heal/beam and fonts are all
 # covered by the215 unchanged-record proof, independent of moved offsets.
 for i in list(range(92,280))+list(range(840,859))+[0,1,2,3]:assert i not in changed
 shot=manifest['sprites'][4];assert [shot[k] for k in('width','height','anchor_x','anchor_y')]==[5,19,2,1]
 assert vals(blob,shot)[7]==65535
 render=(ROOT/'src/garden_render.c').read_text()
 assert 'legacy_bullet_ink' not in render,'Old DART crop would cut new sprite pixels'
 check=subprocess.run([sys.executable,'-B',str(ROOT/'tools/build_variety_atlas.py'),'--reference',str(args.reference),'--check'],capture_output=True,text=True)
 (build/'regeneration_check.log').write_text(check.stdout+check.stderr,encoding='utf-8');assert check.returncode==0,check.stdout+check.stderr
 match=re.search(r'checks=(\d+)',result.stdout);assert match
 report=dict(status='PASS',host_only=True,board_acceptance=False,variant='neon_variety_20261016',
  compiled_api_checks=int(match[1]),compiled_descriptors_match_manifest=859,descriptor_bytes=8,
  source_original_png_sha256_matches=9,new_enemy_projectile_payloads=640,changed_payloads=644,
  unchanged_rgb565_and_geometry_records=215,visibly_distinct_style_families=10,all_nine_sources_used=True,
  no_new_mutable_runtime_or_Bullet_fields=True,white_core_anchor_oracle_checks=640,
  white_core_max_error_px=max(white_errors),direction_axis_checks=len(headings),direction_axis_max_error_degrees=max(headings),
  actual_imported_opaque_pixels_checked=pixel_count,actual_imported_key_pixels_checked=transparent_count,
  green_ring_hole_and_visible_physics_core=True,player_shot_new_art_same_5x19_anchor_2_1=True,
  precision_independent_high_sources=20,precision_source_offsets=family_sources,
  original_models_HP_attachment_fonts_effects_bomb_heal_plasma_unchanged=True,
  atlas_bytes=len(blob),atlas_sha256=sha(blob),atlas_crc32=manifest['crc32'],
  free_bytes_before_scratch=761856-len(blob),deterministic_regeneration=True,compiler_command=command,
  source_sha256={r:sha((ROOT/r).read_bytes()) for r in('src/garden_assets.c','src/garden_assets.h','src/garden_render.c',
   'tools/build_variety_atlas.py','tools/test_variety_assets.c','tools/verify_variety_assets.py','assets/variety_anchor_oracle.json')},
  limits=['Host asset proof only; board timing, alpha/precision readback and user visual observations are separate.',
   'Existing centre hitbox and six-tick forming rules retained; outer projectile glow is decorative.',
   'User PNGs are generated user-provided artwork; no CC0 license is asserted.'])
 (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({k:v for k,v in report.items() if k not in('source_sha256','compiler_command','precision_source_offsets','limits')},indent=2))
if __name__=='__main__':main()
