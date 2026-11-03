#!/usr/bin/env python3
"""Actual production renderer/view ordnance contracts, host RAM only.

SPDX-License-Identifier: GPL-3.0-or-later
No serial, JTAG, FPGA or performance measurement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('garden_render.c', 'garden_ui.c', 'garden_assets.c', 'garden_core.c',
           'garden_fx.c', 'garden_feedback.c', 'garden_scene.c', 'garden_combat.c',
           'garden_actor_ui.c', 'garden_animation.c', 'garden_heading_precision.c',
           'garden_ordnance_view.c', 'garden_motion.c', 'garden_stage.c')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = p.parse_args()
    build = ROOT / 'build/host/cruiser_view'
    build.mkdir(parents=True, exist_ok=True)
    (build / 'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n', encoding='utf-8')
    command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DGARDEN_RENDER_HOST_TEST', '-I', str(build), '-I', str(ROOT / 'src'),
               str(ROOT / 'tools/test_cruiser_view.c'),
               *[str(ROOT / 'src' / n) for n in SOURCES], '-o', str(build / 'test_cruiser_view.exe')]
    subprocess.run(command, check=True)
    result = subprocess.run([str(build / 'test_cruiser_view.exe'), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'test.log').write_text(output, encoding='utf-8')
    print(output, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    m = re.search(r'PASS cruiser_view checks=(\d+) descriptor_bytes=(\d+) popup_bytes=(\d+) combat_bytes=(\d+) motion_bytes=(\d+) mutable_view_bytes=0', output)
    if not m:
        raise SystemExit('No completed host contracts.')
    bound = [ROOT / 'src' / n for n in SOURCES]
    bound += list((ROOT / 'src').glob('garden_*.h'))
    bound += [ROOT / 'tools/test_cruiser_view.c', ROOT / 'tools/verify_cruiser_view_host.py', ROOT / 'assets/atlas.bin']
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'checks': int(m[1]), 'descriptor_bytes': int(m[2]), 'popup_bytes': int(m[3]),
        'combat_bytes': int(m[4]), 'motion_bytes': int(m[5]), 'mutable_view_bytes': 0,
        'compiler_command': command,
        'contracts': [
            '40tick warning monotonically brightens, fire/fade distinct states, exactcore x/y and13px width',
            'new cruiser opaque barrel pixels use asymmetric(-42,+43,+19) canonical real core laser origins throughout warning/fire/fade; inverse has no0.5px drift',
            'new actor HP strips use actual sprite top plus4px gap; independent float labels stayabove strip after liveandkilling hits',
            'allnew cruiser rhythm/flash frames match independent atlascomposite oracle and erasecorrectly acrossbothphysicalFBs',
            'new16x16circular orb shared64metadata preserves fullpixelrect and unchanged forming rule',
            'newcroppedgun deathdebris startsat canonical barrelpoint andkeepsbounded100tick presentation',
            'real bomb/heal/pickup admission reservation and unchanged full core snapshots',
            'entity centres use world coordinates; atlas pivot applied once by renderer',
            'local textured effects below real projectile cores and independent damage labels',
            'capacity canaries, paired beam admission, no player-centred clearance ring',
            '368 complete CPU/GPU Alpha-on/off beam frames vs independent RGB565 atlas oracle',
            'two physical framebuffer dirty erase, no stale beam after off, other/front buffer unchanged',
            'literal13bright pixels per beam texture row match closed+/-6px collision core; dim artglow outside',
            '33independent valid combat events retain33damage labels; no legacy hit-ring assets',
            'all640sprite colour/type/headings usecore6tick arming and3x3to9x9dim formation',
            '2560complete RGB565forming frames match independent atlas croporacle in bothAlpha modes',
            'parallel5.9px/tick pilot/marker/EB preserve true10pxgap atall256q8samples',
            'forming EB matchopacity/virtualID; pilotdeath/respawn/teleport/pause snap guards',
        ],
        'limits': ['Host GPU/display mocks, not FPGA AXI/DDR/display or board visual/FPS acceptance.',
                   'Gameplay collisions are core-owned; the host pixel test checks visual geometry, not new collision rules.',
                   'No new persistent state in the ordnance view; final linked target resource usage still requires SDK build.'],
        'source_sha256': {str(path.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest().upper() for path in bound},
    }
    (build / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
