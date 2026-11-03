#!/usr/bin/env python3
"""Pack selected CC0 angular impact frames and tiny original equipment sprites.
SPDX-License-Identifier: GPL-3.0-or-later
Offline source conversion only; no downloads, board writes or runtime resampling.
The first 631954 bytes and all 840 previous sprite records stay exact.
"""
from __future__ import annotations
import argparse, copy, hashlib, io, json, re, struct, zlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
REFERENCE='neon_bomb_20261016'
BASE_BYTES=631954
BASE_SHA='1b1fa155cc7db9fbfd72a76ae74fd8fdeffb43b4b3321a42aa594aeeabfef076'
KEY=0xf81f
BUDGET=0xba000
HIT_FRAMES=(1,2,3,5,7,10)
BLAST_FRAMES=(1,2,3,4,6,8,11,14)
def sha(data): return hashlib.sha256(data).hexdigest()
def jb(value): return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
def px(im): return im.get_flattened_data() if hasattr(im,'get_flattened_data') else im.getdata()
def png(im):
    out=io.BytesIO();im.save(out,format='PNG');return out.getvalue()
def encode(im):
    result=[];changed=0
    for r,g,b,a in px(im):
        value=KEY if not a else (r>>3)<<11 | (g>>2)<<5 | (b>>3)
        if a and value==KEY: value^=1;changed+=1
        result.append(value)
    return struct.pack('<'+'H'*len(result),*result),changed
def impact_frame(source,index,size,cool=False):
    # Authored 4x4 atlas includes an empty leading cell. Preserve a common
    # center/pivot across frames; independently tight-cropping would cause jitter.
    x,y=(index%4)*1024,(index//4)*1024
    im=source.crop((x,y,x+1024,y+1024)).resize((size,size),Image.Resampling.LANCZOS)
    data=[]
    for red,green,blue,alpha in px(im):
        if alpha<8: data.append((0,0,0,0));continue
        # This author atlas has very low-alpha angular rays. Binarize at8
        # without black-premultiplication, retaining the authored cut silhouette;
        # the renderer animates the whole frame with global hardware Alpha.
        if cool:
            # Source yellow-orange scalar channels map to white/ice-blue cuts.
            red,green,blue=(blue*3+green)//4,(red*3+green)//4,red
        data.append((red,green,blue,255))
    im.putdata(data)
    return im,dict(kind='selected_authored_animation_frame',source_file='assets/source/ordnance_cc0/Hit-Yellow.png',
        author='Sinestesia',license='CC0-1.0',source_frame=index,source_grid=[4,4],
        source_crop=[x,y,x+1024,y+1024],resize=['LANCZOS',size,size],alpha_threshold=8,
        partial_alpha_policy='alpha<8 -> key; otherwise preserve source RGB and binary alpha255; global Alpha supplies fade',
        palette_mapping='R=(B*3+G)/4 G=(R*3+G)/4 B=R' if cool else 'original warm palette',
        fixed_anchor=[size//2,size//2],independent_frame_tight_crop=False)
def original_grid(path,phase):
    item=json.loads(path.read_text(encoding='utf-8'))
    rows=item['frames'][phase]
    assert len(rows)==item['height'] and all(len(row)==item['width'] for row in rows)
    palette={k:tuple(v) for k,v in item['palette'].items()}
    im=Image.new('RGBA',(item['width'],item['height']))
    im.putdata([palette[p] for row in rows for p in row])
    return im,dict(kind='team_C_original_code_native_pixel_grid',source_file=str(path.relative_to(ROOT)).replace('\\','/'),
        author='Team C',license='GPL-3.0-or-later',source_phase=phase,source_file_sha256=sha(path.read_bytes()),
        imported_art_redrawn=False)
def beam_frame(source,phase):
    # Rawdanitsu's red laser cross-section sampled along original row348.
    # Wide white core explicitly matches the game's inclusive +/-6px beam.
    im=Image.new('RGBA',(25,120));data=[]
    for y in range(120):
        modulation=224+((y+phase*12)%24)*31//23
        for x in range(25):
            distance=abs(x-12)
            if distance<=6:
                # 13px exactly; always bright enough to identify real danger.
                data.append((255,237+(modulation-224)//2,249,255))
            else:
                sx=233+((-1 if x<12 else 1)*(12+(distance-6)*20//6))
                r,g,b,a=source.getpixel((max(201,min(265,sx)),348))
                # Tint red author texture to magenta; flatten soft alpha to black.
                data.append((r*a*modulation//65025,24*a//255,130*a*modulation//65025,255))
    im.putdata(data)
    return im,dict(kind='cached_source_beam_profile_plus_collision_core',
        source_file='assets/source/ordnance_cc0/beams.png',author='Rawdanitsu',license='CC0-1.0',
        source_cross_section={'row':348,'center_x':233,'sample_x_range':[201,265]},
        transform='sample original red-laser alpha profile, magenta tint, black-premultiply; explicit 13px white collision core',
        phase=phase,y_period=24,height=120,bright_collision_core_inclusive_x=[6,18],
        renderer_hint='25x120; <=5 clipped repeats per gun for 600px field; no full-screen sprite allocation',
        anchor=[12,0],gameplay_implementation_in_asset=False)
def decode(blob,s):
    values=struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
    im=Image.new('RGBA',(s['width'],s['height']))
    im.putdata([(0,0,0,0) if c==KEY else (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31,255) for c in values])
    return im
def contact_sheet(blob,sprites):
    image=Image.new('RGB',(1120,850),(9,13,25));draw=ImageDraw.Draw(image);font=ImageFont.load_default()
    draw.text((18,14),'HOST RGB565 ATLAS PREVIEW / ORDNANCE - not an FPGA capture',fill=(235,239,255),font=font)
    draw.text((18,36),'Actual CC0 Sinestesia cuts -> particles. Common frame anchor. Black is transparent.',fill=(150,177,202),font=font)
    sections=[('IMPACT / 6 frames / 25ms each',840,6,80,110,2),
              ('THROWN BOMB BLAST / 8 frames / 50ms each',846,8,245,130,2),
              ('FLIGHT / 2 frames + MEDICAL PACKAGE',854,3,440,150,3),
              ('BEAM TILE / 2 phases / core13px /120px tall',857,2,620,150,1)]
    for label,base,count,top,cell,scale in sections:
        draw.text((18,top),label,fill=(192,206,228),font=font)
        for i in range(count):
            s=sprites[base+i];im=decode(blob,s)
            im=im.resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST)
            x=20+i*cell;y=top+27
            image.paste(im,(x,y),im)
            draw.text((x,y+im.height+5),str(s['id'])+' '+str(s['width'])+'x'+str(s['height']),fill=(160,181,205),font=font)
    return png(image)
def construct(reference):
    original=(reference/'assets/atlas.bin').read_bytes()
    before=json.loads((reference/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(original)==BASE_BYTES and sha(original)==BASE_SHA==before['sha256']
    assert len(before['sprites'])==840
    source_dir=ROOT/'assets/source/ordnance_cc0'
    sources=json.loads((source_dir/'sources.json').read_text(encoding='utf-8'))
    for s in sources['files']:
        assert sha((ROOT/s['path']).read_bytes())==s['sha256'],s['path']
    hit=Image.open(source_dir/'Hit-Yellow.png').convert('RGBA')
    beam=Image.open(source_dir/'beams.png').convert('RGBA')
    assert hit.size==(4096,4096) and beam.size==(490,446)
    atlas=bytearray(original);sprites=copy.deepcopy(before['sprites'])
    def add(name,im,anchor,provenance):
        index=len(sprites);assert 840<=index<859
        atlas.extend(b'\0'*((-len(atlas))%4));offset=len(atlas)
        data,collisions=encode(im);atlas.extend(data)
        w,h=im.size;ax,ay=anchor
        assert 0<w<=127 and 0<h<=127 and -128<=ax<=127 and -128<=ay<=127
        assert 0<=ax<w and 0<=ay<h
        sprites.append(dict(id=index,name=name,offset=offset,width=w,height=h,stride=w*2,anchor_x=ax,anchor_y=ay,bytes=len(data),
            source=provenance,opaque_pixels=sum(p[3]!=0 for p in px(im)),rgb565_sha256=sha(data),
            opaque_key_collisions_adjusted=collisions))
    for i,frame in enumerate(HIT_FRAMES):
        im,record=impact_frame(hit,frame,32,True);add('ORDNANCE_HIT_'+str(i),im,(16,16),record)
    for i,frame in enumerate(BLAST_FRAMES):
        im,record=impact_frame(hit,frame,56);add('ORDNANCE_BLAST_'+str(i),im,(28,28),record)
    for i in range(2):
        im,record=original_grid(source_dir/'bomb_pixel_grid.json',i);add('ORDNANCE_BOMB_'+str(i),im,(11,13),record)
    im,record=original_grid(source_dir/'heal_pixel_grid.json',0);add('ORDNANCE_HEAL',im,(11,11),record)
    for i in range(2):
        im,record=beam_frame(beam,i);add('ORDNANCE_BEAM_'+str(i),im,(12,0),record)
    final=bytes(atlas)
    assert len(sprites)==859 and len(final)<=BUDGET and final[:BASE_BYTES]==original
    assert sprites[:840]==before['sprites']
    for s in sprites:
        assert s['stride']==s['width']*2 and 0<s['width']<=127 and 0<s['height']<=127
        assert -128<=s['anchor_x']<=127 and -128<=s['anchor_y']<=127
        assert s['offset']+s['height']*s['stride']<=len(final)
    digest=sha(final);crc=zlib.crc32(final)
    h=(reference/'src/garden_assets.h').read_text(encoding='utf-8-sig')
    h=h.replace('Run tools/build_precision_atlas.py','Run tools/build_ordnance_atlas.py')
    h=re.sub(r'#define GARDEN_ATLAS_BYTES \d+u',f'#define GARDEN_ATLAS_BYTES {len(final)}u',h)
    h=re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u',f'#define GARDEN_ATLAS_CRC32 0x{crc:08X}u',h)
    old_struct=re.search(r'typedef struct \{.*?\} garden_sprite_t;',h,re.S);assert old_struct
    new_struct='''typedef struct {
    uint32_t offset;                 /* byte offset from GARDEN_ATLAS_ADDRESS */
    uint8_t width, height;           /* tight RGB565 source rows */
    int8_t anchor_x, anchor_y;       /* signed logical pivot */
} garden_sprite_t;
_Static_assert(sizeof(garden_sprite_t)==8u,"Sprite descriptor must be 8 bytes");
static inline unsigned garden_sprite_stride(const garden_sprite_t *sprite)
{
    return (unsigned)sprite->width*2u;
}'''
    h=h[:old_struct.start()]+new_struct+h[old_struct.end():]
    h=h.replace('    GARDEN_ASSET_COUNT = 840','''    GARDEN_ASSET_ORDNANCE_HIT_BASE = 840,
    GARDEN_ASSET_ORDNANCE_HIT_COUNT = 6,
    GARDEN_ASSET_ORDNANCE_BLAST_BASE = 846,
    GARDEN_ASSET_ORDNANCE_BLAST_COUNT = 8,
    GARDEN_ASSET_ORDNANCE_BOMB_BASE = 854,
    GARDEN_ASSET_ORDNANCE_BOMB_COUNT = 2,
    GARDEN_ASSET_ORDNANCE_HEAL = 856,
    GARDEN_ASSET_ORDNANCE_BEAM_BASE = 857,
    GARDEN_ASSET_ORDNANCE_BEAM_COUNT = 2,
    GARDEN_ASSET_COUNT = 859''')
    c=(reference/'src/garden_assets.c').read_text(encoding='utf-8-sig')
    c=c.replace('Generated by tools/build_precision_atlas.py','Generated by tools/build_ordnance_atlas.py')
    c=re.sub(r'(garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',lambda m:m[1]+digest+m[2],c)
    rows=[]
    for s in sprites:
        rows.append('    {%du, %du, %du, %d, %d}, /* %d: %s */'%(s['offset'],s['width'],s['height'],s['anchor_x'],s['anchor_y'],s['id'],s['name']))
    c,count=re.subn(r'(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n).*?(\n\};)',
        lambda m:m[1]+'\n'.join(rows)+m[2],c,count=1,flags=re.S);assert count==1
    assert '(id>=GARDEN_ASSET_PRECISION_BASE && id<GARDEN_ASSET_COUNT)' in c
    c=c.replace('(id>=GARDEN_ASSET_PRECISION_BASE && id<GARDEN_ASSET_COUNT)',
                '(id>=GARDEN_ASSET_PRECISION_BASE && id<GARDEN_ASSET_ORDNANCE_HIT_BASE)')
    manifest=copy.deepcopy(before);manifest.update(variant='neon_ordnance_20261016',bytes=len(final),sha256=digest,crc32=f'0x{crc:08X}',
        generator='tools/build_ordnance_atlas.py',sprites=sprites,reference_variant=REFERENCE,reference_atlas_sha256=BASE_SHA,
        unchanged_prefix_bytes=BASE_BYTES,old_sprite_records_unchanged=840,appended_bytes=len(final)-BASE_BYTES,
        budget_bytes=BUDGET,remaining_budget_bytes=BUDGET-len(final),runtime_descriptor_bytes=8,
        runtime_descriptor_fields=['uint32 offset','uint8 width','uint8 height','int8 anchor_x','int8 anchor_y'],
        descriptor_stride_rule='width*2; no stored stride',runtime_descriptor_total_bytes=859*8,
        precision_id_range=[280,839],ordnance_id_range=[840,858],original_ids_unchanged=840)
    generation=dict(status='HOST_ASSET_VALIDATION_PASS_NOT_BOARD_ACCEPTANCE',atlas_bytes=len(final),atlas_sha256=digest,atlas_crc32=f'0x{crc:08X}',
        budget_bytes=BUDGET,remaining_budget_bytes=BUDGET-len(final),old840pixels_and_metadata_exact=True,
        fixed_anchor_across_animation_frames=True,new_frames=19,descriptor_bytes=8,descriptor_total_bytes=859*8,
        descriptor_bytes_saved_against_old_840x16=840*16-859*8,
        hit={'base':840,'count':6,'size':[32,32],'frame_logic_ticks':1,'duration_ms':150},
        blast={'base':846,'count':8,'size':[56,56],'frame_logic_ticks':2,'duration_ms':400},
        bomb={'base':854,'count':2,'size':[22,30]},heal={'id':856,'size':[22,22]},
        beam={'base':857,'count':2,'size':[25,120],'anchor':[12,0],'core_width':13,'y_period':24,
            'tile_bound_per_600px_side':5},asset_sources=sources)
    notice=(reference/'assets/NOTICE.txt').read_text(encoding='utf-8-sig')+'''
Ordnance animation assets (2026-10-16)
IDs0..839 and first631954 bytes remain byte-exact. New IDs840..858:
Sinestesia, Hit Animation2, CC0-1.0: selected real frame-by-frame angular
cuts/fragment bursts. Source https://opengameart.org/content/hit-animation-2-frame-by-frame
Warm source palette retained for blasts; cold hit palette is a documented
channel mapping. Fixed common pivots; resized once offline; no runtime scaling.
Rawdanitsu, Lasers and beams, CC0-1.0:
https://opengameart.org/content/lasers-and-beams
Original red-laser alpha cross-section supplies feathering; a cached13px
bright core matches gameplay hazard width. Vertical modulation is code-native.
Team C bomb/medical-package pixel-grid sources: GPL-3.0-or-later, authored
as small text palettes. No downloaded art was repainted or claimed as ours.
Original unmodified sources, SHA256 and license evidence are under
assets/source/ordnance_cc0/sources.json and LICENSE_EVIDENCE.txt.
assets/ordnance_contact_sheet.png is a labelled host RGB565 preview, not
a hardware capture. The C descriptors are packed to8bytes; implicit stride
is always width*2 and all source offsets/anchors remain unchanged.
'''
    outputs={'assets/atlas.bin':final,'assets/atlas.sha256':(digest+'  atlas.bin\n').encode(),
        'assets/manifest.json':jb(manifest),'assets/ordnance_generation.json':jb(generation),
        'assets/ordnance_contact_sheet.png':contact_sheet(final,sprites),
        'assets/NOTICE.txt':notice.encode('utf-8'),'src/garden_assets.c':c.encode('utf-8'),'src/garden_assets.h':h.encode('utf-8')}
    # GIFs are host previews of the exact selected RGB565 frames.
    for name,base,count,duration in [('hit',840,6,25),('blast',846,8,50)]:
        frames=[]
        for i in range(count):
            rgba=decode(final,sprites[base+i]);bg=Image.new('RGB',rgba.size,(9,13,25));bg.paste(rgba,mask=rgba.getchannel('A'))
            frames.append(bg.resize((rgba.width*4,rgba.height*4),Image.Resampling.NEAREST))
        frames.append(Image.new('RGB',frames[0].size,(9,13,25)))
        out=io.BytesIO();frames[0].save(out,format='GIF',save_all=True,append_images=frames[1:],
            duration=[duration]*count+[500],loop=0,disposal=2)
        outputs['assets/ordnance_'+name+'_preview.gif']=out.getvalue()
    checksums=copy.deepcopy(json.loads((reference/'assets/runtime_checksums.json').read_text(encoding='utf-8-sig'))['files'])
    checksums.update({name:{'bytes':len(data),'sha256':sha(data)} for name,data in outputs.items()})
    for rel in ['tools/build_ordnance_atlas.py','tools/verify_ordnance_assets.py','tools/test_ordnance_assets.c',
                'assets/source/ordnance_cc0/sources.json','assets/source/ordnance_cc0/LICENSE_EVIDENCE.txt']:
        p=ROOT/rel
        if p.exists(): checksums[rel]={'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())}
    outputs['assets/runtime_checksums.json']=jb({'format':'sha256','atlas_crc32':f'0x{crc:08X}','files':checksums})
    return outputs,generation
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,default=ROOT.parent/REFERENCE)
    parser.add_argument('--check',action='store_true');args=parser.parse_args()
    outputs,report=construct(args.reference.resolve())
    for rel,data in outputs.items():
        target=ROOT/rel
        if args.check: assert target.read_bytes()==data,'Generated file differs: '+rel
        else: target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    print(json.dumps({k:v for k,v in report.items() if k!='asset_sources'},indent=2))
if __name__=='__main__':main()

