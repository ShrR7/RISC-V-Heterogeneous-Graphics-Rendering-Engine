#!/usr/bin/env python3
"""Offline import of all nine user bullet PNGs; fixed 859-ID/8-byte ABI.
SPDX-License-Identifier: GPL-3.0-or-later (generator, not user artwork).
No download or hardware access. --check reconstructs all generated outputs.
"""
from __future__ import annotations
import argparse,copy,hashlib,io,json,math,re,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
REFERENCE='neon_cruiser_20261016'
REFERENCE_SHA='1f3f39944e40d982dc7cdb24c8b6ebb5b8011ae3be83e842e7f2b4061f96acbd'
KEY=0xf81f
BUDGET=0xba000
# kind, public colour, source, upright fit, source heading, name, optional tint.
# Names DART/DIAMOND are stable gameplay/ABI categories, not image shapes.
STYLES=(
 ('DART',1,'bullet_04_red_needle.png',(8,22),-90,'red_needle',None),
 ('DART',2,'bullet_07_blue_capsule.png',(10,20),-90,'blue_capsule',None),
 ('DART',3,'bullet_08_white_lance.png',(10,28),-90,'white_lance',None),
 ('DART',4,'bullet_03_amber_diamond.png',(12,18),-90,'amber_diamond',None),
 ('DART',5,'bullet_06_violet_crescent.png',(18,18),180,'violet_crescent',None),
 ('DIAMOND',1,'bullet_02_magenta_orb.png',(14,14),None,'magenta_orb',None),
 ('DIAMOND',2,'bullet_01_cyan_orb.png',(12,12),None,'cyan_orb',None),
 ('DIAMOND',3,'bullet_05_green_ring.png',(18,18),None,'green_ring',None),
 ('DIAMOND',4,'bullet_09_pink_star.png',(18,18),-90,'pink_star',None),
 ('DIAMOND',5,'bullet_09_pink_star.png',(18,18),-90,'amber_star',(255,154,24)),
)
def sha(b):return hashlib.sha256(b).hexdigest()
def jb(v):return (json.dumps(v,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
def data(im):return list(im.get_flattened_data()) if hasattr(im,'get_flattened_data') else list(im.getdata())
def png(im):
 f=io.BytesIO();im.save(f,format='PNG');return f.getvalue()
def bullet(i):return 12<=i<92 or 280<=i<840
def replaced(i):return bullet(i) or i in (4,9,10,11)
def family(s):
 p=s['name'].split('_')
 return (p[1],int(p[2][1:]),int(p[3][1:])) if p[0]=='PRECISION' else (p[0],int(p[1][1:]),int(p[2][1:])*8)
def vals(blob,s):return struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
def decode(blob,s):
 im=Image.new('RGBA',(s['width'],s['height']))
 im.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in vals(blob,s)])
 return im
def fit(im,size):
 box=im.getchannel('A').getbbox();im=im.crop(box)
 ratio=min(size[0]/im.width,size[1]/im.height)
 target=(max(1,int(im.width*ratio+.5)),max(1,int(im.height*ratio+.5)))
 cut=im.resize(target,Image.Resampling.LANCZOS)
 out=Image.new('RGBA',size);xy=((size[0]-cut.width)//2,(size[1]-cut.height)//2);out.paste(cut,xy)
 return out,dict(source_crop=list(box),fit=list(size),resize=list(target),paste=list(xy),aspect_preserved=True)
def bake_base(source,size,name,tint):
 image,recipe=fit(source,size);p=data(image);mapped=[]
 # Premultiply the retained alpha exactly once. Reserve pure RGB565 white
 # for the explicit physical nucleus; other authored highlights remain pale.
 for r,g,b,a in p:
  if a<32:mapped.append((0,0,0,0));continue
  if tint:
   lo=min(r,g,b);ch=max(r,g,b)-lo;r,g,b=[min(255,lo+c*ch//255) for c in tint]
  r,g,b=r*a//255,g*a//255,b*a//255
  if (r>>3,g>>2,b>>3)==(31,63,31):g=247
  mapped.append((r,g,b,255))
 image.putdata(mapped)
 if name=='green_ring':
  # Original ring has a transparent centre. A small separate bright nucleus
  # makes the unchanged centre hitbox visible while preserving the ring hole.
  pivot=(size[0]//2,size[1]//2)
 else:
  lum=[(r*54+g*183+b*19) if a else 0 for r,g,b,a in mapped];peak=max(lum)
  hot=[(i%size[0],i//size[0]) for i,v in enumerate(lum) if v*4>=peak*3]
  cx=sum(x for x,y in hot)/len(hot);cy=sum(y for x,y in hot)/len(hot)
  # A crescent's luminosity centroid can fall in its empty concavity. Choose
  # an actual bright source pixel nearest that centroid, not the bounding box.
  pivot=min(hot,key=lambda q:((q[0]-cx)**2+(q[1]-cy)**2,q[1],q[0]))
 draw=ImageDraw.Draw(image)
 draw.rectangle((pivot[0]-1,pivot[1]-1,pivot[0]+1,pivot[1]+1),fill=(255,255,255,255))
 recipe.update(premultiplied_alpha=True,alpha_cutoff=32,white_nucleus=[*pivot,3,3],
  core_policy='3x3 exact white physical nucleus; other authored highlights below exact white',
  ring_centre_nucleus=(name=='green_ring'),tint=list(tint) if tint else None)
 return image,pivot,recipe
def rotate_tight(im,pivot,clockwise):
 canvas=Image.new('RGBA',(96,96));canvas.paste(im,(48-pivot[0],48-pivot[1]))
 if clockwise:canvas=canvas.rotate(-clockwise,Image.Resampling.NEAREST,center=(48.5,48.5))
 box=canvas.getchannel('A').getbbox();assert box and box[0]>0 and box[1]>0 and box[2]<96 and box[3]<96
 return canvas.crop(box)
def encode(image):
 out=[];clashes=0
 for r,g,b,a in data(image):
  if not a:c=KEY
  else:
   c=(r>>3)<<11|(g>>2)<<5|(b>>3)
   if c==KEY:c^=1;clashes+=1
  out.append(c)
 return struct.pack('<'+'H'*len(out),*out),clashes
def white_anchor(blob,width,height):
 w=[(i%width,i//width) for i,c in enumerate(struct.unpack('<'+'H'*(width*height),blob)) if c==0xffff]
 assert w
 count=len(w);sx=sum(x for x,y in w);sy=sum(y for x,y in w)
 anchor=((2*sx+count)//(2*count),(2*sy+count)//(2*count));assert anchor in w
 return anchor,dict(white_pixels=count,white_centroid_sums=[sx,sy],white_centroid=[sx/count,sy/count],
  white_core_error_px=math.hypot(sx/count-anchor[0],sy/count-anchor[1]))
def build(reference,source):
 old=(reference/'assets/atlas.bin').read_bytes();assert sha(old)==REFERENCE_SHA and len(old)==472732
 before=json.loads((reference/'assets/manifest.json').read_text(encoding='utf-8-sig'))
 sprites=copy.deepcopy(before['sprites']);assert len(sprites)==859
 outputs={};sources={};source_rows=[]
 for filename in sorted({s[2] for s in STYLES}):
  raw=(source/filename).read_bytes();sources[filename]=Image.open(io.BytesIO(raw)).convert('RGBA')
  target='assets/source/user_20261016/'+filename;outputs[target]=raw
  source_rows.append(dict(name=filename,original_sha256=sha(raw),copied_path=target,
   source_size=list(sources[filename].size),original_unmodified=True,
   source_type='user-provided generated asset',license='No explicit license; user README says built-in image generation, not CC0'))
 for filename in ('README.txt','PROMPTS.txt'):outputs['assets/source/user_20261016/'+filename]=(source/filename).read_bytes()
 derived={};records=[];style_map={};base_recipes={}
 for kind,col,filename,size,source_angle,name,tint in STYLES:
  base,pivot,recipe=bake_base(sources[filename],size,name,tint)
  base_recipes[(kind,col)]=recipe
  style_map[(kind,col)]=dict(name=name,source=filename,source_sha256=sha((source/filename).read_bytes()),
   upright_fit=list(size),source_heading_degrees=source_angle,round_reuses_two_physical_copies=source_angle is None,
   color_slot_is_style=True,original_png_unmodified=True,core_rule=recipe['core_policy'])
  for direction in range(64):
   clockwise=(direction*360/64-source_angle) if source_angle is not None else 0
   image=rotate_tight(base,pivot,clockwise);blob,clashes=encode(image)
   anchor,oracle=white_anchor(blob,image.width,image.height)
   assert image.width<=30 and image.height<=30 and oracle['white_core_error_px']<=math.sqrt(.5)+1e-9
   derived[(kind,col,direction)]=(image,blob,anchor,clashes,oracle)
 for s in sprites:
  i=s['id']
  if not bullet(i) and i not in (9,10,11):continue
  key=family(s) if bullet(i) else {9:('DIAMOND',1,0),10:('DIAMOND',2,0),11:('DIAMOND',3,0)}[i]
  image,blob,anchor,clashes,oracle=derived[key];kind,col,direction=key;style=style_map[(kind,col)]
  record=dict(id=i,name=s['name'],style=style['name'],kind=kind,colour_slot=col,direction64=direction,
   source_file='assets/source/user_20261016/'+style['source'],source_sha256=style['source_sha256'],
   upright_recipe=base_recipes[(kind,col)],rotation_clockwise_degrees=(direction*360/64-style['source_heading_degrees']) if style['source_heading_degrees'] is not None else 0,
   rotation_policy='offline nearest neighbour around white-nucleus pixel centre; tight alpha bounds',
   descriptor_anchor=list(anchor),opaque_key_collisions_adjusted=clashes,rgb565_sha256=sha(blob),**oracle)
  s.update(width=image.width,height=image.height,stride=image.width*2,anchor_x=anchor[0],anchor_y=anchor[1],
   bytes=len(blob),source=record,rgb565_sha256=sha(blob),opaque_key_collisions_adjusted=clashes)
  s.pop('render_anchor_override',None);s.pop('shared_rgb565_cache',None)
  outputs['assets/imported/user_bullets_20261016/'+s['name']+'.png']=png(image)
  records.append(record)
 # Replace the obsolete player white bar with an actual authored white lance,
 # preserving the exact5x19 source and original(2,1) shot/collision reference.
 shot,shot_recipe=fit(sources['bullet_08_white_lance.png'],(5,19))
 shot.putdata([(r*a//255,g*a//255,b*a//255,255) if a>=32 else (0,0,0,0) for r,g,b,a in data(shot)])
 shot_blob,clashes=encode(shot)
 shot_blob=bytearray(shot_blob)
 for y in (1,2,3):struct.pack_into('<H',shot_blob,(y*5+2)*2,0xffff)
 shot_blob=bytes(shot_blob);s=sprites[4]
 s.update(source=dict(source_file='assets/source/user_20261016/bullet_08_white_lance.png',
  source_sha256=sha((source/'bullet_08_white_lance.png').read_bytes()),recipe=shot_recipe,
  player_shot_geometry_unchanged=True,white_reference_pixel=[2,1]),rgb565_sha256=sha(shot_blob))
 shot_png=Image.new('RGBA',(5,19));shot_png.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in struct.unpack('<95H',shot_blob)])
 outputs['assets/imported/user_bullets_20261016/SHOT.png']=png(shot_png)
 # Put unchanged actors/effects first. This already exceeds0x3C000, leaving
 # every projectile in the same proven extended DDR source region.
 atlas=bytearray()
 for s in sprites:
  if bullet(s['id']) or s['id'] in (9,10,11):continue
  while len(atlas)&3:atlas.append(0)
  i=s['id'];s['offset']=len(atlas)
  atlas.extend(shot_blob if i==4 else old[before['sprites'][i]['offset']:before['sprites'][i]['offset']+s['bytes']])
 assert len(atlas)>=0x3c000
 bullet_region=len(atlas);cache={}
 for s in sprites:
  if not bullet(s['id']) and s['id'] not in (9,10,11):continue
  kind,col,direction=family(s) if bullet(s['id']) else {9:('DIAMOND',1,0),10:('DIAMOND',2,0),11:('DIAMOND',3,0)}[s['id']]
  style=style_map[(kind,col)]
  key=(kind,col,(direction&1) if style['source_heading_degrees'] is None else direction)
  if key not in cache:
   while len(atlas)&3:atlas.append(0)
   cache[key]=len(atlas);atlas.extend(derived[(kind,col,direction)][1])
  s['offset']=cache[key]
  if style['source_heading_degrees'] is None:s['shared_rgb565_cache']=dict(style=style['name'],copy=direction&1)
 final=bytes(atlas);assert len(final)<=BUDGET
 for i,s in enumerate(sprites):
  assert s['offset']%4==0 and s['offset']+s['bytes']<=len(final)
  if not replaced(i):
   old_s=before['sprites'][i];assert vals(final,s)==vals(old,old_s)
   for field in ('width','height','stride','anchor_x','anchor_y'):assert s[field]==old_s[field]
  else:assert sha(struct.pack('<'+'H'*(s['width']*s['height']),*vals(final,s)))!=sha(struct.pack('<'+'H'*(before['sprites'][i]['width']*before['sprites'][i]['height']),*vals(old,before['sprites'][i]))),'Old projectile pixels survived:'+str(i)
 digest=sha(final);crc=f'0x{zlib.crc32(final):08X}'
 report=dict(status='HOST_ASSET_PREPARED_NOT_BOARD_ACCEPTANCE',variant='neon_variety_20261016',reference_variant=REFERENCE,
  reference_atlas_sha256=REFERENCE_SHA,atlas_bytes=len(final),atlas_sha256=digest,atlas_crc32=crc,budget_bytes=BUDGET,
  remaining_budget_bytes=BUDGET-len(final),sprite_count=859,descriptor_bytes=8,all_numeric_ids_preserved=True,
  changed_ids=[i for i in range(859) if replaced(i)],changed_count=644,unchanged_pixel_geometry_records=215,
  new_enemy_projectile_ids=640,source_bullet_pngs=9,source_style_families=10,directional_families=7,
  round_families=3,round_physical_copies_per_family=2,bullet_region_start=bullet_region,physical_bullet_blobs=len(cache),
  max_projectile_width=max(sprites[r['id']]['width'] for r in records),max_projectile_height=max(sprites[r['id']]['height'] for r in records),
  mean_projectile_area=sum(sprites[r['id']]['width']*sprites[r['id']]['height'] for r in records if bullet(r['id']))/640,
  white_core_max_anchor_error_px=max(r['white_core_error_px'] for r in records),
  player_shot_replaced=True,player_shot_geometry_unchanged=[5,19,2,1],source_pngs_modified=False,
  existing_collision_rule_changes=False,existing_six_tick_forming_unchanged=True,existing_boss_and_effects_retained=True,
  styles=[dict(kind=k,colour_slot=c,**style_map[(k,c)]) for k,c,*_ in STYLES],sources=source_rows)
 manifest=copy.deepcopy(before);manifest.update(variant=report['variant'],bytes=len(final),sha256=digest,crc32=crc,sprites=sprites,
  reference_variant=REFERENCE,reference_atlas_sha256=REFERENCE_SHA,generator='tools/build_variety_atlas.py',
  remaining_budget_bytes=BUDGET-len(final),original_ids_scope='All859 IDs retained; all640 enemy and3legacy circles plus player SHOT use new nine-source art;215 other payloads/geometry unchanged',
  license='Mixed: retained CC0/GPL plus user-provided generated PNGs, no CC0 claim on user images',
  enemy_projectile_style_mapping=report['styles'])
 h=(reference/'src/garden_assets.h').read_text(encoding='utf-8-sig').replace('Run tools/build_cruiser_atlas.py','Run tools/build_variety_atlas.py')
 h=re.sub(r'#define GARDEN_ATLAS_BYTES \d+u',f'#define GARDEN_ATLAS_BYTES {len(final)}u',h)
 h=re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u',f'#define GARDEN_ATLAS_CRC32 {crc}u',h)
 c=(reference/'src/garden_assets.c').read_text(encoding='utf-8-sig').replace('Generated by tools/build_cruiser_atlas.py','Generated by tools/build_variety_atlas.py')
 c=re.sub(r'(garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',lambda m:m[1]+digest+m[2],c)
 rows=['    {%du, %du, %du, %d, %d}, /* %d: %s */'%(s['offset'],s['width'],s['height'],s['anchor_x'],s['anchor_y'],s['id'],s['name']) for s in sprites]
 c,n=re.subn(r'(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n).*?(\n\};)',lambda m:m[1]+'\n'.join(rows)+m[2],c,count=1,flags=re.S);assert n==1
 outputs.update({'assets/atlas.bin':final,'assets/atlas.sha256':(digest+'  atlas.bin\n').encode(),
  'assets/manifest.json':jb(manifest),'assets/variety_generation.json':jb(report),
  'assets/variety_anchor_oracle.json':jb(dict(schema='variety-white-core-v1',records=records)),
  'src/garden_assets.c':c.encode(),'src/garden_assets.h':h.encode(),
  'assets/variety_contact_sheet.png':preview(final,sprites,report)})
 notice=(reference/'assets/NOTICE.txt').read_text(encoding='utf-8-sig')+"""
Nine-source projectile replacement / neon_variety_20261016 /2026-10-16
All nine bullet_01..09 PNGs supplied under 04_Assets_Set_A are copied
unchanged under assets/source/user_20261016. README calls them built-in image
generation followed by transparent centering; no independent license supplied.
They are NOT claimed as CC0. Seven directional styles are baked64directions
offline; three round styles have two identical high-DDR physical source copies
for the20-source precision probe. All640 enemy projectile payloads plus three
legacy circle IDs and player SHOT are replaced. Exact white nuclei make the
unchanged physical centre visible; a ring retains its hole around a small new
central nucleus. Other authored highlights stay pale, not exact white.
SHOT keeps5x19 source dimensions and(2,1) reference. No collision/range/velocity,
forming-age, alpha, Colour Key, wave, reward, actor or effect rule changes.
215 other sprites retain exact pixel payloads, dimensions and anchors.
"""
 outputs['assets/NOTICE.txt']=notice.encode()
 checks=copy.deepcopy(json.loads((reference/'assets/runtime_checksums.json').read_text(encoding='utf-8-sig')).get('files',{}))
 checks.update({path:dict(bytes=len(raw),sha256=sha(raw)) for path,raw in outputs.items()})
 for path in ('tools/build_variety_atlas.py','tools/verify_variety_assets.py','tools/test_variety_assets.c'):
  if (ROOT/path).exists():raw=(ROOT/path).read_bytes();checks[path]=dict(bytes=len(raw),sha256=sha(raw))
 outputs['assets/runtime_checksums.json']=jb(dict(format='sha256',atlas_crc32=crc,files=checks))
 return outputs,report
def preview(blob,sprites,report):
 canvas=Image.new('RGB',(1330,980),(7,12,24));draw=ImageDraw.Draw(canvas);font=ImageFont.load_default()
 draw.text((20,16),'HOST RGB565 PREVIEW / ALL NINE USER SOURCES / NOT A BOARD CAPTURE',fill=(230,237,255),font=font)
 draw.text((20,36),'Seven64-direction styles + three round styles; white physical nucleus, unchanged collision and6-tick forming.',fill=(157,180,211),font=font)
 for n,style in enumerate(report['styles']):
  row,col=divmod(n,5);x=24+col*260;y=90+row*230
  draw.text((x,y),style['kind']+' B'+str(style['colour_slot'])+' '+style['name'],fill=(232,210,239),font=font)
  for j,direction in enumerate((0,8,16,24,32,40,48,56)):
   s=next(s for s in sprites if bullet(s['id']) and family(s)==(style['kind'],style['colour_slot'],direction))
   image=decode(blob,s).resize((s['width']*2,s['height']*2),Image.Resampling.NEAREST)
   xx=x+30+(j%4)*60;yy=y+60+(j//4)*76
   canvas.paste(image,(xx-s['anchor_x']*2,yy-s['anchor_y']*2),image)
   draw.line((xx-5,yy+4,xx-2,yy+4),fill=(60,200,210))
   draw.text((xx-12,yy+38),str(direction),fill=(139,159,188),font=font)
 draw.text((24,570),'DIRECTIONAL EXAMPLE: red needle / all64 exact ID headings, 5.625degree spacing',fill=(229,210,241),font=font)
 for d in range(64):
  s=next(s for s in sprites if bullet(s['id']) and family(s)==('DART',1,d))
  image=decode(blob,s).resize((s['width']*2,s['height']*2),Image.Resampling.NEAREST)
  x=52+(d%16)*80;y=630+(d//16)*72
  canvas.paste(image,(x-s['anchor_x']*2,y-s['anchor_y']*2),image)
  draw.text((x-10,y+27),str(d),fill=(141,167,196),font=font)
 draw.text((24,936),'Atlas '+str(report['atlas_bytes'])+' bytes / '+report['atlas_crc32']+' / 215 non-projectile records unchanged / player shot also replaced',fill=(187,211,227),font=font)
 return png(canvas)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,default=ROOT.parent/REFERENCE)
 p.add_argument('--source',type=Path,default=next(q for q in ROOT.parents if (q/'04_Assets_Set_A').exists())/'04_Assets_Set_A')
 p.add_argument('--check',action='store_true');a=p.parse_args();outputs,report=build(a.reference.resolve(),a.source.resolve())
 for rel,raw in outputs.items():
  path=ROOT/rel
  if a.check:assert path.read_bytes()==raw,'Generated mismatch:'+rel
  else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
 print(json.dumps({k:v for k,v in report.items() if k not in ('styles','sources','changed_ids')},indent=2))
if __name__=='__main__':main()
