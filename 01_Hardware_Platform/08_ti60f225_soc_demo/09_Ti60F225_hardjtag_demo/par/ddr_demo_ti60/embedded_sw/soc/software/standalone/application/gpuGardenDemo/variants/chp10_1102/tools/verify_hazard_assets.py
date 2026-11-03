#!/usr/bin/env python3
"""White-core centroid oracle, exact old pixels and bright beam geometry.
SPDX-License-Identifier: GPL-3.0-or-later
Actual compiled descriptors and real RGB565 source masks are the oracle.
"""
import argparse,csv,hashlib,json,math,re,struct,subprocess,sys,zlib
from collections import defaultdict
from fractions import Fraction
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def bullet(i):return 12<=i<92 or 280<=i<840
def vals(blob,s):return struct.unpack_from('<'+'H'*(s['width']*s['height']),blob,s['offset'])
def family(s):
    p=s['name'].split('_')
    return (p[1],int(p[2][1:]),int(p[3][1:])) if p[0]=='PRECISION' else (p[0],int(p[1][1:]),int(p[2][1:])*8)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
    p.add_argument('--reference',type=Path,default=ROOT.parent/'neon_ordnance_20261016');a=p.parse_args()
    build=ROOT/'build/host/hazard_assets';build.mkdir(parents=True,exist_ok=True)
    old=(a.reference/'assets/atlas.bin').read_bytes();blob=(ROOT/'assets/atlas.bin').read_bytes()
    before=json.loads((a.reference/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    m=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    oracle=json.loads((ROOT/'assets/hazard_anchor_oracle.json').read_text(encoding='utf-8'))
    assert len(old)==710028 and sha(old)=='b6aa06b4b71f91eaec093f935d943b0b1082671589c4ad3b3f75eb6d105e65bb'
    assert len(blob)==719628==m['bytes']<=0xba000 and sha(blob)==m['sha256']
    assert f'0x{zlib.crc32(blob):08X}'==m['crc32'] and blob[:698028]==old[:698028]
    assert len(m['sprites'])==859 and len(oracle['rows'])==640
    exe=ROOT/'build/ha.exe';dump=build/'compiled_descriptors.csv'
    cmd=[a.compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),
        str(ROOT/'tools/test_hazard_assets.c'),str(ROOT/'src/garden_assets.c'),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    run=subprocess.run([str(exe),str(dump),str(ROOT/'assets/atlas.bin')],capture_output=True,text=True)
    (build/'test_api.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout+run.stderr,end='')
    assert run.returncode==0
    rows=[tuple(map(int,r)) for r in csv.reader(dump.read_text().splitlines())];assert len(rows)==859
    keys=['id','offset','width','height','stride','anchor_x','anchor_y']
    for row,s in zip(rows,m['sprites']):assert row==tuple(s[k] for k in keys)
    records={r['id']:r for r in oracle['rows']};groups=defaultdict(list);maxerr=0.;changed=0
    unchanged_nonbullet=0
    for i,s in enumerate(m['sprites']):
        orig=before['sprites'][i]
        if i<857:
            # The source bytes/stride/dimensions do not follow mutable anchors.
            for k in ['id','name','offset','width','height','stride','bytes']:assert s[k]==orig[k]
            start=s['offset'];end=start+s['bytes'];assert blob[start:end]==old[start:end]
        if not bullet(i):
            if i<857:assert s==orig;unchanged_nonbullet+=1
            continue
        white=[(j%s['width'],j//s['width']) for j,c in enumerate(vals(blob,s)) if c==0xffff]
        assert white and (s['anchor_x'],s['anchor_y']) in white
        cx=Fraction(sum(x for x,y in white),len(white));cy=Fraction(sum(y for x,y in white),len(white))
        # Independent exact-rational nearest-point search, not generator formula.
        nearest=min(((x,y) for x in range(math.floor(cx),math.ceil(cx)+1) for y in range(math.floor(cy),math.ceil(cy)+1)),
            key=lambda q:((q[0]-cx)**2+(q[1]-cy)**2,-q[0],-q[1]))
        assert [s['anchor_x'],s['anchor_y']]==list(nearest)
        err=math.hypot(float(cx-s['anchor_x']),float(cy-s['anchor_y']));assert err<=math.sqrt(.5)+1e-10;maxerr=max(maxerr,err)
        r=records[i];assert r['white_pixels']==len(white) and r['render_anchor']==list(nearest)
        assert r['white_centroid_sums']==[sum(x for x,y in white),sum(y for x,y in white)]
        assert r['reference_render_anchor']==[orig['anchor_x'],orig['anchor_y']]
        changed+=r['reference_render_anchor']!=r['render_anchor']
        kind,col,d=family(s)
        groups[(kind,d)].append((col,s['width'],s['height'],s['anchor_x'],s['anchor_y'],tuple(white)))
    assert len(groups)==128
    for g in groups.values():
        assert sorted(x[0] for x in g)==[1,2,3,4,5]
        assert all(x[1:]==g[0][1:] for x in g),'Palette white-core masks or anchors differ'
    assert changed==480 and unchanged_nonbullet==217
    # Independent world placement test: for physical point(320,360), authored
    # white-core centroid after subtracting descriptor is within1px of point.
    max_world_error=0.
    for r in records.values():
        s=m['sprites'][r['id']]
        world=(320-s['anchor_x']+r['centroid'][0],360-s['anchor_y']+r['centroid'][1])
        max_world_error=max(max_world_error,math.hypot(world[0]-320,world[1]-360))
    assert max_world_error<=1.
    # Stronger than core-color bounds: moving braid exists in bright band,
    # outer halo contains real patterns, period24 is exact and styles differ.
    beamhashes=[]
    for s in m['sprites'][857:859]:
        v=vals(blob,s);beamhashes.append(sha(struct.pack('<'+'H'*len(v),*v)))
        for y in range(120):
            bright=[]
            for x in range(45):
                c=v[y*45+x];bright.append((c>>11)>=29 and ((c>>5)&63)>=56 and (c&31)>=27)
            assert [x for x,present in enumerate(bright) if present]==list(range(16,29))
            if y>=24:assert v[y*45:(y+1)*45]==v[(y-24)*45:(y-23)*45]
        assert len({v[y*45+18] for y in range(24)})>=3,'Bright core still featureless'
        assert len({v[y*45+14] for y in range(24)})>=6,'Inner halo plasma lacks moving energy texture'
        assert len({v[y*45+x] for y in range(24) for x in range(7,16)})>=16,'Outer plasma texture lacks distinct flowing knots'
    assert len(set(beamhashes))==2
    reg=subprocess.run([sys.executable,'-B',str(ROOT/'tools/build_hazard_atlas.py'),'--reference',str(a.reference),'--check'],
        capture_output=True,text=True)
    (build/'regeneration_check.log').write_text(reg.stdout+reg.stderr,encoding='utf-8');assert reg.returncode==0,reg.stdout+reg.stderr
    match=re.search(r'checks=(\d+)',run.stdout);assert match
    files=['src/garden_assets.c','src/garden_assets.h','assets/hazard_anchor_oracle.json',
        'tools/build_hazard_atlas.py','tools/verify_hazard_assets.py','tools/test_hazard_assets.c']
    report=dict(status='PASS',host_only=True,board_acceptance=False,compiled_api_checks=int(match[1]),
        descriptor_bytes=8,compiled_descriptors_match_manifest=859,
        old_pixels_identical_prefix=698028,old_840_bullet_actor_font_pixels_identical=True,
        old_640_bullet_pixel_blobs_identical=True,bullet_anchor_oracle_checks=640,
        changed_bullet_anchor_values=changed,unrelated_nonbullet_records_identical=unchanged_nonbullet,
        palette_shape_anchor_groups=128,colors_per_group=5,white_core_centroid_max_quantization_error_px=maxerr,
        physical_world_point_max_white_core_error_px=max_world_error,
        atlas_bytes=len(blob),atlas_sha256=sha(blob),atlas_crc32=m['crc32'],
        free_bytes_before_scratch=0xba000-len(blob),beam_visual_width=45,beam_core_width=13,
        low_luminance_glow_each_side=16,beam_period24_exact=True,beam_styles_distinct=True,
        beam_bright_core_has_moving_pattern=True,deterministic_regeneration=True,
        source_sha256={r:sha((ROOT/r).read_bytes()) for r in files},compiler_command=cmd,
        limits=['Host-only; renderer/core integration and actual board acceptance are separate.',
                'Pure white centroid locates the bullet visual danger point; it does not widen gameplay hitbox to include glow.',
                'Old640sprite anchors intentionally changed; old840metadata identity is not claimed.',
                '13pxbright beam is the collision width;32additional visual glow pixels are decorative.'])
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ['source_sha256','compiler_command','limits']},indent=2))
if __name__=='__main__':main()

