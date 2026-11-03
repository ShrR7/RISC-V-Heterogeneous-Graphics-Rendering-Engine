# SPDX-License-Identifier: GPL-3.0-or-later
# Host-only contract: mocks never launch, stop, enumerate or program hardware.
param([string]$Output='')
$ErrorActionPreference='Stop'
$variant=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$app=[IO.Path]::GetFullPath((Join-Path $variant '../..'))
$recover=Join-Path $PSScriptRoot 'recover_rtl11.ps1'
$resolver=Join-Path $PSScriptRoot 'resolve_rtl11.ps1'
. $resolver
$contract=[pscustomobject]@{checks=0}
function Check([bool]$Condition,[string]$Message) {
    if(-not $Condition) { throw "CONTRACT: $Message" }
    $contract.checks++
}
function MustThrow([scriptblock]$Action,[string]$Pattern) {
    $caught=''
    try { & $Action | Out-Null } catch { $caught=$_.Exception.Message }
    Check ($caught -match $Pattern) "Expected failure matching $Pattern; got $caught"
}
$envBefore=Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }
$logsBefore=Get-ChildItem -LiteralPath (Join-Path $variant 'logs') -Force | Sort-Object Name | ForEach-Object { "$($_.Name)|$($_.LastWriteTimeUtc.Ticks)" }
$dry=& {
    function New-Item { throw 'DryRun attempted directory creation' }
    function Get-Process { throw 'DryRun attempted process inspection' }
    function Get-CimInstance { throw 'DryRun attempted CIM inspection' }
    function Get-NetTCPConnection { throw 'DryRun attempted port inspection' }
    function Stop-Process { throw 'DryRun attempted process termination' }
    function Start-Process { throw 'DryRun attempted child process' }
    function Start-Sleep { throw 'DryRun attempted wait' }
    & $recover -DryRun | ConvertFrom-Json
}
$envAfter=Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }
$logsAfter=Get-ChildItem -LiteralPath (Join-Path $variant 'logs') -Force | Sort-Object Name | ForEach-Object { "$($_.Name)|$($_.LastWriteTimeUtc.Ticks)" }
Check (($envBefore -join "`n") -ceq ($envAfter -join "`n")) 'DryRun leaves process environment identical'
Check (($logsBefore -join "`n") -ceq ($logsAfter -join "`n")) 'DryRun leaves log directory and children identical'
Check ($dry.bytes -eq 2294091 -and $dry.md5 -eq '7BF53A36220B756369371893B1814513') 'DryRun resolves accepted image bytes and hash'
Check ($dry.mode -eq 'jtag' -and -not $dry.flash -and -not $dry.hardware_access -and -not $dry.process_access -and -not $dry.environment_changed -and -not $dry.filesystem_writes) 'DryRun explicitly reports read-only/volatile mode'
Check ($dry.recorded_debuggers.Count -eq 3 -and $dry.recorded_debuggers -contains (Join-Path $variant 'logs/openocd.pid')) 'Uses actual Q loader PID record and only existing Q/P/APP ranges'
Check (-not (Test-Path -LiteralPath $dry.logs)) 'DryRun does not create planned recovery directory'
$explicit=& $recover -DryRun -BitFile $dry.bit | ConvertFrom-Json
Check ($explicit.origin -eq 'explicit' -and $explicit.bit -eq $dry.bit) 'Explicit matching image is validated'
MustThrow { & $recover -DryRun -BitFile (Join-Path $app 'platform/ddr_alpha_original_display_48c0a72.bit') } 'No matching accepted RTL11 image'
MustThrow { & $recover -DryRun -ExpectedMD5 '00000000000000000000000000000000' } 'Only the pinned'

# Pure resolution mocks exercise all fallback paths without writing fake files.
& {
    $fakeApp='E:\__rtl11_contract\a\b\c\d\e\f'
    $project=[IO.Path]::GetFullPath((Join-Path $fakeApp '../../../../../../outflow/ddr_demo_ti60.bit'))
    $cache=Join-Path $fakeApp 'build/rtl11_runtime/20261013085034/ddr_demo_ti60_rtl11.bit'
    $fakeExplicit='E:\__rtl11_contract\explicit.bit'
    $validHash='7BF53A36220B756369371893B1814513'
    $entries=@{}
    function Test-Path { param($LiteralPath,$PathType) return $entries.ContainsKey($LiteralPath) }
    function Get-Item { param($LiteralPath) return [pscustomobject]@{Length=$entries[$LiteralPath].bytes} }
    function Get-FileHash { param($LiteralPath,$Algorithm) return [pscustomobject]@{Hash=$entries[$LiteralPath].md5} }
    $entries[$project]=@{bytes=2294091L;md5=$validHash};$entries[$cache]=@{bytes=2294091L;md5=$validHash}
    Check ((Resolve-Rtl11Bit $fakeApp).origin -eq 'project/outflow') 'Matching project bit is preferred'
    $entries[$project].bytes=2L
    Check ((Resolve-Rtl11Bit $fakeApp).origin -eq 'accepted APP cache') 'Wrong-size project falls back to accepted cache'
    $entries[$project].bytes=2294091L;$entries[$project].md5='bad'
    Check ((Resolve-Rtl11Bit $fakeApp).origin -eq 'accepted APP cache') 'Wrong-hash project falls back to accepted cache'
    $entries.Remove($project)
    Check ((Resolve-Rtl11Bit $fakeApp).origin -eq 'accepted APP cache') 'Missing project falls back to accepted cache'
    $entries[$cache].bytes=1L
    MustThrow { Resolve-Rtl11Bit $fakeApp } 'No matching accepted RTL11 image'
    $entries[$cache].bytes=2294091L
    $entries[$fakeExplicit]=@{bytes=2294091L;md5='bad'}
    MustThrow { Resolve-Rtl11Bit $fakeApp $fakeExplicit } 'No matching accepted RTL11 image'
    $entries[$fakeExplicit].bytes=2L;$entries[$fakeExplicit].md5=$validHash
    MustThrow { Resolve-Rtl11Bit $fakeApp $fakeExplicit } 'No matching accepted RTL11 image'
    $entries[$fakeExplicit].bytes=2294091L
    Check ((Resolve-Rtl11Bit $fakeApp $fakeExplicit).origin -eq 'explicit') 'Explicit path cannot silently fall back'
}

# Extract only the pure predicate; do not dot-source executable recovery flow.
$tokens=$null;$parseErrors=$null
$tree=[Management.Automation.Language.Parser]::ParseFile($recover,[ref]$tokens,[ref]$parseErrors)
Check ($parseErrors.Count -eq 0) 'Recovery parses cleanly'
$function=$tree.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-Rtl11DebuggerRecord'},$true)
. ([scriptblock]::Create($function.Extent.Text))
$expectedExe='D:\Efinity\2026.1\debugger\openocd\bin\openocd.exe'
$now=[DateTime]::UtcNow
$record=[pscustomobject]@{record_pid=4242;process_pid=4242;exe=$expectedExe;
    started_utc=$now;record_written_utc=$now.AddSeconds(2);
    command_line='"D:\Efinity\2026.1\debugger\openocd\bin\openocd.exe" -c "telnet_port 24444" -c "tcl_port 26666" -f ftdi_ti.cfg -f debug_ti.cfg'}
Check (Test-Rtl11DebuggerRecord $record $expectedExe) 'Recorded exact loader process is recognized'
$record.record_written_utc=$now.AddSeconds(-1);Check (-not (Test-Rtl11DebuggerRecord $record $expectedExe)) 'PID reuse after PID-file timestamp is rejected'
$record.record_written_utc=$now.AddSeconds(31);Check (-not (Test-Rtl11DebuggerRecord $record $expectedExe)) 'Unrelated older process is rejected'
$record.record_written_utc=$now.AddSeconds(2);$record.exe='C:\unrelated\openocd.exe';Check (-not (Test-Rtl11DebuggerRecord $record $expectedExe)) 'Unrelated binary is rejected'
$record.exe=$expectedExe;$record.process_pid=4243;Check (-not (Test-Rtl11DebuggerRecord $record $expectedExe)) 'Mismatched PID is rejected'
$record.process_pid=4242;$goodCommand=$record.command_line
foreach($bad in @('',($goodCommand -replace 'debug_ti.cfg','other.cfg'),($goodCommand+' -c shutdown'),($goodCommand -replace '24444','4444'),($goodCommand -replace ' -c "telnet',' -f unrelated.cfg -c "telnet'))) {
    $record.command_line=$bad;Check (-not (Test-Rtl11DebuggerRecord $record $expectedExe)) 'Different or unavailable command line is rejected'
}
$record.command_line=$goodCommand

# Execute recovery's preflight only, with all process/network operations mocked.
# Unknown listener must prevent every Stop-Process; recognized Q process may be
# stopped, then a deliberate New-Item boundary aborts before USB/programmer use.
$flowResults=& {
    $qPid=Join-Path $variant 'logs/openocd.pid'
    $mockProcess=[pscustomobject]@{Id=4242;Path=$expectedExe;StartTime=$now.ToLocalTime()}
    $flowState=[pscustomobject]@{phase='unknown';stops=0;network_reads=0}
    function Test-Path {
        param($LiteralPath,$PathType)
        if($LiteralPath -like '*openocd.pid') { return $LiteralPath -eq $qPid }
        return Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath
    }
    function Get-Content { param($LiteralPath,[switch]$Raw) if($LiteralPath -eq $qPid){return '4242'};throw 'Unexpected mock read' }
    function Get-Item { param($LiteralPath) if($LiteralPath -eq $qPid){return [pscustomobject]@{LastWriteTimeUtc=$now.AddSeconds(2)}};return Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath }
    function Get-Process { param($Id) if($Id -eq 4242){return $mockProcess};throw 'Unexpected process lookup' }
    function Get-CimInstance { param($ClassName,$Filter) return [pscustomobject]@{ProcessId=4242;CommandLine=$goodCommand} }
    function Get-NetTCPConnection {
        param($LocalPort,$State)
        $flowState.network_reads++
        if($flowState.phase -eq 'unknown') { return [pscustomobject]@{OwningProcess=9999} }
        if(-not $flowState.stops) { return [pscustomobject]@{OwningProcess=4242} }
    }
    function Stop-Process { param($InputObject,[switch]$Force) Check ($InputObject.Id -eq 4242) 'Only verified Q process may be stopped';$flowState.stops++ }
    function Start-Sleep { param($Milliseconds) }
    function New-Item { throw 'CONTRACT_BOUNDARY_NO_USB' }
    function Start-Process { throw 'No hardware child process allowed in host test' }
    MustThrow { & $recover -BitFile $dry.bit } 'An unowned debugger is listening'
    Check ($flowState.stops -eq 0) 'Unknown listener leaves even recorded processes untouched'
    $flowState.phase='owned'
    MustThrow { & $recover -BitFile $dry.bit } 'CONTRACT_BOUNDARY_NO_USB'
    Check ($flowState.stops -eq 1) 'Current Q record is recognized and termination is bounded to it'
    [pscustomobject]@{unknown_listener_stops=0;verified_Q_stops=$flowState.stops;usb_boundary_reached=$false}
}
$envFinal=Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }
$differentNames=@(Compare-Object $envBefore $envFinal | ForEach-Object { ($_.InputObject -split '=',2)[0] } | Select-Object -Unique)
Check ($differentNames.Count -eq 0) "Failure after environment setup restores all vendor environment variables; changed names=$($differentNames -join ',')"
if(-not $Output) { $Output=Join-Path $variant 'build/host/rr/verification.json' }
$sourceHashes=[ordered]@{}
foreach($name in @('recover_rtl11.ps1','resolve_rtl11.ps1','test_recovery_contract.ps1')) {
    $sourceHashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash
}
$report=[ordered]@{status='PASS';checks=$contract.checks;host_only=$true;hardware_access=$false;
    actual_programming_tested=$false;dry_run=$dry;process_mocks=$flowResults;source_sha256=$sourceHashes;
    limitations=@('Actual FTDI enumeration and volatile JTAG transfer were deliberately not executed.','No live process was queried or terminated; ownership paths use synthetic facts in this contract.')}
New-Item -ItemType Directory -Path (Split-Path -Parent $Output) -Force | Out-Null
[IO.File]::WriteAllText($Output,($report|ConvertTo-Json -Depth 6)+"`n",[Text.Encoding]::UTF8)
Write-Output "RECOVERY_CONTRACT PASS checks=$($contract.checks) hardware_access=0 report=$Output"
