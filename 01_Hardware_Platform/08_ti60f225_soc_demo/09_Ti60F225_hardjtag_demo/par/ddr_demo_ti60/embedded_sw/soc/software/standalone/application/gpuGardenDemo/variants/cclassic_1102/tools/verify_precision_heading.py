#!/usr/bin/env python3
"""Production nearest64 heading and precision FX host audit (no hardware access).
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import re
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FROZEN_PREFIX_BYTES = 236954
PREFIX_SPRITES = 280
ATLAS_WINDOW_BYTES = 0xBC000
ATLAS_ADDRESS = 0x5C4000
ATLAS_END = 0x680000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return path.read_text(encoding='utf-8-sig')


def function_body(source, name):
    start = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    assert start, name
    begin = source.index('{', start.start())
    depth = 0
    for pos in range(begin, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if depth == 0:
            return source[begin:pos + 1]
    raise AssertionError('Unclosed function ' + name)


def checked_command(command, log):
    result = subprocess.run(command, capture_output=True, text=True)
    output = result.stdout + result.stderr
    log.write_text(output, encoding='utf-8')
    if result.returncode:
        print(output, end='')
        raise RuntimeError(f'Command exit {result.returncode}: {command}')
    return output


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    ap.add_argument('--riscv-compiler', default='D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gcc.exe')
    ap.add_argument('--reference-root', type=Path, default=ROOT.parent / 'neon_animation_20261015')
    ap.add_argument('--output-dir', type=Path, default=ROOT / 'build/host/precision_heading')
    args = ap.parse_args()
    build = args.output_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    reference = args.reference_root.resolve()
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    old_atlas = (reference / 'assets/atlas.bin').read_bytes()
    m = json.loads(read(ROOT / 'assets/manifest.json'))
    old_m = json.loads(read(reference / 'assets/manifest.json'))
    assert len(old_atlas) == FROZEN_PREFIX_BYTES
    assert len(old_m['sprites']) == PREFIX_SPRITES
    assert atlas[:FROZEN_PREFIX_BYTES] == old_atlas, 'Original atlas bytes changed'
    assert len(atlas) == m['bytes'] and sha(atlas) == m['sha256']
    assert len(m['sprites']) == 840, 'Expected original280+fine560 descriptors'
    assert len(atlas) <= ATLAS_WINDOW_BYTES and ATLAS_ADDRESS + len(atlas) <= ATLAS_END
    assert int(m['address'], 0) == ATLAS_ADDRESS
    fields = ('id', 'name', 'offset', 'width', 'height', 'stride', 'anchor_x', 'anchor_y', 'bytes')
    for old, new in zip(old_m['sprites'], m['sprites'][:PREFIX_SPRITES]):
        assert all(old[f] == new[f] for f in fields), (old['id'], 'Old descriptor changed')
    end = 0
    for index, sprite in enumerate(m['sprites']):
        assert sprite['id'] == index
        assert sprite['width'] > 0 and sprite['height'] > 0
        assert sprite['stride'] == sprite['width'] * 2
        assert sprite['bytes'] == sprite['height'] * sprite['stride']
        assert sprite['offset'] % 4 == 0 and sprite['offset'] >= end
        end = sprite['offset'] + sprite['bytes']
        assert end <= len(atlas)
    assert end == len(atlas), 'Trailing stale atlas bytes'
    c = read(ROOT / 'src/garden_assets.c')
    rows = re.findall(r'\{(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(-?\d+),\s*(-?\d+)\}', c)
    assert len(rows) == 840
    for sprite, row in zip(m['sprites'], rows):
        assert tuple(map(int, row)) == tuple(sprite[k] for k in ('offset', 'width', 'height', 'stride', 'anchor_x', 'anchor_y'))
    header = read(ROOT / 'src/garden_assets.h')
    assert int(re.search(r'#define\s+GARDEN_ATLAS_BYTES\s+(\d+)u', header).group(1)) == len(atlas)
    crc = zlib.crc32(atlas) & 0xFFFFFFFF
    assert int(re.search(r'#define\s+GARDEN_ATLAS_CRC32\s+0x([0-9A-Fa-f]+)u', header).group(1), 16) == crc
    assert sha(atlas) in c, 'Exported atlas SHA does not match loaded bytes'
    for name in ('garden_core.c', 'garden_core.h'):
        assert (ROOT / 'src' / name).read_bytes() == (reference / 'src' / name).read_bytes(), name
    old_fx = read(reference / 'src/garden_fx.c')
    fx = read(ROOT / 'src/garden_fx.c')
    assert function_body(old_fx, 'garden_fx_bullet_heading') == function_body(fx, 'garden_fx_bullet_heading')
    assert 'heading64_from_velocity(b->vx,b->vy,b->cord_angle)' in fx
    assert 'garden_bullet_precision_id(b->type,b->colour' in fx
    sources = ('src/garden_heading_precision.c', 'src/garden_heading_precision.h',
               'src/garden_fx.c', 'src/garden_fx.h', 'src/garden_assets.c', 'src/garden_assets.h',
               'src/garden_core.c', 'src/garden_core.h', 'src/garden_render.h',
               'tools/test_precision_heading.c', 'tools/verify_precision_heading.py')
    exe = build / 'test.exe'
    host_cmd = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                str(ROOT / 'tools/test_precision_heading.c'),
                str(ROOT / 'src/garden_heading_precision.c'), str(ROOT / 'src/garden_fx.c'),
                str(ROOT / 'src/garden_assets.c'), str(ROOT / 'src/garden_core.c'), '-lm', '-o', str(exe)]
    checked_command(host_cmd, build / 'compile_host.log')
    output = checked_command([str(exe)], build / 'test_precision_heading.log')
    print(output, end='')
    summary = re.search(r'^SUMMARY (.+)$', output, re.MULTILINE)
    assert summary, 'Host tests did not finish'
    metrics = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', summary.group(1))}
    math_result = re.search(r'worst32_deg=([0-9.]+) worst64_deg=([0-9.]+) Q30_epsilon_deg=([0-9.eE+-]+) near_boundary32=(\d+) near_boundary64=(\d+)', output)
    assert math_result and metrics['real_steps'] == 12000 and metrics['fixture_draws'] == 1280
    assembly = build / 'heading_rv32.s'
    object_file = build / 'heading_rv32.o'
    rv_common = [args.riscv_compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-march=rv32imac', '-mabi=ilp32']
    rv_assembly_cmd = rv_common + ['-S', str(ROOT / 'src/garden_heading_precision.c'), '-o', str(assembly)]
    rv_object_cmd = rv_common + ['-c', str(ROOT / 'src/garden_heading_precision.c'), '-o', str(object_file)]
    checked_command(rv_assembly_cmd, build / 'compile_rv32_assembly.log')
    checked_command(rv_object_cmd, build / 'compile_rv32_object.log')
    asm = read(assembly)
    assert not re.search(r'^\s*(?:divu?|remu?|f(?:add|sub|mul|div|sqrt|cvt|mv|eq|lt|le|class|sgnj)[.\w]*)\s', asm, re.MULTILINE), 'Divide/remainder or floating-point opcode in RV32 heading'
    local_labels = set(re.findall(r'^([A-Za-z_]\w*):', asm, re.MULTILINE))
    call_targets = re.findall(r'^\s*(?:call|tail)\s+([A-Za-z_]\w*)', asm, re.MULTILINE)
    assert all(target in local_labels for target in call_targets), 'External helper/call in heading'
    assert not re.search(r'__(?:[u]?div|[u]?mod|add[df]|sub[df]|mul[df]|fix|float)', asm), 'Arithmetic helper in heading'
    nm = Path(args.riscv_compiler).with_name('riscv-none-elf-nm.exe')
    undefined = checked_command([str(nm), '--undefined-only', str(object_file)], build / 'rv32_undefined_symbols.log')
    assert not undefined.strip(), 'RV32 heading object requires runtime helpers'
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'directions': 64, 'retained_optional_math_directions': 32,
        'quantization_step_degrees': 5.625, 'nominal_half_bin_degrees': 2.8125,
        'Q30_boundary_epsilon_degrees': float(math_result.group(3)),
        'measured_worst32_degrees': float(math_result.group(1)), 'measured_worst64_degrees': float(math_result.group(2)),
        'oracle_disagreements_within_declared_epsilon': {'32': int(math_result.group(4)), '64': int(math_result.group(5))},
        'metrics': metrics, 'host_compiler_command': host_cmd,
        'rv32': {'assembly_command': rv_assembly_cmd, 'object_command': rv_object_cmd,
                 'assembly_sha256': sha(assembly.read_bytes()), 'object_sha256': sha(object_file.read_bytes()),
                 'no_division_or_remainder': True, 'no_float_instructions': True,
                 'no_external_calls_or_undefined_helpers': True, 'internal_call_targets': call_targets,
                 'maximum_threshold_comparisons': 4, 'candidate_predicate_threshold_comparisons_max': 2, 'maximum_product_or_scaled_magnitude': '2^61'},
        'atlas': {'bytes': len(atlas), 'sha256': sha(atlas), 'crc32': f'0x{crc:08X}',
                  'count': 840, 'append_count': 560, 'old_enemy_ids_reused': 80,
                  'prefix_bytes': FROZEN_PREFIX_BYTES, 'prefix_sha256': sha(old_atlas),
                  'old280_metadata_preserved': True, 'all_C_descriptors_match_manifest': True,
                  'address': f'0x{ATLAS_ADDRESS:08X}', 'window_bytes': ATLAS_WINDOW_BYTES,
                  'end_exclusive': f'0x{ATLAS_ADDRESS + len(atlas):08X}'},
        'core_source_byte_identical': True, 'legacy8_helper_byte_identical': True,
        'checks': ['Binary search equals frozen original linear scan for every tested vector, including exact Q30 equalities and neighbors in all8octants; predicate accepts exactly one of64 candidates and all stationary fallback ties.',
                   'Independent atan2 oracle: axes, literal32/64 rays, integer boundary neighbors, signed32 extremes, dense velocity grid,2million seeded random vectors and full16-bit stationary fallback wrapping.',
                   'Actual production FX: all640 colour/type/direction combinations retain raw position, alpha32, one sprite per projectile, cord independence and pause stability.',
                   'Real12000-step core simulation: velocity-aligned sprites, independent fine IDs, natural rotating Diamonds, pauses/restarts and complete byte-for-byte state equality.',
                   'Every capacity0..250 in both Alpha modes:234 essential full pool,2ghosts+2rings decor admission, panel reservation, invalid/null guards and leading/tail canaries.',
                   'Frozen original236954 atlas bytes and280 descriptor geometries retained; all840 generated C descriptors bounded, aligned and matched to loaded atlas manifest.',
                   'Exact RV32IMAC compiler assembly/object: no floating-point, divide/remainder, helper calls or undefined symbols.'],
        'source_sha256': {name: sha((ROOT / name).read_bytes()) for name in sources},
        'reference_sha256': {name: sha((reference / name).read_bytes()) for name in ('src/garden_core.c', 'src/garden_core.h', 'src/garden_fx.c', 'assets/atlas.bin', 'assets/manifest.json')},
        'stdout_sha256': sha(output.encode()),
        'limits': ['Finite Q30 thresholds permit at most2.668043e-8degrees beyond the ideal2.8125degree half-bin; raster artwork angle is not measured by this math oracle.',
                   'A stationary projectile has no movement direction and uses its stable cord_angle nearest64 fallback.',
                   'This is a host/RV32 compilation contract audit. No production GPU/MMIO renderer, DDR hardware transfer, FPS or physical display acceptance is measured.',
                   'The reserved0xBC000 atlas window is checked numerically here; expanded DDR protection/scratch/board loading is separately owned by the parent task.']}
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS precision host report: {build / "verification.json"}')


if __name__ == '__main__':
    main()
