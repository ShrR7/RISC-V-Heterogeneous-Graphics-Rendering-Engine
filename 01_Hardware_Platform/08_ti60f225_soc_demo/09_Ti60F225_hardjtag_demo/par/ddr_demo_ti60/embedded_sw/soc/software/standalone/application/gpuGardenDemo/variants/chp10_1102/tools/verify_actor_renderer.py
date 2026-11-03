#!/usr/bin/env python3
"""Presentation-state and production CPU/GPU pixel contracts, host RAM only.
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
    parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');args=parser.parse_args()
    build=ROOT/'build/host/actor_renderer';build.mkdir(parents=True,exist_ok=True)
    atlas=(ROOT/'assets/atlas.bin').read_bytes();manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert sha(atlas)==manifest['sha256'] and len(atlas)==236954
    assert 0x5C4000+len(atlas)<=0x5FE000
    (build/'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n',encoding='utf-8')
    reports={}
    for label,source,extra in (
        ('actor_ui','tools/test_actor_ui.c',['src/garden_actor_ui.c','src/garden_assets.c']),
        ('actor_renderer','tools/test_actor_renderer.c',['src/garden_assets.c','src/garden_actor_ui.c','src/garden_render.c','src/garden_ui.c'])):
        exe=build/f'test_{label}.exe'
        command=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-DGARDEN_RENDER_HOST_TEST',
                 '-I',str(build),'-I',str(ROOT/'src'),str(ROOT/source),*[str(ROOT/p) for p in extra],'-o',str(exe)]
        subprocess.run(command,check=True)
        run=subprocess.run([str(exe),*([str(ROOT/'assets/atlas.bin')] if label=='actor_renderer' else [])],capture_output=True,text=True)
        output=run.stdout+run.stderr;(build/f'test_{label}.log').write_text(output,encoding='utf-8');print(output,end='')
        if run.returncode: raise SystemExit(run.returncode)
        match=re.search(r'host contract checks=(\d+)',output);assert match
        reports[label]={'contract_checks':int(match.group(1)),'compiler_command':command,'stdout_sha256':sha(output.encode())}
    sources=('src/garden_actor_ui.c','src/garden_actor_ui.h','src/garden_render.c','src/garden_render.h',
             'src/garden_assets.c','src/garden_assets.h','src/garden_core.h','src/garden_ui.c','src/garden_ui.h',
             'src/gpu_alpha_ref.h','tools/test_actor_ui.c','tools/test_actor_renderer.c','tools/verify_actor_renderer.py')
    report={'status':'PASS','host_only':True,'board_acceptance':False,'results':reports,'atlas_bytes':len(atlas),'atlas_sha256':sha(atlas),
            'checks':['true player anchor never changes; five progressive presentation-only bank poses; three unchanged thruster frames',
                      'pause/idempotency/respawn/restart reset; matching last real body only, old trail and marker preserved',
                      'every possible original Darter/Stopper HP and Boss1 HP; no score substitution; ceil-to-1px live sliver',
                      'all nine visible alive actor bars with adequate capacity; fully offscreen models omit orphan bars; fixed-array capacity canary and damage/marker ordering',
                      'production CPU/GPU strip/bar/damage pixels equal independent full RGB565 oracle',
                      'both FB histories erase one composite rectangle, including partial strip/fill faults and all field edges',
                      'watchdog <=16 rows, immutable atlas bounds, unknown framebuffer and actual target rejection',
                      'all20large white/cyan digit masks scanned independently; exact full16x16glyph pixel equivalence with trimmed GPU command source/target/Alpha',
                      '-95 blend rectangles512->280px and common history erase704->532px; unchanged public fontsize/advance',
                      '32overlapping damage labels on alternating FBs; all field edges and actual second-digit GPU failure cleanup'],
            'source_sha256':{p:sha((ROOT/p).read_bytes()) for p in sources},
            'limits':['GPU/display/cache callbacks are host mocks, no FPGA timing or hardware screenshot claimed.',
                      'Pixel-area reductions describe GPU command rectangles, not measured AXI bus bytes or board FPS; burst alignment/command overhead remain.',
                      'Static80byte digit crops bind frozen font atlas; a font atlas change requires recomputing bounds and rerunning pixel contracts.',
                      'Banking uses six discrete nonoverlapping source strips, not continuous scale/rotation or collision-box rotation.',
                      'Bars and minus sign are code-native black-scaled fills; only sprite/digit Alpha reads the destination.',
                      'Enemy maximum HP is fixed to original spawn values 300/3800; future spawn HP changes require this observer to change.',
                      'Insufficient caller capacity omits bars with counter evidence rather than dropping real projectiles/actors.']}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__': main()
