#!/usr/bin/env python3
"""Bounded in-place history merging, host pixel/model contracts.
SPDX-License-Identifier: GPL-3.0-or-later
No host wall-clock or command-count result is a measured board speedup.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FROZEN_DRIVER_SHA = '49d83fa1432df22675f4ea764903652ec46282143b688e8e02da0df5b7031c88'
FROZEN_RENDER_SHA = '66ea9e74adee4bb97501b7a2aac20865c7a75b8123cfcdef1777a6db402dc0e5'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked(command, build, name):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    text = result.stdout + result.stderr
    (build / name).write_text(text, encoding='utf-8')
    if result.returncode:
        print(text, end='')
        raise RuntimeError(f'Command exit={result.returncode}: {command}')
    return text


def metrics(output, prefix):
    line = re.search(r'^' + prefix + r' (.+)$', output, re.MULTILINE)
    assert line, f'Missing {prefix} completion'
    return {k: int(v, 0) for k, v in re.findall(r'(\w+)=(0x[0-9A-Fa-f]+|\d+)', line.group(1))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--reference-root', type=Path, default=ROOT.parent / 'neon_heading_precision_20261015')
    parser.add_argument('--erase-reference', type=Path,
                        default=ROOT / 'evidence/motion_trial_20261015_172344/source_snapshot/src/garden_render.c')
    args = parser.parse_args()
    reference = args.reference_root.resolve()
    erase_reference = args.erase_reference.resolve()
    build = ROOT / 'build/host/rm'
    build.mkdir(parents=True, exist_ok=True)
    # Short fixed output paths avoid MinGW's Windows MAX_PATH limit.
    (build / 'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n', encoding='utf-8')
    assert sha((reference / 'src/gpu_driver.c').read_bytes()) == FROZEN_DRIVER_SHA
    renderer_bytes = erase_reference.read_bytes()
    assert sha(renderer_bytes) == FROZEN_RENDER_SHA
    # MinGW cannot compile the long sealed-case pathname; copy exact fixed bytes.
    (build / 'reference.c').write_bytes(renderer_bytes)
    for name in ('assets/atlas.bin', 'src/garden_assets.c', 'src/garden_assets.h',
                 'src/garden_render.h', 'src/garden_ui.c', 'src/garden_ui.h'):
        assert (ROOT / name).read_bytes() == (reference / name).read_bytes(), name + ' changed relative to fixed pixel input'
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(atlas) == manifest['bytes'] and sha(atlas) == manifest['sha256']
    base = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
            '-I', 'build/host/rm', '-I', 'src']
    geom_command = base + ['-DGPU_DRIVER_GEOMETRY_HOST_TEST', 'tools/test_render_batch.c',
                          'src/gpu_driver.c', '-o', 'build/host/rm/geom.exe']
    checked(geom_command, build, 'compile_geometry.log')
    geometry_text = checked([str(build / 'geom.exe')], build, 'geometry.log')
    print(geometry_text, end='')
    geometry = metrics(geometry_text, 'GEOMETRY')
    assert geometry['dense_calls'] == 128 and geometry['dense_reference_rows'] == 3968
    assert geometry['dense_candidate_rows'] == 0 and geometry['dense_fastpaths'] == 128
    results = {}
    for label, renderer, flags in (
        ('reference', 'build/host/rm/reference.c', ['-DRENDER_MERGE_REFERENCE']),
        ('candidate', 'src/garden_render.c', []),
    ):
        command = base + ['-DGARDEN_RENDER_HOST_TEST', *flags, 'tools/test_render_batch.c',
                          renderer, 'src/garden_assets.c', 'src/garden_ui.c',
                          '-o', f'build/host/rm/{label}.exe']
        checked(command, build, f'compile_{label}.log')
        text = checked([str(build / (label + '.exe')), str(ROOT / 'assets/atlas.bin')], build, label + '.log')
        print(text, end='')
        results[label] = {'batch': metrics(text, 'BATCH'), 'renderer': metrics(text, 'RENDER'),
                          'merge_budget': metrics(text, 'MERGE_BUDGET'),
                          'compiler_command': command, 'stdout_sha256': sha(text.encode('utf-8'))}
    before, after = (results[k]['batch'] for k in ('reference', 'candidate'))
    assert before['dense_frames'] == after['dense_frames'] == 104
    assert before['dense_gpu_frames'] == after['dense_gpu_frames'] == 48
    assert before['pixel_digest'] == after['pixel_digest'], 'Old/new full scene pixels differ'
    assert after['dense_gpu_commands'] < before['dense_gpu_commands'], 'Dense command count did not decrease'
    assert after['dense_erase_commands'] < before['dense_erase_commands']
    assert after['dense_erase_pixels'] <= before['dense_erase_pixels']
    for label, commands, pixels in (('reference', 720, 240*83*117), ('candidate', 3, 83*117)):
        budget = results[label]['merge_budget']
        assert budget == dict(max_history=240, comparison_bound=28680,
                              overlap_commands=commands, overlap_pixels=pixels,
                              sparse_commands=240, threshold_guard=1)
    paths = ('src/gpu_driver.c', 'src/gpu_driver.h', 'src/garden_render.c', 'src/garden_render.h',
             'src/garden_assets.c', 'src/garden_assets.h', 'src/garden_ui.c', 'src/garden_ui.h',
             'src/gpu_regs.h', 'src/gpu_alpha_ref.h', 'tools/test_render_batch.c', 'tools/verify_render_batch.py')
    report = dict(
        status='PASS', revision='bounded_in_place_history_merge', host_only=True, board_acceptance=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        geometry=geometry, geometry_compiler_command=geom_command, renderer_results=results,
        dense_commands_before=before['dense_gpu_commands'], dense_commands_after=after['dense_gpu_commands'],
        dense_commands_saved=before['dense_gpu_commands']-after['dense_gpu_commands'],
        dense_command_reduction_percent=100*(1-after['dense_gpu_commands']/before['dense_gpu_commands']),
        dense_erase_commands_before=before['dense_erase_commands'],
        dense_erase_commands_after=after['dense_erase_commands'],
        dense_erase_pixels_before=before['dense_erase_pixels'],
        dense_erase_pixels_after=after['dense_erase_pixels'],
        maximum_history=240, maximum_pair_comparisons=28680, additional_firmware_state_bytes=0,
        exact_same_dense_pixels=True, narrow_width_max=128, narrow_rows_max=64,
        narrow_pixels_max=4096, wide_rows_unchanged=16,
        atlas_bytes=len(atlas), atlas_sha256=sha(atlas),
        reference_sha256={'src/gpu_driver.c': FROZEN_DRIVER_SHA, 'src/garden_render.c': FROZEN_RENDER_SHA},
        erase_reference_path=str(erase_reference),
        source_sha256={p: sha((ROOT / p).read_bytes()) for p in paths},
        checks=[
            'Original row-interval geometry reference vs production fast path, literal bounds and deterministic varied inputs',
            'Overlapping bounding ranges retain exact row-gap/cross-row fallback; end2^32 and overflow ordering preserved',
            'Alpha source/destination lane/range checks and busy waiting occur after geometry and preserve MMIO parameters',
            '128bullet geometry calls:3968reference row checks versus0candidate row checks, all128fast paths exercised',
            'Exact post-clip width policy: <=128 uses min64rows/4096pixels; wider uses original16rows',
            'Same128projectiles with real64heading atlas plus Boss/wings/core/banking/bar/damage on alternating physical FBs',
            'CPU/GPU, Alpha enabled/disabled, clipping, dirty retirement, and original independent full-pixel actor oracle',
            'First/second multi-band GPU faults retain complete dirty rect and erase when the same FB returns',
            'Fixed172344 renderer already uses64row batching; this comparison isolates dirty erase merging only',
            'Clustered birth then dispersed128projectiles: exact full-frame pixels on bothFBs and lower erase command/area totals',
            '240overlapping histories merge to one;240separated histories unchanged; width133 union rejected because it increases Fill bands',
            'In-place prefix compaction has no restart; each pair comparison advances or removes one kept item, <=240*239/2 comparisons',
        ],
        limits=[
            'GPU, display and cache callbacks are host mocks; real FPGA watchdog/arbitration/time remains unverified here.',
            'Command and row-check reductions describe CPU submission work, not measured milliseconds or board FPS.',
            'The original per-command poll, cache invalidation, live Alpha behavior and RTL are unchanged.',
            'Dedicated alpha-panel/key-startup loops retain original16rows; only clipped boxes and sprite_region use the new policy.',
        ],
    )
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
