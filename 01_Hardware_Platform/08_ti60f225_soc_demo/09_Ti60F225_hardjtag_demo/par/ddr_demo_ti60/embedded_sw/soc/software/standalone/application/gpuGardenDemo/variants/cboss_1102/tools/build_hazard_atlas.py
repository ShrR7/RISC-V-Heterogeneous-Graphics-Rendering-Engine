#!/usr/bin/env python3
"""Align dangerous sprite white cores with physics points; bake plasma beam45.
SPDX-License-Identifier: GPL-3.0-or-later
Old bullet pixels and nonhazard geometry stay exact; only640bullet anchors are
re-derived from authored pure white cores. Beam tail is repacked, no board use.
"""
from __future__ import annotations
import argparse,copy,hashlib,io,json,math,re,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
REFERENCE='neon_ordnance_20261016'
REFERENCE_SHA='b6aa06b4b71f91eaec093f935d943b0b1082671589c4ad3b3f75eb6d105e65bb'
OLD_BYTES=710028
PREFIX_BYTES=698028
BUDGET=0xba000
KEY=0xf81f
def sha(data):return hashlib.sha256(data).hexdigest()
def jb(value):return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
def pixels(blob,s):return struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
def png(im):
    f=io.BytesIO();im.save(f,format='PNG');return f.getvalue()
def is_bullet(i):return 12<=i<92 or 280<=i<840
def bullet_direction(s):
    name=s['name']
    if name.startswith('PRECISION_'):
        p=name.split('_');return p[1],int(p[2][1:]),int(p[3][1:])
    p=name.split('_');return p[0],int(p[1][1:]),int(p[2][1:])*8
def white_anchor(blob,s):
    white=[(index%s['width'],index//s['width']) for index,c in enumerate(pixels(blob,s)) if c==0xffff]
    assert white,'Dangerous sprite has no authored white core:'+s['name']
    count=len(white);sx=sum(p[0] for p in white);sy=sum(p[1] for p in white)
    # Integer nearest rounding; exact half rounds toward +axis, deterministic.
    ax=(2*sx+count)//(2*count);ay=(2*sy+count)//(2*count)
    assert (ax,ay) in white,'White core centroid unexpectedly falls outside core'
    cx,cy=sx/count,sy/count;kind,col,dir=bullet_direction(s)
    return (ax,ay),dict(method='exact RGB5650xFFFF authored-core centroid; nearest integer half toward positive axis',
        white_pixels=count,white_bbox=[min(p[0] for p in white),min(p[1] for p in white),
            max(p[0] for p in white)+1,max(p[1] for p in white)+1],
        centroid=[cx,cy],quantization_error_px=math.hypot(ax-cx,ay-cy),white_centroid_sums=[sx,sy],
        reference_render_anchor=[s['anchor_x'],s['anchor_y']],render_anchor=[ax,ay],kind=kind,
        palette_family=col,nominal64_direction=dir,source_pixels_modified=False,core_physics_point_modified=False)
def beam_frame(source,style):
    values=[]
    for y in range(120):
        for x in range(45):
            signed=x-22;distance=abs(signed);phase=(y+style*9)%24
            if distance<=6:
                # All13px remain bright white-pink; diagonal white cells travel
                # through that core instead of a featureless static white bar.
                braid=(phase+signed*2)%24
                hot=braid<5 or 12<=braid<15 or distance<=1
                color=(255,255,255) if hot else (255,229+(phase%6),243+(distance%3))
            else:
                # Original Rawdanitsu red alpha profile supplies the feather.
                sample=233+(-1 if signed<0 else 1)*(12+(distance-7)*20//15)
                red,green,blue,alpha=source.getpixel((max(201,min(265,sample)),348))
                profile=red*alpha//255
                base=profile*(110+phase*2)//255
                coil_distance=7+((phase if signed<0 else 23-phase)*13//23)
                on_coil=abs(distance-coil_distance)<=1
                accent=profile*4//5 if on_coil else 0
                # Halo has deliberately lower luminance than the collision core.
                # Blue-violet diagonal filaments wrap the magenta source body.
                color=(min(204,base+accent),min(78,base//7+accent//3),
                    min(188,base*3//5+accent*4//5))
            red,green,blue=color;value=(red>>3)<<11 | (green>>2)<<5 | (blue>>3)
            if value==KEY:value^=1
            values.append(value)
    data=struct.pack('<'+'H'*len(values),*values)
    return data,dict(kind='source_beam_feather_plus_code_native_braided_plasma',
        author='Rawdanitsu / Team C plasma cache',license='CC0-1.0 source; GPL-3.0-or-later cache generator',
        source_file='assets/source/ordnance_cc0/beams.png',
        source_sha256='0628fe6f326efbf97d6637b76913c77a1f6247a7f815bce2307744fcd62a82b0',
        source_cross_section={'row':348,'center_x':233,'range':[201,265]},
        visual_width=45,collision_core_width=13,bright_core_columns_inclusive=[16,28],
        low_luminance_halo_columns=[[0,15],[29,44]],anchor=[22,0],height=120,
        y_period=24,style=style,source_png_modified=False,
        transform='source alpha feather; magenta/violet double diagonal filaments; white13px core with moving pale braid',
        renderer_phase_hint='scroll source Y modulo24; phase*3 gives eight positions; two styles share width/core',
        runtime_resampling=False,gameplay_damage_in_asset=False)
def decode(blob,s):
    im=Image.new('RGBA',(s['width'],s['height']))
    im.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in pixels(blob,s)])
    return im
def anchored_preview(blob,sprites):
    im=Image.new('RGB',(1176,780),(8,12,24));draw=ImageDraw.Draw(im);font=ImageFont.load_default()
    draw.text((16,12),'HOST RGB565 HAZARD PREVIEW - white-core point matches physics; not a board capture',fill=(229,235,250),font=font)
    draw.text((16,30),'All64directions / B1 shown. All5palettes share the exact white mask. Cyan ticks identify the physical point.',fill=(150,178,204),font=font)
    by={}
    for s in sprites:
        if is_bullet(s['id']):
            kind,col,d=bullet_direction(s)
            if col==1:by[(kind,d)]=s
    for kind,top in [('DART',74),('DIAMOND',410)]:
        draw.text((16,top-20),kind+' / white-core centroid / 5.625 degree quantization retained',fill=(214,167,203),font=font)
        for d in range(64):
            s=by[(kind,d)];pic=decode(blob,s).resize((s['width']*2,s['height']*2),Image.Resampling.NEAREST)
            cx=42+(d%16)*72;cy=top+36+(d//16)*77
            im.paste(pic,(cx-s['anchor_x']*2,cy-s['anchor_y']*2),pic)
            # Four separated ticks leave the white anchor pixel unobscured.
            for x0,y0,x1,y1 in [(cx-7,cy,cx-4,cy),(cx+4,cy,cx+7,cy),(cx,cy-7,cx,cy-4),(cx,cy+4,cx,cy+7)]:
                draw.line((x0,y0,x1,y1),fill=(0,219,234))
            draw.text((cx-9,cy+29),str(d),fill=(151,169,197),font=font)
    return png(im)
def beam_preview(blob,sprites):
    image=Image.new('RGB',(720,520),(8,12,24));d=ImageDraw.Draw(image);font=ImageFont.load_default()
    d.text((16,14),'HOST RGB565 PLASMA BEAM /45px visual,13px bright hazard core',fill=(229,235,250),font=font)
    d.text((16,32),'Source24px period; two cached styles + runtime Y scroll. Lower-light halo is not collision.',fill=(150,178,204),font=font)
    for i,s in enumerate(sprites[857:859]):
        pic=decode(blob,s).resize((s['width']*3,s['height']*3),Image.Resampling.NEAREST)
        x=100+i*300;y=80;image.paste(pic,(x,y),pic)
        d.line((x+16*3-1,y-12,x+16*3-1,y+370),fill=(47,128,119))
        d.line((x+29*3,y-12,x+29*3,y+370),fill=(47,128,119))
        d.text((x,y+390),'STYLE'+str(i)+' ID'+str(s['id']),fill=(195,209,227),font=font)
    return png(image)
def construct(reference):
    old=(reference/'assets/atlas.bin').read_bytes()
    before=json.loads((reference/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(old)==OLD_BYTES and sha(old)==REFERENCE_SHA==before['sha256'] and len(before['sprites'])==859
    assert before['sprites'][857]['offset']==PREFIX_BYTES
    atlas=bytearray(old[:PREFIX_BYTES]);sprites=copy.deepcopy(before['sprites']);rows=[]
    for s in sprites:
        if is_bullet(s['id']):
            anchor,record=white_anchor(old,s);s['anchor_x'],s['anchor_y']=anchor
            s['render_anchor_override']=record;rows.append(dict(id=s['id'],name=s['name'],**record))
    sourcepath=ROOT/'assets/source/ordnance_cc0/beams.png'
    assert sha(sourcepath.read_bytes())=='0628fe6f326efbf97d6637b76913c77a1f6247a7f815bce2307744fcd62a82b0'
    source=Image.open(sourcepath).convert('RGBA')
    for style in range(2):
        data,provenance=beam_frame(source,style);s=sprites[857+style];s.update(offset=len(atlas),width=45,height=120,
            stride=90,anchor_x=22,anchor_y=0,bytes=len(data),source=provenance,opaque_pixels=45*120,
            rgb565_sha256=sha(data),opaque_key_collisions_adjusted=0)
        atlas.extend(data)
    final=bytes(atlas);assert len(final)==719628<=BUDGET and final[:PREFIX_BYTES]==old[:PREFIX_BYTES]
    assert len(rows)==640 and max(r['quantization_error_px'] for r in rows)<=math.sqrt(.5)+1e-10
    # Palette identity is measured from actualRGB565 white masks, not inferred.
    groups={}
    for s in sprites:
        if not is_bullet(s['id']):continue
        kind,col,d=bullet_direction(s);mask=tuple(c==0xffff for c in pixels(final,s))
        geometry=(s['width'],s['height'],s['anchor_x'],s['anchor_y'],mask)
        key=(kind,d)
        if key in groups:assert groups[key]==geometry
        else:groups[key]=geometry
    assert len(groups)==128
    for i,s in enumerate(sprites):
        assert 0<s['width']<=127 and 0<s['height']<=127 and -128<=s['anchor_x']<=127 and -128<=s['anchor_y']<=127
        if not is_bullet(i) and i<857:assert s==before['sprites'][i]
    digest=sha(final);crc=zlib.crc32(final)
    h=(reference/'src/garden_assets.h').read_text(encoding='utf-8-sig').replace('Run tools/build_ordnance_atlas.py','Run tools/build_hazard_atlas.py')
    h=re.sub(r'#define GARDEN_ATLAS_BYTES \d+u',f'#define GARDEN_ATLAS_BYTES {len(final)}u',h)
    h=re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u',f'#define GARDEN_ATLAS_CRC32 0x{crc:08X}u',h)
    h=h.replace('#define GARDEN_FONT_WIDTH 8u','#define GARDEN_BEAM_VISUAL_WIDTH 45u\n#define GARDEN_BEAM_CORE_WIDTH 13u\n#define GARDEN_BEAM_TEXTURE_PERIOD 24u\n#define GARDEN_FONT_WIDTH 8u')
    c=(reference/'src/garden_assets.c').read_text(encoding='utf-8-sig').replace('Generated by tools/build_ordnance_atlas.py','Generated by tools/build_hazard_atlas.py')
    c=re.sub(r'(garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',lambda m:m[1]+digest+m[2],c)
    cs=['    {%du, %du, %du, %d, %d}, /* %d: %s */'%(s['offset'],s['width'],s['height'],s['anchor_x'],s['anchor_y'],s['id'],s['name']) for s in sprites]
    c,n=re.subn(r'(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n).*?(\n\};)',
        lambda m:m[1]+'\n'.join(cs)+m[2],c,count=1,flags=re.S);assert n==1
    manifest=copy.deepcopy(before);manifest.update(variant='neon_hazard_20261016',bytes=len(final),sha256=digest,
        crc32=f'0x{crc:08X}',generator='tools/build_hazard_atlas.py',sprites=sprites,reference_variant=REFERENCE,
        reference_atlas_sha256=REFERENCE_SHA,unchanged_prefix_bytes=PREFIX_BYTES,
        original_bullet_pixels_unchanged=True,old_sprite_records_unchanged=217,
        old_nonbullet_records_unchanged=217,reanchored_bullet_count=640,
        changed_bullet_anchor_count=sum(r['reference_render_anchor']!=r['render_anchor'] for r in rows),
        original_ids_unchanged=859,original_ids_scope='IDs unchanged; names/pixels0..856 unchanged;640bullet render anchors intentionally derive white core',
        remaining_budget_bytes=BUDGET-len(final),appended_bytes=len(final)-OLD_BYTES)
    generation=dict(status='HOST_ASSET_VALIDATION_PASS_NOT_BOARD_ACCEPTANCE',reference_variant=REFERENCE,
        old_atlas_sha256=REFERENCE_SHA,new_atlas_sha256=digest,atlas_bytes=len(final),atlas_crc32=f'0x{crc:08X}',
        same_prefix_bytes=PREFIX_BYTES,old_840_pixels_byte_identical=True,bullet_pixel_blobs_changed=0,
        bullet_records_checked=640,bullet_anchor_values_changed=manifest['changed_bullet_anchor_count'],
        white_core_centroid_max_quantization_error_px=max(r['quantization_error_px'] for r in rows),
        each_palette_geometry_identical=True,type_direction_groups=128,
        nonbullet_before_beam_records_unchanged=217,descriptor_bytes=8,sprite_count=859,
        beam_visual_width=45,beam_core_width=13,beam_low_luminance_halo_each_side=16,
        beam_height=120,beam_y_period=24,beam_style_count=2,
        free_bytes_before_scratch=BUDGET-len(final),physics_point_modified=False,source_pngs_modified=False,
        anchor_oracle='assets/hazard_anchor_oracle.json')
    oracle=dict(schema='white-core-anchor-oracle-v1',reference_atlas_sha256=REFERENCE_SHA,
        rule='Exact0xFFFF pixels only; centroid sums/count; nearest integer positive half; never glow or bounding-box center',
        physical_point='core x/y; sprite top-left=x-anchor_x,y-anchor_y',max_euclidean_error_px=math.sqrt(.5),
        palette_count=5,type_direction_groups=128,rows=rows)
    notice=(reference/'assets/NOTICE.txt').read_text(encoding='utf-8-sig')+'''
Hazard presentation correction (2026-10-16)
All original bullet pixel bytes are identical.640 DART/DIAMOND descriptors,
all64directions and5colors, now place their exact0xFFFF white-core centroid at
the physics x/y point.480 anchor values changed;160 were already correct.
This supersedes the earlier tail/center render anchors without changing the
author's images, velocity, collision point or64direction lookup.
assets/hazard_anchor_oracle.json records white-mask sums/count and rounding.
Nonbullet geometry remains exact through ID856.
Beam857/858 is rebaked45x120 with13px bright white-pink core and16px lower-
luminance halo on each side. Original Rawdanitsu source alpha profile supplies
feathering; Team C coded diagonal plasma filaments repeat every24rows.
Only beam tail bytes are repacked. No runtime scaling or new source download.
'''
    out={'assets/atlas.bin':final,'assets/atlas.sha256':(digest+'  atlas.bin\n').encode(),
        'assets/manifest.json':jb(manifest),'assets/hazard_generation.json':jb(generation),
        'assets/hazard_anchor_oracle.json':jb(oracle),'assets/hazard_anchor_contact_sheet.png':anchored_preview(final,sprites),
        'assets/hazard_beam_preview.png':beam_preview(final,sprites),'assets/NOTICE.txt':notice.encode(),
        'src/garden_assets.c':c.encode(),'src/garden_assets.h':h.encode()}
    frames=[]
    for phase in range(8):
        s=sprites[857+(phase//4)%2];pic=decode(final,s);shift=phase*3
        canvas=Image.new('RGBA',pic.size)
        for y in range(120):
            crop=pic.crop((0,(y+shift)%120,45,(y+shift)%120+1));canvas.paste(crop,(0,y))
        bg=Image.new('RGB',canvas.size,(8,12,24));bg.paste(canvas,mask=canvas.getchannel('A'));frames.append(bg.resize((180,480),Image.Resampling.NEAREST))
    f=io.BytesIO();frames[0].save(f,format='GIF',save_all=True,append_images=frames[1:],duration=50,loop=0)
    out['assets/hazard_beam_scroll_preview.gif']=f.getvalue()
    checksums=copy.deepcopy(json.loads((reference/'assets/runtime_checksums.json').read_text(encoding='utf-8-sig'))['files'])
    checksums.update({r:{'bytes':len(b),'sha256':sha(b)} for r,b in out.items()})
    for rel in ['tools/build_hazard_atlas.py','tools/verify_hazard_assets.py','tools/test_hazard_assets.c']:
        p=ROOT/rel
        if p.exists():checksums[rel]={'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())}
    out['assets/runtime_checksums.json']=jb(dict(format='sha256',atlas_crc32=f'0x{crc:08X}',files=checksums))
    return out,generation
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,default=ROOT.parent/REFERENCE);p.add_argument('--check',action='store_true');a=p.parse_args()
    out,report=construct(a.reference.resolve())
    for rel,data in out.items():
        target=ROOT/rel
        if a.check:assert target.read_bytes()==data,'Generated mismatch:'+rel
        else:target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()

