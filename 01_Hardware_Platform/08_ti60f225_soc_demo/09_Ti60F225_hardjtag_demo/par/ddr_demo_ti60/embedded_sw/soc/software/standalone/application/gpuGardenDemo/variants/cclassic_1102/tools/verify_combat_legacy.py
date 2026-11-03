#!/usr/bin/env python3
"""Run retained FX/feedback/Stage contracts with the authorized scoring fixture.

SPDX-License-Identifier: GPL-3.0-or-later
Unchanged gameplay physics is established by verify_combat_core.py against
the separately compiled trusted scene core, rather than claimed source equality.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parent/'neon_scene_20261014'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');args=parser.parse_args()
    build=ROOT/'build/host/combat_legacy';build.mkdir(parents=True,exist_ok=True)
    original=(BASE/'tools/test_feedback.c').read_text();current=(ROOT/'tools/test_feedback.c').read_text()
    assert original.replace('g.score==1000','g.score==1002')==current,'Only declared scoring fixture may change'
    unchanged=('garden_fx.c','garden_fx.h','garden_feedback.c','garden_feedback.h','garden_scene.c','garden_scene.h','garden_stage.c','garden_stage.h','garden_assets.c','garden_assets.h')
    for n in unchanged:assert (ROOT/'src'/n).read_bytes()==(BASE/'src'/n).read_bytes(),n
    assert (ROOT/'assets/atlas.bin').read_bytes()==(BASE/'assets/atlas.bin').read_bytes()
    cases={
        'fx':('garden_fx.c','garden_core.c','garden_assets.c'),
        'feedback':('garden_feedback.c','garden_fx.c','garden_core.c','garden_assets.c'),
        'stage':('garden_stage.c','garden_core.c','garden_assets.c','garden_ui.c'),
        'scene':('garden_scene.c','garden_feedback.c','garden_fx.c','garden_core.c','garden_assets.c'),
    }
    rows=[];source_files={'src/garden_core.c','src/garden_core.h','tools/verify_combat_legacy.py'}
    for case,sources in cases.items():
        exe=build/f'test_{case}.exe';test=f'tools/test_{case}.c'
        command=[args.compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/test)]
        command += [str(ROOT/'src'/n) for n in sources]+['-o',str(exe)]
        subprocess.run(command,check=True)
        invocation=[str(exe)]
        if case in ('fx','stage'):invocation+=[str(ROOT/'assets/atlas.bin')]
        run=subprocess.run(invocation,capture_output=True,text=True);out=run.stdout+run.stderr
        (build/f'test_{case}.log').write_text(out,encoding='utf-8');print(out,end='')
        if run.returncode:raise SystemExit(run.returncode)
        match=re.search(r'(?:host contract checks|checks)=(\d+)',out);assert match,case
        rows.append({'case':case,'checks':int(match.group(1)),'stdout_sha256':hashlib.sha256(out.encode()).hexdigest()})
        source_files.add(test);source_files.update('src/'+n for n in sources)
    source_files.update('src/'+n for n in unchanged)
    report={'status':'PASS','host_only':True,'board_acceptance':False,'cases':rows,
        'authorized_fixture_change':'Two effective Boss hits2 + original1000killreward =1002',
        'unchanged_FX_feedback_scene_stage_assets':True,'atlas_byte_identical':True,
        'source_sha256':{n:sha(ROOT/n) for n in sorted(source_files)},
        'limits':['Core source intentionally adds user-approved scoring and bounded events; old physics proof is separate.',
                  'These suites establish observer purity and legacy contracts, not hardware frames or timing.']}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
