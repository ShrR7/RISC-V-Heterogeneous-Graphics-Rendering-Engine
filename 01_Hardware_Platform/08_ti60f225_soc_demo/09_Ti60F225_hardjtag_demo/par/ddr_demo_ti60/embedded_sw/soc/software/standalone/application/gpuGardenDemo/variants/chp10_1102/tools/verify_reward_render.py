#!/usr/bin/env python3
"""Real-atlas CPU/GPU pixel contracts for bounded redundant work removal.

Host RAM and mock GPU callbacks only. Command/pixel areas do not prove board FPS.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_SHA = '00f7eee6b53d155455d10893ac56e4c3c9e68914d1df104b43c910ffce3107b1'
ATLAS_SHA = '1b1fa155cc7db9fbfd72a76ae74fd8fdeffb43b4b3321a42aa594aeeabfef076'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--reference', type=Path,
                        default=ROOT.parent / 'neon_motion_20261015/src/garden_render.c')
    args = parser.parse_args()
    build = ROOT / 'build/host/reward_render'
    build.mkdir(parents=True, exist_ok=True)
    reference = args.reference.read_bytes()
    assert sha(reference) == REFERENCE_SHA, 'Expected immutable pre-change renderer'
    (build / 'reference.c').write_bytes(reference)
    (build / 'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n', encoding='utf-8')
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    assert len(atlas) == 631954 and sha(atlas) == ATLAS_SHA
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    sprites = manifest['sprites']
    # Independently derive each mask from actual pixels. All five colours must
    # share the crop; appended precision assets are deliberately unchanged.
    crops = []
    full_area = cropped_area = 0
    for ident in [*range(12, 92), *range(5, 9)]:
        s = sprites[ident]
        pixels = struct.unpack_from('<' + str(s['width'] * s['height']) + 'H', atlas, s['offset'])
        visible = [(i % s['width'], i // s['width']) for i, c in enumerate(pixels) if c != 0xF81F]
        x = min(p[0] for p in visible)
        y = min(p[1] for p in visible)
        w = max(p[0] for p in visible) + 1 - x
        h = max(p[1] for p in visible) + 1 - y
        crops.append(dict(id=ident, x=x, y=y, w=w, h=h))
        full_area += s['width'] * s['height']
        cropped_area += w * h
    for kind in range(2):
        for direction in range(8):
            boxes = [tuple(crops[kind * 40 + colour * 8 + direction][k] for k in ('x', 'y', 'w', 'h'))
                     for colour in range(5)]
            assert len(set(boxes)) == 1
    results = {}
    for label, source, flags in (
        ('reference', 'build/host/reward_render/reference.c', ['-DREWARD_RENDER_REFERENCE']),
        ('candidate', 'src/garden_render.c', []),
    ):
        command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-DGARDEN_RENDER_HOST_TEST', *flags, '-I', 'build/host/reward_render', '-I', 'src',
                   'tools/test_reward_render.c', source, 'src/garden_assets.c', 'src/garden_ui.c',
                   '-o', f'build/host/reward_render/{label}.exe']
        compile_result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (build / f'compile_{label}.log').write_text(compile_result.stdout + compile_result.stderr, encoding='utf-8')
        if compile_result.returncode:
            print(compile_result.stdout + compile_result.stderr)
            raise SystemExit(compile_result.returncode)
        run = subprocess.run([str(build / f'{label}.exe'), str(ROOT / 'assets/atlas.bin')],
                             cwd=ROOT, capture_output=True, text=True)
        output = run.stdout + run.stderr
        (build / f'{label}.log').write_text(output, encoding='utf-8')
        print(output, end='')
        if run.returncode:
            raise SystemExit(run.returncode)
        match = re.search(r'^REWARD_RENDER (.+)$', output, re.MULTILINE)
        assert match, 'Missing focused completion marker'
        metrics = {k: int(v, 0) for k, v in re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', match.group(1))}
        results[label] = dict(metrics=metrics, compiler_command=command,
                              output_sha256=sha(output.encode('utf-8')))
    old, new = (results[k]['metrics'] for k in ('reference', 'candidate'))
    assert old['frames'] == new['frames'] and old['digest'] == new['digest']
    assert new['blit_pixels'] < old['blit_pixels']
    assert new['fill_pixels'] < old['fill_pixels']
    assert new['zero_commands'] == 0 < old['zero_commands']
    assert new['full_bar_commands'] == 2 and old['full_bar_commands'] == 3
    paths = ('src/garden_render.c', 'src/garden_render.h', 'src/garden_assets.c', 'src/garden_assets.h',
             'src/garden_ui.c', 'src/garden_ui.h', 'src/gpu_regs.h', 'src/gpu_alpha_ref.h',
             'tools/test_reward_render.c', 'tools/test_render_batch.c', 'tools/verify_reward_render.py')
    report = dict(
        status='PASS', host_only=True, board_acceptance=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        reference_sha256=REFERENCE_SHA, atlas_sha256=ATLAS_SHA, results=results,
        cropped_asset_count=84, extra_mutable_firmware_state_bytes=0, extra_constant_table_bytes=80,
        extra_atlas_bytes=0, metadata_mask_full_pixels=full_area, metadata_mask_cropped_pixels=cropped_area,
        exact_full_frame_pixels=True, source_sha256={p: sha((ROOT / p).read_bytes()) for p in paths},
        checks=[
            'Original existing full renderer contracts also execute against both reference and candidate.',
            '80 legacy bullet masks and four explosion masks; every non-Key pixel retained with descriptor anchors unchanged.',
            'CPU/GPU, Alpha0/1/16/32, Alpha-off fallback, centre/corners, alternating real framebuffer histories.',
            'Every HP fill value for enemy and Boss bars; clipping and black-scaled procedural Alpha remain exact.',
            'All840 valid zero-alpha asset IDs preserve full destination pixels; no submission for invisible sprites.',
            'Invalid atlas/Alpha inputs still fail; cropped GPU faults retain enough dirty history to retire completely.',
            'All560 appended64heading assets, colours, game collision rules and per-hit damage labels unchanged.',
        ],
        limits=[
            'Host GPU/display mocks do not model DDR latency, real command cost, watchdogs or display underflow.',
            'Pixel/command area reductions are not measured AXI bytes, milliseconds, board FPS or visual acceptance.',
            'No collision-bearing draw is dropped; only alpha0 draws and fully Color-Key margins are omitted.',
            'Only a bounded real-board full-game capture can establish strict60FPS and PX/PART/FULL=0/0/0.',
        ],
    )
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
