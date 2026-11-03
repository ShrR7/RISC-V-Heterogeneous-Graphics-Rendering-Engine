#!/usr/bin/env python3
"""Review the isolated 210-second mechanical-Boss/charge/parallax smoke using the native DISPLAY7 parser.

SPDX-License-Identifier: GPL-3.0-or-later
No board access. Preserve the raw capture and derive its exact latest-boot
suffix. Output directories must be new. This is never a 600-second acceptance.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import zlib

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'neon_feedback_20261014'
CASE = ROOT / 'evidence/scene_smoke_20261014'
EXPECTED_ATLAS_BYTES = 236954
EXPECTED_ATLAS_CRC = 0x2A64AD91
LIFETIME = ('flash_frames', 'trail_frames', 'ring_frames', 'peak_draws',
            'decor_dropped', 'essential_dropped')
PROTECTED = ('garden_core.c', 'garden_core.h', 'garden_fx.c', 'garden_fx.h',
             'gpu_driver.c', 'gpu_driver.h', 'gpu_display.c',
             'gpu_display.h', 'gpu_alpha_verify.c', 'garden_feedback.c',
             'garden_feedback.h', 'garden_ui.c', 'garden_ui.h', 'garden_render.c', 'garden_render.h')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def json_write(path, value):
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def identity(path):
    data = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(data), 'sha256': sha(data)}


def historical_or_local(name, local):
    path = CASE / name
    return path if path.is_file() else local


def monotonic(records, keys):
    return bool(records) and all(
        isinstance(row.get(key), int) and row[key] >= 0
        and (index == 0 or row[key] >= records[index - 1].get(key, -1))
        for index, row in enumerate(records) for key in keys)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, default=historical_or_local(
        'scene_smoke_20261014.log', ROOT / 'logs/scene_smoke_20261014.log'))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'logs/feedback_review')
    parser.add_argument('--console', type=Path, default=historical_or_local(
        'capture_completion_tail.txt', ROOT / 'logs/capture_completion_tail.txt'),
                        help='Completed capture console, including Captured 210s')
    parser.add_argument('--visual', type=Path,
                        help='Optional user observation JSON bound to ELF and atlas hashes')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if output.exists():
        parser.error('Use a new --output-dir; existing directories are never overwritten')
    if output == CASE.resolve() or CASE.resolve() in output.parents:
        parser.error('Audit output must stay outside the closed historical case')
    raw_path = args.log.resolve()
    data = raw_path.read_bytes()  # Read the growing source once, never tail/poll it.
    start = data.rfind(b'GARDEN Ti60 cropped port ')
    if start < 0:
        parser.error('UART contains no game boot banner')
    suffix = data[start:]
    tool = Path(__file__).resolve().with_name('analyze_display7.py')
    spec = importlib.util.spec_from_file_location('fx_native_display7', tool)
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'raw_capture.log').write_bytes(data)
    current = output / 'current_boot.log'
    current.write_bytes(suffix)
    source = {
        'raw_path': str(raw_path), 'raw_bytes': len(data), 'raw_sha256': sha(data),
        'raw_copy': 'raw_capture.log', 'kept_start_byte': start,
        'kept_end_byte': len(data), 'discarded_prior_UART_bytes': start,
        'raw_boot_line': data[:start].count(b'\n') + 1,
        'derived_bytes': len(suffix), 'derived_sha256': sha(suffix),
        'exact_suffix_verified': (output / 'raw_capture.log').read_bytes()[start:] == current.read_bytes(),
        'note': 'Latest-boot exact byte suffix; raw prior UART remains in raw_capture.log. Source is unchanged.',
    }
    json_write(output / 'current_boot_source.json', source)
    display = native.analyze([current])
    json_write(output / 'display_review.json', display)
    text = suffix.decode('utf-8-sig', errors='replace')
    partial = bool(text and not text.endswith(('\n', '\r')))
    lines = text.splitlines()
    if partial:
        lines = lines[:-1]
    rows = [{'line': index + 1, 'raw': line,
             **{key: native.value(value) for key, value in native.FIELDS.findall(line)}}
            for index, line in enumerate(lines)]
    live = [row for row in rows if row['raw'].startswith('[LIVE]')]
    fx = [row for row in rows if row['raw'].startswith('[FX]')]
    controls = [row for row in rows if row['raw'].startswith('[FX_CTRL]')]
    atlas_rows = [row for row in rows if row['raw'].startswith('[ATLAS]')]
    alpha = display['native_alpha']
    wins, restarts, previous = [], [], None
    for row in display['native_game_events']:
        mode = row.get('mode')
        if mode == 2 and previous != 2 and row.get('score') == 1320:
            wins.append(row)
        if previous == 2 and mode == 0:
            restarts.append(row)
        previous = mode

    host_path = historical_or_local('host_verification.json', ROOT / 'build/host/fx/verification.json')
    host_log_path = historical_or_local('host_test_fx.log', ROOT / 'build/host/fx/test_fx.log')
    host = json_read(host_path)
    host_source = {
        name: {'expected_sha256': expected.lower(),
               'current_sha256': sha((ROOT / name).read_bytes()),
               'matches': expected.lower() == sha((ROOT / name).read_bytes())}
        for name, expected in host.get('source_sha256', {}).items()
    }
    baseline_manifest = json_read(ROOT / 'scene_baseline.json')
    frozen = {row['path']: row['source_sha256'].lower() for row in baseline_manifest['files']}
    protected = {}
    for name in PROTECTED:
        candidate, baseline = (ROOT / 'src' / name).read_bytes(), (BASE / 'src' / name).read_bytes()
        protected[name] = {'variant_sha256': sha(candidate), 'baseline_sha256': sha(baseline),
                           'frozen_sha256': frozen.get('src/' + name),
                           'unchanged': candidate == baseline
                           and sha(candidate) == frozen.get('src/' + name)}
    manifest = json_read(ROOT / 'assets/manifest.json')
    build_path = historical_or_local('build_manifest.json', ROOT / 'build/build_manifest.json')
    build = json_read(build_path)
    elf = ROOT / 'build/neonSceneGardenDemo.elf'
    elf_identity = identity(elf)
    atlas_data = (ROOT / 'assets/atlas.bin').read_bytes()
    atlas_identity = {'bytes': len(atlas_data), 'sha256': sha(atlas_data),
                      'crc32': zlib.crc32(atlas_data) & 0xFFFFFFFF}
    runtime_path = ROOT / 'assets/runtime_checksums.json'
    runtime_manifest = json_read(runtime_path)
    runtime_checks = {
        name: {'expected': expected, 'actual': identity(ROOT / name),
               'matches': (ROOT / name).stat().st_size == expected['bytes']
               and sha((ROOT / name).read_bytes()) == expected['sha256'].lower()}
        for name, expected in runtime_manifest['files'].items()
    }
    console = {'provided': bool(args.console), 'completed_210_seconds': False}
    if args.console:
        console_data = args.console.read_bytes()
        console_text = console_data.decode('utf-8-sig', errors='replace')
        (output / 'capture_console.log').write_bytes(console_data)
        console.update({'source': str(args.console.resolve()), 'bytes': len(console_data),
                        'sha256': sha(console_data),
                        'completed_210_seconds': bool(re.search(r'Captured\s+210s\s+->', console_text))})
    runtime_pairs = [pair for pair in display['pairs'] if pair['after_GAME_START_window']]
    counter_fields = (0, 1, 4)
    count = display['runtime_window_observed_counts']
    zero_fields = ('errors', 'swap_timeout', 'front_reject', 'atlas_bounds', 'missed',
                   'dropped', 'log_dropped', 'log_truncated')
    host_names = {'src/garden_core.c', 'src/garden_core.h', 'src/garden_fx.c', 'src/garden_fx.h', 'src/garden_assets.c'}
    fx_fields = ('flags', 'flash_frames', 'trail_frames', 'ring_frames', 'ghosts', 'rings',
                 'peak_draws', 'decor_dropped', 'essential_dropped')
    stage_host_path=historical_or_local('stage_host_verification.json', ROOT/'build/host/stage/verification.json')
    stage_host=json_read(stage_host_path)
    stage_rows=[row for row in rows if row['raw'].startswith('[STAGE]')]
    stage_host_sources=stage_host.get('source_sha256', {})
    stage_checks={
        'stage_host_contract_and_current_sources': stage_host.get('status')=='PASS'
            and {'src/garden_stage.c','src/garden_stage.h'} <= set(stage_host_sources)
            and all(sha((ROOT/name).read_bytes())==expected.lower() for name,expected in stage_host_sources.items()),
        'stage_enabled_and_monotonic_reports': bool(stage_rows)
            and all(row.get('enabled')==1 for row in stage_rows)
            and monotonic(stage_rows,('boss_entries','victories','restarts','banner_redraws','fade_frames')),
        'two_stage_victories_and_restarts_recorded': bool(stage_rows)
            and stage_rows[-1].get('boss_entries',0)>=2
            and stage_rows[-1].get('victories',0)>=2 and stage_rows[-1].get('restarts',0)>=2,
        'Boss_fade_triggered': bool(stage_rows) and stage_rows[-1].get('fade_frames',0)>0
            and all(8<=row.get('boss_alpha',0)<=32 for row in stage_rows),
    }
    checks = {
        'exact_current_boot_suffix': source['exact_suffix_verified'],
        'display_records_paired': display['status'] == 'PAIRED' and not display['issues']
            and not display['incomplete_rids'] and not display['duplicates'],
        'display_counter_conservation': not display['counter_total_window_gaps'],
        'GAME_START_boundary_verified': display['GAME_START_boundary_verified'],
        'display_runtime_PX_PART_FULL_zero': bool(runtime_pairs)
            and all(count[index] == 0 for index in counter_fields)
            and all(pair['main']['sum'][index] == 0 and pair['main']['sat'][index] == 0
                    and pair['main']['max'][index] == 0 and pair['main']['last'][index] == 0
                    for pair in runtime_pairs for index in counter_fields),
        'GPU_hardware_Alpha_relock_enabled_legacy_panel_level16': display['runtime_all_sampled_ON']
            and display['runtime_all_sampled_GPU_Alpha16'],
        'no_runtime_overflows_or_refused_reports': not display['runtime_overflow_pairs']
            and display['refused_max'] == 0,
        'original35_Alpha_tests_pass': len(alpha) == 1 and alpha[0].get('rc') == 0
            and alpha[0].get('present') == 1 and alpha[0].get('cases') == 35
            and alpha[0].get('commands') == 35
            and all(alpha[0].get(key) == 0 for key in ('pixel_errors', 'source_errors', 'guard_errors')),
        'ColorKey_pass': len(display['native_color_key']) == 1
            and '[COLOR_KEY] source pixels and transparent holes verified PASS' in display['native_color_key'][0]['raw'],
        'matching_atlas_CRC': len(atlas_rows) == 1
            and atlas_rows[0].get('bytes') == EXPECTED_ATLAS_BYTES
            and atlas_rows[0].get('crc32') == EXPECTED_ATLAS_CRC
            and atlas_rows[0].get('expected') == EXPECTED_ATLAS_CRC,
        'runtime_errors_zero': bool(live) and all(row.get(key) == 0 for row in live for key in zero_fields)
            and not display['native_log_nonzero'] and not display['native_faults'] and not display['fatal'],
        'FPS_at_least60': bool(live) and all(isinstance(row.get('fps10'), int) and row['fps10'] >= 600 for row in live),
        'LIVE_GPU_Alpha_enabled': bool(live) and all(row.get('backend') == 'GPU'
            and row.get('alpha_supported') == 1 and row.get('alpha') == 1 for row in live),
        'FX_all_three_enabled': bool(fx) and bool(controls)
            and all(row.get('flags') == 7 for row in fx + controls),
        'FX_complete_reports': bool(fx) and len(fx) == len(live)
            and all(isinstance(row.get(key), int) and row[key] >= 0 for row in fx for key in fx_fields),
        'FX_lifetime_monotonic': monotonic(fx, LIFETIME),
        'FX_all_lifetime_effects_triggered': bool(fx)
            and all(isinstance(fx[-1].get(key), int) and fx[-1][key] > 0
                    for key in ('flash_frames', 'trail_frames', 'ring_frames')),
        'FX_within_decorative_budget': bool(fx)
            and all(isinstance(row.get('peak_draws'), int) and 0 <= row['peak_draws'] <= 239
                    and isinstance(row.get('ghosts'), int) and 0 <= row['ghosts'] <= 2
                    and isinstance(row.get('rings'), int) and 0 <= row['rings'] <= 2 for row in fx),
        'FX_essential_drop_zero': bool(fx) and all(row.get('essential_dropped') == 0 for row in fx),
        'two_1320_victories_and_auto_restarts': len(wins) >= 2 and len(restarts) >= 2
            and not any(row['raw'].startswith('[INPUT]') for row in rows),
        'host_contract_pass_matches_current_sources': host.get('status') == 'PASS'
            and host.get('host_only') is True and host.get('board_acceptance') is False
            and host.get('contract_checks', 0) > 0 and host_names <= set(host_source)
            and all(row['matches'] for row in host_source.values())
            and sha(host_log_path.read_text(encoding='utf-8-sig').encode('utf-8'))
                == host.get('stdout_sha256', '').lower(),
        'gameplay_feedback_UI_renderer_and_MMIO_match_frozen_feedback_baseline': all(row['unchanged'] for row in protected.values()),
        'actual_ELF_matches_build': elf_identity['sha256'] == build.get('elf_sha256', '').lower(),
        'actual_atlas_matches_build_and_manifest': atlas_identity['bytes'] == EXPECTED_ATLAS_BYTES
            and atlas_identity['crc32'] == EXPECTED_ATLAS_CRC
            and atlas_identity['bytes'] == build.get('atlas_bytes') == manifest.get('bytes') == host.get('atlas_bytes')
            and atlas_identity['sha256'] == build.get('atlas_sha256', '').lower()
            == manifest.get('sha256', '').lower() == host.get('atlas_sha256', '').lower(),
        'runtime_asset_checksums_match': len(runtime_checks) == 13 and all(row['matches'] for row in runtime_checks.values()),
        'within_atlas_budget': atlas_identity['bytes'] <= manifest.get('budget_bytes', 0)
            and 0x005C4000 + atlas_identity['bytes'] <= 0x005FE000,
        'capture_completion': console['completed_210_seconds'],
    }
    checks.update(stage_checks)
    feedback_host_path=historical_or_local('feedback_host_verification.json',ROOT/'build/host/feedback/verification.json')
    hud_host_path=historical_or_local('feedback_hud_host_verification.json',ROOT/'build/host/feedback_hud/verification.json')
    feedback_host=json_read(feedback_host_path);hud_host=json_read(hud_host_path)
    feedback_rows=[row for row in rows if row['raw'].startswith('[FEEDBACK]')]
    def source_bound_host(report):
        hashes=report.get('source_sha256',{})
        return report.get('status')=='PASS' and bool(hashes) and all(
            sha((ROOT/name).read_bytes())==digest.lower() for name,digest in hashes.items())
    feedback_checks={
        'feedback_host_contract_and_current_sources':source_bound_host(feedback_host),
        'fast_Boss_UI_host_contract_and_current_sources':source_bound_host(hud_host),
        'feedback_complete_current_boot_reports':bool(feedback_rows) and len(feedback_rows)==len(live),
        'feedback_lifetime_monotonic':monotonic(feedback_rows,('hits','spawned','anchored','fallback','spark_frames','marker_frames','peak_draws','boss_hud_calls')),
        'hit_sparks_and_collision_marker_triggered':bool(feedback_rows) and feedback_rows[-1].get('hits',0)>0 and feedback_rows[-1].get('spark_frames',0)>0 and feedback_rows[-1].get('marker_frames',0)>0,
        'diagnostic_panel_hidden':bool(feedback_rows) and all(row.get('panel_visible')==0 for row in feedback_rows),
        'feedback_draw_bounds_and_essential_priority':bool(feedback_rows) and all(0<=row.get('peak_draws',-1)<=240 and row.get('essential_dropped')==0 and 0<=row.get('sparks',-1)<=3 and 0<=row.get('marker',-1)<=1 for row in feedback_rows),
        'independent10Hz_Boss_UI_serviced':bool(feedback_rows) and feedback_rows[-1].get('boss_hud_calls',0)>1000,
    }
    checks.update(feedback_checks)
    scene_host_path=historical_or_local('scene_host_verification.json',ROOT/'build/host/scene/verification.json')
    scene_host=json_read(scene_host_path)
    scene_preview_path=historical_or_local('scene_preview_verification.json',ROOT/'build/host/scene_preview/preview_verification.json')
    scene_preview=json_read(scene_preview_path)
    scene_rows=[row for row in rows if row['raw'].startswith('[SCENE]')]
    scene_checks={
        'scene_host_contract_and_current_sources':source_bound_host(scene_host),
        'scene_host_completed_stdout_bound':sha(historical_or_local('scene_host_test.log',ROOT/'build/host/scene/test_scene.log').read_text(encoding='utf-8-sig').encode('utf-8'))==scene_host.get('stdout_sha256','').lower(),
        'scene_preview_source_and_output_binding':source_bound_host(scene_preview)
            and all(sha((ROOT/name).read_bytes())==digest.lower() for group in ('integration_source_sha256','tool_sha256','output_sha256') for name,digest in scene_preview.get(group,{}).items())
            and bool(scene_preview.get('integration_source_sha256')) and bool(scene_preview.get('tool_sha256')) and bool(scene_preview.get('output_sha256'))
            and scene_preview.get('atlas_sha256','').lower()==atlas_identity['sha256'],
        'scene_complete_current_boot_reports':bool(scene_rows) and len(scene_rows)==len(live),
        'scene_enabled':bool(scene_rows) and all(row.get('enabled')==1 for row in scene_rows),
        'scene_lifetime_monotonic':monotonic(scene_rows,('shots','attack_changes','half_hp','charge_windows','charge_frames','burst_frames','peak','essential_drop','decor_drop')),
        'scene_attack_charge_and_actual_burst_triggered':bool(scene_rows) and all(scene_rows[-1].get(k,0)>0 for k in ('shots','charge_windows','charge_frames','burst_frames')),
        'scene_decorations_observed':bool(scene_rows) and any(row.get('wings')==2 and row.get('core')==1 for row in scene_rows) and any(row.get('charge')==1 for row in scene_rows) and all(row.get('backgrounds')==8 for row in scene_rows),
        'scene_draw_bounds_and_essential_priority':bool(scene_rows) and all(0<=row.get('peak',-1)<=240 and row.get('essential_drop')==0 and 0<=row.get('wings',-1)<=2 and 0<=row.get('core',-1)<=1 and 0<=row.get('charge',-1)<=1 and 0<=row.get('countdown',-1)<=12 and 0<=row.get('backgrounds',-1)<=8 for row in scene_rows),
        'entire_feedback_asset_prefix_preserved':atlas_data[:228602]==(BASE/'assets/atlas.bin').read_bytes(),
    }
    checks.update(scene_checks)
    binding_path = CASE / 'input_manifest.json'
    if binding_path.is_file():
        binding = json_read(binding_path)
        checks['closed_case_capture_and_ELF_bound'] = (
            sha(data) == binding['captured_raw_UART_sha256']
            and elf_identity['sha256'] == binding['captured_ELF_sha256'].lower()
            and atlas_identity['sha256'] == binding['captured_atlas_sha256'].lower()
            and all(sha((ROOT / path).read_bytes()) == digest.lower()
                    for path, digest in binding['runtime_source_sha256'].items()))
    report = {
        'variant': 'neon_scene_20261014', 'test_kind': '210-second scene smoke; not 600-second acceptance',
        'machine_smoke_pass': all(checks.values()), 'guards': checks,
        'failed_guards': [name for name, passed in checks.items() if not passed],
        'current_boot_source': source, 'partial_tail_excluded': partial,
        'runtime_counter_device_seconds': display['runtime_counter_duration_seconds'],
        'runtime_complete_pairs': display['runtime_complete_pairs'], 'runtime_samples': display['runtime_samples'],
        'display_counter_order': list(native.NAMES), 'observed_counts': count,
        'counter_lower_bound_fields': display['runtime_counter_lower_bound_fields'],
        'counter_saturation_incidence': display['runtime_window_saturation_incidence'],
        'live_samples': len(live), 'last_LIVE': live[-1] if live else None,
        'min_fps10': min((row['fps10'] for row in live if isinstance(row.get('fps10'), int)), default=None),
        'FX_samples': len(fx), 'last_FX': fx[-1] if fx else None,
        'stage_reports': len(stage_rows), 'last_STAGE': stage_rows[-1] if stage_rows else None,
        'stage_host_verification': identity(stage_host_path),
        'feedback_host_verification':identity(feedback_host_path),'feedback_hud_host_verification':identity(hud_host_path),
        'last_FEEDBACK':feedback_rows[-1] if feedback_rows else None,
        'last_SCENE':scene_rows[-1] if scene_rows else None,
        'scene_host_verification':identity(scene_host_path),'scene_preview_verification':identity(scene_preview_path),
        'decorative_budget_cuts_total': fx[-1].get('decor_dropped') if fx else None,
        'decorative_budget_cuts_are_errors': False,
        'victories': len(wins), 'restarts': len(restarts),
        'victory_events': wins, 'restart_events': restarts,
        'transition_coverage': 'two cycles covered' if len(wins) >= 2 and len(restarts) >= 2 else 'two-cycle coverage incomplete',
        'protected_sources': protected, 'host_verification': {'identity': identity(host_path), 'source_review': host_source},
        'identity_review': {'ELF': elf_identity, 'atlas': atlas_identity,
                            'build_manifest': identity(build_path), 'display_parser': identity(tool),
                            'runtime_checksum_manifest': identity(runtime_path), 'runtime_files': runtime_checks},
        'capture_console': console, 'full_600s_acceptance_complete': False,
        'full_600s_acceptance_started_by_this_tool': False,
        'visual_acceptance': 'pending user observation',
        'limits': [
            'This fresh scene variant has a 210-second smoke capture, not a new 600-second acceptance.',
            'PAIRED means parsing only. Zero PX/PART/FULL cannot replace a user display observation.',
            'AR/R saturation is a lower bound; raw starvation modulo and startup snapshots are not screen D.',
            'The GAME_START window is excluded; startup/calibration/benchmarks remain in the full native review.',
            'Console completion proves requested host capture duration; device counter coverage is reported separately.',
            'Host contracts verify FX/core behavior; they do not execute the GPU renderer or establish board pixels.',
            'FX/feedback/scene lifetime counters include earlier initial builds; they establish triggers within this boot.',
            'Decorative budget truncation is permitted and counted separately; essential truncation fails.',
            'Scene charge is a visual forecast of existing core cooldown; half-HP opening is a visual form, not a new attack.',
            'Mechanical width does not enlarge collisions; backdrop and cue remain behind projectile cores.',
            'Legacy panel16 telemetry is configuration only; FEEDBACK.panel_visible0 confirms the label is hidden.',
        ],
    }
    if args.visual:
        visual = json_read(args.visual)
        report['visual_observation'] = visual
        matched = visual.get('source') == 'user observation' and bool(visual.get('answer'))
        matched = matched and str(visual.get('applies_to_elf_sha256', '')).lower() == elf_identity['sha256']
        matched = matched and str(visual.get('applies_to_atlas_sha256', '')).lower() == atlas_identity['sha256']
        report['visual_acceptance'] = 'HASH_BOUND_USER_OBSERVATION_RECORDED' if matched else 'VISUAL_IDENTITY_NOT_MATCHED'
        # Recording an answer does not assume that its content was favorable.
    json_write(output / 'smoke_summary.json', report)
    print(json.dumps({key: report[key] for key in (
        'machine_smoke_pass', 'failed_guards', 'runtime_counter_device_seconds',
        'runtime_samples', 'observed_counts', 'last_FX', 'victories', 'restarts',
        'last_STAGE', 'last_FEEDBACK', 'last_SCENE', 'visual_acceptance')}, ensure_ascii=False))
    return 0 if report['machine_smoke_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
