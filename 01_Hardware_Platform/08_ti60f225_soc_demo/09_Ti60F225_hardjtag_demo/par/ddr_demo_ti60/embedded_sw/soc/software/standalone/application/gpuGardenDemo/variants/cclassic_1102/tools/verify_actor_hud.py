#!/usr/bin/env python3
"""Real-font RGB565 HUD/cache host contracts; no board access or timing claims.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT / 'build/host/actor_hud'
    build.mkdir(parents=True, exist_ok=True)
    atlas = (ROOT / 'assets/atlas.bin').read_bytes()
    manifest = json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8-sig'))
    assert sha(atlas) == manifest['sha256'], 'Atlas manifest mismatch'
    assert 0x5C4000 + len(atlas) <= 0x5FE000, 'Atlas scratch overlap'
    exe = build / 'test_actor_hud.exe'
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT / 'src'), str(ROOT / 'tools/test_actor_hud.c'),
               str(ROOT / 'src/garden_assets.c'), str(ROOT / 'src/garden_ui.c'),
               '-o', str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe), str(ROOT / 'assets/atlas.bin')], capture_output=True, text=True)
    output = result.stdout + result.stderr
    (build / 'test_actor_hud.log').write_text(output, encoding='utf-8')
    print(output, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(r'host contract checks=(\d+)', output)
    assert match, 'Completed HUD contract summary missing'
    sources = ('src/garden_ui.c', 'src/garden_ui.h', 'src/garden_assets.c',
               'src/garden_assets.h', 'tools/test_actor_hud.c', 'tools/verify_actor_hud.py')
    report = {
        'status': 'PASS', 'host_only': True, 'board_acceptance': False,
        'contract_checks': int(match.group(1)), 'ignored_remote_HP_updates': 40,
        'compiler_command': command, 'atlas_bytes': len(atlas), 'atlas_sha256': sha(atlas),
        'checks': ['real RGB565 font/full-HUD oracle across two physical framebuffers',
                   'remote Boss API no-op preserves full-HUD initialization',
                   '40 HP/active/attack edge cases never repaint the static right guide',
                   'actual score-only box/glyph fault resets both HUD histories',
                   'remaining fields, mutable state and unknown-buffer handling'],
        'source_sha256': {p: sha((ROOT / p).read_bytes()) for p in sources},
        'stdout_sha256': sha(output.encode('utf-8')),
        'limits': ['Runs production HUD source in host RAM using actual RGB565 font pixels.',
                   'No production GPU/MMIO commands or real scheduling/performance were executed.',
                   'Fault retry requires caller to reset both HUD caches when renderer counters change.'],
    }
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
