#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Same-tick effective-damage label contracts using production host modules.

Host-only: does not access UART, JTAG, GPU MMIO or FPGA configuration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PARENT = ROOT.parent / 'neon_wave_patrol_20261022'
SOURCES = ('garden_combat.c', 'garden_scene.c', 'garden_feedback.c',
           'garden_fx.c', 'garden_core.c', 'garden_assets.c', 'garden_actor_ui.c',
           'garden_ordnance_view.c', 'garden_heading_precision.c')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/damage_sum'
    build.mkdir(parents=True, exist_ok=True)
    runtime = sorted(p for p in (ROOT / 'src').iterdir() if p.suffix in ('.c', '.h'))
    changed = [p.name for p in runtime if p.read_bytes() != (PARENT / 'src' / p.name).read_bytes()]
    assert changed == ['garden_combat.c', 'garden_combat.h'], changed
    assert (ROOT / 'assets/atlas.bin').read_bytes() == (PARENT / 'assets/atlas.bin').read_bytes()
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_damage_sum.c'),
               *[str(ROOT / 'src' / name) for name in SOURCES],
               '-o', str(build / 'test_damage_sum.exe')]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (build / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    if compiled.returncode:
        print(compiled.stdout + compiled.stderr, end='')
        raise SystemExit(compiled.returncode)
    result = subprocess.run([str(build / 'test_damage_sum.exe')], capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'test.log').write_text(output, encoding='utf-8')
    print(output, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'PASS damage_sum checks=(\d+) real_batches=(\d+) stream_steps=(\d+) popup_bytes=(\d+) combat_bytes=(\d+) pool=(\d+)', output)
    assert match, 'No completed production damage-sum contract summary'
    checks, real_batches, stream_steps, popup_bytes, combat_bytes, pool = map(int, match.groups())
    assert real_batches == 7 and stream_steps == 24000 and pool == 33
    bound = runtime + [ROOT / 'tools/test_damage_sum.c', ROOT / 'tools/verify_damage_sum.py',
                       ROOT / 'assets/atlas.bin']
    report = {
        'status': 'PASS', 'host_only': True, 'hardware_access': False,
        'board_acceptance': False, 'checks': checks,
        'real_production_hit_batches': real_batches,
        'natural_full_game_snapshot_steps': stream_steps,
        'popup_bytes': popup_bytes, 'combat_bytes': combat_bytes, 'pool': pool,
        'changed_runtime_files': changed, 'game_rules_changed': False,
        'core_unchanged': True, 'main_unchanged': True, 'atlas_unchanged': True,
        'core_sha256': sha(ROOT / 'src/garden_core.c'),
        'parent_core_sha256': sha(PARENT / 'src/garden_core.c'),
        'parent_variant': PARENT.name,
        'popup_policy': 'one effective-damage sum per target slot/type per consumed40Hz logic tick; later ticks remain independent',
        'simultaneous_examples': ['95+95 ->190', '95+5 lethal ->100', '600blast+95shot ->695'],
        'compiler_command': command,
        'source_sha256': {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p) for p in bound},
        'parent_runtime_sha256': {p.name: sha(PARENT / 'src' / p.name) for p in runtime},
        'test_log_sha256': sha(build / 'test.log'),
        'contracts': [
            'Actual public garden_step emits two consumed95shot events for DARTER, STOPPER and Boss; one190label and two original hit points',
            'Actual remaining100HP clips second hit to5; summed100label excludes20/80/1000kill rewards',
            'Subsequent logic tick spawns a separate95label while prior190continues original animation age',
            'Actual bomb600and bullet95same target/tick sum695without changing HP, original hit score or bomb damage ledger',
            'Interleaved enemy slots and Boss stay isolated; different target type cannot reuse the same group',
            'Malformed zero/slot/boss/type/kill/reward/score records rejected before sum;255count bounded to33stored events',
            'Full33popup pool with33interleaved hits creates9groups and24merges; each new group retains its identity under oldest replacement andUINT32sequence wrap',
            '70000observed damage remains exact while32767display cap counts37233omitted units once',
            'Pause and same-tick observer are bytewise idempotent; disable consumes without replay; epoch/restart clear;24tick600ms expiry unchanged',
            'Combined190value chooses one target-centred head anchor, rises and fades monotonically without relocating or refreshing birth identity',
            '24000natural same-seed core snapshots across8seeds with pause/restart remain byte-identical after all production observers and builds',
            'Every natural accepted event contributes exactly one popup birth or merge; event, HP and score accounting remain per hit',
            'Every runtime source except combat implementation and contract documentation byte-identical to preserved patrol parent; atlas and main unchanged',
        ],
        'limits': [
            'Host-only production observer/core contracts do not prove FPGA pixels, GPU/DDR timing, hardware FPS or display counters.',
            'Same-tick means one40Hz logic step,25ms; adjacent steps do not merge even if one rendered frame spans both.',
            'Long-lived patrol enemy visual acceptance remains deferred by the user; these tests do not close that observation.',
            'Inherited old per-hit-only tests/results are preserved as historical material and are not represented as passing the new popup policy.',
        ],
    }
    (build / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': 'PASS', 'checks': checks, 'core_sha256': report['core_sha256'],
                      'report': str(build / 'verification.json')}))


if __name__ == '__main__':
    main()
