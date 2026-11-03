"""Offline RGB565 cache assembly for chp10_1102; no device access.

The accepted cjui atlas is an immutable prefix. Existing asset IDs and pixels
remain exact; new material comes from user PNGs and the existing CC0 blast sheet.
"""
from pathlib import Path
import argparse,copy,hashlib,json,re,struct,zlib,shutil
from PIL import Image,ImageDraw

V=Path(__file__).resolve().parents[1]
ROOT=next(p for p in V.parents if (p/'01_Hardware_Platform').is_dir())
BASE=V.parent/'cjui_1031'
KEY=0xF81F;BUDGET=0xB9000 # 0x67D000 reserved GPU batch list starts after atlas
BASE_SHA='584c47be285778f8680a0f0a5f21a12aa610b3e02b7aa5c58885cedb06b998a6'
def sha(b):return hashlib.sha256(b).hexdigest()
def encode(im,cutoff=32,premultiply=True):
    words=[];collisions=0
    for red,green,blue,alpha in struct.iter_unpack('4B',im.convert('RGBA').tobytes()):
        if alpha<cutoff:words.append(KEY);continue
        if premultiply:
            red=(red*alpha+127)//255;green=(green*alpha+127)//255;blue=(blue*alpha+127)//255
        value=((red>>3)<<11)|((green>>2)<<5)|(blue>>3)
        if value==KEY:value^=1;collisions+=1
        words.append(value)
    return struct.pack('<'+'H'*len(words),*words),collisions
def decode(blob,w,h):
    im=Image.new('RGBA',(w,h));px=[]
    for v in struct.unpack('<'+'H'*(w*h),blob):
        px.append((0,0,0,0) if v==KEY else (((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255))
    im.putdata(px);return im
def validate(blob,manifest):
    assert len(blob)<=BUDGET and len(blob)==manifest['bytes']
    assert sha(blob)==manifest['sha256'] and zlib.crc32(blob)==int(manifest['crc32'],16)
    assert sha(blob[:614916])==BASE_SHA
    assert len(manifest['sprites'])==872
    for i,s in enumerate(manifest['sprites']):
        assert s['id']==i and s['offset']%4==0 and 0<s['width']<=255 and 0<s['height']<=255
        assert 0<=s['anchor_x']<=127 and 0<=s['anchor_y']<=127
        actual=blob[s['offset']:s['offset']+s['width']*s['height']*2]
        assert len(actual)==s['bytes'],i
        if 'rgb565_sha256' in s:assert sha(actual)==s['rgb565_sha256'],i
        else:assert i<859 and actual==(BASE/'assets/atlas.bin').read_bytes()[s['offset']:s['offset']+s['bytes']],i
    return {'status':'PASS','hardware_access':False,'old_prefix_bytes':614916,'old_sprite_ids_unchanged':859,
        'new_sprite_count':13,'atlas_bytes':len(blob),'remaining_bytes':BUDGET-len(blob),'sha256':sha(blob)}
def build():
    original=(BASE/'assets/atlas.bin').read_bytes();assert sha(original)==BASE_SHA
    manifest=json.loads((BASE/'assets/manifest.json').read_text(encoding='utf-8'))
    assert len(manifest['sprites'])==859
    manifest=copy.deepcopy(manifest);atlas=bytearray(original);new=[];preview=[]
    def append(name,im,source,cutoff=32,premultiply=True):
        while len(atlas)%4:atlas.append(0)
        data,collisions=encode(im,cutoff,premultiply);i=859+len(new)
        item={'id':i,'name':name,'offset':len(atlas),'width':im.width,'height':im.height,'stride':im.width*2,
            'anchor_x':im.width//2,'anchor_y':im.height//2,'bytes':len(data),'source':source,
            'rgb565_sha256':sha(data),'opaque_key_collisions_adjusted':collisions,
            'opaque_pixels':sum(v!=KEY for v, in struct.iter_unpack('<H',data))}
        atlas.extend(data);new.append(item);preview.append((name,decode(data,im.width,im.height)));return item
    source_dir=V/'assets/source/triple_boss_1102';source_dir.mkdir(parents=True,exist_ok=True)
    for index,name in enumerate(('boss_01_cruiser.png','boss_02_hive.png','boss_03_overlord.png')):
        path=ROOT/'04_Assets_Set_A'/name;local=source_dir/name;shutil.copyfile(path,local)
        im=Image.open(path).convert('RGBA');bounds=im.getchannel('A').getbbox();assert bounds
        crop=im.crop(bounds);factor=min(80/crop.width,80/crop.height)
        size=(max(1,round(crop.width*factor)),max(1,round(crop.height*factor)))
        scaled=crop.convert('RGBa').resize(size,Image.Resampling.LANCZOS).convert('RGBA')
        canvas=Image.new('RGBA',(80,80));pos=((80-size[0])//2,(80-size[1])//2);canvas.paste(scaled,pos)
        append('TRIPLE_BOSS'+str(index),canvas,{'kind':'user_provided_png_offline_cache','source_file':local.relative_to(V).as_posix(),
            'source_sha256':sha(path.read_bytes()),'source_crop':list(bounds),'fit':list(size),'canvas':[80,80],'paste':list(pos),
            'source_png_unmodified':True,'aspect_preserved':True,'alpha_cutoff':32,'premultiplied_against_black':True,
            'license':'User supplied generated PNG; no independent CC0 license inferred'})
    for phase in range(2):
        old=manifest['sprites'][854+phase];w,h=old['width'],old['height']
        parent=struct.unpack('<'+'H'*(w*h),original[old['offset']:old['offset']+w*h*2])
        nw,nh=30,42;words=[parent[(y*h//nh)*w+x*w//nw] for y in range(nh) for x in range(nw)]
        im=decode(struct.pack('<'+'H'*len(words),*words),nw,nh)
        item=append('LARGE_BOMB_'+str(phase),im,{'kind':'nearest_scaled_existing_team_C_pixel_grid','parent_id':854+phase,
            'parent_sha256':old['rgb565_sha256'],'parent_size':[w,h],'size':[nw,nh],
            'source_pixel':'floor(dst*old_size/new_size)','license':'GPL-3.0-or-later Team C source'},premultiply=False)
        # Preserve the bomb's flight pivot rather than changing its trajectory.
        item['anchor_x']=(old['anchor_x']*nw+w//2)//w;item['anchor_y']=(old['anchor_y']*nh+h//2)//h
        assert struct.unpack('<'+'H'*len(words),bytes(atlas[item['offset']:item['offset']+item['bytes']]))==tuple(words)
    sheet_path=V/'assets/source/ordnance_cc0/Hit-Yellow.png';sheet=Image.open(sheet_path).convert('RGBA')
    for frame in range(8):
        old=manifest['sprites'][846+frame];crop=old['source']['source_crop']
        im=sheet.crop(crop).convert('RGBa').resize((78,78),Image.Resampling.LANCZOS).convert('RGBA')
        append('LARGE_BLAST_'+str(frame),im,{'kind':'selected_authored_animation_frame_larger_cache',
            'source_file':sheet_path.relative_to(V).as_posix(),'source_sha256':sha(sheet_path.read_bytes()),
            'author':'Sinestesia','license':'CC0-1.0','source_frame':old['source']['source_frame'],
            'source_crop':crop,'resize':['LANCZOS',78,78],'fixed_anchor':[39,39],
            'alpha_policy':'alpha<8 -> key, retained source RGB; global alpha supplies fade'},8,False)
    assert len(new)==13 and len(atlas)==755700
    manifest.update(variant='chp10_1102',bytes=len(atlas),sha256=sha(atlas),crc32='0x%08X'%zlib.crc32(atlas),
        generator='tools/build_boss_stress_atlas.py',remaining_budget_bytes=BUDGET-len(atlas),
        reference_variant='cjui_1031',reference_atlas_sha256=BASE_SHA,runtime_descriptor_total_bytes=872*8,
        original_ids_unchanged=list(range(859)),new_id_range=[859,871],
        triple_boss_id_range=[859,861],large_bomb_id_range=[862,863],large_blast_id_range=[864,871])
    manifest['sprites'].extend(new)
    header=(BASE/'src/garden_assets.h').read_text(encoding='utf-8-sig')
    header=re.sub(r'#define GARDEN_ATLAS_BYTES \d+u',f'#define GARDEN_ATLAS_BYTES {len(atlas)}u',header)
    header=re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u',f'#define GARDEN_ATLAS_CRC32 0x{zlib.crc32(atlas):08X}u',header)
    header=header.replace('    GARDEN_ASSET_COUNT = 859','''    GARDEN_ASSET_TRIPLE_BOSS0 = 859,
    GARDEN_ASSET_TRIPLE_BOSS1 = 860,
    GARDEN_ASSET_TRIPLE_BOSS2 = 861,
    GARDEN_ASSET_LARGE_BOMB_BASE = 862,
    GARDEN_ASSET_LARGE_BOMB_COUNT = 2,
    GARDEN_ASSET_LARGE_BLAST_BASE = 864,
    GARDEN_ASSET_LARGE_BLAST_COUNT = 8,
    GARDEN_ASSET_COUNT = 872''')
    code=(BASE/'src/garden_assets.c').read_text(encoding='utf-8-sig').replace(BASE_SHA,sha(atlas))
    end=code.index('\n};',code.index('static const garden_sprite_t sprites'))
    additions=''.join('\n    {%du, %du, %du, %d, %d}, /* %d: %s */'%(s['offset'],s['width'],s['height'],s['anchor_x'],s['anchor_y'],s['id'],s['name']) for s in new)
    code=code[:end]+additions+code[end:]
    code=code.replace('Generated by tools/build_bullet_scale_atlas.py;','Generated by tools/build_boss_stress_atlas.py;')
    (V/'src/garden_assets.h').write_text(header,encoding='utf-8');(V/'src/garden_assets.c').write_text(code,encoding='utf-8')
    (V/'assets/atlas.bin').write_bytes(atlas);(V/'assets/manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # The loader binds this unchanged scatter cache to the whole loaded atlas.
    # Existing source slices stay byte-exact; only the enclosing-atlas identity changes.
    scatter_meta_path=V/'assets/pure_scatter16.json'
    scatter_meta=scatter_meta_path.read_text(encoding='utf-8')
    scatter_meta=re.sub(r'("source_atlas_sha256": ")[0-9a-f]+(")',lambda m:m[1]+sha(atlas)+m[2],scatter_meta)
    scatter_meta_path.write_text(scatter_meta,encoding='utf-8')
    notice=(BASE/'assets/NOTICE.txt').read_text(encoding='utf-8-sig')+'\nC Boss/skills cache additions (2026-11-02)\n859..861: user supplied red cruiser, violet hive and green overlord PNGs, offline fitted into80x80; no independent CC0 license inferred.\n862..863: enlarged Team C bomb pixel grid, GPL-3.0-or-later.\n864..871: larger78x78 cache of the existing Sinestesia Hit-Yellow CC0-1.0 sheet, same authored frame selections.\nAll previous859sprite IDs, metadata and source bytes remain exact. Collision geometry unchanged.\n'
    (V/'assets/NOTICE.txt').write_text(notice,encoding='utf-8')
    check=validate(bytes(atlas),manifest)
    (V/'assets/cboss_asset_validation.json').write_text(json.dumps(check,indent=2)+'\n')
    canvas=Image.new('RGB',(660,360),(9,13,25));draw=ImageDraw.Draw(canvas)
    for i,(name,im) in enumerate(preview):
        x=12+(i%7)*92;y=18+(i//7)*164;canvas.paste(im,(x,y),im);draw.text((x,y+88),name.replace('LARGE_',''),fill=(210,225,255))
    canvas.save(V/'assets/cboss_asset_preview.png')
    print(json.dumps({**check,'sprites':[{k:s[k] for k in ('id','name','width','height','anchor_x','anchor_y')} for s in new]},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--verify-only',action='store_true');a=p.parse_args()
    if a.verify_only:print(json.dumps(validate((V/'assets/atlas.bin').read_bytes(),json.loads((V/'assets/manifest.json').read_text(encoding='utf-8'))),indent=2))
    else:build()
