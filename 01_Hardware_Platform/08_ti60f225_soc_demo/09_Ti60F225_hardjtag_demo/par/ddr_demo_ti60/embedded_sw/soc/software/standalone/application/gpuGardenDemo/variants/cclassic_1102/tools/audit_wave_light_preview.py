"""Audit the limited board preview separately from full-game acceptance."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'delivery_manifest.json')
    args = parser.parse_args()
    module_file = ROOT / 'tools/summarize_batch_game.py'
    spec = importlib.util.spec_from_file_location('wave_preview_protocol', module_file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    data = module.analyze(args.log)
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    source_path = ROOT / manifest['source_manifest']['path']
    assert sha(source_path) == manifest['source_manifest']['sha256']
    source = json.loads(source_path.read_text(encoding='utf-8-sig'))
    for rel, expected in source['runtime_source_sha256'].items():
        assert sha(ROOT / rel) == expected
    for rel, expected in manifest['loader_sha256'].items():
        assert sha(ROOT / rel) == expected
    for key in ('elf', 'atlas'):
        assert sha(ROOT / manifest[key]['path']) == manifest[key]['sha256']
    assert sha(ROOT / manifest['FPGA']['path']) == manifest['FPGA']['sha256']
    host_path = Path(manifest['host_report']['path'])
    assert sha(host_path) == manifest['host_report']['sha256']
    assert json.loads(host_path.read_text(encoding='utf-8-sig'))['status'] == 'PASS'
    startup, display, live = (data['ordnance'][k] for k in ('startup', 'display', 'live'))
    text = args.log.read_text(encoding='utf-8-sig')
    text = text[text.rfind('GARDEN Ti60 cropped port '):]
    ready = [line for line in text.splitlines() if line.startswith('[READY]')]
    controls = [line for line in text.splitlines() if line.startswith('[BATCH_CTRL]')]
    expected = {'fill_limit': '256', 'blit_limit': '256', 'first_item_deferred': '1'}
    identity = (ready == ['[READY] COM11 WASD Q F P R T I C B V N J'] and len(controls) == 1 and
                all(re.findall(r'\b'+key+r'=([^\s]+)', controls[0]) == [value] for key, value in expected.items()))
    if ROOT.name in ('neon_wave_fixed_20261022', 'neon_boss_calm_20261022', 'neon_wave_patrol_20261022', 'neon_damage_sum_20261023', 'neon_bullet_large_20261023'):
        volleys = [line for line in text.splitlines() if line.startswith('[VOLLEY_CTRL]')]
        identity = identity and len(volleys) == 1 and re.findall(r'\benemy_scope=([^\s]+)', volleys[0]) == ['type']
    # The inherited R-generation parser requires obsolete verbose READY text.
    # Keep its original result and independently check the current bound build.
    checks = {'startup_probes': all(v for k, v in startup.items() if k != 'current_R_identified'),
              'current_bound_build_identity': identity,
              'live_progress': live['samples'] > 0,
              'zero_live_faults': not any(live['counter_maxima'].values()),
              'display_complete_zero': display['status'] == 'PASS' and display['runtime_px_part_full'] == [0, 0, 0],
              'batch_ledger': data['batch_game']['status'] == 'PASS'}
    failed = (data['status'] == 'FAIL' or any(live['counter_maxima'].values()) or
              display['status'] == 'FAIL' or data['batch_game']['status'] == 'FAIL')
    status = 'FAIL' if failed else 'PASS' if all(checks.values()) else 'PENDING'
    record = {'schema': 1, 'status': status, 'scope': 'Limited new software preview: startup, observed LIVE and paired display counters. Does not require or certify complete-game, strict60, visual density, or 600 seconds.',
              'input': str(args.log.resolve()), 'raw_sha256': sha(args.log),
              'checks': checks, 'protocol': data,
              'binding': {'variant': ROOT.name, 'manifest_path': str(manifest_path), 'manifest_SHA256': sha(manifest_path),
                          'ELF_SHA256': manifest['elf']['sha256'], 'FPGA_MD5': manifest['FPGA']['MD5'],
                          'core_SHA256': source['runtime_source_sha256']['src/garden_core.c'],
                          'host_report_SHA256': sha(host_path)},
              'visual_status': 'PENDING', 'hardware_access_by_auditor': False,
              'new600s_run': False, 'Flash_written': False}
    with args.output.open('x', encoding='utf-8') as f:
        json.dump(record, f, ensure_ascii=True, indent=2)
        f.write('\n')
    print(json.dumps({'status': status, 'checks': checks,
        'PX_PART_FULL': display['runtime_px_part_full'], 'strict60': data['strict_60fps'],
        'observed_victories': len(data['ordnance']['victories']), 'observed_restarts': len(data['ordnance']['restarts'])}))
    return 0 if status == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
