#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Production per-effective-hit damage labels; host-only, no MMIO actions.

Every event births an independent label in a fixed32 pool. Labels may overlap;
only actual actor/HP-bar/marker obstacles are avoided. Real scores are audited
independently, and exact HP damage excludes overkill and nominal rewards.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'neon_rhythm_20261014'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/combat'
    build.mkdir(parents=True, exist_ok=True)
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    assert atlas == (BASE / 'assets/atlas.bin').read_bytes()
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert len(atlas) == 236954 and len(manifest['sprites']) == 280
    assert sha(atlas) == manifest['sha256']
    assert (ROOT / 'src/garden_assets.c').read_bytes() == (BASE / 'src/garden_assets.c').read_bytes()
    assert (ROOT / 'src/garden_assets.h').read_bytes() == (BASE / 'src/garden_assets.h').read_bytes()
    end = 0
    for item in manifest['sprites']:
        assert item['offset'] % 4 == 0 and item['offset'] >= end
        assert item['stride'] == item['width'] * 2
        assert item['bytes'] == item['stride'] * item['height']
        end = item['offset'] + item['bytes']
        assert end <= len(atlas)
    frozen = ('garden_feedback.c', 'garden_feedback.h')
    for name in frozen:
        assert (ROOT / 'src' / name).read_bytes() == (BASE / 'src' / name).read_bytes(), name
    exe = build / 'test_combat_feedback.exe'
    sources = ('garden_combat.c', 'garden_scene.c', 'garden_feedback.c', 'garden_fx.c',
               'garden_core.c', 'garden_assets.c', 'garden_actor_ui.c')
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_combat_feedback.c')]
    command += [str(ROOT / 'src' / name) for name in sources] + ['-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (build / 'test_combat_feedback.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, end='')
        raise SystemExit(result.returncode)
    animation_exe = build / 'test_popup_animation.exe'
    animation_trace = build / 'popup_trajectory.jsonl'
    animation_command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_popup_animation.c')]
    animation_command += [str(ROOT / 'src' / name) for name in sources] + ['-o', str(animation_exe)]
    subprocess.run(animation_command, check=True)
    animation = subprocess.run([str(animation_exe), str(animation_trace)], capture_output=True, text=True)
    (build / 'test_popup_animation.log').write_text(animation.stdout + animation.stderr, encoding='utf-8')
    print(animation.stdout, end='')
    if animation.returncode:
        print(animation.stderr, end='')
        raise SystemExit(animation.returncode)
    animation_match = re.search(r'independent popup animation timestamp checks=(\d+)', animation.stdout)
    assert animation_match and animation_trace.is_file(), 'Missing completed timestamp lifecycle proof'
    match = re.search(r'combat feedback host contract checks=(\d+)', result.stdout)
    assert match, 'Missing completed contract summary'
    sizes = re.search(r'combat_state_bytes=(\d+) popup_bytes=(\d+) popup_pool_slots=(\d+) rise_pixels=(\d+) jitter_pixels=(\d+)', result.stdout)
    assert sizes and tuple(map(int, sizes.groups()[2:])) == (32, 32, 0), 'Unexpected pool/animation constants'
    assert int(sizes.group(1)) < 1600, 'Combat fixed-state memory budget exceeded'

    files = tuple('src/' + name for name in sources) + (
        'src/garden_actor_ui.h', 'src/garden_combat.h', 'src/garden_core.h', 'src/garden_scene.h',
        'src/garden_feedback.h', 'src/garden_fx.h', 'src/garden_assets.h', 'src/garden_render.h',
        'tools/test_combat_feedback.c', 'tools/test_popup_animation.c', 'tools/verify_combat_feedback.py')
    report = dict(status='PASS', host_only=True, board_acceptance=False,
        compiler_command=command, contract_checks=int(match.group(1)),
        compiled_host_combat_state_bytes=int(sizes.group(1)), compiled_host_popup_bytes=int(sizes.group(2)),
        independent_timestamp_checks=int(animation_match.group(1)), animation_compiler_command=animation_command,
        trajectory_sha256=sha(animation_trace.read_bytes()), trajectory_clock="scheduled elapsed microseconds, host simulation",
        independent_rates_hz=[60, 57.6, 32.7], per_identity_lifetime_ms=600, lifetime_quantization_max="one presentation frame",
        birth_pop_at_100ms_min_fraction_of_rise_budget=0.30, no_extra_render_commands=True,
        atlas_bytes=len(atlas), atlas_sha256=sha(atlas), unchanged_all_280_asset_slots=True,
        all_descriptors_bounded=True, unchanged_existing_feedback=True, boss_whole_body_white_intentionally_removed=True,
        popup_semantics_effective_hp_damage=True, core_score_rule_retained=True,
        killing_damage_never_includes_rewards=True, hp_bar_geometry_shared=True,
        damage_labels_per_effective_hit=1, fixed_popup_pool_slots=32, intentional_label_overlap=True,
        floating_rise_pixels_max=32, deterministic_birth_jitter_pixels=0,
        one_time_centred_overhead_birth_anchors=True, bounded_rise_budget_per_label=True, entry_fallback_below_hp_bar=True, no_per_frame_relayout_or_top_clamp=True,
        motion_curve="quadratic ease-out 1-(1-t)^2", no_added_textures_or_commands=True,
        render_fraction_q8_supported=True, continuous_idle_alpha_32_to_1=True,
        model_and_player_marker_boxes_avoided=True, oldest_pool_replacement_counted=True,
        reserved_legacy_merge_update_counters_zero=True,
        complete_core_snapshots_compared_steps=12000,
        checks=['actual production Boss/enemy effective HP damage and original real scores, no overkill or nominal reward labels',
                'independent event births, same-tick idempotency, malformed records and255-count bounds',
                'fixed32 pool pressure replaces16 oldest for48 hits; birth order remains exact acrossUINT32 wrap',
                'full score/damage audit and compatibility merge/update counters remain unchanged under presentation pressure',
                'each32767 display cap reports its own omitted units without value wrapping',
                'first build fixes target-centred head anchor and0..32 rise budget once; top-entry fallback18-to10 never crosses HP',
                'later target motion cannot relocate or re-anchor established labels; no per-frame y clamp',
                'simultaneous same-target hits deliberately overlap at the same head anchor, still separate values/ages/events',
                'pure readout binds per-sequence geometry to admitted production draw-list multiset',
                'isolated and3-second every250ms dual-hit streams at60,57.6 and32.7Hz scheduled microsecond rates',
                'all78 tracked identities expire600ms after their own event within one presentation frame',
                'each label moves>=30percent of its rise budget by100ms and>=75percent by300ms, completes bounded ease-out and fades32-to1',
                'no bright trajectory remains pinned to an HP/top boundary beyond75ms; no refresh/merge of old labels',
                'pause retains all ages/anchors/Q8 fraction; restart/H clear without replay; slot reuse clears stale identities',
                'Boss body never switches to whole-sprite WHITE; local impacts and enemy held flashes remain',
                'all capacities0..250/null/canaries protect234 essentials and marker priority; every anchor remains field-bounded',
                'real descriptor/event target coordinates and shared HP/model top determine exact head anchors, including killed inactive slots',
                'killing event survives inactive target using true event position rather than zeroed actor slot',
                '12000 complete actual game snapshots preserve core bytes/real score across pause/restart; every natural event chooses exactly one swept anchor without failures'],
        stdout_sha256=sha(result.stdout.encode('utf-8')),
        source_sha256={name: sha((ROOT / name).read_bytes()) for name in files},
        limits=['Host production-module tests do not establish board pixels, FPS or GPU/DDR load.',
                'Main integration, banked GPU/CPU history execution and board pixels/FPS are separate tests.',
                'Gameplay/core source comparison and hardware600ms wall-clock confirmation are separate checks.',
                'Head labels protect their own birth HP bar and stay inside the field; overlapping labels/material are intentional, and unrelated later actors can cross a fixed path.',
                'Timestamp schedules use elapsed microseconds without sleeping; they do not claim measured board wall-clock duration.',
                'Bounded queue replacement and decorative budget cuts do not change credited game score.'])
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
