#!/usr/bin/env python3
"""Host-only tests of V ordnance rules; never opens a board or serial port."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
PINNED = {
    'garden_core.c': 'F94324282912069E6278DDE447F95609BDE824E779DB30B8E994C15A8EC4924A',
    'garden_core.h': 'FBBB3AC70B6BC44EDC8F8335091519448C8D8C63DB10AEC6C89EF2C354B472C2',
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc') or r'D:\mingw64\bin\gcc.exe')
    parser.add_argument('--out', type=Path, default=ROOT/'build/host/ordnance_core')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reference = ROOT/'tools/reference_ordnance_collision'
    for name, expected in PINNED.items():
        if sha(reference/name) != expected:
            raise RuntimeError('Pinned historical reference changed: '+name)
    exe = args.out/'test_ordnance_core.exe'
    command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
               str(ROOT/'tools/test_ordnance_core.c'), str(reference/'bridge.c'), '-o', str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (args.out/'compile.log').write_text(compiled.stdout+compiled.stderr, encoding='utf-8')
    if compiled.returncode:
        raise RuntimeError('Host compilation failed; see compile.log')
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
    (args.out/'host.log').write_text(run.stdout+run.stderr, encoding='utf-8')
    result = re.search(r'RESULT checks=(\d+) runs=(\d+) wins=(\d+) state_bytes=(\d+) event_max=(\d+)', run.stdout)
    passed = run.returncode == 0 and result is not None
    metrics = dict(zip(('checks', 'runs', 'wins', 'state_bytes', 'event_max'), map(int, result.groups()))) if result else {}
    runs = []
    for line in run.stdout.splitlines():
        if line.startswith('RUN '):
            runs.append(dict(re.findall(r'(\w+)=([^ ]+)', line)))
        elif line.startswith('RESTART '):
            data = dict(re.findall(r'(\w+)=([^ ]+)', line))
            next(item for item in runs if item['seed']==data['seed'])['restart_best'] = int(data['best'])
    files = ['src/garden_core.c', 'src/garden_core.h', 'tools/test_ordnance_core.c',
             'tools/verify_ordnance_core.py', 'tools/ORDNANCE_CORE_TESTS.txt',
             'tools/reference_ordnance_collision/bridge.c',
             'tools/reference_ordnance_collision/garden_core.c',
             'tools/reference_ordnance_collision/garden_core.h']
    report = {
        'schema':'garden-ordnance-core-host-v1',
        'created_utc':datetime.now(timezone.utc).isoformat(),
        'status':'PASS' if passed else 'FAIL',
        'scope':'HOST/SYNTHETIC fixtures plus real game-step seeded runs; no board/FPS/600-second acceptance',
        'compiler':str(args.cc), 'command':command, 'compile_exit':compiled.returncode,
        'test_exit':run.returncode, 'metrics':metrics, 'runs':runs,
        'bindings':{name:sha(ROOT/name) for name in files},
        'logs':{'compile.log':sha(args.out/'compile.log'), 'host.log':sha(args.out/'host.log')},
        'contracts':{
            'ordnance':'2 finite physical upward projectiles, single flight,600 damage, inclusive90px circle, no bullet clear or bonus grace',
            'ordinary_hits':'95 per projectile; +1 positive-damage hit and original20/80/1000 kill rewards preserved',
            'healing':'STOPPER real moving pickup; contact adds1life capped3; full health consumes once; +25 pickup points retained',
            'collision':'Exact relative segment-vs-original9px-square; independent Liang-Barsky oracle160000cases; legal original speeds reproduced against pinned old core',
            'activation':'Newly armed cord and first WARN-to-FIRE step use current endpoint only; continuous armed/FIRE movement uses sweep; final active-to-FADE interval is swept',
            'laser':'40tick warning,40fire,12fade; twin13pixel inclusive cores; body/mount/light geometry anchored until OFF; pause/player hit/Boss kill/restart clear hazard',
            'terminal':'Final-life GAMEOVER removes flight/blast; ordinary life loss preserves an already launched projectile; restart clears weapon state and retains runtime high score',
            'event_bound':'24 consumed player shots plus8enemies+1Boss bomb targets=33 separate events; no merge/drop',
            'score':'Per-tick actual score ledger checked for all8seeds; no fixed expected final score',
        },
        'limitations':['One of8seeds legitimately reaches GAMEOVER; game still vulnerable. Default shipped seed is required to win.',
                       'Auto strategies need board visibility confirmation; host timing is40Hz logic, not a frame-rate measurement.']}
    (args.out/'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'status':report['status'], **metrics, 'default':runs[0] if runs else None,
                      'report':str(args.out/'verification.json')}, ensure_ascii=False))
    return 0 if passed else 1

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        print('FAIL: '+str(exc), file=sys.stderr)
        sys.exit(1)
