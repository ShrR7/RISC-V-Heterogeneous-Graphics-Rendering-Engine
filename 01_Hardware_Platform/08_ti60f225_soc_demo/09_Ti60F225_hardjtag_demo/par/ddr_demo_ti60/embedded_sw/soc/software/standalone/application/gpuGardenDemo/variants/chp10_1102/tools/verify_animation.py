#!/usr/bin/env python3
"""Restricted visual animation/UI host contracts; no FPGA/board operations.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');args=parser.parse_args()
    build=ROOT/'build/host/animation';build.mkdir(parents=True,exist_ok=True)
    atlas=ROOT/'assets/atlas.bin';manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert atlas.stat().st_size==236954 and sha(atlas)==manifest['sha256']
    exe=build/'test_animation.exe'
    sources=('src/garden_animation.c','src/garden_animation.h','src/garden_ui.c','src/garden_ui.h',
             'src/garden_assets.c','src/garden_assets.h','src/garden_core.c','src/garden_core.h','src/garden_render.h',
             'tools/test_animation.c','tools/verify_animation.py')
    cmd=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),
         str(ROOT/'tools/test_animation.c'),*[str(ROOT/p) for p in sources if p.startswith('src/') and p.endswith('.c')],'-o',str(exe)]
    subprocess.run(cmd,check=True);run=subprocess.run([str(exe),str(atlas)],capture_output=True,text=True)
    output=run.stdout+run.stderr;(build/'test_animation.log').write_text(output,encoding='utf-8');print(output,end='')
    if run.returncode:raise SystemExit(run.returncode)
    n=re.search(r'host contract checks=(\d+)',output);assert n
    report={'status':'PASS','host_only':True,'board_acceptance':False,'contract_checks':int(n.group(1)),
            'compiler_command':cmd,'atlas_bytes':atlas.stat().st_size,'atlas_sha256':sha(atlas),
            'checks':['all80enemy sprite family/type/rotation IDs and full source pixels unchanged; five original palettes remain distinct, opacity/coordinates retained in both Alpha modes; remapped_total remains zero; corecolour untouched',
                      'genuine original vulnerable player death and original50tick respawn; true fresh effect anchor',
                      'six small existing5x19shot/13x13spark particles; 30tick750ms lifetime; no new atlas, full-plane ghosts or large Alpha ring',
                      'protected-death-only4px uniform field offset including enemybullets/HP/damage/true marker; no vulnerable camera motion',
                      'fraction,pause,idempotency,restart,capacity canary, no-core-mutation and saturating malformed coordinates',
                      'muted static8pxheaders/frames, compact176pxperformance block with actual FPS, CPU/GPUbenchmark/ratio/errors',
                      'independent two-FB HUD and actualscore refresh; no writes for unchanged values or omitted visual timing rows'],
            'source_sha256':{p:sha(ROOT/p) for p in sources},
            'limits':['UI rendering callbacks are host recording mocks, not GPU/board pixel/performance measurements.',
                      'Camera shifts whole world draw list during protected death, but the sparse static background and physical UI remain fixed.',
                      'Fresh draw list required for each apply; applying twice to a previously transformed list is unsupported.',
                      'Small fragments use hardwareAlpha when supported; Alpha-off fallback remains opaque until the same fixed lifetime expires.',
                      'Capacity pressure omits only new decorative fragments and counts cuts; actual actors/bullets/bars/marker retain their earlier admission.',
                      'No core timing, hitbox, HP,colour/RNG writes. All five original visible enemy hue families and rotations are retained.']}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
