#!/usr/bin/env python3
"""Production stage-event host checks, including RGB565 footer cache oracle.

SPDX-License-Identifier: GPL-3.0-or-later
No UART, JTAG, FPGA pixels or measured board performance are accessed here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'neon_stage_20261013'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/stage'
    build.mkdir(parents=True, exist_ok=True)
    # Feedback deliberately changes HUD, diagnostic-panel visibility and
    # appends three assets; freeze gameplay, FX/stage and MMIO separately.
    protected = ('garden_core.c', 'garden_core.h', 'garden_fx.c', 'garden_fx.h',
                 'garden_stage.c', 'garden_stage.h', 'gpu_driver.c', 'gpu_driver.h')
    for name in protected:
        assert (ROOT / 'src' / name).read_bytes() == (BASE / 'src' / name).read_bytes(), name
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    previous=(BASE / 'assets/atlas.bin').read_bytes()
    assert atlas[:len(previous)] == previous, 'Frozen stage atlas prefix changed'
    exe = build / 'test_stage.exe'
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_stage.c'),
               str(ROOT / 'src/garden_stage.c'), str(ROOT / 'src/garden_core.c'),
               str(ROOT / 'src/garden_assets.c'), str(ROOT / 'src/garden_ui.c'),
               '-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'test_stage.log').write_text(output, encoding='utf-8')
    print(output, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'host contract checks=(\d+)', result.stdout)
    assert match, 'No completed stage contract summary'
    sources = [ROOT / 'src' / name for name in
               ('garden_core.c', 'garden_core.h', 'garden_stage.c', 'garden_stage.h',
                'garden_ui.c', 'garden_ui.h', 'garden_assets.c', 'garden_assets.h')]
    sources += [ROOT / 'tools/test_stage.c', ROOT / 'tools/verify_stage_host.py']
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'compiler_command': command, 'contract_checks': int(match.group(1)),
        'atlas_bytes': len(atlas), 'atlas_sha256': sha(atlas),
        'protected_core_FX_stage_and_MMIO_byte_identical': True,
        'frozen_stage_atlas_prefix_byte_identical': True,
        'intentional_changes': ['HUD health display', 'diagnostic-panel visibility', 'three appended feedback assets'],
        'checks': ['real WAVES-to-BOSS transition and 100tick warning',
                   '64tick monotonic Boss alpha8-to32 and alpha-off fallback',
                   'same-tick calls and pause freeze; restart/mode/off lifecycle',
                   'two independent framebuffer footer caches, high-to-low score',
                   'production full-reset footer vs cached RGB565 pixel oracle',
                   'mid-draw box/font errors,bad_targets,atlas_bounds reject both caches and fully retry',
                   'footer-only render bounds and unsupported glyph rejection',
                   '12000 complete core snapshot comparisons including automatic restart'],
        'stdout_sha256': sha(output.encode('utf-8')),
        'source_sha256': {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p.read_bytes()) for p in sources},
        'limits': ['Runs production stage/core/UI with a host RAM renderer and real RGB565 fonts.',
                   'Does not run the board GPU/MMIO renderer or establish hardware performance or visual acceptance.'],
    }
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
