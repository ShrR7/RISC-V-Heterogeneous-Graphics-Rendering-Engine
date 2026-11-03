#!/usr/bin/env python3
"""Bounded reward presentation and two-framebuffer RGB565 host verification.

SPDX-License-Identifier: GPL-3.0-or-later
No serial, JTAG, framebuffer MMIO, bitstream programming or board claims.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    p.add_argument('--output-dir', type=Path, default=ROOT / 'build/host/reward_presentation')
    args = p.parse_args()
    build = args.output_dir.resolve()
    if ROOT not in build.parents or 'evidence' in build.relative_to(ROOT).parts:
        raise SystemExit('Host output must be below this variant and outside evidence.')
    build.mkdir(parents=True, exist_ok=True)
    sources = ['tools/test_reward_presentation.c', 'src/garden_stage.c',
               'src/garden_animation.c', 'src/garden_assets.c', 'src/garden_ui.c']
    exe = build / 'test_reward_presentation.exe'
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), *[str(ROOT / x) for x in sources], '-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    log = result.stdout + result.stderr
    (build / 'test_reward_presentation.log').write_text(log, encoding='utf-8')
    print(log, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    m = re.search(r'PASS reward_presentation checks=(\d+) stage_bytes=(\d+) animation_bytes=(\d+)', log)
    if not m:
        raise SystemExit('No completed contract summary.')
    bound = sources + ['src/garden_stage.h', 'src/garden_animation.h', 'src/garden_core.h',
                       'src/garden_render.h', 'src/garden_assets.h', 'src/garden_ui.h',
                       'tools/verify_reward_presentation_host.py', 'assets/atlas.bin']
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'checks': int(m[1]), 'stage_bytes': int(m[2]), 'animation_bytes': int(m[3]),
        'RAM_delta_vs_motion': {'stage': 32, 'animation': 8, 'total': 40},
        'compiler_command': command,
        'contracts': [
            '100 logic ticks=2.5s bounded multi-stage Boss death; maximum12 appended atlas sprites',
            'all existing real draw entries retained; tight capacities and canaries',
            'actual atlas glyphs and production stars, full RGB565 pixel oracle on both physical framebuffers',
            'static card once per FB; subsequent updates limited to score and delayed prompt windows',
            'score rolls monotonically to exact true credit including UINT32_MAX without multiplication overflow',
            'restart clears each old modal before fresh scene objects; no other UI pixels changed',
            'partial render errors invalidate both histories and force full retry; cleanup errors retain dirty marker',
            'genuine life-loss triggers8white/gold fragments for40ticks=1s; marker remains on top',
            'real core grace state powers marker pulse; actual12tick bomb state appends at most8localcyan rings',
            'read-only complete GardenGame snapshots; no score, collision, RNG, clock or hazard colour remap',
        ],
        'limits': [
            'Host RAM renderer only; does not establish target GPU correctness, board FPS or user visual acceptance.',
            'Boss body/wing/debris sprites are reused; no geometric petal deformation or arbitrary scaling.',
            'No global hit-stop or slow motion. Protected-player camera remains the existing four-tick bounded effect.',
            'Best score is a RAM-session statistic supplied by core; not persistent Flash storage.',
        ],
        'source_sha256': {x: hashlib.sha256((ROOT / x).read_bytes()).hexdigest().upper() for x in bound},
    }
    (build / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
