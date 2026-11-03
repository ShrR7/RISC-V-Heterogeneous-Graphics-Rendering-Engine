#!/usr/bin/env python3
"""Bounded real engineering-UI cache/string/layout contracts, host RGB565 only.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import datetime
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
    build = ROOT / 'build/host/engineering_ui'
    build.mkdir(parents=True, exist_ok=True)
    command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-Isrc',
               'tools/test_engineering_ui.c', 'src/garden_ui.c', '-o', 'build/host/engineering_ui/test_ui.exe']
    compile_result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (build / 'compile.log').write_text(compile_result.stdout + compile_result.stderr, encoding='utf-8')
    if compile_result.returncode:
        print(compile_result.stdout + compile_result.stderr)
        raise SystemExit(compile_result.returncode)
    result = subprocess.run([str(build / 'test_ui.exe')], cwd=ROOT, capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'test.log').write_text(output, encoding='utf-8')
    print(output, end='')
    match = re.search(r'^ENGINEERING_UI (.+)$', output, re.MULTILINE)
    assert match, 'Missing engineering UI completion marker'
    metrics = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', match.group(1))}
    maxima = [dict(re.findall(r'(\w+)=(\S+)', line))
              for line in re.findall(r'^ENGINEERING_MAX (.+)$', output, re.MULTILINE)]
    paths = ('src/garden_ui.c', 'src/garden_ui.h', 'src/garden_render.h',
             'tools/test_engineering_ui.c', 'tools/verify_engineering_ui.py')
    report = dict(
        status='PASS' if result.returncode == 0 else 'FAIL', host_only=True, board_acceptance=False,
        variant='neon_engineering_20261016',
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        compiler_command=command, metrics=metrics, maximum_value_full_string_traces=maxima,
        source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
        output_sha256=hashlib.sha256(output.encode('utf-8')).hexdigest(),
        contracts=[
            'Execute real engineering garden_ui.c with two independent1280x720 RGB565 mock framebuffers.',
            'Keep complete text callback strings and original8/16pixel advances; score anchored at56,220.',
            'Both physicalFB caches initialize independently; identicalHUD/fastscore issue zero draws.',
            'Score-only changes exactly its clear and text; early fastscore preserves full initial HUD.',
            'FPS, both benchmarks, speedup and errors update actual values independently on bothFBs.',
            'Zero denominator showsPRESS B; ratio overflow showsOVER99999X without divide/format overflow.',
            'Counters preserve full uint32 strings where space permits;32px hull explicitly showsMAX.',
            'R safevalue clears original16px height through fallback; shorter hull/FPS/benchmarks erase old trailing text.',
            'UnknownFB identities conservatively redraw and invalidate physical histories.',
            'All dynamic callbacks remain within engineering side panels and outside640x480combat field.',
            'Engineering panels are permanent; diagnostics flag does not hide them. Q bomb is explicitly documented.',
        ],
        limits=[
            'Synthetic opaque glyph rectangles test actual text advances/layout; atlas raster/GPU timing not measured.',
            'No hardware, UART/JTAG, FPS, display-underflow or human visual acceptance is inferred.',
            'This test is for engineering sidebars only; core/stage/renderer pixel contracts are separate.',
        ],
    )
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result.returncode == 0 else 2)


if __name__ == '__main__':
    main()
