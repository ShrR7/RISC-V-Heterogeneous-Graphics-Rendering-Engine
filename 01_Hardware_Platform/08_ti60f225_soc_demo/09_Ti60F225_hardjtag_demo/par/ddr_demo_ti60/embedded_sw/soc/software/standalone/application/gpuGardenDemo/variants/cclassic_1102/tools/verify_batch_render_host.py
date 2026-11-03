#!/usr/bin/env python3
"""Actual production CPU/single/batch complete RGB565 differential, host only."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe');p.add_argument('--skip-legacy',action='store_true');a=p.parse_args()
    build=ROOT/'build/host/batch_render';build.mkdir(parents=True,exist_ok=True)
    (build/'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n')
    sources=['garden_render.c','garden_ui.c','garden_assets.c']
    cmd=[a.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-DGARDEN_RENDER_HOST_TEST','-I',str(build),'-I',str(ROOT/'src'),str(ROOT/'tools/test_batch_render.c'),*[str(ROOT/'src'/n) for n in sources],'-o',str(build/'test_batch_render.exe')]
    subprocess.run(cmd,check=True)
    r=subprocess.run([str(build/'test_batch_render.exe'),str(ROOT/'assets/atlas.bin')],text=True,capture_output=True)
    out=r.stdout+r.stderr;(build/'test.log').write_text(out);print(out,end='')
    if r.returncode:raise SystemExit(r.returncode)
    m=re.search(r'PASS batch_render checks=(\d+) full_rgb565_frames=(\d+) comparison_frames=(\d+) frame_bytes=(\d+)',out)
    if not m:raise SystemExit('No completed pixel comparison')
    legacy_sources=['garden_render.c','garden_ui.c','garden_assets.c','garden_core.c','garden_fx.c','garden_feedback.c','garden_scene.c','garden_combat.c','garden_actor_ui.c','garden_animation.c','garden_heading_precision.c','garden_ordnance_view.c','garden_motion.c','garden_stage.c']
    legacy_checks=0
    if not a.skip_legacy:
        legacy_cmd=[a.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-DGARDEN_RENDER_HOST_TEST','-I',str(build),'-I',str(ROOT/'src'),str(ROOT/'tools/test_variety_view.c'),str(ROOT/'tools/test_batch_render_legacy_mock.c'),*[str(ROOT/'src'/n) for n in legacy_sources],'-o',str(build/'test_batch_render_legacy.exe')]
        subprocess.run(legacy_cmd,check=True)
        r2=subprocess.run([str(build/'test_batch_render_legacy.exe'),str(ROOT/'assets/atlas.bin')],text=True,capture_output=True)
        out2=r2.stdout+r2.stderr;(build/'legacy.log').write_text(out2);print(out2,end='')
        if r2.returncode:raise SystemExit(r2.returncode)
        match=re.search(r'PASS variety_view checks=(\d+)',out2)
        if not match:raise SystemExit('No completed independent legacy pixel oracles')
        legacy_checks=int(match[1])
    capacity_cmd=[a.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-DGARDEN_RENDER_HOST_TEST','-I',str(build),'-I',str(ROOT/'src'),str(ROOT/'tools/test_render_capacity.c'),str(ROOT/'src/garden_ui.c'),str(ROOT/'src/garden_assets.c'),'-o',str(build/'test_render_capacity.exe')]
    subprocess.run(capacity_cmd,check=True)
    r3=subprocess.run([str(build/'test_render_capacity.exe'),str(ROOT/'assets/atlas.bin'),'1'],text=True,capture_output=True)
    out3=r3.stdout+r3.stderr;(build/'capacity.log').write_text(out3);print(out3,end='')
    if r3.returncode:raise SystemExit(r3.returncode)
    capacity_match=re.search(r'PASS render_capacity checks=(\d+) fill_capacity=256 blit_capacity=256 overflow_trigger=257 deferred=1 timeout_no_rewrite=1',out3)
    if not capacity_match:raise SystemExit('Production capacity-256 queue contract did not complete')
    bound=[ROOT/'src'/n for n in legacy_sources]+[ROOT/'src'/n for n in ['garden_render.h','garden_ui.h','garden_assets.h','gpu_driver.h','gpu_regs.h','gpu_alpha_ref.h']]+[ROOT/'tools/test_batch_render.c',ROOT/'tools/test_render_capacity.c',ROOT/'tools/test_batch_render_legacy_mock.c',ROOT/'tools/test_variety_view.c',Path(__file__),ROOT/'assets/atlas.bin']+list((ROOT/'src').glob('garden_*.h'))
    report={'status':'PASS','host_only':True,'board_acceptance':False,'checks':int(m[1]),'capacity_checks':int(capacity_match[1]),'fill_queue_capacity_proven':256,'blit_queue_capacity_proven':256,'deferred_first_item':True,'inherited_independent_oracle_checks':legacy_checks,'legacy_oracles_run':not a.skip_legacy,'full_rgb565_frames':int(m[2]),'comparison_frames':int(m[3]),'frame_bytes':int(m[4]),'compiler_command':cmd,'contracts':['Production CPU/single/batch full RGB565 equality with both physical dirty histories','Alpha-overlapping ordered destinations, different sources and opacity barriers, actual glyph/UI composites, bank bands, beams, clipping and alpha0','GPU Fill and Blit batch submission counters vs mocks; independent runtime switches; external HUD immediate completion','Partial Alpha list hardware failure consumed exactly once, no single fallback/replay; full dirty erase next same physical buffer','Timed-out GPU still busy locks DDR list and rejects next frame, public CPU writes, readback probe and reinitialization until idle','Original per-instance row and pixel bounds retained; fixed DDR list address outside atlas, panel and probe','PAGE_NOACCESS list page proves opaque/Alpha Blit and differently colored Fill singletons perform no DDR list reads or writes','Second compatible Blit materializes ordered first and second destinations; complete frame differences verify all Fill entries','Fill/Blit queues both retain 256-entry capacity for the corrected RTL13 image','Direct production queue injection reaches256 Fill/Blit entries;257th triggers timeout and cannot rewrite list; separate real-frame partial timeout also tested'],'limits':['Host memory/GPU mocks do not prove hardware AXI/DDR/list semantics, frame pacing or board visual correctness.','This test establishes pixel equivalence rather than measured performance.'],'source_sha256':{str(f.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest().upper() for f in bound}}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
