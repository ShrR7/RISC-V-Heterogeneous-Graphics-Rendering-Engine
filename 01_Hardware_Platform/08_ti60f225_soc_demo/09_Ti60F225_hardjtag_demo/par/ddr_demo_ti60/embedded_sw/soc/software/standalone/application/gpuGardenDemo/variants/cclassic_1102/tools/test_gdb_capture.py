#!/usr/bin/env python3
"""Exercise native GDB capture under real PS5.1 and PS7, without a board.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GUARD = r'''param([string]$Helper,[string]$Executable,[string]$Working,[string]$Scenario,
                  [string]$Atlas,[string]$Relock,[string]$LaunchError)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. $Helper
$global:LASTEXITCODE=91
$result=$null
$failure=$null
try { $result=Invoke-GardenGdbBatch -Executable $Executable -WorkingDirectory $Working -ScriptPath $Scenario }
catch { $failure=$_.Exception.Message }
if($LaunchError -eq 'true') {
    [ordered]@{launch_failed=[bool]$failure;failure=$failure;ps=$PSVersionTable.PSVersion.ToString();
        stderr_is_error_record=$false} | ConvertTo-Json -Compress
    exit 0
}
if($failure) { throw $failure }
$gate=$true
$gateFailure=$null
try { Assert-GardenGdbLoad -Result $result -RequireAtlas ($Atlas -eq 'true') -RequireRelock ($Relock -eq 'true') }
catch { $gate=$false;$gateFailure=$_.Exception.Message }
[ordered]@{ps=$PSVersionTable.PSVersion.ToString();exit_code=$result.exit_code;
    stdout_length=$result.stdout.Length;stderr_length=$result.stderr.Length;output_length=$result.output.Length;
    warning_captured=$result.stderr.Contains('warning: No executable');
    stdout_has_marker=$result.stdout.Contains('load size');
    stderr_has_marker=$result.stderr.Contains('load size');
    gate_pass=$gate;gate_failure=$gateFailure;ambient_last_exit_code=$LASTEXITCODE} | ConvertTo-Json -Compress
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gcc', default='D:/mingw64/bin/gcc.exe')
    parser.add_argument('--pwsh', default=shutil.which('pwsh.exe') or shutil.which('pwsh'))
    parser.add_argument('--powershell', default=os.path.join(os.environ.get('WINDIR', 'C:/Windows'),
                        'System32/WindowsPowerShell/v1.0/powershell.exe'))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/host/gdb_capture')
    args = parser.parse_args()
    helper = ROOT / 'tools/invoke_gdb.ps1'
    for name in (helper, Path(args.gcc), Path(args.powershell)):
        if not name.is_file():
            raise RuntimeError('Required host tool is missing: ' + str(name))
    if not args.pwsh or not Path(args.pwsh).is_file():
        raise RuntimeError('A real PowerShell7 executable is required')
    helper_before = sha(helper)
    delivery = json.loads((ROOT / 'delivery_manifest.json').read_text(encoding='utf-8-sig'))
    source_manifest = ROOT / delivery['source_manifest']['path']
    source_binding = json.loads(source_manifest.read_text(encoding='utf-8-sig'))
    protected = [ROOT / name for name in source_binding['runtime_source_sha256']]
    protected += [source_manifest, ROOT / delivery['elf']['path'], ROOT / delivery['atlas']['path']]
    protected_before = {str(path.relative_to(ROOT)): sha(path) for path in protected}
    out = args.output_dir.resolve()
    allowed = (ROOT / 'build/host').resolve()
    if out != allowed and allowed not in out.parents:
        raise RuntimeError('Host-test output must stay within variant/build/host')
    out.mkdir(parents=True, exist_ok=True)
    working = out / 'working directory'
    working.mkdir(exist_ok=True)
    guard = out / 'capture guard.ps1'
    guard.write_text(GUARD, encoding='utf-8')
    fixture = out / 'native fixture.exe'
    source = ROOT / 'tools/test_gdb_capture.c'
    built = subprocess.run([args.gcc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(fixture)], capture_output=True,
                           text=True, encoding='utf-8', errors='replace', timeout=30)
    if built.returncode:
        raise RuntimeError('Host fixture compilation failed: ' + built.stdout + built.stderr)
    cases = [
        ('warning_success', 'warning_success', True, True, 0, True),
        ('native_nonzero_even_with_markers', 'error_exit', True, True, 17, False),
        ('missing_load', 'missing_load', True, True, 0, False),
        ('missing_atlas', 'missing_restore', True, True, 0, False),
        ('missing_relock', 'missing_relock', True, True, 0, False),
        ('atlas_not_required', 'missing_restore', False, True, 0, True),
        ('relock_not_required', 'missing_relock', True, False, 0, True),
        ('empty_success_exit_is_not_load_success', 'empty', True, True, 0, False),
        ('both_pipes_exceed_capacity', 'long_streams', True, True, 0, True),
        ('markers_on_stderr_are_not_completion', 'stderr_markers', True, True, 0, False),
        ('warning_only_success_exit_is_not_load_success', 'warning_only', True, True, 0, False),
        ('missing_executable', 'warning_success', True, True, None, None),
    ]
    records = []
    for shell_name, shell in [('windows_powershell_5_1', args.powershell), ('powershell_7', args.pwsh)]:
        for index, (name, scenario, atlas, relock, code, expected_gate) in enumerate(cases):
            # Keep paths below native fopen/MAX_PATH while retaining spaces.
            script = working / (f'case {index:02}.gdb')
            script.write_text(scenario + '\n', encoding='ascii')
            missing = name == 'missing_executable'
            cmd = [str(shell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(guard),
                   '-Helper', str(helper), '-Executable', str(fixture if not missing else out / 'not present.exe'),
                   '-Working', str(working), '-Scenario', str(script),
                   '-Atlas', str(atlas).lower(), '-Relock', str(relock).lower(),
                   '-LaunchError', str(missing).lower()]
            run = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8',
                                 errors='replace', timeout=25)
            if run.returncode:
                raise RuntimeError(f'{shell_name}/{name}: guard failed: ' + run.stdout + run.stderr)
            data = json.loads(run.stdout.lstrip('\ufeff').strip())
            version_prefix = '5.1.' if shell_name == 'windows_powershell_5_1' else '7.'
            assert data['ps'].startswith(version_prefix), (shell_name, data)
            if missing:
                assert data['launch_failed'], (shell_name, name, data)
            else:
                assert data['exit_code'] == code, (shell_name, name, data)
                assert data['gate_pass'] is expected_gate, (shell_name, name, data)
                if scenario != 'empty':
                    assert data['warning_captured'], (shell_name, name, data)
                if scenario == 'long_streams':
                    assert data['stdout_length'] > 524288 and data['stderr_length'] > 524288, data
                if scenario == 'stderr_markers':
                    assert data['stderr_has_marker'] and not data['stdout_has_marker'], data
            records.append({'shell': shell_name, 'case': name, 'passed': True, **data})
    assert sha(helper) == helper_before, 'Capture helper changed while tests were running'
    protected_after = {str(path.relative_to(ROOT)): sha(path) for path in protected}
    assert protected_after == protected_before, 'Protected runtime/delivery artifact changed'
    result = {'schema': 1, 'result': 'PASS', 'test_cases': len(records),
              'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'helper_sha256': sha(helper), 'test_python_sha256': sha(Path(__file__)),
              'test_c_sha256': sha(source), 'fixture_sha256': sha(fixture),
              'protected_runtime_and_delivery_files_unchanged': len(protected_before),
              'hardware_access': False, 'uart_access': False, 'jtag_access': False,
              'runtime_or_sealed_cases_modified': False,
              'coverage': 'Real native process under PS5.1 and PS7 with EAP Stop; zero/nonzero exit, warning, exact optional marker gates, empty output, both full pipes, spaces, cwd, and launch failure.',
              'records': records}
    report = out / 'verification.json'
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'result': result['result'], 'test_cases': len(records),
                      'report': str(report), 'report_sha256': sha(report)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
