#!/usr/bin/env python3
"""Real production-renderer old/new modal RGB565 and Fill submission audit.

Host RAM/GPU mocks only. These command counts do not establish board FPS.
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
REFERENCE_STAGE_SHA = '5f62f0bc1d689a20107e9a40dec4e838922ced4c2589c9cf54128e3a3e3ab890'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked(command, build, name):
    run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    output = run.stdout + run.stderr
    (build / name).write_text(output, encoding='utf-8')
    print(output, end='')
    if run.returncode:
        raise SystemExit(run.returncode)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    baseline = json.loads((ROOT / 'settlement_baseline.json').read_text(encoding='utf-8-sig'))
    reference = (ROOT / 'tools/reference_settlement/garden_stage.c').read_bytes()
    assert sha(reference) == REFERENCE_STAGE_SHA == baseline['reference_stage_sha256'].lower()
    actual_changed = []
    for name, expected in baseline['runtime_source_sha256'].items():
        if sha((ROOT / name).read_bytes()) != expected.lower():
            actual_changed.append(name)
    assert actual_changed == ['src/garden_stage.c'], actual_changed
    build = ROOT / 'build/host/settlement_fill'
    build.mkdir(parents=True, exist_ok=True)
    (build / 'reference_stage.c').write_bytes(reference)
    (build / 'vexriscv.h').write_text('#pragma once\nvoid data_cache_invalidate_all(void);\n', encoding='utf-8')
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    assert len(atlas) == 631954 and sha(atlas) == '1b1fa155cc7db9fbfd72a76ae74fd8fdeffb43b4b3321a42aa594aeeabfef076'
    results = {}
    for label, stage, mode in (
        ('reference', 'build/host/settlement_fill/reference_stage.c', 'write'),
        ('candidate', 'src/garden_stage.c', 'compare'),
    ):
        command = [args.compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-DGARDEN_RENDER_HOST_TEST', '-I', 'build/host/settlement_fill', '-I', 'src',
                   'tools/test_settlement_fill.c', 'src/garden_render.c', 'src/garden_assets.c',
                   'src/garden_ui.c', stage, '-o', f'build/host/settlement_fill/{label}.exe']
        checked(command, build, f'compile_{label}.log')
        output = checked([str(build / f'{label}.exe'), str(ROOT / 'assets/atlas.bin'), mode,
                          str(build / 'reference_snapshots.rgb565')], build, f'{label}.log')
        first = [{k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
                 for line in re.findall(r'^FIRST_MODAL (.+)$', output, re.MULTILINE)]
        marker = re.search(r'^SETTLEMENT_FILL (.+)$', output, re.MULTILINE)
        assert marker and len(first) == 2
        metrics = {k: int(v, 0) for k, v in re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', marker.group(1))}
        results[label] = dict(compiler_command=command, first_modal=first, metrics=metrics,
                             output_sha256=sha(output.encode('utf-8')))
    old, new = results['reference'], results['candidate']
    assert old['metrics']['snapshots'] == new['metrics']['snapshots'] == 22
    assert old['metrics']['digest'] == new['metrics']['digest']
    for before, after in zip(old['first_modal'], new['first_modal']):
        assert before['fb'] == after['fb']
        assert before['blit_commands'] == after['blit_commands']
        assert before['blit_pixels'] == after['blit_pixels']
        assert after['fill_commands'] < before['fill_commands']
        assert after['fill_pixels'] < before['fill_pixels']
        # The old complete rectangle included189504 pixels later hidden by
        # the inner panel. Four visible strips retain the exact final image.
        assert before['fill_pixels'] - after['fill_pixels'] == 504 * 376
    snapshot_bytes = (build / 'reference_snapshots.rgb565').stat().st_size
    assert snapshot_bytes == 22 * 1280 * 720 * 2
    paths = ('src/garden_stage.c', 'src/garden_stage.h', 'src/garden_render.c', 'src/garden_render.h',
             'src/garden_ui.c', 'src/garden_ui.h', 'src/garden_assets.c', 'src/garden_assets.h',
             'tools/test_settlement_fill.c', 'tools/test_render_batch.c', 'tools/verify_settlement_fill.py')
    report = dict(
        status='PASS', host_only=True, board_acceptance=False, variant='neon_settle_20261016',
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        changed_runtime_files=actual_changed, reference_stage_sha256=REFERENCE_STAGE_SHA,
        results=results, snapshots=22, exact_full_frame_memcmp=True,
        compared_rgb565_pixels=22 * 1280 * 720,
        reference_snapshot_bytes=snapshot_bytes,
        fill_commands_saved_per_first_card=old['first_modal'][0]['fill_commands'] - new['first_modal'][0]['fill_commands'],
        fill_pixels_saved_per_first_card=504 * 376,
        source_sha256={p: sha((ROOT / p).read_bytes()) for p in paths},
        atlas_sha256=sha(atlas),
        contracts=[
            'Compile old fixed stage and new stage separately against identical real production renderer/UI/atlas.',
            'Record actual production gpu_fill callbacks after width-dependent band splitting, not primitive-call estimates.',
            'Whole1280x720 RGB565 snapshots use byte-for-byte comparison, not digest-only equality.',
            'Two independent physicalFBs: last death frame, first modal, score rolling, true late pickup credit, final score and delayed prompt.',
            'Same-tick/same-FB stage calls submit no work; both previous cards clear before fresh restart scene objects.',
            'All560 precision assets, gameplay, engineering sidebars, score and stage timing remain unchanged.',
        ],
        limits=[
            'GPU/display/cache callbacks are host mocks; real command latency, AXI arbitration and FPS require the separately identified boardA/B.',
            'Pixel rectangle totals are submission areas, not measured AXI bytes or timing speedup.',
            'No hardware, reset, FPGA, Flash or600secondtest is performed by this tool.',
        ],
    )
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
