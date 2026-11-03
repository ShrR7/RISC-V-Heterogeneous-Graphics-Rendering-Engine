#!/usr/bin/env python3
"""Finite production display-target cache contract, not a board FPS test.
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
REFERENCE_SHA = '063bfafc5a898eae973e0ec28f802358498e1b67b645b6abd8311be123a7c385'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--rv-prefix', default='D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-')
    parser.add_argument('--reference-source', type=Path, default=ROOT / 'evidence/motion_trial_20261015_172344/source_snapshot/src/gpu_display.c')
    args = parser.parse_args()
    build = ROOT / 'build/host/dt'
    build.mkdir(parents=True, exist_ok=True)
    sources = {'reference': args.reference_source.read_bytes(), 'candidate': (ROOT / 'src/gpu_display.c').read_bytes()}
    assert sha(sources['reference']) == REFERENCE_SHA, 'Historical reference bytes changed'
    (build / 'display_host.h').write_text(
        '#pragma once\n#include "gpu_regs.h"\nuint32_t host_fb_status(void);\nvoid host_fb_write(uint32_t);\n', encoding='utf-8')
    (build / 'display_bsp.h').write_text(
        '#pragma once\n#include <stdint.h>\n#define BSP_CLINT 0U\nuint32_t clint_getTimeLow(uint32_t);\n', encoding='utf-8')
    commands = []

    def run(command, log):
        commands.append(command)
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        output = result.stdout + result.stderr
        (build / log).write_text(output, encoding='utf-8')
        if result.returncode:
            print(output, end='')
            raise RuntimeError(f'exit {result.returncode}: {command}')
        return output

    results = {}
    for label, data in sources.items():
        # MMIO substitution only: no firmware branch/hook and no lifecycle rewrite.
        source = data.decode('utf-8')
        assert source.count('#include "gpu_regs.h"') == source.count('#include "bsp.h"') == 1
        source = source.replace('#include "gpu_regs.h"', '#include "display_host.h"').replace('#include "bsp.h"', '#include "display_bsp.h"')
        assert source.count('GPU_FB_STATUS') == (3 if label == 'candidate' else 2)
        source = source.replace('GPU_FB_STATUS', 'host_fb_status()')
        source, writes = re.subn(r'GPU_FB_CTRL = (want|active);', r'host_fb_write(\1);', source)
        assert writes == 3 and 'GPU_FB_CTRL' not in source
        host_source = build / (label + '.c')
        host_source.write_text(source, encoding='utf-8')
        run([args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
             '-I', 'src', '-I', 'build/host/dt', f'-DEXPECT_TARGET_CACHE={int(label == "candidate")}',
             'tools/test_display_target.c', str(host_source), '-o', 'build/host/dt/test.exe'], label + '_compile.log')
        output = run([str(build / 'test.exe')], label + '.log')
        print(label + ': ' + output, end='')
        match = re.search(r'^DISPLAY_TARGET (.+)$', output, re.MULTILINE)
        assert match, output
        results[label] = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', match.group(1))}
    assert results['reference']['clean_guard_calls'] == results['candidate']['clean_guard_calls'] == 4000
    assert results['reference']['clean_status_reads'] == 4000 and results['candidate']['clean_status_reads'] == 0
    # Actual RV32 compiler/BSP, identical flags: measure object delta and confirm
    # no extra static RAM. Final linked ELF size remains the SDK build's decision.
    standalone = ROOT.parents[3]
    bsp = standalone.parent.parent / 'bsp/efinix/EfxSapphireSoc'
    common = [args.rv_prefix + 'gcc.exe', '-std=gnu99', '-Os', '-march=rv32im_zicsr_zifencei',
              '-mabi=ilp32', '-DUSE_GP', '-fcommon', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
              '-Wno-comment', '-Wno-unused-but-set-variable', '-I', str(ROOT / 'src'), '-I', str(standalone / 'include'),
              '-I', str(standalone / 'driver'), '-I', str(bsp / 'include'), '-I', str(bsp / 'app')]
    sizes = {}
    for label, data in sources.items():
        exact = build / (label + '_rv.c')
        exact.write_bytes(data)
        obj = build / (label + '.o')
        run(common + ['-c', str(exact), '-o', str(obj)], label + '_rv_compile.log')
        output = run([args.rv_prefix + 'size.exe', str(obj)], label + '_rv_size.log')
        numbers = re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+', output, re.MULTILINE)
        assert numbers, output
        sizes[label] = dict(zip(('text', 'data', 'bss'), map(int, numbers.groups())))
    assert sizes['candidate']['data'] == sizes['reference']['data']
    assert sizes['candidate']['bss'] == sizes['reference']['bss']
    report = dict(status='PASS', host_only=True, board_acceptance=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        source_sha256={p: sha((ROOT / p).read_bytes()) for p in
                       ('src/gpu_display.c', 'src/gpu_display.h', 'src/gpu_regs.h',
                        'src/gpu_driver.h', 'tools/test_display_target.c', 'tools/verify_display_target.py')},
        reference_sha256=REFERENCE_SHA, results=results, rv32_object_sizes=sizes,
        rv32_text_delta=sizes['candidate']['text']-sizes['reference']['text'],
        additional_firmware_state_bytes=0, command_lines=commands,
        checks=['Two active FB selections, exact range boundaries, live calls outside frames',
                'Single atomic active/pending snapshot, pending startup fallback and pending completion',
                '4000 guard calls: 4000 reference status reads, zero candidate reads inside stable frames',
                'Request/probe invalidate before MMIO, no-op invalidation, both probe success branches and restore',
                'Init clears cache; successful end, idle failure and swap timeout return to live checks',
                'Unchanged stats, frame timing and default public state-read semantics',
                'Actual RV32 object has unchanged data and bss; no firmware test-hook branches'],
        limits=['This model substitutes MMIO/timer/idle only; it does not measure FPGA speed or FPS.',
                'No external writer may mutate FB_CTRL directly during a cached frame.',
                'Pending-start frames retain prior live checks; they do not gain a new atomic rendering guarantee.',
                'Final linked text/RAM is subject to the complete SDK build.'])
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'rv32_object_sizes': sizes,
                      'rv32_text_delta': report['rv32_text_delta'], 'additional_state_bytes': 0}))


if __name__ == '__main__':
    main()
