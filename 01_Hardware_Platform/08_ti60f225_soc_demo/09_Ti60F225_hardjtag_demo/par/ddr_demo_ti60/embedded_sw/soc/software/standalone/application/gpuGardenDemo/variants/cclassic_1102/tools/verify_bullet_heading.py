#!/usr/bin/env python3
"""Production FX velocity-heading host audit. No FPGA/board operations.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');args=parser.parse_args()
    build=ROOT/'build/host/bullet_heading';build.mkdir(parents=True,exist_ok=True)
    atlas=ROOT/'assets/atlas.bin';manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert atlas.stat().st_size==236954 and sha(atlas)==manifest['sha256']
    imported=json.loads((ROOT/'assets/imported/master484/manifest.json').read_text(encoding='utf-8-sig'))
    recipes={sprite['name']:sprite for sprite in imported['sprites']}
    for kind,base in (('DART',12),('DIAMOND',52)):
        for colour in range(1,6):
            for rotation in range(8):
                name=f'{kind}_B{colour}_R{rotation}';s=manifest['sprites'][base+(colour-1)*8+rotation]
                assert s['name']==name and recipes[name]['original_id']==s['id']
                assert sha(ROOT/'assets/imported/master484'/recipes[name]['png'])==recipes[name]['png_sha256']
    assert 'select east, SE, south, SW, west, NW, north, NE' in (ROOT/'assets/source/master484/prepare_selected_frames.py').read_text(encoding='utf-8-sig')
    sources=('src/garden_fx.c','src/garden_fx.h','src/garden_assets.c','src/garden_assets.h',
             'src/garden_core.c','src/garden_core.h','src/garden_render.h',
             'tools/test_bullet_heading.c','tools/verify_bullet_heading.py')
    exe=build/'test_bullet_heading.exe'
    cmd=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),
         str(ROOT/'tools/test_bullet_heading.c'),*[str(ROOT/p) for p in sources if p.startswith('src/') and p.endswith('.c')],'-lm','-o',str(exe)]
    subprocess.run(cmd,check=True);run=subprocess.run([str(exe)],capture_output=True,text=True)
    output=run.stdout+run.stderr;(build/'test_bullet_heading.log').write_text(output,encoding='utf-8');print(output,end='')
    if run.returncode:raise SystemExit(run.returncode)
    n=re.search(r'host contract checks=(\d+)',output);assert n
    report={'status':'PASS','host_only':True,'board_acceptance':False,'contract_checks':int(n.group(1)),
            'compiler_command':cmd,'atlas_bytes':atlas.stat().st_size,'atlas_sha256':sha(atlas),
            'checks':['exact integer nearest8direction production helper vs independent libm atan2/floor oracle; axes/diagonals/22.5boundary/extreme signed velocities/stationary unsigned fallback wrap',
                      'all80 original enemy colour/type/direction IDs preserved; source recipes and frozen PNG/atlas hashes retained',
                      'true position/anchors/full opacity and activation-cord independence; both Alpha modes; pause/capacity canary; complete core-byte purity',
                      'natural unchanged real Boss attack3 spinning Diamond: core angle rotates while velocity stays fixed; production draw follows real movement without modifying spin'],
            'source_sha256':{p:sha(ROOT/p) for p in sources},
            'limits':['Only8frozen sprite directions exist; heading quantization can differ from movement by up to22.5degrees.',
                      'A stationary bullet has no movement direction; it uses nearest original stable firing cord_angle.',
                      'Dart atlas anchor remains its original tail and Diamond anchor remains its original center; no collision/position/HP/colour/clock/RNG changes.',
                      'Host checks do not measure GPU/DDR performance or claim board visual acceptance.']}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
