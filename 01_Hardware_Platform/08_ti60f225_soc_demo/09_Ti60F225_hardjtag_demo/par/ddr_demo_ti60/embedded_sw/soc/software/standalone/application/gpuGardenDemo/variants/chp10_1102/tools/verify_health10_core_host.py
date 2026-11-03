#!/usr/bin/env python3
"""Focused actual-core HP10/session contracts. No hardware access or FPGA FPS claim.

SPDX-License-Identifier: GPL-3.0-or-later
Run from this variant: python tools/verify_health10_core_host.py
Evidence: validation/health10/{verification.json,core.log,session.log,damage_trace.csv}.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=None)
    args = parser.parse_args()
    compiler = args.compiler or ('D:/mingw64/bin/gcc.exe' if Path('D:/mingw64/bin/gcc.exe').is_file() else shutil.which('gcc'))
    if not compiler:
        raise SystemExit('Host GCC unavailable; specify --compiler. No board operation attempted.')
    output = ROOT / 'validation/health10'
    output.mkdir(parents=True, exist_ok=True)
    names = ['src/garden_core.c', 'src/garden_core.h', 'src/garden_clear.c',
             'src/garden_session.c', 'src/garden_session.h', 'src/main.c',
             'tools/test_health10_core.c', 'tools/test_session_policy.c',
             'tools/test_ordnance_core.c', 'tools/test_hazard_core.c',
             'tools/verify_health10_core_host.py']
    hashes = {name: sha(ROOT / name) for name in names}
    report = dict(status='RUNNING', host_only=True, board_acceptance=False,
                  source_sha256=hashes, tests=[],
                  rule=dict(initial_hp=10, maximum_hp=10, hit_damage=1,
                            hit_grace_ticks=40, startup_grace_ticks=80,
                            logic_hz=40, gameover_only_at_hp_zero=True,
                            gameover_idle_ms=15000, medical_restore=1),
                  limits=['Actual production core/session with deterministic host fixtures; no serial/JTAG/Flash.',
                          'This entry runs focused health and existing session contracts, not the entire inherited gameplay suite.',
                          'Legacy ordnance/hazard fixtures have HP expectations updated; full suites are not claimed as passed here.',
                          'No new performance, controller-operation, visual or cold-start acceptance is inferred.'])
    try:
        for name, source_names in [
            ('core', ['tools/test_health10_core.c', 'src/garden_clear.c', 'src/garden_session.c']),
            ('session', ['tools/test_session_policy.c', 'src/garden_core.c', 'src/garden_clear.c', 'src/garden_session.c'])]:
            exe = f'{name}.exe' if Path(compiler).suffix.lower() == '.exe' else name
            command = [compiler, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-function', '-I', str(ROOT / 'src'),
                       *[str(ROOT / source) for source in source_names], '-o', exe]
            built = subprocess.run(command, cwd=output, capture_output=True, text=True)
            (output / f'{name}_compile.log').write_text(built.stdout + built.stderr, encoding='utf-8')
            built.check_returncode()
            run = [str(output / exe)]
            if name == 'core':
                run.append(str(output / 'damage_trace.csv'))
            result = subprocess.run(run, cwd=output, capture_output=True, text=True)
            text = result.stdout + result.stderr
            (output / f'{name}.log').write_text(text, encoding='utf-8')
            print(text, end='')
            result.check_returncode()
            test = dict(name=name, status='PASS', compile=command, run=run, log=f'{name}.log')
            if name == 'core':
                test['cases'] = [dict(name=label, assertions=int(count)) for label, count in
                                 re.findall(r'PASS case=(\S+) checks=(\d+)', text)]
                match = re.search(r'PASS health10_core cases=(\d+) checks=(\d+) game_bytes=(\d+)', text)
                if not match or int(match[1]) != 7:
                    raise RuntimeError('Missing completed seven-case health core result')
                test.update(assertions=int(match[2]), game_bytes=int(match[3]), trace='damage_trace.csv')
            else:
                match = re.search(r'PASS session/core: (\d+) assertions', text)
                if not match:
                    raise RuntimeError('Missing completed session result')
                test['assertions'] = int(match[1])
            report['tests'].append(test)
        if hashes != {name: sha(ROOT / name) for name in names}:
            raise RuntimeError('Inputs changed during host checks; rerun one consistent source snapshot')
        elf = ROOT / 'build/gardenStage9Game.elf'
        if elf.is_file():
            report['current_elf_sha256'] = sha(elf)
            report['elf_scope'] = 'Identity only; this host runner does not rebuild or execute the RISC-V ELF.'
        report['damage_trace_sha256'] = sha(output / 'damage_trace.csv')
        report['status'] = 'PASS'
    except Exception as error:
        report.update(status='FAIL', error=str(error))
        raise
    finally:
        (output / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Health10 core/session PASS: {output / "verification.json"}')


if __name__ == '__main__':
    main()
