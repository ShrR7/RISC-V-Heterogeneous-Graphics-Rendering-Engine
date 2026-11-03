#!/usr/bin/env python3
"""Run production g9_route.c against a separate software source/descriptor compositor."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / 'build/host'
    output.mkdir(parents=True, exist_ok=True)
    exe = output / 'verify_g9_route.exe'
    compile_cmd = [r'D:\mingw64\bin\gcc.exe', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-Wno-misleading-indentation', '-DG9_ROUTE_HOST', '-I' + str(root / 'src'),
                   str(root / 'tools/verify_g9_route.c'), str(root / 'src/g9_route.c'),
                   str(root / 'src/garden_assets.c'), '-o', str(exe)]
    build = subprocess.run(compile_cmd, capture_output=True, text=True)
    (output / 'route_verification_build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
    report = dict(schema=1, status='FAIL', method='Windows VirtualAlloc fixed DDR; production router; independent source-vs-descriptor compositor',
                  compile_command=compile_cmd, build_exit_code=build.returncode,
                  scope='Host CPU model, not hardware FPS or scanout readback. Real RGB565 sprites; forming, banking, health bars, damage labels, lasers and panel order modeled independently.')
    if build.returncode == 0:
        run = subprocess.run([str(exe), str(root / 'assets/stage9_tiles.bin'),
                              str(root / 'assets/atlas.bin')], capture_output=True, text=True)
        (output / 'route_verification_run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        report['run_exit_code'] = run.returncode
        if run.returncode == 0:
            result = json.loads(run.stdout.strip())
            report['result'] = result
            report['status'] = result['status']
        else:
            report['failure'] = run.stderr
    report['source_sha256'] = {}
    for name in ['src/g9_route.c', 'src/g9_route.h', 'src/g9_memory.h', 'src/garden_render.h',
                 'src/garden_assets.c', 'src/stage9_tiles.h', 'assets/atlas.bin',
                 'assets/stage9_tiles.bin', 'tools/verify_g9_route.c', 'tools/verify_g9_route.py']:
        report['source_sha256'][name] = hashlib.sha256((root / name).read_bytes()).hexdigest().upper()
    (output / 'route_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
