# SPDX-License-Identifier: GPL-3.0-or-later
# Accepted RTL11 -> volatile configuration RAM over JTAG; never SPI Flash.
param(
    [string]$ExpectedMD5='7BF53A36220B756369371893B1814513',
    [string]$BitFile='',
    [switch]$HelpOnly,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'

# Pure ownership predicate, also exercised with synthetic process facts.
function Test-Rtl11DebuggerRecord {
    param($Record, [string]$ExpectedExe)
    if(-not $Record -or $Record.record_pid -le 0 -or $Record.record_pid -ne $Record.process_pid) { return $false }
    if(-not [string]::Equals($Record.exe,$ExpectedExe,[StringComparison]::OrdinalIgnoreCase)) { return $false }
    $delay=($Record.record_written_utc-$Record.started_utc).TotalSeconds
    if($delay -lt 0 -or $delay -gt 30) { return $false }
    # Same port/configuration arguments emitted by the existing C board loader.
    $argsPattern='\s+-c\s+"?telnet_port\s+24444"?\s+-c\s+"?tcl_port\s+26666"?\s+-f\s+ftdi_ti\.cfg\s+-f\s+debug_ti\.cfg\s*$'
    return [bool]($Record.command_line -match ('^"?'+[regex]::Escape($ExpectedExe)+'"?'+$argsPattern))
}

if($HelpOnly) {
    Write-Output 'recover_rtl11.ps1 [-BitFile accepted.bit] [-DryRun]: validated 2294091-byte RTL11, volatile JTAG only. DryRun reads files and returns JSON; no USB/process/environment/directory changes.'
    return
}
if($ExpectedMD5 -ne '7BF53A36220B756369371893B1814513') { throw 'Only the pinned, accepted RTL11 MD5 is allowed' }
$variantDir=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$appDir=[IO.Path]::GetFullPath((Join-Path $variantDir '../..'))
. (Join-Path $PSScriptRoot 'resolve_rtl11.ps1')
$image=Resolve-Rtl11Bit -AppDir $appDir -BitFile $BitFile
$logs=Join-Path $variantDir ('logs/fpga_recovery_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))
$ocdExe='D:\Efinity\2026.1\debugger\openocd\bin\openocd.exe'
$pidPaths=@((Join-Path $variantDir 'logs/openocd.pid'),
            (Join-Path $appDir 'logs/openocd.pid'),
            (Join-Path $appDir 'variants/neon_heading_precision_20261015/logs/openocd.pid'),
            (Join-Path $appDir 'variants/neon_hazard_20261016/logs/openocd.pid'),
            (Join-Path $appDir 'variants/neon_cruiser_20261016/logs/openocd.pid'),
            (Join-Path $appDir 'variants/neon_variety_20261016/logs/openocd.pid'))
if($DryRun) {
    [ordered]@{bit=$image.path;md5=$image.md5;bytes=$image.bytes;origin=$image.origin;
        mode='jtag';flash=$false;logs=$logs;recorded_debuggers=$pidPaths;
        hardware_access=$false;process_access=$false;environment_changed=$false;filesystem_writes=$false} | ConvertTo-Json -Depth 4
    return
}

# Only process IDs recorded by the existing APP/Q/P/W loaders are candidates.
# PID reuse, stale timestamps, another binary/configuration => leave untouched.
$owned=@{}
foreach($pidPath in $pidPaths) {
    if(-not (Test-Path -LiteralPath $pidPath -PathType Leaf)) { continue }
    $oldId=0
    if(-not [int]::TryParse((Get-Content -LiteralPath $pidPath -Raw).Trim(),[ref]$oldId) -or $oldId -le 0) { continue }
    $process=Get-Process -Id $oldId -ErrorAction SilentlyContinue
    if(-not $process) { continue }
    try {
        $details=Get-CimInstance Win32_Process -Filter "ProcessId=$oldId" -ErrorAction Stop
        $record=[pscustomobject]@{record_pid=$oldId;process_pid=$details.ProcessId;exe=$process.Path;
            record_written_utc=(Get-Item -LiteralPath $pidPath).LastWriteTimeUtc;
            started_utc=$process.StartTime.ToUniversalTime();command_line=$details.CommandLine}
        if(Test-Rtl11DebuggerRecord $record $ocdExe) { $owned[$oldId]=$process }
    } catch { Write-Warning "Unable to verify recorded debugger $oldId; left untouched" }
}
$listeners=@(Get-NetTCPConnection -LocalPort 3333,24444,26666 -State Listen -ErrorAction SilentlyContinue)
foreach($listener in $listeners) {
    if(-not $owned.ContainsKey([int]$listener.OwningProcess)) { throw 'An unowned debugger is listening; it has been left untouched' }
}
foreach($oldId in @($owned.Keys)) {
    $process=$owned[$oldId]
    $again=Get-Process -Id $oldId -ErrorAction SilentlyContinue
    if($again -and $again.Path -eq $ocdExe -and $again.StartTime -eq $process.StartTime) {
        Stop-Process -InputObject $process -Force
    }
}
if($owned.Count) { Start-Sleep -Milliseconds 700 }
if(Get-NetTCPConnection -LocalPort 3333,24444,26666 -State Listen -ErrorAction SilentlyContinue) {
    throw 'A debugger still owns a required port; no FPGA download attempted'
}

# No environment mutation/output creation before the read-only checks above.
$savedEnv=@{}
$settings=@{EFINITY_HOME='D:/Efinity/2026.1';EFXPGM_HOME='D:/Efinity/2026.1/pgm';
    EFXDBG_HOME='D:/Efinity/2026.1/debugger';PYTHONHOME='D:/Efinity/2026.1/python311';
    PYTHONPATH='D:/Efinity/2026.1/pgm/bin;D:/Efinity/2026.1/debugger/bin';
    EFINITY_USER_DIR_INI=(Join-Path $logs 'efinity_user_dir.ini').Replace('\','/')}
$processEnv=[Environment]::GetEnvironmentVariables('Process')
foreach($name in $settings.Keys) { $savedEnv[$name]=@{present=$processEnv.Contains($name);value=$processEnv[$name]} }
try {
    foreach($name in $settings.Keys) { [Environment]::SetEnvironmentVariable($name,$settings[$name],'Process') }
    if(Test-Path -LiteralPath $logs) { throw 'Recovery log directory already exists; retry with a fresh timestamp' }
    $userDir=(Join-Path $logs 'efinity_user').Replace('\','/')
    New-Item -ItemType Directory -Path $userDir -Force | Out-Null
    [IO.File]::WriteAllText($settings.EFINITY_USER_DIR_INI,"[General]`nEFINITY_USER_DIR=$userDir`n",[Text.Encoding]::ASCII)
    $pythonExe='D:\Efinity\2026.1\python311\bin\python.exe'
    $programmer='D:\Efinity\2026.1\pgm\bin\efx_pgm\ftdi_program.py'
    Write-Host "JTAG volatile image MD5 $($image.md5), $($image.bytes) bytes : $($image.path)"
    $usbList=& $pythonExe $programmer --list_usb | Out-String
    if($LASTEXITCODE -ne 0) { throw 'USB JTAG enumeration failed' }
    $urls=@([regex]::Matches($usbList,'url\[1\]:\s*(ftdi://0x0403:0x6011:\S+)') | ForEach-Object {$_.Groups[1].Value})
    if($urls.Count -ne 1) { throw 'Expected exactly one Ti60 FTDI JTAG target' }
    Write-Host "Selected actual USB target $($urls[0])"
    # Fixed jtag mode is volatile configuration; no Flash command is available.
    & $pythonExe $programmer $image.path -m jtag -u $urls[0] --jtag_clock_freq 6000000 | Tee-Object -FilePath (Join-Path $logs 'fpga_load.log')
    if($LASTEXITCODE -ne 0) { throw 'FPGA programming failed' }
    [IO.File]::WriteAllText((Join-Path $logs 'fpga_md5.txt'),$image.md5)
    Start-Sleep -Seconds 8
} finally {
    foreach($name in $savedEnv.Keys) {
        if($savedEnv[$name].present) { [Environment]::SetEnvironmentVariable($name,$savedEnv[$name].value,'Process') }
        else { Remove-Item -LiteralPath ("Env:"+$name) -ErrorAction SilentlyContinue }
    }
}
