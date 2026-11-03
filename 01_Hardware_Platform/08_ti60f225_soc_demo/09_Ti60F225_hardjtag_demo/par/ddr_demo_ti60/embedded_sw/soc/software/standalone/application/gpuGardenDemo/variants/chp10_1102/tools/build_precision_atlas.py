#!/usr/bin/env python3
"""Append560tight Master48464-direction RGB565 frames; retain old280IDs/prefix.
SPDX-License-Identifier: GPL-3.0-or-later

All work is offline. No FPGA/DDR writes. --check reconstructs outputs without
writing. Exact original native16direction crops and only nearest-neighbour
rotations fill the intermediate64directions; no new palette or gameplay.
"""
from __future__ import annotations
import argparse,copy,hashlib,json,math,re,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
REFERENCE_VARIANT='neon_animation_20261015'
PREFIX_BYTES=236954
PREFIX_SHA='82b626660cda45307a6b8f88b808eb4bb66ca240aff32de2a171e80bc9e22762'
SOURCE_SHA='cae852d8f4511cd1172a2e302faa91110372ad574a14b7f32907df88d5eefee8'
KEY=0xf81f
ADDRESS=0x5c4000
SPAN=0xbc000
RESERVED=8192
COUNT=840
ROTATIONS=64
NATIVE={
 'DART':{'roi':(390,80,475,158),'center':(432,119),'base':12},
 'DIAMOND':{'roi':(390,159,479,250),'center':(432,204),'base':52},
}
def sha(data):return hashlib.sha256(data).hexdigest()
def json_bytes(value):return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
def pixels(im):return im.get_flattened_data() if hasattr(im,'get_flattened_data') else im.getdata()
def components(source,description):
    x0,y0,x1,y1=description['roi'];cx,cy=description['center']
    pending={(x,y) for y in range(y0,y1) for x in range(x0,x1) if source.getpixel((x,y))[:3]!=(0,0,0)}
    found={}
    while pending:
        work=[pending.pop()];part=[]
        while work:
            x,y=work.pop();part.append((x,y))
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,1),(1,-1),(-1,-1)):
                neighbour=(x+dx,y+dy)
                if neighbour in pending:pending.remove(neighbour);work.append(neighbour)
        xs=[p[0] for p in part];ys=[p[1] for p in part]
        angle=math.degrees(math.atan2(sum(ys)/len(ys)-cy,sum(xs)/len(xs)-cx))%360
        direction=round(angle/22.5)%16
        assert direction not in found and len(part)>=64
        found[direction]={'crop':[min(xs),min(ys),max(xs)+1,max(ys)+1],'opaque_pixels':len(part)}
    assert set(found)==set(range(16))
    return found
def tail_anchor(im,native):
    angle=math.radians(native*22.5);ux,uy=math.cos(angle),math.sin(angle)
    ink=[(x,y) for y in range(im.height) for x in range(im.width) if im.getpixel((x,y))[3]]
    # The tail's logical reference is an actual rearmost source pixel. For a
    # flat rear edge, use the closest pixel to its perpendicular centerline.
    return min(ink,key=lambda p:(round(p[0]*ux+p[1]*uy,10),abs((p[0]-(im.width-1)/2)*uy-(p[1]-(im.height-1)/2)*ux)))
def native_frames(reference,source,kind,family,parts,recipes):
    frames={};scales={}
    for native in range(0,16,2):
        recipe=recipes[f'{kind}_B{family}_R{native//2}'];box=parts[native]['crop']
        assert recipe['source_crop_xyxy_exclusive']==box
        path=reference/'assets/imported/master484'/recipe['png'];raw=path.read_bytes();assert sha(raw)==recipe['png_sha256']
        im=Image.open(path).convert('RGBA');assert set(im.getchannel('A').tobytes())<={0,255}
        frames[native]=(im,(recipe['anchor_x'],recipe['anchor_y']),{
            'kind':'frozen_existing_direction_png','png':str(path.relative_to(reference)).replace('\\','/'),'png_sha256':sha(raw),
            'native16_index':native,'original_recipe_name':recipe['name'],'source_crop':box,
            'normalized_size':[im.width,im.height],'reference_anchor':[recipe['anchor_x'],recipe['anchor_y']]})
        size=recipe['resize']['size'];scales[native]=(size[0]/(box[2]-box[0])+size[1]/(box[3]-box[1]))/2
    substitutions=recipes[f'{kind}_B{family}_R0']['palette_substitution']
    palette={tuple(p['source_rgb']):tuple(p['target_rgb']) for p in substitutions}
    for native in range(1,16,2):
        box=parts[native]['crop'];im=source.crop(box).convert('RGBA')
        im.putdata([(0,0,0,0) if p[:3]==(0,0,0) else (*palette.get(p[:3],p[:3]),255) for p in pixels(im)])
        scale=(scales[native-1]+scales[(native+1)%16])/2
        size=(max(1,round(im.width*scale)),max(1,round(im.height*scale)))
        im=im.resize(size,Image.Resampling.NEAREST)
        anchor=tail_anchor(im,native) if kind=='DART' else (im.width//2,im.height//2)
        frames[native]=(im,anchor,{'kind':'author_native16_intermediate','native16_index':native,
            'source_file':'assets/source/master484/M484BulletCollection2.png','source_sha256':SOURCE_SHA,'source_crop':box,
            'palette_recipe_name':f'{kind}_B{family}_R0','normalized_size':list(size),'reference_anchor':list(anchor),
            'resize':'nearest-neighbour; mean scale of adjacent unchanged authored8direction frames'})
    return frames
def rotated_tight(im,anchor,angle):
    center=64;canvas=Image.new('RGBA',(128,128),(0,0,0,0))
    canvas.paste(im,(center-anchor[0],center-anchor[1]))
    # PIL centers use pixel-edge coordinates. center+0.5 pins the logical
    # anchor pixel CENTER, so its integer pixel remains the exact same pivot.
    if angle:canvas=canvas.rotate(-angle,Image.Resampling.NEAREST,center=(64.5,64.5),expand=False)
    box=canvas.getchannel('A').getbbox();assert box and box[0]>0 and box[1]>0 and box[2]<128 and box[3]<128
    result=canvas.crop(box);ax,ay=center-box[0],center-box[1]
    assert 0<=ax<result.width and 0<=ay<result.height and result.getpixel((ax,ay))[3]
    assert set(result.getchannel('A').tobytes())<={0,255}
    return result,(ax,ay),list(box)
def rgb565(im):
    values=[];adjusted=0
    for red,green,blue,alpha in pixels(im):
        if not alpha:value=KEY
        else:
            value=(red>>3)<<11|(green>>2)<<5|(blue>>3)
            if value==KEY:value^=1;adjusted+=1
        values.append(value)
    return struct.pack('<'+'H'*len(values),*values),adjusted
def precision_id(kind,family,direction):
    if not direction%8:return NATIVE[kind]['base']+(family-1)*8+direction//8
    return 280+(5 if kind=='DIAMOND' else 0)*56+(family-1)*56+direction-direction//8-1
def preview(blob,sprites):
    image=Image.new('RGB',(1472,770),(7,12,24));draw=ImageDraw.Draw(image);font=ImageFont.load_default()
    draw.text((14,12),'HOST RGB565 ATLAS PREVIEW - 64 directions, 5.625 degree step; not FPGA capture or measured FPS',fill=(218,232,247),font=font)
    draw.text((14,29),'2x nearest zoom; cyan cross is the unchanged logical tail/center reference. Five original palettes retained.',fill=(113,178,201),font=font)
    for kind,left in (('DART',16),('DIAMOND',752)):
        draw.text((left,52),kind+' B1: EAST -> clockwise; screen Y positive downward',fill=(244,158,196),font=font)
        for direction in range(64):
            s=sprites[precision_id(kind,1,direction)];values=struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
            rgba=Image.new('RGBA',(s['width'],s['height']));rgba.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in values])
            x=left+(direction%8)*90;y=80+(direction//8)*76;cx=x+44;cy=y+34
            image.paste(rgba.resize((rgba.width*2,rgba.height*2),Image.Resampling.NEAREST),(cx-s['anchor_x']*2,cy-s['anchor_y']*2),rgba.resize((rgba.width*2,rgba.height*2),Image.Resampling.NEAREST))
            draw.line((cx-2,cy,cx+2,cy),fill=(65,166,197));draw.line((cx,cy-2,cx,cy+2),fill=(65,166,197))
            draw.text((x+4,y+57),f'{direction:02d} {direction*5.625:g}deg',fill=(164,185,210),font=font)
        for family in range(1,6):
            s=sprites[precision_id(kind,family,4)];values=struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
            rgba=Image.new('RGBA',(s['width'],s['height']));rgba.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in values])
            x=left+family*112-90;image.paste(rgba,(x,719),rgba);draw.text((x,700),f'ORIGINAL B{family}',fill=(164,185,210),font=font)
    import io
    buffer=io.BytesIO();image.save(buffer,format='PNG');return buffer.getvalue()
def construct(reference):
    old=(reference/'assets/atlas.bin').read_bytes();manifest=json.loads((reference/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(old)==PREFIX_BYTES and sha(old)==PREFIX_SHA==manifest['sha256'] and len(manifest['sprites'])==280
    srcpath=reference/'assets/source/master484/M484BulletCollection2.png';srcdata=srcpath.read_bytes();assert sha(srcdata)==SOURCE_SHA
    source=Image.open(srcpath).convert('RGBA');imports=json.loads((reference/'assets/imported/master484/manifest.json').read_text(encoding='utf-8-sig'))
    recipes={s['name']:s for s in imports['sprites']};parts={kind:components(source,description) for kind,description in NATIVE.items()}
    blob=bytearray(old);sprites=copy.deepcopy(manifest['sprites']);rows=[];geometry={}
    for kind in NATIVE:
        measurements=[]
        for family in range(1,6):
            natives=native_frames(reference,source,kind,family,parts[kind],recipes)
            for direction in range(64):
                if direction%8==0:continue
                native_unwrapped=(direction+2)//4;native=native_unwrapped%16;angle=(direction-native_unwrapped*4)*5.625
                im,anchor,recipe=natives[native];result,new_anchor,bbox=rotated_tight(im,anchor,angle);data,adjusted=rgb565(result)
                blob.extend(b'\0'*((-len(blob))%4));offset=len(blob);index=len(sprites);assert index==precision_id(kind,family,direction)
                blob.extend(data);width,height=result.size;ax,ay=new_anchor
                source_record=dict(recipe,author='Master484',license='CC0-1.0',palette_family=family,
                    nominal_angle_units=direction*16,nominal_angle_degrees=direction*5.625,
                    rotation_clockwise_degrees=angle,rotation_method='nearest-neighbour, binary alpha',rotation_canvas=[128,128],
                    rotation_pivot_edge_coordinates=[64.5,64.5],paste_xy=[64-anchor[0],64-anchor[1]],tight_crop=bbox,
                    rgba_sha256=sha(result.tobytes()),rgb565_sha256=sha(data),anchor_policy='original tail reference' if kind=='DART' else 'original center reference',
                    collision_or_core_coordinate_change=False)
                record={'id':index,'name':f'PRECISION_{kind}_B{family}_R{direction:02d}','offset':offset,'width':width,'height':height,
                    'stride':width*2,'anchor_x':ax,'anchor_y':ay,'bytes':len(data),'source':source_record,
                    'opaque_pixels':sum(p[3]!=0 for p in pixels(result)),'opaque_key_collisions_adjusted':adjusted}
                sprites.append(record);rows.append(f'    {{{offset}u, {width}u, {height}u, {width*2}u, {ax}, {ay}}}, /* {index}: {record["name"]} */')
                measurements.append({'pixels':width*height,'opaque':record['opaque_pixels'],'width':width,'height':height})
        geometry[kind]={'new_frames':len(measurements),'width_range':[min(x['width'] for x in measurements),max(x['width'] for x in measurements)],
            'height_range':[min(x['height'] for x in measurements),max(x['height'] for x in measurements)],
            'mean_new_bbox_pixels':sum(x['pixels'] for x in measurements)/len(measurements),
            'mean_new_opaque_pixels':sum(x['opaque'] for x in measurements)/len(measurements)}
    final=bytes(blob);assert len(sprites)==COUNT and final[:PREFIX_BYTES]==old and len(final)<=SPAN-RESERVED
    digest=sha(final);crc=zlib.crc32(final)
    h=(reference/'src/garden_assets.h').read_text(encoding='utf-8-sig')
    h=h.replace('Run tools/build_scene_atlas.py','Run tools/build_precision_atlas.py')
    h=re.sub(r'#define GARDEN_ATLAS_BYTES \d+u',f'#define GARDEN_ATLAS_BYTES {len(final)}u',h)
    h=re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u',f'#define GARDEN_ATLAS_CRC32 0x{crc:08X}u',h)
    h=h.replace('#define GARDEN_BULLET_ROTATIONS 8u','#define GARDEN_BULLET_ROTATIONS 8u\n#define GARDEN_PRECISION_ROTATIONS 64u')
    assert h.count('    GARDEN_ASSET_COUNT = 280')==1
    h=h.replace('    GARDEN_ASSET_COUNT = 280','    GARDEN_ASSET_PRECISION_BASE = 280,\n    GARDEN_ASSET_COUNT = 840')
    marker='const garden_sprite_t *garden_bullet_asset(unsigned type, unsigned colour, unsigned angle);'
    assert marker in h
    h=h.replace(marker,marker+'\n/*64directions, old8IDs retained; round angle to nearest16units. */\nunsigned garden_bullet_precision_id(unsigned type,unsigned colour,unsigned angle);\n/*Includes only original80and appended560enemy projectile sprites. */\nunsigned garden_asset_is_enemy_bullet(unsigned id);')
    c=(reference/'src/garden_assets.c').read_text(encoding='utf-8-sig')
    c=c.replace('Generated by tools/build_scene_atlas.py','Generated by tools/build_precision_atlas.py')
    c=re.sub(r'(const char garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',lambda m:m[1]+digest+m[2],c)
    match=re.search(r'(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n)(.*?)(\n\};)',c,re.S);assert match
    c=c[:match.end(2)]+'\n'+'\n'.join(rows)+c[match.end(2):]
    c+='\n/*Precision descriptors append without changing the old8direction API. */\nunsigned garden_bullet_precision_id(unsigned type,unsigned colour,unsigned angle)\n{\n    unsigned rotation=((angle+8u)&1023u)>>4;\n    if(colour<1u || colour>5u) colour=3u;\n    if(!(rotation&7u)) return garden_bullet_id(type,colour,rotation*16u);\n    return GARDEN_ASSET_PRECISION_BASE+(type==2u ? 5u:0u)*56u\n           +(colour-1u)*56u+rotation-(rotation>>3)-1u;\n}\nunsigned garden_asset_is_enemy_bullet(unsigned id)\n{\n    return (id>=GARDEN_ASSET_DART_BASE && id<GARDEN_ASSET_FONT_WHITE_BASE) ||\n           (id>=GARDEN_ASSET_PRECISION_BASE && id<GARDEN_ASSET_COUNT);\n}\n'
    newmanifest=copy.deepcopy(manifest);newmanifest.update(variant='neon_heading_precision_20261015',bytes=len(final),sha256=digest,crc32=f'0x{crc:08X}',
        safe_window_end=hex(ADDRESS+SPAN),scratch_start=hex(ADDRESS+SPAN-RESERVED),budget_bytes=SPAN-RESERVED,
        sprites=sprites,reference_variant=REFERENCE_VARIANT,reference_atlas_sha256=PREFIX_SHA,unchanged_prefix_bytes=PREFIX_BYTES,
        original_ids_unchanged=280,original_bullet_rotations=8,precision_rotations=64,precision_id_range=[280,839],
        appended_bytes=len(final)-PREFIX_BYTES,remaining_budget_bytes=SPAN-RESERVED-len(final),generator='tools/build_precision_atlas.py')
    generation={'status':'GENERATED_OFFLINE_NOT_BOARD_ACCEPTED','prefix_byte_equal':True,'prefix_bytes':PREFIX_BYTES,'prefix_sha256':PREFIX_SHA,
        'old_records_equal':280,'new_frames':560,'sprite_count':COUNT,'atlas_bytes':len(final),'atlas_sha256':digest,'atlas_crc32':f'0x{crc:08X}',
        'span':[hex(ADDRESS),hex(ADDRESS+SPAN)],'reserved_tail_bytes':RESERVED,'free_bytes_before_scratch':SPAN-RESERVED-len(final),
        'native16_source_components':parts,'new_frame_geometry':geometry,'precision_step_degrees':5.625,'ideal_max_quantization_error_degrees':2.8125,
        'descriptor_extra_bytes_if_16byte_layout':560*16,'lookup_table_extra_bytes':0,'nominal_source_palette_count':5,
        'core_collision_coordinates_unchanged':True,'runtime_resampling':False,'hardware_access':False}
    notice=(reference/'assets/NOTICE.txt').read_text(encoding='utf-8-sig')+'\nPrecision64projectiles (2026-10-15)\nThe old236954RGB565bytes and old280records remain exact.560newframes\nretain Master484CC0pixels/palette recipes, use the real native16wheel,\nthen nearest-neighbour offline rotation and tight cropping around the\noriginal tail(Dart)/center(Diamond) reference. No runtime resampling,\nnew palette, core coordinates, collision, damage or clock changes.\nassets/precision_heading64_preview.png is a labelled host atlas preview.\n'
    outputs={'assets/atlas.bin':final,'assets/atlas.sha256':(digest+'  atlas.bin\n').encode(),
        'assets/manifest.json':json_bytes(newmanifest),'assets/precision_generation.json':json_bytes(generation),
        'assets/precision_heading64_preview.png':preview(final,sprites),'assets/NOTICE.txt':notice.encode('utf-8'),
        'src/garden_assets.c':c.encode('utf-8'),'src/garden_assets.h':h.encode('utf-8')}
    checksums=json.loads((reference/'assets/runtime_checksums.json').read_text(encoding='utf-8-sig'))['files']
    checksums=copy.deepcopy(checksums);checksums.update({name:{'bytes':len(data),'sha256':sha(data)} for name,data in outputs.items()})
    for name in ('tools/build_precision_atlas.py','tools/verify_precision_assets.py','tools/test_precision_assets.c'):
        path=ROOT/name
        if path.exists():checksums[name]={'bytes':path.stat().st_size,'sha256':sha(path.read_bytes())}
    outputs['assets/runtime_checksums.json']=json_bytes({'format':'sha256','atlas_crc32':f'0x{crc:08X}','files':checksums})
    return outputs,generation
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--reference',type=Path,default=ROOT.parent/REFERENCE_VARIANT);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    outputs,generation=construct(args.reference.resolve())
    for relative,data in outputs.items():
        target=ROOT/relative
        if args.check:assert target.read_bytes()==data,'Generated output differs:'+relative
        else:target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    print(json.dumps(generation,indent=2))
if __name__=='__main__':main()
