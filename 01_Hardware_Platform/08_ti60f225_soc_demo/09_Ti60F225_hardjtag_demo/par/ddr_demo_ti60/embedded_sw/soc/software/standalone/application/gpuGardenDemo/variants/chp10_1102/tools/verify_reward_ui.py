#!/usr/bin/env python3
"""Finite real UI cache/string/layout checks with RGB565 host mocks only.
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
    build = ROOT / 'build/host/reward_ui'
    build.mkdir(parents=True, exist_ok=True)
    command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-Isrc',
               'tools/test_reward_ui.c', 'src/garden_ui.c', '-o', 'build/host/reward_ui/test_ui.exe']
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (build / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    result = subprocess.run([str(build / 'test_ui.exe')], cwd=ROOT, capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'test.log').write_text(output, encoding='utf-8')
    print(output, end='')
    match = re.search(r'^REWARD_UI (.+)$', output, re.MULTILINE)
    assert match, 'Missing UI completion marker'
    metrics = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', match.group(1))}
    maximum_values = [dict(re.findall(r'(\w+)=(\S+)', line))
                      for line in re.findall(r'^MAXIMUM_FB (.+)$', output, re.MULTILINE)]
    paths = ('src/garden_ui.c', 'src/garden_ui.h', 'src/garden_render.h',
             'tools/test_reward_ui.c', 'tools/verify_reward_ui.py')
    report = dict(
        status='PASS' if result.returncode == 0 else 'FAIL', host_only=True, board_acceptance=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        compiler_command=command, metrics=metrics, maximum_value_full_string_traces=maximum_values,
        source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
        full_stdout_sha256=hashlib.sha256(output.encode('utf-8')).hexdigest(),
        checks=[
            'Execute production garden_ui.c directly; two independent1280x720RGB565 mock framebuffers.',
            'Record complete text strings, styles and positions; model actual8/16pixel glyph advances.',
            'Both physical FBs initialize independently; identicalHUD and score-only repeats issue no draw callbacks.',
            'Score-only updates one value; fullHUD avoids duplicate score; fast-score-before-initial-full preserves initial labels.',
            'I-off clears all old diagnostic labels/values on bothFBs; hidden metric changes draw nothing; I-on restores current values.',
            'Unknown framebuffer identity causes conservative redraw and invalidates both histories.',
            'Maximum score/session/graze/core counters preserve complete10digits and composite FPS/GPU/CPU strings without side-rail overflow.',
            'Narrow hull/bomb counters explicitly showMAX when ten digits cannot fit; eight digits use complete small-font fallback.',
            'Large-to-small glyph transitions clear old lower halves on bothFBs; shrinking text removes old trailing digits.',
            'All dynamic HUD box/glyph rectangles stay outside the640x480combat field.',
            'NullHUD and null state are safe.',
        ],
        limits=[
            'Synthetic glyph rectangles model layout advances; real atlas glyph raster and GPU timing are not exercised.',
            'No UART/JTAG/FPGA/Flash access, FPS result or human visual acceptance.',
            'Finite directed cache/value cases; arbitrary developer-supplied overlength state strings are outside this fixture.',
        ],
    )
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result.returncode == 0 else 2)


if __name__ == '__main__':
    main()
