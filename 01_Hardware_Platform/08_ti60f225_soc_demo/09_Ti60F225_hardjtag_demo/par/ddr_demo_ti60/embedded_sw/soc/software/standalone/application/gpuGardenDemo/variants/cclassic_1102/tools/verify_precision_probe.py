#!/usr/bin/env python3
"""Public reproducible MMIO probe contract using the actual precision atlas.
SPDX-License-Identifier: GPL-3.0-or-later
This is a synchronous host mock, not physical GPU/DDR/cache verification.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    'src/gpu_precision_verify.c', 'src/gpu_precision_verify.h',
    'src/garden_assets.c', 'src/garden_assets.h', 'src/gpu_regs.h',
    'src/gpu_driver.h', 'src/gpu_alpha_ref.h', 'src/garden_core.h',
    'tools/test_precision_probe_contract.c', 'tools/verify_precision_probe.py',
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run_checked(command, log_path):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    output = result.stdout + result.stderr
    log_path.write_text(output, encoding='utf-8')
    if result.returncode:
        print(output, end='')
        raise RuntimeError(f'Command failed with exit {result.returncode}: {command}')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--atlas', type=Path, default=ROOT / 'assets/atlas.bin',
                        help='Actual precision atlas to load, bound to production metadata.')
    args = parser.parse_args()
    # Fixed short output avoids MinGW's 260-character Windows path limit.
    build = ROOT / 'build/host/pp'
    build.mkdir(parents=True, exist_ok=True)
    atlas_path = args.atlas.resolve()
    atlas = atlas_path.read_bytes()
    header = (ROOT / 'src/garden_assets.h').read_text(encoding='utf-8-sig')
    assets_c = (ROOT / 'src/garden_assets.c').read_text(encoding='utf-8-sig')
    expected_bytes = int(re.search(r'#define\s+GARDEN_ATLAS_BYTES\s+(\d+)u', header).group(1))
    expected_crc = int(re.search(r'#define\s+GARDEN_ATLAS_CRC32\s+0x([0-9A-Fa-f]+)u', header).group(1), 16)
    assert len(atlas) == expected_bytes, 'Atlas length differs from production metadata'
    assert zlib.crc32(atlas) & 0xFFFFFFFF == expected_crc, 'Atlas CRC differs from production metadata'
    assert sha(atlas) in assets_c, 'Atlas SHA differs from production metadata'
    exe = build / 'test.exe'
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DGPU_PRECISION_VERIFY_HOST_TEST=1', '-I', 'src',
               'tools/test_precision_probe_contract.c', 'src/gpu_precision_verify.c',
               'src/garden_assets.c', '-o', 'build/host/pp/test.exe']
    run_checked(command, build / 'compile_host.log')
    test_command = [str(exe), str(atlas_path)]
    output = run_checked(test_command, build / 'test_precision_probe_contract.log')
    print(output, end='')
    summary = re.search(r'^SUMMARY (.+)$', output, re.MULTILINE)
    success = re.search(r'^SUCCESS (.+)$', output, re.MULTILINE)
    assert summary and success, 'Missing completed host-contract summary'
    metrics = {name: int(value) for name, value in re.findall(r'(\w+)=(\d+)', summary.group(1))}
    expected = dict(checks=650, successful_cases=40, high_region_sources=20,
                    families=10, faults=5, initial_busy=1, missing_family=1, host_only=1)
    assert metrics == expected, ('Unexpected host coverage', metrics)
    success_values = dict(re.findall(r'(\w+)=(0x[0-9A-Fa-f]+|\d+)', success.group(1)))
    observed = {key: int(value, 0) for key, value in success_values.items()}
    assert observed['cases'] == 40 and observed['sprites'] == 20
    assert 0 < observed['pixels'] <= 2560 and 0 < observed['key_pixels'] < observed['pixels']
    assert observed['min_source'] >= 0x600000 and observed['immutable_FB_panel_atlas'] == 1
    source_hashes = {name: sha((ROOT / name).read_bytes()) for name in SOURCES}
    report = dict(
        status='PASS', host_only=True, physical_gpu_verified=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        compiler_command=command, compiler_working_directory=str(ROOT),
        test_command=test_command, output_directory='build/host/pp',
        metrics=metrics, actual_atlas_path=str(atlas_path),
        actual_atlas_bytes=len(atlas), actual_atlas_sha256=sha(atlas),
        actual_atlas_crc32=f'{expected_crc:08X}',
        opaque_colorkey_cases=20, alpha16_colorkey_cases=20,
        pixels=observed['pixels'], key_pixels=observed['key_pixels'],
        min_source=f"0x{observed['min_source']:08X}",
        max_source=f"0x{observed['max_source']:08X}",
        destination='ASSET_BASE_ADDR + ASSET_SIZE_BYTES - 2048 .. ASSET_BASE_ADDR + ASSET_SIZE_BYTES',
        source_sha256=source_hashes, test_log_sha256=sha(output.encode('utf-8')),
        executable_sha256=sha(exe.read_bytes()),
        checks=[
            '20 distinct real appended fine-angle sources across all 10 type/colour families',
            'Every source beyond the old window end includes ColorKey and visible pixels',
            '40 exact RGB565 keyed opaque32/Alpha16 readback cases and complete scratch guards',
            'Framebuffers, atlas, panel source and display/debug registers remain untouched',
            'Pixel, guard and source faults stop after one command',
            'Busy timeout preserves live ALPHA/CTRL; idle errors restore ALPHA',
            'Initial BUSY leaves scratch and parameters unchanged',
            'A missing family fails instead of silently skipping coverage',
        ],
        limits=[
            'Synchronous host MMIO oracle does not validate real GPU, AXI or cache behavior.',
            'Board startup must separately pass physical source readback and the complete atlas CRC.',
            'Only selected source patches are directly compared; the parent CRC covers the full atlas.',
        ],
    )
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
