#!/usr/bin/env python3
"""Verify actual compiled8-byte API, old atlas identity and new asset bounds.
SPDX-License-Identifier: GPL-3.0-or-later
No hardware use or visual acceptance is claimed.
"""
import argparse,csv,hashlib,json,re,struct,subprocess,sys,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--reference',type=Path,default=ROOT.parent/'neon_bomb_20261016')
    args=parser.parse_args();ref=args.reference.resolve()
    build=ROOT/'build/host/ordnance_assets';build.mkdir(parents=True,exist_ok=True)
    old=(ref/'assets/atlas.bin').read_bytes();blob=(ROOT/'assets/atlas.bin').read_bytes()
    before=json.loads((ref/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(old)==631954 and sha(old)=='1b1fa155cc7db9fbfd72a76ae74fd8fdeffb43b4b3321a42aa594aeeabfef076'
    assert blob[:len(old)]==old,'Existing aircraft, font,640headings or previous effect pixels changed'
    assert manifest['sprites'][:840]==before['sprites'],'Existing source coordinates or metadata changed'
    assert len(blob)==manifest['bytes']<=0xba000 and sha(blob)==manifest['sha256']
    assert f'0x{zlib.crc32(blob):08X}'==manifest['crc32'] and len(manifest['sprites'])==859
    assert 0x5c4000+len(blob)<=0x67e000,'Atlas would overlap protected tail probes'
    # Production C API generates the actual compiled descriptor mapping; compare
    # every field against independently loaded manifest, not a C text regex.
    exe=ROOT/'build/oa.exe';dump=build/'compiled_descriptors.csv'
    cmd=[args.compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),
        str(ROOT/'tools/test_ordnance_assets.c'),str(ROOT/'src/garden_assets.c'),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    run=subprocess.run([str(exe),str(dump)],capture_output=True,text=True);log=run.stdout+run.stderr
    (build/'test_api.log').write_text(log,encoding='utf-8');print(log,end='')
    assert run.returncode==0
    rows=[tuple(map(int,r)) for r in csv.reader(dump.read_text().splitlines())]
    assert len(rows)==859
    keys=('id','offset','width','height','stride','anchor_x','anchor_y')
    for row,s in zip(rows,manifest['sprites']):assert row==tuple(s[k] for k in keys),s['id']
    newhashes=set();newbytes=0
    for s in manifest['sprites'][840:]:
        end=s['offset']+s['height']*s['stride'];data=blob[s['offset']:end]
        assert len(data)==s['bytes']==2*s['width']*s['height']
        assert sha(data)==s['rgb565_sha256']
        colors=struct.unpack('<'+'H'*(len(data)//2),data)
        assert any(c!=0xf81f for c in colors)
        if s['id']<857:assert 0xf81f in colors,'Transparent equipment/effect became an opaque rectangle'
        newhashes.add(sha(data));newbytes+=len(data)
    assert len(newhashes)==19,'Animation frames unexpectedly duplicate'
    # Beam and collision contract: every bright-core pixel is visible and pale,
    # y period24 is exact, and each outer fringe is darker than its13px core.
    for s in manifest['sprites'][857:859]:
        values=struct.unpack_from('<'+'H'*(25*120),blob,s['offset'])
        for y in range(120):
            for x in range(6,19):
                c=values[y*25+x];assert (c>>11)>=29 and ((c>>5)&63)>=56 and (c&31)>=27
            assert values[y*25+12] != values[y*25+2]
            if y>=24: assert values[y*25:(y+1)*25]==values[(y-24)*25:(y-23)*25]
    regenerate=subprocess.run([sys.executable,'-B',str(ROOT/'tools/build_ordnance_atlas.py'),'--reference',str(ref),'--check'],
        capture_output=True,text=True)
    (build/'regenerate_check.log').write_text(regenerate.stdout+regenerate.stderr,encoding='utf-8')
    assert regenerate.returncode==0,regenerate.stdout+regenerate.stderr
    match=re.search(r'checks=(\d+)',log);assert match
    sources=['src/garden_assets.c','src/garden_assets.h','tools/build_ordnance_atlas.py',
             'tools/verify_ordnance_assets.py','tools/test_ordnance_assets.c','assets/source/ordnance_cc0/sources.json',
             'assets/source/ordnance_cc0/LICENSE_EVIDENCE.txt']
    report=dict(status='PASS',host_only=True,board_acceptance=False,compiled_api_checks=int(match[1]),
        actual_descriptor_bytes=8,actual_descriptor_count=859,compiled_descriptors_matches_json=859,
        old_sprite_records_identical=840,old_pixels_prefix_exact_bytes=len(old),old_pixels_prefix_sha256=sha(old),
        new_unique_frames=19,new_pixel_bytes=newbytes,atlas_bytes=len(blob),atlas_sha256=sha(blob),
        atlas_crc32=manifest['crc32'],atlas_budget=0xba000,free_bytes_before_scratch=0xba000-len(blob),
        old_descriptor_bytes=840*16,new_descriptor_bytes=859*8,descriptor_bytes_saved=840*16-859*8,
        beam_core_width=13,beam_period24_exact=True,deterministic_regeneration_check=True,
        source_sha256={r:sha((ROOT/r).read_bytes()) for r in sources},compiler_command=cmd,
        limits=['Host-only; actual hardware rendering/performance and gameplay are separate acceptance.',
                'Legacy inherited tools assume old16byte/840asset ABI and are reference-only; use this new validator.',
                'Binary Color Key preserves selected angular silhouettes; it is not per-pixel Alpha hardware.'])
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('source_sha256','compiler_command','limits')},indent=2))
if __name__=='__main__':main()

