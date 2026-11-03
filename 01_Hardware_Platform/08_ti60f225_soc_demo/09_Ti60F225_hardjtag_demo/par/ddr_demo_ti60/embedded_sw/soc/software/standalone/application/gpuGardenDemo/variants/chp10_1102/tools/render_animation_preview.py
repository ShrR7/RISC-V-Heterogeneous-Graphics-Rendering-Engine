#!/usr/bin/env python3
"""Actual-core banking, damage, overhead HP and rhythm preview with production renderer in host RAM.
SPDX-License-Identifier: GPL-3.0-or-later
No board access, hardware timing or framebuffer capture. Performance HUD sample.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
SOURCE_NAMES=('garden_render.c','garden_render.h','garden_ui.c','garden_ui.h',
              'garden_core.c','garden_core.h','garden_assets.c','garden_assets.h',
              'garden_fx.c','garden_fx.h','garden_heading_precision.c','garden_heading_precision.h','garden_stage.c','garden_stage.h',
              'garden_feedback.c','garden_feedback.h','garden_scene.c','garden_scene.h',
              'garden_combat.c','garden_combat.h','garden_actor_ui.c','garden_actor_ui.h','garden_animation.c','garden_animation.h','garden_motion.c','garden_motion.h','gpu_alpha_ref.h','gpu_driver.h')
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
    args=parser.parse_args();build=ROOT/'build/host/pv';build.mkdir(parents=True,exist_ok=True)
    atlas=ROOT/'assets/atlas.bin';manifest=json.loads((ROOT/'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert digest(atlas)==manifest['sha256'],'Runtime atlas manifest mismatch'
    (build/'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n',encoding='utf-8')
    exe=build/'preview.exe'
    command=[args.compiler,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-DGARDEN_RENDER_HOST_TEST',
             '-I',str(build),'-I',str(ROOT/'src'),str(ROOT/'tools/preview_animation.c')]
    command += [str(ROOT/'src'/name) for name in SOURCE_NAMES if name.endswith('.c')]
    command += ['-o',str(exe)];subprocess.run(command,check=True)
    result=subprocess.run([str(exe),str(atlas),str(build/'rhythm')],capture_output=True,text=True)
    output=result.stdout+result.stderr;(build/'preview_checks.log').write_text(output,encoding='utf-8');print(output,end='')
    if result.returncode: raise SystemExit(result.returncode)
    rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
    by_name={row['name']:row for row in rows};outputs={}
    for phase in ('enemy_hit','single_hit','enemy_kill','boss_hit','boss_death','bank_left','bank_right','charge','burst','rest','player_death'):
        row=by_name[phase]
        target=ROOT/f'assets/animation_runtime_{phase}_preview.png'
        with Image.open(build/f'rhythm_{phase}.ppm') as im:
            labelled=Image.new('RGB',(im.width,im.height+56),(7,12,26));labelled.paste(im,(0,32));draw=ImageDraw.Draw(labelled)
            draw.text((16,10),'HOST ACTUAL-CORE / PRODUCTION RENDERER IN RAM / NOT BOARD / HUD TIMINGS ARE SAMPLES',fill=(115,244,255))
            damages=', '.join('-'+str(p['damage']) for p in row['damage_draws']) or 'NONE'
            draw.text((16,im.height+40),f"SEED 12345 / TICK {row['tick']} / SCORE {row['score']} / ACTUAL DAMAGE {damages} / DRAWS {row['draws']}/240",fill=(255,215,130))
            labelled.save(target)
        outputs[str(target.relative_to(ROOT))]=digest(target);print(target)
    for group in ('impact','kill'):
        frames=[]
        for i in range(12):
            row=by_name[f'{group}_{i}']
            with Image.open(build/f'rhythm_{group}_{i}.ppm') as im:
                crop=im.crop((320,120,960,600));labelled=Image.new('RGB',(640,524),(7,12,26));labelled.paste(crop,(0,44));draw=ImageDraw.Draw(labelled)
                draw.text((12,6),'HOST / NOT BOARD / 40 HZ CORE TICKS / 4X SLOW PREVIEW',fill=(115,244,255))
                damages=', '.join('-'+str(p['damage']) for p in row['damage_draws']) or 'NONE'
                draw.text((12,24),f"TICK {row['tick']} / SCORE {row['score']} / DAMAGE {damages} / MARKER {row['marker']}",fill=(255,215,130))
                frames.append(labelled.copy())
        gif=ROOT/f'assets/animation_runtime_{group}_preview.gif'
        frames[0].save(gif,save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2,optimize=False)
        outputs[str(gif.relative_to(ROOT))]=digest(gif);print(gif)
    frames=[]
    for i in range(36):
        row=by_name[f'fade_{i}']
        with Image.open(build/f'rhythm_fade_{i}.ppm') as im:
            crop=im.crop((320,120,960,600));labelled=Image.new('RGB',(640,524),(7,12,26));labelled.paste(crop,(0,44));draw=ImageDraw.Draw(labelled)
            draw.text((12,6),'HOST / NOT BOARD / 60HZ RENDER SAMPLES / 40HZ LOGIC',fill=(115,244,255))
            damages=', '.join('-'+str(v['damage']) for v in row['damage_draws']) or 'NONE'
            draw.text((12,24),f"TICK {row['tick']} / ACTUAL DAMAGE {damages} / SUBTICK RISE AND FADE",fill=(255,215,130))
            frames.append(labelled.copy())
    gif=ROOT/'assets/animation_runtime_damage_fade_preview.gif'
    frames[0].save(gif,save_all=True,append_images=frames[1:],duration=[16,17,17]*12,loop=0,disposal=2,optimize=False)
    outputs[str(gif.relative_to(ROOT))]=digest(gif);print(gif)
    frames=[]
    for i in range(45):
        row=by_name[f'death_{i}']
        with Image.open(build/f'rhythm_death_{i}.ppm') as im:
            crop=im.crop((320,120,960,600));labelled=Image.new('RGB',(640,512),(7,12,26));labelled.paste(crop,(0,32));draw=ImageDraw.Draw(labelled)
            draw.text((12,8),'HOST / NOT BOARD / REAL PLAYER DEATH / 60HZ SAMPLES',fill=(115,244,255));frames.append(labelled.copy())
    gif=ROOT/'assets/animation_runtime_player_death_preview.gif'
    frames[0].save(gif,save_all=True,append_images=frames[1:],duration=[16,17,17]*15,loop=0,disposal=2,optimize=False)
    outputs[str(gif.relative_to(ROOT))]=digest(gif)
    match=re.search(r'(\d+) alternating production-renderer combined history frames; (\d+) exact RGB565 playfield pixel comparisons',output)
    assert match,'Combined history verification missing'
    report={'status':'PASS','host_only':True,'board_acceptance':False,'game_snapshot_seed':12345,
            'timings_are_sample_values':True,'animations_are_slowed_snapshots':True,
            'contiguous_animation_logic_hz':40,'gif_display_frame_ms':{'impact_kill':100,'damage_fade':[16,17,17]},'damage_fade_render_hz':60,
            'production_renderer_history_frames':int(match.group(1)),
            'exact_rgb565_playfield_pixel_comparisons':int(match.group(2)),
            'snapshots':rows,'compiler_command':command,
            'checks':['actual seed12345 core/events observed after every logic step',
                      'production GPU renderer code executed on mapped host RGB565 RAM',
                      'actual enemy/Boss hit and kill feedback, actual damage labels and overhead HP bars',
                      'main-compatible banking, per-target HP overlays, shared Boss entrance Alpha clamp and marker last',
                      'combined two-framebuffer dirty erase equals independent clean redraw',
                      'actual final score equals hit-score total plus unchanged kill awards',
                      '12 contiguous actual logic ticks for impact and kill animations','36 actual sub-tick60Hz fade samples after real Boss kill, 40Hz core unchanged'],
            'source_sha256':{'src/'+name:digest(ROOT/'src'/name) for name in SOURCE_NAMES},
            'integration_source_sha256':{'src/main.c':digest(ROOT/'src/main.c')},
            'tool_sha256':{'tools/'+name:digest(ROOT/'tools'/name) for name in ('preview_animation.c','render_animation_preview.py')},
            'atlas_sha256':digest(atlas),'stdout_sha256':hashlib.sha256(output.encode('utf-8')).hexdigest(),
            'output_sha256':outputs,
            'limits':['Host preview is not a board framebuffer/video capture or hardware acceptance.',
                      'GPU/display APIs are host mocks; RISC-V fence/cache replaced by callback.',
                      'No FPGA AXI/DDR arbitration, physical display or hardware render timings tested.',
                      'HUD FPS/performance fields are illustrative sample values.',
                      'Impact/kill animations show 25ms simulation ticks at100ms, four times slower; damage fade shows36subtick60Hzsamples with16/17/17msGIFdelays.',
                      'Procedural minus uses RGB565 fill scaled against black; digits use destination Alpha.']}
    (build/'preview_verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__': main()
