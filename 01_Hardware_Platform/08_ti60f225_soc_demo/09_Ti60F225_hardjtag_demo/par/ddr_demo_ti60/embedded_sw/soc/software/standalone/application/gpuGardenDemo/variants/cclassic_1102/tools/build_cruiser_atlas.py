#!/usr/bin/env python3
"""Import five user PNGs into the existing 859-ID RGB565 atlas.
SPDX-License-Identifier: GPL-3.0-or-later (generator only; see source notice).
No download, hardware access, runtime resampling or modification of originals.
"""
from __future__ import annotations
import argparse,copy,hashlib,io,json,math,re,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
REFERENCE='neon_hazard_20261016'
REFERENCE_SHA='2ab126d0d86749947aa34b69370e6325b1c561a1e68f9aad5984dd561275dd42'
KEY=0xf81f
BUDGET=0xba000
FILES=('player_02_delta.png','enemy_01_scout.png','enemy_03_bomber.png','boss_01_cruiser.png','bullet_02_magenta_orb.png')
EXPECTED=('b34473ccc7282680f2023fb781ea6000891a523f23797aaa17afd1149bb292ce',
 'e04e6589847bb957cedb9754af83744f9bf5f22ff46d74a9d2626b4e4197e0b8',
 '223c50753ec262502b23583118ee53d2c42af1eaec459c453a2db54cdd735db0',
 '3a567a471742e52e4a968a0117b324bb51816d4f593eb92da65a195d0054bbf8',
 '7bca20152de0cab6adaf0c4b33421616a60cfa83896526ecd4b529f9897083dc')
TINTS=((255,48,192),(255,165,20),(100,255,80),(70,170,255),(184,75,255))
POD_RECTS=((0,16,44,88),(84,16,128,88))
MUZZLES=((22,85),(107,85))
BODY_ANCHOR=(64,66)
def sha(data):return hashlib.sha256(data).hexdigest()
def jb(value):return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
def data(im):return list(im.get_flattened_data()) if hasattr(im,'get_flattened_data') else list(im.getdata())
def png(im):
    out=io.BytesIO();im.save(out,format='PNG');return out.getvalue()
def values(blob,s):return struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
def encode(im,threshold=32,white_mask=()):
    result=[];collisions=0;mask=set(white_mask)
    for i,(r,g,b,a) in enumerate(data(im)):
        if a<threshold:value=KEY
        elif i in mask:value=0xffff
        else:
            r,g,b=r*a//255,g*a//255,b*a//255
            value=(r>>3)<<11 | (g>>2)<<5 | (b>>3)
            if value==KEY:value^=1;collisions+=1
        result.append(value)
    return struct.pack('<'+'H'*len(result),*result),collisions
def decode(blob,s):
    im=Image.new('RGBA',(s['width'],s['height']))
    im.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in values(blob,s)])
    return im
def fit(im,size):
    bbox=im.getchannel('A').getbbox();cut=im.crop(bbox)
    ratio=min(size[0]/cut.width,size[1]/cut.height)
    target=(int(cut.width*ratio+.5),int(cut.height*ratio+.5))
    small=cut.resize(target,Image.Resampling.LANCZOS)
    out=Image.new('RGBA',size);origin=((size[0]-target[0])//2,(size[1]-target[1])//2)
    out.paste(small,origin)
    return out,dict(source_crop=list(bbox),resize=['LANCZOS',*target],canvas=list(size),paste=list(origin),aspect_preserved=True)
def core_anchor(blob,w,h):
    pts=[(i%w,i//w) for i,c in enumerate(struct.unpack('<'+'H'*(w*h),blob)) if c==0xffff]
    assert pts
    n=len(pts);sx=sum(x for x,y in pts);sy=sum(y for x,y in pts)
    point=((2*sx+n)//(2*n),(2*sy+n)//(2*n))
    assert point in pts
    return point,dict(white_pixels=n,white_centroid_sums=[sx,sy],centroid=[sx/n,sy/n],anchor=list(point),
        max_error_px=math.hypot(sx/n-point[0],sy/n-point[1]))
def family(s):
    parts=s['name'].split('_')
    if parts[0]=='PRECISION':return parts[1],int(parts[2][1:]),int(parts[3][1:])
    return parts[0],int(parts[1][1:]),int(parts[2][1:])*8
def is_bullet(i):return 12<=i<92 or 280<=i<840
def build(reference,source):
    old=(reference/'assets/atlas.bin').read_bytes();assert len(old)==719628 and sha(old)==REFERENCE_SHA
    before=json.loads((reference/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(before['sprites'])==859
    sprites=copy.deepcopy(before['sprites']);assets={};im_by_name={};source_rows=[];out={}
    for name,expected in zip(FILES,EXPECTED):
        raw=(source/name).read_bytes();assert sha(raw)==expected,'User source changed:'+name
        path='assets/source/user_20261016/'+name;out[path]=raw
        image=Image.open(io.BytesIO(raw)).convert('RGBA');im_by_name[name]=image
        source_rows.append(dict(name=name,original_sha256=expected,source_size=list(image.size),copied_path=path,
            original_unmodified=True,source_type='user-provided generated asset',
            license='No explicit license supplied; source README states built-in image generation, not CC0'))
    for name in ('README.txt','PROMPTS.txt'):out['assets/source/user_20261016/'+name]=(source/name).read_bytes()
    derived={};changed_ids=set();provenance={}
    def add(i,image,anchor,origin,white=()):
        blob,keyhits=encode(image,white_mask=white)
        derived[i]=blob;changed_ids.add(i)
        s=sprites[i];s.update(width=image.width,height=image.height,stride=image.width*2,
            anchor_x=anchor[0],anchor_y=anchor[1],bytes=len(blob),opaque_key_collisions_adjusted=keyhits,
            rgb565_sha256=sha(blob),source=origin)
        s.pop('render_anchor_override',None)
        provenance[str(i)]=dict(rgb565_sha256=sha(blob),key_pixels=sum(v==KEY for v in struct.unpack('<'+'H'*(len(blob)//2),blob)),
            opaque_key_collisions_adjusted=keyhits,source=origin)
        out[f'assets/imported/user_20261016/{s["name"]}.png']=png(image)
    def origin(name,recipe):return dict(kind='user_provided_generated_png_offline_conversion',
        source_file='assets/source/user_20261016/'+name,source_sha256=EXPECTED[FILES.index(name)],
        attribution='User supplied / folder README says built-in image generation',
        license='No independent license file provided; not inherited CC0',
        alpha_policy='alpha<32 -> key; retained RGB premultiplied by original alpha against black; opaque key collision -> flip bit0',
        source_png_unmodified=True,recipe=recipe)
    player,recipe=fit(im_by_name[FILES[0]],(40,36))
    # Reuse the authored tail pixels and modulate their brightness. Banking is
    # the existing six-band shear about the same anchor for every source frame.
    for phase,i in enumerate((0,264,265)):
        pose=player.copy();pixels=data(pose)
        scale=(255,208,235)[phase]
        pixels=[(r*scale//255,g*scale//255,b*scale//255,a) if (j//40)>=27 else (r,g,b,a)
            for j,(r,g,b,a) in enumerate(pixels)];pose.putdata(pixels)
        add(i,pose,(20,18),origin(FILES[0],dict(**recipe,tail_luma_phase=phase,tail_rows=[27,35],tail_scale=scale,
            banking='all five existing six-band renderer poses use this new source; no old plane fallback')))
    for name,base,flash,size,anchor in [(FILES[1],1,266,(40,44),(20,22)),(FILES[2],2,267,(72,68),(36,34))]:
        image,recipe=fit(im_by_name[name],size);add(base,image,anchor,origin(name,recipe))
        silhouette=Image.new('RGBA',size);silhouette.putdata([(255,255,255,255) if p[3]>=32 else (0,0,0,0) for p in data(image)])
        add(flash,silhouette,anchor,origin(name,dict(**recipe,white_silhouette='same alpha>=32 mask; opaque 0xFFFF; geometry identical')))
    full,recipe=fit(im_by_name[FILES[3]],(128,132));assert recipe['source_crop']==[8,5,184,187]
    assert recipe['resize']==['LANCZOS',128,132] and recipe['paste']==[0,0]
    body=full.copy();draw=ImageDraw.Draw(body)
    for rect in POD_RECTS:draw.rectangle((rect[0],rect[1],rect[2]-1,rect[3]-1),fill=(0,0,0,0))
    add(3,body,BODY_ANCHOR,origin(FILES[3],dict(**recipe,pod_rects=[list(r) for r in POD_RECTS],
        split_policy='body rectangle holes plus two exact disjoint source crops reconstruct full cruiser at closed pose')))
    flash=Image.new('RGBA',body.size);flash.putdata([(255,255,255,255) if p[3]>=32 else (0,0,0,0) for p in data(body)])
    add(268,flash,BODY_ANCHOR,origin(FILES[3],dict(**recipe,pod_rects=[list(r) for r in POD_RECTS],white_silhouette=True)))
    for side,(rect,muzzle) in enumerate(zip(POD_RECTS,MUZZLES)):
        cut=full.crop(rect);anchor=(muzzle[0]-rect[0],muzzle[1]-rect[1])
        assert full.getpixel(muzzle)[3]>=64
        add(274+side,cut,anchor,origin(FILES[3],dict(**recipe,pod_crop=list(rect),muzzle_local=list(muzzle),
            anchor_is_muzzle=True,closed_draw_offset=[muzzle[0]-64,muzzle[1]-66])))
    full_blob,full_key_hits=encode(full)
    out['assets/imported/user_20261016/BOSS_FULL_COMPOSITE.png']=png(full)
    out['assets/imported/user_20261016/BOSS_FULL_COMPOSITE.rgb565']=full_blob
    orb,recipe=fit(im_by_name[FILES[4]],(16,16))
    orbpx=data(orb);white=[i for i,p in enumerate(orbpx) if min(p[:3])>=180 and p[3]>=160]
    assert len(white)>=3
    orb_frames={};orb_anchors={};orb_oracles={}
    for color in range(1,6):
        tint=TINTS[color-1];image=orb.copy();mapped=[]
        for r,g,b,a in orbpx:
            if color==1:mapped.append((r,g,b,a));continue
            low=min(r,g,b);chroma=max(r,g,b)-low
            mapped.append(tuple(min(255,low+channel*chroma//255) for channel in tint)+(a,))
        image.putdata(mapped);blob,clash=encode(image,white_mask=white)
        anchor,white_info=core_anchor(blob,16,16)
        orb_frames[color]=(image,blob,clash);orb_anchors[color]=anchor;orb_oracles[str(color)]=white_info
    for s in sprites:
        i=s['id']
        if is_bullet(i) and family(s)[0]=='DIAMOND':
            kind,color,direction=family(s);image,blob,clash=orb_frames[color]
            add(i,image,orb_anchors[color],origin(FILES[4],dict(**recipe,palette=color,tint=list(TINTS[color-1]),
                normalize_authored_white_core='source minRGB>=180 and alpha>=160 -> exact white; shape unchanged',
                semantic='round projectile has no directional axis; all64 IDs stable',shared_copy=direction&1)),white)
        elif i in (9,10,11):
            color={9:1,10:4,11:3}[i];image,blob,clash=orb_frames[color]
            add(i,image,orb_anchors[color],origin(FILES[4],dict(**recipe,palette=color,legacy_circle_slot=True)),white)
    # Pack unchanged and actor content first, then compact orb cache in the
    # proven high DDR region. Each palette has two identical cache addresses
    # so the existing20-independent-source startup verification remains real.
    atlas=bytearray();orb_ids=[];cache={}
    for s in sprites:
        i=s['id']
        if i in (9,10,11) or (is_bullet(i) and family(s)[0]=='DIAMOND'):orb_ids.append(i);continue
        while len(atlas)&3:atlas.append(0)
        s['offset']=len(atlas);blob=derived.get(i)
        if blob is None:blob=old[before['sprites'][i]['offset']:before['sprites'][i]['offset']+before['sprites'][i]['bytes']]
        atlas.extend(blob)
    assert len(atlas)>=0x3c000,'Precision probe must sample actual extended DDR source addresses'
    for i in orb_ids:
        s=sprites[i]
        if i in (9,10,11):color={9:1,10:4,11:3}[i];copy_index=0
        else:kind,color,direction=family(s);copy_index=direction&1
        key=(color,copy_index)
        if key not in cache:
            while len(atlas)&3:atlas.append(0)
            cache[key]=len(atlas);atlas.extend(derived[i])
        s['offset']=cache[key];s['shared_rgb565_cache']=dict(palette=color,copy=copy_index)
    assert len(cache)==10
    final=bytes(atlas);assert len(final)<=BUDGET
    for s in sprites:
        assert 0<s['width']<=255 and 0<s['height']<=255
        assert 0<=s['anchor_x']<=127 and 0<=s['anchor_y']<=127
        assert s['offset']%4==0 and s['offset']+s['width']*s['height']*2<=len(final)
        if s['id'] not in changed_ids:
            old_s=before['sprites'][s['id']]
            assert values(final,s)==values(old,old_s)
            for key in ('width','height','stride','anchor_x','anchor_y'):assert s[key]==old_s[key]
    # Reconstruct the split Boss using actual quantized payloads, independently
    # of scene rendering. The muzzle is a point on retained opaque barrel art.
    composite=list(values(final,sprites[3]))
    for i,rect in zip((274,275),POD_RECTS):
        ss=sprites[i];raw=values(final,ss)
        for y in range(ss['height']):
            for x in range(ss['width']):composite[(rect[1]+y)*128+rect[0]+x]=raw[y*ss['width']+x]
    assert struct.pack('<'+'H'*len(composite),*composite)==full_blob
    digest=sha(final);crc=zlib.crc32(final)
    geometry=dict(schema='cruiser-attachment-v1',canvas=[128,132],body_anchor=list(BODY_ANCHOR),
        source_bbox=[8,5,184,187],source_canvas=[192,192],target_muzzles=[list(m) for m in MUZZLES],
        muzzle_offsets=[[-42,19],[43,19]],pod_rects=[list(r) for r in POD_RECTS],
        pod_ids=[274,275],pod_anchors=[[22,69],[23,69]],core_local=[64,47],core_offset=[0,-19],
        charge_local=[64,126],charge_offset=[0,60],neutral_composite_rgb565_sha256=sha(full_blob),
        neutral_composite_exact=True,muzzle_alpha=[full.getpixel(m)[3] for m in MUZZLES],
        muzzle_terminal_observation='row85 barrel shrinks to left20..23 / right105..108 at alpha>=64; row86 has no barrel pixels',
        alignment_rule='pod draw x/y is the canonical muzzle; WARN/FIRE/FADE pod, glow and13pxbeam collision all share it')
    generation=dict(status='HOST_ASSET_PREPARED_NOT_BOARD_ACCEPTANCE',variant='neon_cruiser_20261016',
        reference_variant=REFERENCE,reference_atlas_sha256=REFERENCE_SHA,atlas_bytes=len(final),atlas_sha256=digest,
        atlas_crc32=f'0x{crc:08X}',budget_bytes=BUDGET,remaining_budget_bytes=BUDGET-len(final),sprite_count=859,
        descriptor_bytes=8,original_ids_preserved=859,changed_ids=sorted(changed_ids),changed_count=len(changed_ids),
        unchanged_pixel_and_geometry_records=859-len(changed_ids),dart64_all5colors_pixels_and_white_anchors_unchanged=320,
        round64_stable_IDs=320,round_cached_blobs=10,round_palette_count=5,precision_sources_per_palette=2,
        source_rows=source_rows,orb_white_core=orb_oracles,source_pngs_modified=False,
        original_CC0_effects_retained=True,existing_gameplay_collision_or_reward_changes=False,
        boss_geometry='assets/cruiser_geometry.json',import_provenance='assets/user_art_import.json')
    manifest={k:copy.deepcopy(before[k]) for k in ('format','address','safe_window_end','scratch_start','key_colour','budget_bytes',
        'unchanged_font_slots','player_animation_frames','added_player_slot_ids','runtime_descriptor_bytes','runtime_descriptor_fields',
        'descriptor_stride_rule','runtime_descriptor_total_bytes','ordnance_id_range') if k in before}
    manifest.update(variant='neon_cruiser_20261016',bytes=len(final),sha256=digest,crc32=f'0x{crc:08X}',sprites=sprites,
        reference_variant=REFERENCE,reference_atlas_sha256=REFERENCE_SHA,generator='tools/build_cruiser_atlas.py',
        remaining_budget_bytes=BUDGET-len(final),legacy_IDs_preserved=True,original_ids_unchanged=859,
        original_bullet_rotations=8,precision_rotations=64,precision_id_range=[280,839],
        original_ids_scope='All859numeric IDs and helper selection retained; DIAMOND becomes round art with per-palette duplicated cache addresses',
        collision_logic_changed=False,license='Mixed: retained CC0/GPL sources plus five user-provided generated images; see NOTICE and source records')
    h=(reference/'src/garden_assets.h').read_text(encoding='utf-8-sig').replace('Run tools/build_hazard_atlas.py','Run tools/build_cruiser_atlas.py')
    h=re.sub(r'#define GARDEN_ATLAS_BYTES \d+u',f'#define GARDEN_ATLAS_BYTES {len(final)}u',h)
    h=re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u',f'#define GARDEN_ATLAS_CRC32 0x{crc:08X}u',h)
    c=(reference/'src/garden_assets.c').read_text(encoding='utf-8-sig').replace('Generated by tools/build_hazard_atlas.py','Generated by tools/build_cruiser_atlas.py')
    c=re.sub(r'(garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',lambda m:m[1]+digest+m[2],c)
    rows=['    {%du, %du, %du, %d, %d}, /* %d: %s */'%(s['offset'],s['width'],s['height'],s['anchor_x'],s['anchor_y'],s['id'],s['name']) for s in sprites]
    c,n=re.subn(r'(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n).*?(\n\};)',lambda m:m[1]+'\n'.join(rows)+m[2],c,count=1,flags=re.S);assert n==1
    out.update({'assets/atlas.bin':final,'assets/atlas.sha256':(digest+'  atlas.bin\n').encode(),
        'assets/manifest.json':jb(manifest),'assets/cruiser_generation.json':jb(generation),
        'assets/cruiser_geometry.json':jb(geometry),'assets/user_art_import.json':jb(dict(schema='user-art-import-v1',
            sources=source_rows,alpha_threshold=32,color_key='0xF81F',opaque_key_collision_policy='flip bit0',
            generated_sprite_provenance=provenance,original_images_unmodified=True)),
        'src/garden_assets.c':c.encode(),'src/garden_assets.h':h.encode()})
    # Old generation notes remain historical; make runtime atlas validation
    # consume the actual replacement payload instead of inherited W CRC.
    ordnance=json.loads((reference/'assets/ordnance_generation.json').read_text(encoding='utf-8-sig'))
    ordnance.update(status='REPLACED_BY_CRUISER_GENERATION_SEE_CRUISER_MANIFEST',atlas_bytes=len(final),
        atlas_sha256=digest,atlas_crc32=f'0x{crc:08X}',remaining_budget_bytes=BUDGET-len(final),
        old840pixels_and_metadata_exact=False,cruiser_generation='assets/cruiser_generation.json',
        runtime_beam_payloads_unchanged_from_hazard=True)
    out['assets/ordnance_generation.json']=jb(ordnance)
    notice=(reference/'assets/NOTICE.txt').read_text(encoding='utf-8-sig')+'''
User five-image replacement, 2026-10-16 / neon_cruiser_20261016
player_02_delta, enemy_01_scout, enemy_03_bomber, boss_01_cruiser and
bullet_02_magenta_orb are USER-PROVIDED images from 04_Assets_Set_A.
The supplied README says built-in image generation followed by scaling and
transparent centering. Original PNGs, README and PROMPTS are copied verbatim
under assets/source/user_20261016, with original SHA256 records. No explicit
license was supplied. These five images are NOT claimed as CC0; previous
third-party notices apply only to retained third-party assets.
Offline import: crop alpha bounds, proportional LANCZOS fit, alpha<32 ->
0xF81F key, other RGB multiplied by source alpha against black. Any opaque
RGB565 quantization collision with0xF81F flips bit0. Round projectile authored
white core is normalized to0xFFFF and its centroid is the physics pivot.
All three player tail frames and all five bank poses use the new delta source.
Both enemies retain matching white-hit masks. Boss body and two source-cropped
gun pods reconstruct the complete new cruiser exactly at closed pose; gun pod
anchors are actual barrel tips. Core/charge glow are existing retained effects.
All numeric859IDs and8-byte descriptor ABI remain; offsets are repacked and
may alias identical round cache payloads. DART64direction pixels/white pivots,
font glyphs, angular hit frames, blast, thrown bomb, heal and plasma beam payloads
remain byte-identical. DIAMOND source family is replaced by five round palettes,
with two identical high-DDR caches per palette for startup precision coverage.
No core collision, attack, scoring, health, wave or gameplay rule is changed by
this importer. Actual board acceptance and renderer integration are separate.
'''
    out['assets/NOTICE.txt']=notice.encode('utf-8')
    out['assets/cruiser_contact_sheet.png']=preview(final,sprites,full,geometry)
    checks=copy.deepcopy(json.loads((reference/'assets/runtime_checksums.json').read_text(encoding='utf-8-sig')).get('files',{}))
    checks.update({k:{'bytes':len(v),'sha256':sha(v)} for k,v in out.items()})
    for rel in ('tools/build_cruiser_atlas.py','tools/test_cruiser_assets.c','tools/verify_cruiser_assets.py'):
        if (ROOT/rel).exists():raw=(ROOT/rel).read_bytes();checks[rel]=dict(bytes=len(raw),sha256=sha(raw))
    out['assets/runtime_checksums.json']=jb(dict(format='sha256',atlas_crc32=f'0x{crc:08X}',files=checks))
    return out,generation
def preview(blob,sprites,full,geometry):
    canvas=Image.new('RGB',(1120,840),(9,13,24));d=ImageDraw.Draw(canvas);font=ImageFont.load_default()
    d.text((16,12),'HOST RGB565 USER-ART PREVIEW / actual converted atlas; not a board capture',fill=(229,237,253),font=font)
    d.text((16,31),'New player / scout / bomber / cruiser / round family. Retained effects and DART64 are unchanged.',fill=(154,181,204),font=font)
    positions=[(0,34,80,4),(264,240,80,4),(265,446,80,4),(1,30,300,3),(266,185,300,3),(2,360,300,3),(267,610,300,3)]
    for i,x,y,scale in positions:
        s=sprites[i];pic=decode(blob,s).resize((s['width']*scale,s['height']*scale),Image.Resampling.NEAREST)
        canvas.paste(pic,(x,y),pic);d.text((x,y-16),s['name'],fill=(207,220,240),font=font)
    boss=Image.new('RGBA',(128,132));boss.alpha_composite(decode(blob,sprites[3]))
    for i,rect in zip((274,275),POD_RECTS):boss.alpha_composite(decode(blob,sprites[i]),(rect[0],rect[1]))
    boss=boss.resize((384,396),Image.Resampling.NEAREST);canvas.paste(boss,(720,66),boss)
    for m in MUZZLES:
        x=720+m[0]*3;y=66+m[1]*3;d.line((x,y,x,y+75),fill=(255,180,236),width=3)
        d.ellipse((x-4,y-4,x+4,y+4),outline=(0,255,210))
    d.text((744,479),'Canonical muzzle: (-42,+19) / (+43,+19)',fill=(210,221,243),font=font)
    for c in range(1,6):
        target=next(s for s in sprites[52:92] if family(s)[1]==c);pic=decode(blob,target).resize((80,80),Image.Resampling.NEAREST)
        x=50+(c-1)*126;y=580;canvas.paste(pic,(x,y),pic)
        d.text((x,y+90),'ROUND B'+str(c),fill=(198,215,237),font=font)
    for j,i in enumerate((840,842,845,847,850,853,854,856)):
        s=sprites[i];pic=decode(blob,s).resize((s['width']*2,s['height']*2),Image.Resampling.NEAREST)
        x=20+j*137;y=725;canvas.paste(pic,(x,y),pic)
        d.text((x,y-18),'retained '+str(i),fill=(148,175,200),font=font)
    return png(canvas)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference',type=Path,default=ROOT.parent/REFERENCE)
    p.add_argument('--source',type=Path,default=next(p for p in ROOT.parents if (p/'04_Assets_Set_A').exists())/'04_Assets_Set_A')
    p.add_argument('--check',action='store_true');a=p.parse_args()
    outputs,report=build(a.reference.resolve(),a.source.resolve())
    for rel,raw in outputs.items():
        path=ROOT/rel
        if a.check:assert path.read_bytes()==raw,'Generated mismatch:'+rel
        else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
    print(json.dumps(report,indent=2,ensure_ascii=False))
if __name__=='__main__':main()
