#!/usr/bin/env python3
"""Production score composite/HUD pixel checks in host RAM; no board timing.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(data): return hashlib.sha256(data).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
    args=parser.parse_args()
    build=ROOT/'build/host/score_renderer';build.mkdir(parents=True,exist_ok=True)
    atlas=(ROOT/'assets/atlas.bin').read_bytes()
    manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert sha(atlas)==manifest['sha256'],'Atlas manifest mismatch'
    assert 0x5C4000+len(atlas)<=0x5FE000,'Atlas scratch overlap'
    # Host replacement contains only a declaration for an unexecuted cache
    # helper. Production CPU/GPU paths are compiled unchanged; RISC-V fence is
    # explicitly redirected by the production GARDEN_RENDER_HOST_TEST switch.
    (build/'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n',encoding='utf-8')
    exe=build/'test_score_renderer.exe'
    command=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror',
             '-DGARDEN_RENDER_HOST_TEST','-I',str(build),'-I',str(ROOT/'src'),
             str(ROOT/'tools/test_score_renderer.c'),str(ROOT/'src/garden_assets.c'),
             str(ROOT/'src/garden_render.c'),str(ROOT/'src/garden_ui.c'),'-o',str(exe)]
    subprocess.run(command,check=True)
    result=subprocess.run([str(exe),str(ROOT/'assets/atlas.bin')],capture_output=True,text=True)
    output=result.stdout+result.stderr
    (build/'test_score_renderer.log').write_text(output,encoding='utf-8');print(output,end='')
    if result.returncode: raise SystemExit(result.returncode)
    match=re.search(r'host contract checks=(\d+)',output);assert match,'Completed score summary missing'
    sources=('src/garden_render.c','src/garden_render.h','src/garden_ui.c','src/garden_ui.h',
             'src/garden_assets.c','src/garden_assets.h','src/gpu_alpha_ref.h',
             'tools/test_score_renderer.c','tools/verify_score_renderer.py')
    report={'status':'PASS','host_only':True,'board_acceptance':False,
            'contract_checks':int(match.group(1)),'compiler_command':command,
            'atlas_bytes':len(atlas),'atlas_sha256':sha(atlas),
            'checks':['production CPU/GPU score composite paths using real atlas fonts',
                      'one common dirty rectangle per composite; max five digits; positive values only',
                      'real RGB565 Alpha digits; procedural minus scales against black',
                      'logical field clipping at all edges and independent two-framebuffer history',
                      'complete frame equals independent full draw oracle with overlapping sprite/text',
                      'watchdog <=16-row commands and render-target guards',
                      'production score-only HUD equals full HUD across 240 mixed updates',
                      'partial-first draw, zero/max/unknown-index/reset, full HUD cache synchronization',
                      'actual GPU glyph/box faults and caller cache reset recover both framebuffers'],
            'source_sha256':{p:sha((ROOT/p).read_bytes()) for p in sources},
            'stdout_sha256':sha(output.encode('utf-8')),
            'limits':['Windows VirtualAlloc maps RAM to production physical addresses.',
                      'RISC-V fence/cache uses host callback; GPU commands and display target guards use host mocks.',
                      'No hardware arbitration, FPGA pixel capture, real scheduling or performance claims.',
                      'Procedural minus uses black-scaled fill, not destination-aware Alpha; font digits use true Alpha.',
                      'UI fault recovery relies on caller checking render counters and resetting all HUD caches.']}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__': main()
