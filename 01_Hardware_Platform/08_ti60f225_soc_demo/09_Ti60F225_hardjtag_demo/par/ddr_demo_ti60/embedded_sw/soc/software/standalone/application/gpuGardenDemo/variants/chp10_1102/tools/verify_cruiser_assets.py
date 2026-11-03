#!/usr/bin/env python3
"""Independent source/alpha, compiled descriptor, anchor and packing oracles.
SPDX-License-Identifier: GPL-3.0-or-later
Host only; no game launch, COM or JTAG access.
"""
import argparse,csv,hashlib,importlib.util,json,math,re,struct,subprocess,sys,zlib
from fractions import Fraction
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
KEY=0xf81f
def sha(data):return hashlib.sha256(data).hexdigest()
def readjson(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def vals(blob,s):return struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
def family(s):
    p=s['name'].split('_')
    return (p[1],int(p[2][1:]),int(p[3][1:])) if p[0]=='PRECISION' else (p[0],int(p[1][1:]),int(p[2][1:])*8)
def bullet(i):return 12<=i<92 or 280<=i<840
def image_data(im):return list(im.get_flattened_data()) if hasattr(im,'get_flattened_data') else list(im.getdata())
def patch(blob,s):
    if s['offset']<0x3c000:return None
    w,h=min(8,s['width']),min(8,s['height']);p=vals(blob,s)
    for y in range(s['height']-h+1):
        for x in range(s['width']-w+1):
            colors=[p[(y+yy)*s['width']+x+xx] for yy in range(h) for xx in range(w)]
            if KEY in colors and any(v!=KEY for v in colors):return s['offset']+(y*s['width']+x)*2
    return None
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
    p.add_argument('--reference',type=Path,default=ROOT.parent/'neon_hazard_20261016');a=p.parse_args()
    build=ROOT/'build/host/cruiser_assets';build.mkdir(parents=True,exist_ok=True)
    blob=(ROOT/'assets/atlas.bin').read_bytes();old=(a.reference/'assets/atlas.bin').read_bytes()
    m=readjson(ROOT/'assets/manifest.json');before=readjson(a.reference/'assets/manifest.json')
    gen=readjson(ROOT/'assets/cruiser_generation.json');origin=readjson(ROOT/'assets/user_art_import.json');geo=readjson(ROOT/'assets/cruiser_geometry.json')
    assert len(old)==719628 and sha(old)=='2ab126d0d86749947aa34b69370e6325b1c561a1e68f9aad5984dd561275dd42'
    assert len(blob)==472732==m['bytes']==gen['atlas_bytes']<=0xba000
    assert sha(blob)==m['sha256']==gen['atlas_sha256'] and f'0x{zlib.crc32(blob):08X}'==m['crc32']=='0xC760FA2E'
    exe=ROOT/'build/ca.exe';dump=build/'compiled_descriptors.csv'
    command=[a.compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/'tools/test_cruiser_assets.c'),str(ROOT/'src/garden_assets.c'),'-o',str(exe)]
    subprocess.run(command,check=True)
    run=subprocess.run([str(exe),str(dump),str(ROOT/'assets/atlas.bin')],capture_output=True,text=True)
    (build/'test_api.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout+run.stderr,end='');assert run.returncode==0
    compiled=[tuple(map(int,row)) for row in csv.reader(dump.read_text().splitlines())]
    assert len(compiled)==859
    fields=('id','offset','width','height','stride','anchor_x','anchor_y')
    for row,s in zip(compiled,m['sprites']):assert row==tuple(s[k] for k in fields)
    # Original user images remain exactly the files assessed before import.
    original_dir=next(q for q in ROOT.parents if(q/'04_Assets_Set_A').exists())/'04_Assets_Set_A'
    for source in origin['sources']:
        raw=(ROOT/source['copied_path']).read_bytes()
        assert sha(raw)==source['original_sha256']==sha((original_dir/source['name']).read_bytes())
        assert Image.open(ROOT/source['copied_path']).size==tuple(source['source_size'])
    for name in ('README.txt','PROMPTS.txt'):
        assert (ROOT/'assets/source/user_20261016'/name).read_bytes()==(original_dir/name).read_bytes()
    changed=set(gen['changed_ids']);assert len(changed)==334
    unchanged=0;dart=0;rounds=0;white_errors=[];orb_groups={};known_ranges={}
    for i,s in enumerate(m['sprites']):
        payload=struct.pack('<'+'H'*(s['width']*s['height']),*vals(blob,s))
        start,end=s['offset'],s['offset']+len(payload)
        for (lo,hi),other in known_ranges.items():
            if start<hi and lo<end:assert (start,end)==(lo,hi) and payload==other,'Only exact payload aliases may overlap'
        known_ranges[(start,end)]=payload
        if i not in changed:
            prev=before['sprites'][i]
            assert vals(blob,s)==vals(old,prev)
            for key in ('width','height','stride','anchor_x','anchor_y'):assert s[key]==prev[key]
            unchanged+=1
        if not bullet(i):continue
        kind,color,direction=family(s)
        pts=[(j%s['width'],j//s['width']) for j,v in enumerate(vals(blob,s)) if v==0xffff]
        assert pts
        cx=Fraction(sum(x for x,y in pts),len(pts));cy=Fraction(sum(y for x,y in pts),len(pts))
        nearest=min(((x,y) for x in range(math.floor(cx),math.ceil(cx)+1) for y in range(math.floor(cy),math.ceil(cy)+1)),key=lambda q:((q[0]-cx)**2+(q[1]-cy)**2,-q[0],-q[1]))
        assert nearest==(s['anchor_x'],s['anchor_y']) and nearest in pts
        error=math.hypot(float(cx-nearest[0]),float(cy-nearest[1]));assert error<=math.sqrt(.5)+1e-10;white_errors.append(error)
        if kind=='DART':assert i not in changed;dart+=1
        else:
            assert s['width']==16 and s['height']==16 and nearest==(7,6);rounds+=1
            orb_groups.setdefault(color,[]).append((s['offset'],tuple(vals(blob,s)),tuple(pts)))
    assert unchanged==525 and dart==320 and rounds==320 and len(white_errors)==640
    for color,group in orb_groups.items():
        assert len(group)==64 and len({r[0] for r in group})==2 and all(r[1:]==group[0][1:] for r in group)
    assert len({group[0][1] for group in orb_groups.values()})==5,'New round art still visibly multicolor'
    assert len({group[0][2] for group in orb_groups.values()})==1,'Tint must not shift core geometry'
    # Every new runtime imported PNG is a source conversion oracle with alpha
    # retained before RGB565. No alpha>=32 pixel may become the global key.
    opaque_pixels=0;transparent_pixels=0;actual_key_collisions=0
    for id_text,record in origin['generated_sprite_provenance'].items():
        s=m['sprites'][int(id_text)];im=Image.open(ROOT/'assets/imported/user_20261016'/f'{s["name"]}.png').convert('RGBA')
        real=vals(blob,s);core=set(j for j,v in enumerate(real) if v==0xffff) if bullet(s['id']) or s['id'] in(9,10,11) else set()
        assert im.size==(s['width'],s['height'])
        synthetic_collisions=0
        for j,(r,g,b,alpha) in enumerate(image_data(im)):
            if alpha<32:assert real[j]==KEY;transparent_pixels+=1;continue
            opaque_pixels+=1;assert real[j]!=KEY
            if j in core:
                assert min(r,g,b)>=180 and alpha>=160 and real[j]==0xffff
            else:
                rr,gg,bb=r*alpha//255,g*alpha//255,b*alpha//255
                expected=(rr//8)*2048+(gg//4)*32+(bb//8)
                if expected==KEY:expected=KEY^1;synthetic_collisions+=1
                assert real[j]==expected
        assert synthetic_collisions==record['opaque_key_collisions_adjusted'];actual_key_collisions+=synthetic_collisions
    # Exercise exact key-color boundary even if the five authored PNGs never
    # happen to quantize to it; this is an importer contract, not board proof.
    spec=importlib.util.spec_from_file_location('cruiser_importer',ROOT/'tools/build_cruiser_atlas.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    sample=Image.new('RGBA',(3,1));sample.putdata([(255,0,255,255),(255,0,255,0),(255,255,255,255)])
    packed,clashes=module.encode(sample);assert struct.unpack('<3H',packed)==(KEY^1,KEY,0xffff) and clashes==1
    # Quantized closed pose is the full imported cruiser, with no double body
    # at gun locations. Local-to-world muzzle equation uses descriptor anchors.
    full=(ROOT/'assets/imported/user_20261016/BOSS_FULL_COMPOSITE.rgb565').read_bytes()
    result=list(vals(blob,m['sprites'][3]));assert len(full)==128*132*2
    for i,rect,muzzle in zip((274,275),geo['pod_rects'],geo['target_muzzles']):
        s=m['sprites'][i];v=vals(blob,s)
        assert [s['width'],s['height']]==[44,72]
        assert [rect[0]+s['anchor_x'],rect[1]+s['anchor_y']]==muzzle
        assert v[s['anchor_y']*s['width']+s['anchor_x']]!=KEY
        for y in range(s['height']):
            for x in range(s['width']):
                pos=(rect[1]+y)*128+rect[0]+x;assert result[pos]==KEY;result[pos]=v[y*s['width']+x]
    assert struct.pack('<'+'H'*len(result),*result)==full and sha(full)==geo['neutral_composite_rgb565_sha256']
    assert geo['muzzle_offsets']==[[-42,19],[43,19]] and geo['pod_anchors']==[[22,69],[23,69]]
    # Independent playback of the production precision source selection. All
    #10families still supply two real mixed Key/non-Key high-window addresses.
    sources=[];family_sources={}
    for kind in ('DART','DIAMOND'):
        for color in range(1,6):
            chosen=[]
            for direction in range(1,64):
                if direction%8==0:continue
                s=next(s for s in m['sprites'] if bullet(s['id']) and family(s)==(kind,color,direction))
                address=patch(blob,s)
                if address is not None:chosen.append(address)
                if len(chosen)==2:break
            assert len(chosen)==2 and len(set(chosen))==2
            family_sources[kind+'_B'+str(color)]=chosen;sources.extend(chosen)
    assert len(sources)==20 and len(set(sources))==20
    # Every byte in original angular impacts/blasts/bomb/heal/beam and fonts
    # is accounted for by the unchanged-payload loop, independent of offsets.
    for i in list(range(92,264))+list(range(840,859)):assert i not in changed
    regenerate=subprocess.run([sys.executable,'-B',str(ROOT/'tools/build_cruiser_atlas.py'),'--reference',str(a.reference),'--check'],capture_output=True,text=True)
    (build/'regeneration_check.log').write_text(regenerate.stdout+regenerate.stderr,encoding='utf-8');assert regenerate.returncode==0,regenerate.stdout+regenerate.stderr
    match=re.search(r'checks=(\d+)',run.stdout);assert match
    report=dict(status='PASS',host_only=True,board_acceptance=False,variant='neon_cruiser_20261016',
        compiled_api_checks=int(match[1]),compiled_descriptors_match_manifest=859,descriptor_bytes=8,
        source_original_png_sha256_matches=5,original_README_and_PROMPTS_preserved=True,original_pngs_modified=False,
        unchanged_rgb565_and_geometry_records=525,dart64_all5palette_exact_payload_and_anchors=320,
        replaced_round_ids=320,round_palette_count=5,round_cached_blobs=10,identical_copy_alias_overlap_only=True,
        white_core_anchor_oracle_checks=640,white_core_max_quantization_error_px=max(white_errors),
        imported_alpha_opaque_pixels_checked=opaque_pixels,imported_alpha_key_pixels_checked=transparent_pixels,
        actual_opaque_key_conflicts_remapped=actual_key_collisions,synthetic_opaque_key_boundary_pass=True,
        split_boss_closed_composite_byte_exact=True,boss_muzzle_offsets=geo['muzzle_offsets'],
        actual_muzzle_pixels_opaque=True,boss_pod_anchors=geo['pod_anchors'],
        player_all3_tail_sources_new40x36=True,existing5bank_poses_use_same_new_sources=True,
        precision_independent_high_sources=20,precision_source_offsets=family_sources,
        old_fonts_hits_blast_bomb_heal_plasma_pixels_unchanged=True,
        atlas_bytes=len(blob),atlas_sha256=sha(blob),atlas_crc32=m['crc32'],free_bytes_before_scratch=0xba000-len(blob),
        deterministic_regeneration=True,compiler_command=command,
        source_sha256={r:sha((ROOT/r).read_bytes()) for r in ('src/garden_assets.c','src/garden_assets.h','tools/build_cruiser_atlas.py',
            'tools/test_cruiser_assets.c','tools/verify_cruiser_assets.py','assets/cruiser_geometry.json','assets/user_art_import.json')},
        limits=['Host asset proof only; scene/core/renderer integration and board timing/visual checks are separate.',
            'Round glow is decorative; existing bullet-center hitbox and six-tick activation rules are unchanged.',
            'Five imported PNGs are user provided generated assets; no CC0 claim is made for them.',
            'Offsets repacked and identical round source ranges alias; numeric IDs and descriptor ABI remain stable.'])
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in('source_sha256','compiler_command','limits','precision_source_offsets')},indent=2))
if __name__=='__main__':main()
