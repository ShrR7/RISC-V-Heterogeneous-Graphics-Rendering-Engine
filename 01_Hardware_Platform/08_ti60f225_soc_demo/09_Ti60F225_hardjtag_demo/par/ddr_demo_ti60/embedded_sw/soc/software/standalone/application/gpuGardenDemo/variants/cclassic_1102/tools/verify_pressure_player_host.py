#!/usr/bin/env python3
"""Reproduce pressure-player/fan/union host checks; never access a board.

SPDX-License-Identifier: GPL-3.0-or-later
Run: python tools/verify_pressure_player_host.py [--compiler path/to/gcc]
Evidence: build/host/pressure_player/{verification.json,*.log,executables}.
GPU/display/input are mocks. Host model counts do not establish FPGA FPS.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def function_body(source, name):
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^)]*\)\s*\{', source)
    if not match:
        raise ValueError(f'Cannot extract production function {name}')
    start = match.end() - 1
    depth = 1
    pos = start + 1
    while depth and pos < len(source):
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    if depth:
        raise ValueError(f'Unbalanced production function {name}')
    return source[start:pos]


def prepare_storage_header(output):
    source = (ROOT / 'src/pure_bullet_test.c').read_text(encoding='utf-8')
    match = re.search(r'typedef struct \{\s*uint32_t before\[4\];.*?\} PureStorage;', source, re.S)
    if not match:
        raise ValueError('Cannot extract actual production PureStorage')
    assertions = source[match.end():source.index('#define STORAGE', match.end())]
    calls = re.findall(r'if\(three\)triple_boss_init\(STORAGE->triple_particles,.*?'
                       r'else\s*\{rebuild_plan\(count\)', source)
    if len(calls) != 2 or 'triple_boss_build(STORAGE->triple_particles,' not in source:
        raise ValueError('B must use triple_particles and rebuild T plans only in both else branches')
    if source.count('rebuild_plan(count)') != 2:
        raise ValueError('Unexpected extra plan rebuild call: review B/T ownership')
    body = function_body(source, 'rebuild_plan').replace('PLANS', 'plans')
    header = ('/* Extracted verbatim production layout/assertions; not a parallel model. */\n'
              '#pragma once\n#include "triple_boss.h"\n#include "gpu_driver.h"\n'
              '#include "g9_memory.h"\n' + match[0] + '\n' + assertions + '\n'
              'static void pressure_actual_rebuild(PureBulletPlan *plans,unsigned count)\n' + body + '\n')
    path = output / 'pressure_storage_actual.h'
    path.write_text(header, encoding='utf-8')
    return path


def check_scene_identity():
    source = (ROOT / 'src/triple_boss.c').read_text(encoding='utf-8')
    identity = {}
    for name, expected in [('origin_x', [320, 180, 460]),
                           ('origin_y', [79, 118, 119]), ('boss_y', [40, 80, 80])]:
        match = re.search(r'\b' + name + r'\[3\]\s*=\s*\{([^}]+)\}', source)
        if not match:
            raise ValueError(f'Missing production {name}')
        value = [int(item.strip()) for item in match[1].split(',')]
        if value != expected:
            raise ValueError(f'{name} changed to {value}; review and update the scene contract')
        identity[name] = value
    if 'rays+30U+(ray+1U)/3U' not in re.sub(r'\s+', '', source):
        raise ValueError('Expected production60..120degree ray mapping changed')
    assets = (ROOT / 'src/garden_assets.c').read_text(encoding='utf-8')
    player = re.search(r'\{\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(-?\d+),\s*(-?\d+)\s*\},'
                       r'\s*/\*\s*0:\s*PLAYER\s*\*/', assets)
    if not player or list(map(int, player.groups())) != [0, 40, 36, 20, 18]:
        raise ValueError('Player atlas offset/dimensions/pivot changed; review the actual-art oracle')
    identity.update(fan_degrees=[60, 120], player_sprite=dict(offset=0, width=40, height=36,
                    anchor_x=20, anchor_y=18), speed_q8=612, tile_size=[16, 16])
    return identity


def run_logged(command, output, name):
    result = subprocess.run(command, cwd=output, capture_output=True, text=True)
    log = result.stdout + result.stderr
    (output / name).write_text(log, encoding='utf-8')
    if result.returncode:
        print(log, end='')
        raise RuntimeError(f'{name} failed with exit code {result.returncode}')
    return log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='Host GCC; defaults to installed GCC or repository workstation toolchain')
    parser.add_argument('--riscv-compiler', help='Optional SDK compiler for independent RV32IM object size')
    args = parser.parse_args()
    workstation_gcc = Path('D:/mingw64/bin/gcc.exe')
    compiler = args.compiler or (str(workstation_gcc) if workstation_gcc.is_file() else shutil.which('gcc'))
    if not compiler:
        raise SystemExit('Host GCC unavailable; pass --compiler. No board access was attempted.')
    output = ROOT / 'build/host/pressure_player'
    output.mkdir(parents=True, exist_ok=True)
    bound = [ROOT / 'src' / name for name in (
        'pressure_player.c', 'pressure_player.h', 'triple_boss.c', 'triple_boss.h',
        'pure_bullet_test.c', 'pure_bullet_test.h', 'pure_bullet_scatter16.h',
        'garden_assets.c', 'garden_assets.h', 'garden_core.h', 'gpu_driver.h',
        'gpu_display.h', 'pad_hw.h', 'g9_memory.h')]
    bound += [ROOT / 'tools' / name for name in ('test_pressure_player_host.c',
              'test_pressure_player_fan_host.c', 'test_pressure_storage_host.c')]
    bound += [Path(__file__), ROOT / 'assets/atlas.bin', ROOT / 'assets/pure_scatter16.bin']
    hashes = {path.relative_to(ROOT).as_posix(): digest(path) for path in bound}
    report = dict(status='RUNNING', host_only=True, board_acceptance=False,
                  utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  source_sha256=hashes, tests=[], limits=[
                      'Host GPU/display/input mocks; no AXI/DDR/HDMI board validation or FPGA FPS measurement.',
                      'Per-phase object counts are model bounds, not hardware throughput.',
                      'Independent object size is not final linked firmware size or timing.',
                      '3003 objects can exceed the separate2048 Stage9 descriptor limit; that bounded rejection is recorded.'])
    try:
        report['scene'] = check_scene_identity()
        generated = prepare_storage_header(output)
        report['extracted_storage_sha256'] = digest(generated)
        report['compiler_version'] = run_logged([compiler, '--version'], output, 'compiler.log').splitlines()[0]
        common = [compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-Wno-misleading-indentation', '-DPURE_BULLET_HOST_TEST',
                  '-I', str(ROOT / 'src'), '-I', str(output)]
        atlas = str(ROOT / 'assets/atlas.bin')
        scatter = str(ROOT / 'assets/pure_scatter16.bin')
        cases = [
            ('player', 'test_pressure_player_host.c', ['pressure_player.c'], [atlas]),
            ('fan', 'test_pressure_player_fan_host.c', ['pressure_player.c', 'triple_boss.c'], [scatter, atlas]),
            ('storage', 'test_pressure_storage_host.c', ['pure_bullet_test.c', 'triple_boss.c'], [scatter]),
        ]
        for name, fixture, source_names, arguments in cases:
            exe = f'test_{name}.exe' if Path(compiler).suffix.lower() == '.exe' else f'test_{name}'
            command = common + [str(ROOT / 'tools' / fixture)]
            command += [str(ROOT / 'src' / source) for source in source_names]
            command += ['-o', exe]
            run_logged(command, output, f'{name}_compile.log')
            run = [str(output / exe), *arguments]
            text = run_logged(run, output, f'{name}.log')
            print(text, end='')
            if 'PASS ' not in text:
                raise RuntimeError(f'{name} log has no completed contract')
            result = dict(name=name, status='PASS', compile=command, run=run, log=f'{name}.log')
            if name == 'player':
                result['assertions'] = int(re.search(r'player (\d+) assertions', text)[1])
            elif name == 'fan':
                result['assertions'] = int(re.search(r'order: (\d+) total assertions', text)[1])
                result['rgb565_frames'] = 320
                result['model_generations'] = [dict(zip(('objects', 'frames', 'stage9_max', 'gpu_max'), map(int, row)))
                    for row in re.findall(r'FAN count=(\d+) frames=(\d+) s9max=(\d+) gpumax=(\d+)', text)]
            else:
                result['assertions'] = int(re.search(r'checks=(\d+)', text)[1])
                result['capacity'] = [dict(zip(('objects', 'frames', 'return_code', 'stage9_max', 'gpu_max'), map(int, row)))
                    for row in re.findall(r'CAPACITY count=(\d+) frames=(\d+) rc=(-?\d+) S9max=(\d+) GPUmax=(\d+)', text)]
            report['tests'].append(result)

        sdk = Path(args.riscv_compiler) if args.riscv_compiler else Path('D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gcc.exe')
        if sdk.is_file():
            command = [str(sdk), '-std=gnu99', '-march=rv32im', '-mabi=ilp32', '-Os',
                       '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), '-c',
                       str(ROOT / 'src/pressure_player.c'), '-o', 'pressure_player.rv32.o']
            run_logged(command, output, 'rv32_compile.log')
            size = sdk.with_name(sdk.name.replace('gcc', 'size'))
            text = run_logged([str(size), 'pressure_player.rv32.o'], output, 'rv32_size.log')
            numbers = re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+', text, re.M)
            if not numbers:
                raise RuntimeError('Cannot parse independent RV32IM size')
            report['independent_object'] = dict(zip(('text', 'data', 'bss', 'total'), map(int, numbers.groups())))
            report['independent_object']['compile'] = command
            print(text, end='')
        else:
            report['independent_object'] = dict(status='NOT_RUN', reason='SDK compiler unavailable on this host')
        if hashes != {path.relative_to(ROOT).as_posix(): digest(path) for path in bound}:
            raise RuntimeError('Inputs changed during verification; rerun against one consistent snapshot')
        report['status'] = 'PASS'
        report['contracts'] = [
            'Actual40x36 player atlas pixels, transparent holes and exact two-buffer RGB565 background restoration.',
            'Restore before bottom clear, legacy bullets before player, then Stage9;320 complete current-frame pixel comparisons.',
            'Independent pilot state, held input/neutral/slow/diagonal/bounds, oversize and copy/blit error recovery.',
            'Actual production60degree fan, sources79/118/119, five4096phase generations, natural-edge bounds and capacity guards.',
            'Production union and static assertions extracted directly; legacy offset22000 and3003/2730 branch ownership.',
            'After B borrows T plan bytes, actual T plan reconstruction matches clean2093particle state and256 draw/update frames.',
            '3003 Stage9 overflow remains an explicit bounded-capacity observation, not a board pass.']
    except Exception as error:
        report.update(status='FAIL', error=str(error))
        raise
    finally:
        (output / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Host verification PASS: {output / "verification.json"}')


if __name__ == '__main__':
    main()
