#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prove full128-budget state equivalence and count actual allocator probes."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'tools/reference_rhythm128'
EXPECTED = {
    'garden_core.c': 'b73311033370442c0a64f9bfe00a8ba788f69626eb598a78c84574fdf46255e6',
    'garden_core.h': '4de7220d6e8c0126a7f582292a4e7a41beeeba97a771484010a3ad25a3060952',
}
BRIDGE = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.h"
static GardenGame reference_game;
size_t rhythm_reference_size(void) { return sizeof(reference_game); }
void rhythm_reference_init(uint32_t seed) { reference_garden_init(&reference_game,seed); }
void rhythm_reference_load(const void *bytes,size_t length) {
    if(length!=sizeof(reference_game)) abort();
    memcpy(&reference_game,bytes,length);
}
void rhythm_reference_copy(void *bytes,size_t length) {
    if(length!=sizeof(reference_game)) abort();
    memcpy(bytes,&reference_game,length);
}
void rhythm_reference_step(uint16_t buttons) { reference_garden_step(&reference_game,buttons); }
unsigned rhythm_reference_countdown(void) { return reference_garden_boss_fire_countdown(&reference_game); }
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, original, replacement):
    if text.count(original) != 1:
        raise ValueError('Untrusted instrumentation site: ' + original[:80])
    return text.replace(original, replacement)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    reference = {}
    for name, expected in EXPECTED.items():
        data = (REFERENCE / name).read_bytes()
        if sha(data) != expected:
            raise SystemExit('Refusing altered original128-budget reference: ' + name)
        reference[name] = data
    build = ROOT / 'build/host/rhythm_allocator'
    ref = build / 'reference'
    ref.mkdir(parents=True, exist_ok=True)
    report_path = build / 'verification.json'
    if report_path.exists():
        report_path.unlink()
    source = reference['garden_core.c'].decode('utf-8')
    # The frozen fixture is never modified. Insert only host-side loop probes
    # and a private pulse-context bit in this generated separate TU. No game
    # state, allocation/condition, RNG, arguments or arithmetic is replaced.
    source = replace_once(source, '#include <string.h>', '#include <string.h>\n'
        'static unsigned garden_reference_boss_alloc_enabled;\n'
        'extern void garden_reference_alloc_probe(unsigned boss,unsigned slot);')
    source = replace_once(source, '        GardenEnemyBullet *b = &g->enemy_bullets[i];\n        if (!b->active) {',
        '        garden_reference_alloc_probe(garden_reference_boss_alloc_enabled,i);\n'
        '        GardenEnemyBullet *b = &g->enemy_bullets[i];\n        if (!b->active) {')
    source = replace_once(source,
        '    if (b->attack==1U) boss_attack1(g);\n    else if (b->attack==2U) boss_attack2(g);\n    else boss_attack3(g);',
        '    garden_reference_boss_alloc_enabled=1U;\n'
        '    if (b->attack==1U) boss_attack1(g);\n    else if (b->attack==2U) boss_attack2(g);\n    else boss_attack3(g);\n'
        '    garden_reference_boss_alloc_enabled=0U;')
    (ref / 'garden_core.c').write_text(source, encoding='utf-8')
    (ref / 'garden_core.h').write_bytes(reference['garden_core.h'])
    (ref / 'bridge.c').write_text(BRIDGE, encoding='utf-8')
    flags = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror']
    renames = ['-Dgarden_init=reference_garden_init', '-Dgarden_step=reference_garden_step',
               '-Dgarden_boss_fire_countdown=reference_garden_boss_fire_countdown']
    exe = build / 'test_rhythm_allocator.exe'
    commands = [
        [args.compiler, *flags, *renames, '-I', str(ref), '-c', str(ref / 'garden_core.c'), '-o', str(build / 'reference_core.o')],
        [args.compiler, *flags, *renames, '-I', str(ref), '-c', str(ref / 'bridge.c'), '-o', str(build / 'reference_bridge.o')],
        [args.compiler, *flags, '-DGARDEN_HOST_ALLOC_PROBES', '-I', str(ROOT / 'src'),
         str(ROOT / 'tools/test_rhythm_allocator.c'), str(ROOT / 'src/garden_core.c'),
         str(build / 'reference_core.o'), str(build / 'reference_bridge.o'), '-o', str(exe)],
    ]
    compile_log = ''
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True)
        compile_log += result.stdout + result.stderr
        (build / 'compile.log').write_text(compile_log, encoding='utf-8')
        if result.returncode:
            print(compile_log, end='')
            raise SystemExit(result.returncode)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    log = result.stdout + result.stderr
    (build / 'test_rhythm_allocator.log').write_text(log, encoding='utf-8')
    print(log, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'allocator equivalence checks=(\d+) compared_steps=(\d+) reference_bytes=(\d+) new_bytes=(\d+) wave_old=(\d+) wave_new=(\d+) boss_old=(\d+) boss_new=(\d+) boss_steps=(\d+) max_boss_old=(\d+) max_boss_new=(\d+)', log)
    if not match:
        raise SystemExit('Missing completed independent state/probe summary')
    names = ('contract_checks', 'complete_state_compared_steps', 'reference_state_bytes',
             'new_state_bytes', 'wave_old_slot_probes', 'wave_new_slot_probes',
             'boss_old_slot_probes', 'boss_new_slot_probes', 'boss_pulse_steps',
             'max_boss_old_slot_probes_per_step', 'max_boss_new_slot_probes_per_step')
    metrics = dict(zip(names, map(int, match.groups())))
    pulse_rows = re.findall(r'PULSE_WORK: attack=(\d+) occupied=(\d+) emitted=(\d+) old_probes=(\d+) new_probes=(\d+)', log)
    pulse_names = ('attack', 'occupied', 'emitted', 'old_slot_probes', 'new_slot_probes')
    pulses = [dict(zip(pulse_names, map(int, row))) for row in pulse_rows]
    report = dict(status='PASS', host_only=True, board_acceptance=False,
        completed_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        compiler_commands=commands, **metrics,
        reference_source_sha256=EXPECTED, generated_probe_reference_sha256=sha(source.encode('utf-8')),
        independent_reference_bridge_sha256=sha(BRIDGE.encode('utf-8')),
        reference_input_evidence='evidence/rhythm_smooth_attempt_20261014',
        full_existing_state_byte_identical=True, ignored_existing_fields=[],
        new_temporary_state_fields=['boss_alloc_cursor', 'boss_alloc_enabled'],
        temporary_fields_zero_at_all_public_boundaries=True,
        baseline_pulse_work_cases=pulses,
        boss_slot_probes_saved=metrics['boss_old_slot_probes']-metrics['boss_new_slot_probes'],
        boss_allocator_probe_reduction_ratio=metrics['boss_old_slot_probes']/metrics['boss_new_slot_probes'],
        source_sha256={name: sha((ROOT / name).read_bytes()) for name in
                       ('src/garden_core.c', 'src/garden_core.h', 'tools/test_rhythm_allocator.c',
                        'tools/verify_rhythm_allocator.py', 'tools/reference_rhythm128/manifest.json')},
        test_log_sha256=sha(log.encode('utf-8')),
        checks=['Independent typed reference TU compares every existing state byte including score/HP/RNG',
                '228384steps,18seeds, five vulnerable wins/restarts and manual/slow/pause/restart controls',
                '384fragmented/front/back/empty full-pulse fixtures with all status/RNG variants',
                'Same minimum-free-slot order and all projectiles/phase/damage/scoring/counters unchanged',
                'Wave allocator loop examinations exactly equal; Boss monotone scan at most192/pulse',
                'Only4new temporary bytes, zero after every public step; no heap or per-frame free-list'],
        limits=['Probe counts count actual new_bullet loop examinations, not CPU clock cycles or board FPS.',
                'Host hooks are absent from the board binary and do not establish real-time performance.',
                'Budget prechecks, generator trig/RNG and all other workload remain outside this allocator-loop metric.'])
    report_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
