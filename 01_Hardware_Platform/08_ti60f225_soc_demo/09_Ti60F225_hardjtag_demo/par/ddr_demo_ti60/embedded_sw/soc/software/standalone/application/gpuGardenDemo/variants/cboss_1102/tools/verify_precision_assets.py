#!/usr/bin/env python3
"""Independently replay authored source recipes and verify production64asset API.
SPDX-License-Identifier: GPL-3.0-or-later
No FPGA/board access or visual/FPS acceptance is claimed.
"""
import argparse,hashlib,json,math,re,struct,subprocess,zlib
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def px(im):return im.get_flattened_data() if hasattr(im,'get_flattened_data') else im.getdata()
def replay(reference,s,master,recipes):
    r=s['source']
    if r['kind']=='frozen_existing_direction_png':
        path=reference/r['png'];assert sha(path.read_bytes())==r['png_sha256'];im=Image.open(path).convert('RGBA')
    else:
        im=master.crop(r['source_crop']).convert('RGBA')
        palette={tuple(p['source_rgb']):tuple(p['target_rgb']) for p in recipes[r['palette_recipe_name']]['palette_substitution']}
        im.putdata([(0,0,0,0) if p[:3]==(0,0,0) else (*palette.get(p[:3],p[:3]),255) for p in px(im)])
        im=im.resize(tuple(r['normalized_size']),Image.Resampling.NEAREST)
        native=r['native16_index'];anchor=r['reference_anchor']
        if s['name'].startswith('PRECISION_DIAMOND'):assert anchor==[im.width//2,im.height//2]
        else:
            u=(math.cos(math.radians(native*22.5)),math.sin(math.radians(native*22.5)))
            ink=[(x,y) for y in range(im.height) for x in range(im.width) if im.getpixel((x,y))[3]]
            projection=round(anchor[0]*u[0]+anchor[1]*u[1],10)
            assert projection==min(round(x*u[0]+y*u[1],10) for x,y in ink),'tail reference must be the actual rear source boundary'
    assert list(im.size)==r['normalized_size'] and set(im.getchannel('A').tobytes())<={0,255}
    canvas=Image.new('RGBA',(128,128),(0,0,0,0));canvas.paste(im,tuple(r['paste_xy']))
    # Independently check the source anchor's geometric transformation and
    # pin its pixel-center to64.5, not an arbitrary upper-left crop corner.
    assert [r['paste_xy'][axis]+r['reference_anchor'][axis] for axis in (0,1)]==[64,64]
    assert r['rotation_pivot_edge_coordinates']==[64.5,64.5]
    canvas=canvas.rotate(-r['rotation_clockwise_degrees'],Image.Resampling.NEAREST,center=(64.5,64.5),expand=False)
    bounds=canvas.getchannel('A').getbbox();assert list(bounds)==r['tight_crop']
    cropped=canvas.crop(bounds);assert sha(cropped.tobytes())==r['rgba_sha256']
    assert [64-bounds[0],64-bounds[1]]==[s['anchor_x'],s['anchor_y']]
    assert cropped.getpixel((s['anchor_x'],s['anchor_y']))[3]==255
    assert not canvas.getpixel((0,64))[3] and not canvas.getpixel((127,64))[3]
    values=[]
    for red,green,blue,alpha in px(cropped):
        value=0xf81f if not alpha else ((red//8)*2048+(green//4)*32+blue//8)
        if alpha and value==0xf81f:value^=1
        values.append(value)
    return struct.pack('<'+'H'*len(values),*values),cropped.getchannel('A').tobytes()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--reference',type=Path,default=ROOT.parent/'neon_animation_20261015');parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');args=parser.parse_args()
    reference=args.reference.resolve();build=ROOT/'build/host/precision_assets';build.mkdir(parents=True,exist_ok=True)
    old=(reference/'assets/atlas.bin').read_bytes();atlas=(ROOT/'assets/atlas.bin').read_bytes()
    original=json.loads((reference/'assets/manifest.json').read_text(encoding='utf-8-sig'));manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(old)==236954 and sha(old)=='82b626660cda45307a6b8f88b808eb4bb66ca240aff32de2a171e80bc9e22762'
    assert atlas[:len(old)]==old and manifest['sprites'][:280]==original['sprites'] and len(manifest['sprites'])==840
    assert len(atlas)==manifest['bytes'] and sha(atlas)==manifest['sha256'] and f'0x{zlib.crc32(atlas):08X}'==manifest['crc32']
    assert len(atlas)<=0xbc000-8192 and 0x5c4000+len(atlas)<=0x67e000
    master_path=reference/'assets/source/master484/M484BulletCollection2.png';assert sha(master_path.read_bytes())=='cae852d8f4511cd1172a2e302faa91110372ad574a14b7f32907df88d5eefee8'
    master=Image.open(master_path).convert('RGBA');recipes={r['name']:r for r in json.loads((reference/'assets/imported/master484/manifest.json').read_text(encoding='utf-8-sig'))['sprites']}
    checks=0;masks={};palette_pixels={};anchor_positions={'DART':set(),'DIAMOND':set()};last_end=0
    for s in manifest['sprites']:
        assert s['offset']%4==0 and s['offset']>=last_end and s['stride']==s['width']*2
        last_end=s['offset']+s['bytes'];assert last_end<=len(atlas)
        if s['id']<280:continue
        data,mask=replay(reference,s,master,recipes);assert atlas[s['offset']:last_end]==data and sha(data)==s['source']['rgb565_sha256'];checks+=1
        kind='DART' if 'DART' in s['name'] else 'DIAMOND';r=s['source'];direction=r['nominal_angle_units']//16;family=r['palette_family'];key=(kind,direction)
        if key in masks:assert masks[key]==mask
        else:masks[key]=mask
        palette_pixels[(kind,direction,family)]=sha(data)
        assert r['nominal_angle_degrees']==direction*5.625
        assert abs(((r['native16_index']*22.5+r['rotation_clockwise_degrees']-direction*5.625+180)%360)-180)<1e-9
        assert r['collision_or_core_coordinate_change'] is False
        assert s['opaque_pixels']==sum(alpha==255 for alpha in mask) and set(mask)<={0,255}
        assert any(mask[:s['width']]) and any(mask[-s['width']:]) and any(mask[::s['width']]) and any(mask[s['width']-1::s['width']]),'each bbox edge actually contains source ink'
        anchor_positions[kind].add((s['anchor_x'],s['anchor_y']))
    for kind,direction in masks:assert len({palette_pixels[(kind,direction,family)] for family in range(1,6)})==5;checks+=1
    assert len(masks)==112 and len(anchor_positions['DART'])>20 and len(anchor_positions['DIAMOND'])>10,'anchors must transform with frames, not be fixed at upper-left'
    c=(ROOT/'src/garden_assets.c').read_text(encoding='utf-8-sig');rows=[tuple(map(int,m.groups())) for m in re.finditer(r'\{(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(-?\d+),\s*(-?\d+)\}',c)]
    assert len(rows)==840
    for row,s in zip(rows,manifest['sprites']):assert row==tuple(s[k] for k in ('offset','width','height','stride','anchor_x','anchor_y'));checks+=1
    #Keep CreateProcess executable path below Windows260character limit.
    exe=ROOT/'build/hp64.exe';cmd=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/'tools/test_precision_assets.c'),str(ROOT/'src/garden_assets.c'),'-o',str(exe)]
    subprocess.run(cmd,check=True);run=subprocess.run([str(exe)],capture_output=True,text=True);output=run.stdout+run.stderr;(build/'test_precision_assets.log').write_text(output,encoding='utf-8');print(output,end='')
    if run.returncode:raise SystemExit(run.returncode)
    match=re.search(r'checks=(\d+) descriptor_bytes=(\d+)',output);assert match
    sources=('src/garden_assets.c','src/garden_assets.h','tools/build_precision_atlas.py','tools/verify_precision_assets.py','tools/test_precision_assets.c')
    report={'status':'PASS','host_only':True,'board_acceptance':False,'asset_recipe_checks':checks,'production_api_checks':int(match[1]),
        'original_prefix_bytes':len(old),'original_prefix_sha256':sha(old),'original_records_identical':280,'new_frames':560,'atlas_bytes':len(atlas),'atlas_sha256':sha(atlas),'atlas_crc32':manifest['crc32'],
        'extra_descriptor_bytes':560*int(match[2]),'lookup_bytes':0,'span':[hex(0x5c4000),hex(0x680000)],'tail8KiB_start':hex(0x67e000),'free_bytes_before_scratch':0xbc000-8192-len(atlas),
        'checks':['all560source crops/palette/size/binary alpha/rotation replay to exact generated RGB565; real tail/center anchor pixel fixed under offline rotation; tight bbox edges contain ink',
            '640type/colour/direction combinations unique; all1024coreangles independently nearest-center search; legacy8direction API exact; original280descriptors/prefix/fonts/body unchanged',
            'five original visible colour families share silhouettes but remain distinct; source and imported PNG hashes unchanged; no world coordinates or collision changes',
            'DDR extent/scratch mathematical bounds only; no board performance or image acceptance claim'],
        'source_sha256':{p:sha((ROOT/p).read_bytes()) for p in sources},'compiler_command':cmd,
        'limits':['Theoretical nearest64center error is<=2.8125degrees; pixel raster shape itself is not a continuous-angle vector.',
            'Nearest-neighbour rotation retains binary ColorKey edges; per-pixel antialias Alpha is not supplied.',
            'New native16intermediate sizes interpolate the adjacent accepted source resize factors; actual art proportions need host/board visual review.',
            'Expanded DDR safety and relocated panel/Alpha probes are root-owned integration checks.']}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: independent source/metadata recipe checks={checks}, prefix/legacy280records exact, new560frames, atlas_bytes={len(atlas)}')
if __name__=='__main__':main()
