#!/usr/bin/env python3
"""Finite host-only delivery-entry contract. Never runs the board loader.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import base64
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
GUARD = r'''param([string]$Entry,[string]$ArgsBase64)
$ErrorActionPreference='Stop'
# Script breakpoints disable PS5.1 module autoload while stopped: load the
# ordinary hash/path cmdlets before installing the safety breakpoint.
Import-Module Microsoft.PowerShell.Utility,Microsoft.PowerShell.Management
function New-Item { throw 'ENTRY_CONTRACT_FORBIDDEN_WRITE' }
function Set-Content { throw 'ENTRY_CONTRACT_FORBIDDEN_WRITE' }
function Copy-Item { throw 'ENTRY_CONTRACT_FORBIDDEN_WRITE' }
function Start-Process { throw 'ENTRY_CONTRACT_FORBIDDEN_PROCESS' }
function Stop-Process { throw 'ENTRY_CONTRACT_FORBIDDEN_PROCESS' }
function Get-Process { throw 'ENTRY_CONTRACT_FORBIDDEN_PROCESS' }
function Get-CimInstance { throw 'ENTRY_CONTRACT_FORBIDDEN_PROCESS' }
function Get-NetTCPConnection { throw 'ENTRY_CONTRACT_FORBIDDEN_NETWORK' }
$before=@(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
$text=Get-Content -LiteralPath $Entry
$boundary=0
for($i=0;$i -lt $text.Count;$i++) {
    if($text[$i].StartsWith('if($stagedElf.Length')) { $boundary=$i+1;break }
}
if(-not $boundary) { throw 'ENTRY_CONTRACT: cannot locate active-path safety boundary' }
$breakpoint=Set-PSBreakpoint -Script $Entry -Line $boundary -Action { throw 'ENTRY_CONTRACT_FORBIDDEN_ACTIVE_PATH' }
try {
    $data=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($ArgsBase64)) | ConvertFrom-Json
    $arguments=@{}
    foreach($property in $data.PSObject.Properties) { $arguments[$property.Name]=$property.Value }
    if(-not $arguments.DryRun) { throw 'ENTRY_CONTRACT: every invocation must use DryRun' }
    $result=@(& $Entry @arguments)
    $after=@(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
    if(($before -join "`n") -cne ($after -join "`n")) { throw 'ENTRY_CONTRACT_ENV_CHANGED' }
    $result | Write-Output
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 3
} finally { Remove-PSBreakpoint -Breakpoint $breakpoint }
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pwsh', default=shutil.which('pwsh.exe') or shutil.which('pwsh'))
    parser.add_argument('--powershell', default=os.path.join(os.environ.get('WINDIR', 'C:/Windows'),
                        'System32/WindowsPowerShell/v1.0/powershell.exe'))
    args = parser.parse_args()
    if not args.pwsh or not Path(args.powershell).is_file():
        raise RuntimeError('PowerShell7 and Windows PowerShell5.1 are both required')
    build = ROOT / 'build/host/entry'
    build.mkdir(parents=True, exist_ok=True)
    token = 'entry_contract_' + uuid.uuid4().hex
    guard = build / (token + '.ps1')
    bad_bit = build / (token + '.bit')
    manifest_path = ROOT / 'delivery_manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    source_path = ROOT / manifest['source_manifest']['path']
    source = json.loads(source_path.read_text(encoding='utf-8-sig'))
    protected = {ROOT / name for name in source['runtime_source_sha256']}
    protected.update((manifest_path, source_path, ROOT / manifest['elf']['path'],
                      ROOT / manifest['atlas']['path'], ROOT / 'start_garden.ps1',
                      ROOT / 'tools/recover_rtl11.ps1', ROOT / 'tools/resolve_rtl11.ps1'))
    for local_artifact in (ROOT / 'build/neonMotionGardenDemo.elf', ROOT / 'build/build_manifest.json'):
        if local_artifact.is_file():
            protected.add(local_artifact)
    before = {path.relative_to(ROOT).as_posix(): sha(path) for path in protected}
    assert before[source_path.relative_to(ROOT).as_posix()] == manifest['source_manifest']['sha256'].upper()
    for name, expected in source['runtime_source_sha256'].items():
        assert before[name] == expected.upper(), name
    assert before[manifest['elf']['path']] == manifest['elf']['sha256'].upper()
    assert before[manifest['atlas']['path']] == manifest['atlas']['sha256'].upper()
    records = []
    guard.write_text(GUARD, encoding='utf-8')
    bad_bit.write_bytes(b'not an accepted RTL11 image\n')

    def run(name, parameters, *, shell=None, expected_error=None, log_name=None):
        inputs = {'DryRun': True, **parameters}
        if log_name is not None:
            inputs['LogName'] = log_name
        elif 'LogName' not in inputs:
            inputs['LogName'] = token + '_' + name + '.log'
        log = inputs['LogName']
        candidates = [ROOT / 'logs' / (log + suffix) for suffix in ('', '.timing.jsonl', '.host.log')]
        # Observe only this case's named files; another legitimate capture can
        # run concurrently without invalidating this read-only entry test.
        candidate_before = {str(path): (sha(path) if path.is_file() else None) for path in candidates}
        encoded = base64.b64encode(json.dumps(inputs).encode('utf-8')).decode('ascii')
        command = [shell or args.pwsh, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                   '-File', str(guard), '-Entry', str(ROOT / 'start_garden.ps1'), '-ArgsBase64', encoded]
        result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace', timeout=25)
        combined = result.stdout + result.stderr
        candidate_after = {str(path): (sha(path) if path.is_file() else None) for path in candidates}
        assert candidate_before == candidate_after, name + ': DryRun changed named log files'
        assert 'ENTRY_CONTRACT_FORBIDDEN' not in combined, name + ': attempted active access'
        if expected_error:
            assert result.returncode != 0 and expected_error in combined, name + ': ' + combined
            record = {'name': name, 'status': 'PASS', 'expected_rejection': expected_error}
            value = None
        else:
            assert result.returncode == 0, name + ': ' + combined
            value = json.loads(result.stdout.lstrip('\ufeff'))
            assert value['dry_run'] and value['runtime_sources_verified'] and not value['Flash_written']
            assert value['elf_sha256'].upper() == manifest['elf']['sha256'].upper()
            assert value['bit_md5'].upper() == manifest['FPGA']['MD5'].upper()
            assert value['bit_bytes'] == manifest['FPGA']['bytes']
            assert Path(value['staged_elf']) == ROOT / 'build/neonMotionGardenDemo.elf'
            record = {'name': name, 'status': 'PASS', 'plan': value}
        records.append({**record, 'shell': str(shell or args.pwsh),
                        'stdout_sha256': hashlib.sha256(result.stdout.encode()).hexdigest(),
                        'named_logs_unchanged': True})
        return value

    try:
        default = run('default_dryrun', {})
        assert default['seconds'] == 30 and default['port'] == 'COM11'
        assert not default['recover_fpga'] and not default['rebuild'] and not default['controls_test']
        recover = run('recover_dryrun', {'RecoverFpga': True})
        assert recover['recover_fpga'] and recover['seconds'] == 30
        controls = run('controls_default', {'CheckControls': True})
        assert controls['seconds'] == 120 and controls['controls_test']
        run('controls_short_rejected', {'CheckControls': True, 'Seconds': 111},
            expected_error='Control checks require at least 112 seconds')
        run('relative_log_rejected', {'LogName': '../outside.log'},
            expected_error='LogName must be a new simple .log filename')
        run('absolute_log_rejected', {'LogName': str(build / 'outside.log')},
            expected_error='LogName must be a new simple .log filename')
        # Reuse a pre-existing old capture only; never create files in logs or
        # select openocd/load/current captures that another run may be updating.
        old_logs = [path for path in (ROOT / 'logs').glob('*.log')
                    if path.stat().st_mtime < datetime.datetime.now().timestamp()-600
                    and path.name not in ('openocd.log', 'load.log')]
        assert old_logs, 'Need one existing old capture to verify no-overwrite rejection'
        existing = min(old_logs, key=lambda path: path.stat().st_mtime)
        run('existing_log_rejected', {}, log_name=existing.name,
            expected_error='Choose a new LogName; existing capture files are preserved')
        run('bad_bit_rejected', {'BitFile': str(bad_bit)}, expected_error='No matching accepted RTL11 image')
        rebuild = run('rebuild_dryrun', {'Rebuild': True})
        assert rebuild['rebuild'] and not rebuild['recover_fpga']
        legacy = run('ps51_json_controls', {'CheckControls': True}, shell=args.powershell)
        assert legacy['seconds'] == 120 and legacy['port'] == default['port']
        assert legacy['elf_sha256'] == default['elf_sha256'] and legacy['bit_md5'] == default['bit_md5']
        # GDB itself is outside DryRun coverage. Verify the precise path
        # counterexample observed during real loading without invoking GDB.
        app = ROOT.parent.parent
        sealed_elf = ROOT / manifest['elf']['path']
        relative_elf = sealed_elf.relative_to(app)
        staged_elf = Path(default['staged_elf'])
        assert sealed_elf.is_file() and (app / relative_elf).resolve() == sealed_elf.resolve()
        assert len(str(sealed_elf)) > 260 and len(relative_elf.as_posix()) < 260
        assert staged_elf.is_file() and len(str(staged_elf)) < 260
        assert sha(staged_elf) == manifest['elf']['sha256'].upper()
        records.append({'name': 'gdb_path_readonly_counterexample', 'status': 'PASS',
                        'absolute_length': len(str(sealed_elf)), 'relative_length': len(relative_elf.as_posix()),
                        'staged_absolute_length': len(str(staged_elf)), 'staged_elf_sha256': sha(staged_elf),
                        'app_relative_elf': relative_elf.as_posix(), 'file_sha256': sha(sealed_elf),
                        'actual_gdb_invoked': False})
        assert len(records) == 11
        after = {path.relative_to(ROOT).as_posix(): sha(path) for path in protected}
        assert before == after, 'Entry contract altered runtime, frozen tools, ELF, atlas or manifests'
        report = {'status': 'PASS', 'cases': len(records), 'host_only': True, 'hardware_access': False,
                  'active_path_blocked_by_breakpoint': True, 'runtime_elf_atlas_unchanged': True,
                  'only_named_log_paths_observed': True, 'results': records, 'protected_sha256': after,
                  'test_source_sha256': sha(Path(__file__)),
                  'completed_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  'limitations': ['Every invocation uses DryRun; no serial, USB, debugger, firmware build or board control was executed.',
                                  'DryRun does not exercise GDB file loading or staging; the path case checks only long sealed versus short staged exact-ELF identity. GDB may expand relative paths.',
                                  'Concurrent legitimate captures are outside this contract; their log directory is not compared as a whole.']}
        (build / 'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(f'ENTRY_CONTRACT PASS cases={len(records)} hardware_access=0 runtime_unchanged=1 report={build / "verification.json"}')
    finally:
        # Delete only the two files created by this invocation, with UUID names.
        for path in (guard, bad_bit):
            if path.is_file():
                path.unlink()


if __name__ == '__main__':
    main()
