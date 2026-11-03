#!/usr/bin/env python3
"""Create labelled HOST PREVIEW screenshots from actual R core and renderer.

SPDX-License-Identifier: GPL-3.0-or-later
No hardware connection, board capture or performance acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('garden_core.c', 'garden_render.c', 'garden_ui.c', 'garden_assets.c',
           'garden_fx.c', 'garden_feedback.c', 'garden_scene.c', 'garden_combat.c',
           'garden_stage.c', 'garden_actor_ui.c', 'garden_animation.c', 'garden_motion.c',
           'garden_heading_precision.c')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    p.add_argument('--output-dir', type=Path, default=ROOT / 'build/host/reward_preview')
    p.add_argument('--no-gif', action='store_true')
    args = p.parse_args()
    build = args.output_dir.resolve()
    if ROOT not in build.parents or 'evidence' in build.relative_to(ROOT).parts:
        raise SystemExit('Preview output must be within this variant and outside evidence.')
    build.mkdir(parents=True, exist_ok=True)
    atlas = ROOT / 'assets/atlas.bin'
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert digest(atlas) == manifest['sha256'].upper(), 'Atlas digest mismatch'
    (build / 'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n', encoding='utf-8')
    command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DGARDEN_RENDER_HOST_TEST', '-I', str(build), '-I', str(ROOT / 'src'),
               str(ROOT / 'tools/preview_reward.c'),
               *[str(ROOT / 'src' / name) for name in SOURCES], '-o', str(build / 'preview_reward.exe')]
    subprocess.run(command, check=True)
    result = subprocess.run([str(build / 'preview_reward.exe'), str(atlas), str(build / 'reward')],
                            capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'preview.log').write_text(output, encoding='utf-8')
    if result.returncode:
        print(output)
        raise SystemExit(result.returncode)
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
    indexed = {row['name']: row for row in rows}
    assert 'victory_tick=3894 final_score=2220 no_board=1' in output
    font_path = Path('C:/Windows/Fonts/consola.ttf')
    font = ImageFont.truetype(str(font_path), 16) if font_path.exists() else ImageFont.load_default()
    outputs = {}
    for name, row in indexed.items():
        if name.startswith('anim_'):
            continue
        with Image.open(build / f'reward_{name}.ppm') as source:
            labelled = Image.new('RGB', (1280, 780), (6, 12, 24))
            labelled.paste(source, (0, 36))
            draw = ImageDraw.Draw(labelled)
            draw.text((16, 8), 'HOST PREVIEW / NOT BOARD / ACTUAL SEED 12345 / PRODUCTION RENDERER IN HOST RAM', font=font, fill=(115, 244, 255))
            draw.text((16, 758), f"TICK {row['tick']}  SCORE {row['score']}  DISPLAY {row['visible_score']}  GRAZE {row['graze']}  PICKUPS {row['items']}  BOMBS USED {row['bombs_used']}", font=font, fill=(255, 215, 130))
            target = build / f'{name}.png'
            labelled.save(target)
            outputs[target.name] = digest(target)
    if not args.no_gif:
        frames = []
        for i in range(120):
            row = indexed[f'anim_{i:03d}']
            with Image.open(build / f'reward_anim_{i:03d}.ppm') as source:
                crop = source.crop((320, 120, 960, 600)).resize((480, 360), Image.Resampling.NEAREST)
                labelled = Image.new('RGB', (480, 404), (6, 12, 24))
                labelled.paste(crop, (0, 44))
                draw = ImageDraw.Draw(labelled)
                draw.text((8, 3), 'HOST PREVIEW / NOT BOARD / REAL CORE', fill=(115, 244, 255))
                draw.text((8, 20), f"{row['victory_age'] / 40:.2f}s  SCORE {row['visible_score']}/{row['score']}  20FPS PREVIEW", fill=(255, 215, 130))
                frames.append(labelled.quantize(colors=128))
        target = build / 'death_and_settlement.gif'
        frames[0].save(target, save_all=True, append_images=frames[1:], duration=50,
                       loop=0, disposal=2, optimize=False)
        outputs[target.name] = digest(target)
    source_paths = [ROOT / 'src' / name for name in SOURCES]
    source_paths += list((ROOT / 'src').glob('garden_*.h'))
    source_paths += [ROOT / 'src/main.c', ROOT / 'tools/preview_reward.c', ROOT / 'tools/render_reward_preview.py']
    report = {
        'status': 'HOST_PREVIEW_GENERATED', 'host_only': True, 'board_acceptance': False,
        'seed': 12345, 'actual_victory_tick': 3894, 'actual_final_score': 2220,
        'performance_values_fabricated': False, 'diagnostics_hidden': True,
        'draw_assembly': 'matches current main.make_draws order; world centres; production atlas anchors',
        'modal_order': 'prepare_frame -> render_frame(empty when modal) -> HUD -> stage_draw',
        'gif': {'frames': 120, 'display_ms_per_frame': 50, 'logic_ticks_between_frames': 2,
                'playfield_preview_resize': '640x480 to480x360 nearest', 'measured_hardware_fps': None},
        'snapshots': rows, 'compiler_command': command,
        'source_sha256': {str(path.relative_to(ROOT)).replace('\\', '/'): digest(path) for path in source_paths},
        'atlas_sha256': digest(atlas), 'output_sha256': outputs,
        'limits': ['Real core/gameplay; GPU and display calls are host RAM mocks.',
                   'Not a board screenshot/video and not user visual acceptance.',
                   'No FPGA arbitration, DDR timing, physical display output, UART or COM11 access.',
                   'The GIF is a labelled20FPS host preview, not a measured game frame rate.',
                   'No invented score, pickup, bomb, hit, graze or Boss death events.'],
    }
    (build / 'preview_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'outputs': list(outputs),
                      'victory_tick': 3894, 'final_score': 2220, 'board_acceptance': False}, ensure_ascii=False))


if __name__ == '__main__':
    main()
