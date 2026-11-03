#!/usr/bin/env python3
"""Compile production warning/bomb visuals; no FPGA timing or board claims."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    output = ROOT / 'build/host/warning_bomb'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n')
    names = ('garden_render.c', 'garden_ui.c', 'garden_assets.c',
             'garden_stage.c', 'garden_ordnance_view.c')
    sources = [ROOT / 'src' / name for name in names]
    executable = output / 'test_warning_bomb.exe'
    command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DGARDEN_RENDER_HOST_TEST', '-I', str(output), '-I', str(ROOT / 'src'),
               str(ROOT / 'tools/test_warning_bomb.c'), *map(str, sources), '-o', str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    text = result.stdout + result.stderr
    (output / 'test.log').write_text(text, encoding='utf-8')
    print(text, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'PASS warning_bomb checks=(\d+) rgb565_frames=(\d+)', text)
    if not match:
        raise SystemExit('No completed warning/bomb contract')
    bound = sources + [ROOT / 'src/garden_render.h', ROOT / 'src/garden_ordnance_view.h',
                       ROOT / 'src/garden_stage.h', ROOT / 'src/garden_assets.h',
                       ROOT / 'tools/test_warning_bomb.c', ROOT / 'tools/test_batch_render.c',
                       Path(__file__), ROOT / 'assets/atlas.bin']
    report = {'host_only': True, 'board_acceptance': False, 'status': 'PASS',
              'checks': int(match[1]), 'full_rgb565_comparison_frames': int(match[2]),
              'command': command,
              'contracts': ['Whole-field red pulses and black/star restoration on both physical buffers',
                            'Moving dirty rectangles restore the same warning red without holes or trails',
                            'Warning banner repaints above actors; fixed engineering panels remain unchanged',
                            'Larger real bomb and multi-layer impact retain actual centres and existing world order',
                            'Capacity guards, reserved admission, alpha-off fallback and unchanged core snapshot',
                            'Damped uniform world shake has signed bounds and expires before blast end',
                            'Independent RGB565 atlas oracle for CPU/single/list rendering with all four field edges',
                            'All impact pieces erase fully after expiration; front framebuffer remains untouched',
                            'Partial full-field Fill timeout blocks retry until idle and restores black even if warning expired'],
              'source_sha256': {str(path.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest().upper() for path in bound}}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
