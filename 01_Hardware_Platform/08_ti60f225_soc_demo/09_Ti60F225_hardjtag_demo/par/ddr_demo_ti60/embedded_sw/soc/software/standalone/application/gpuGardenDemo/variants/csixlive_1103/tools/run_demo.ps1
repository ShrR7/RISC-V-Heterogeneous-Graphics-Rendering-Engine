# Loads this release into RAM on COM11 / physical USB1-10. Never writes Flash.
[CmdletBinding()]
param(
    [ValidateSet('COM11')][string]$Port='COM11',
    [ValidateRange(1,3600)][int]$CaptureSeconds=240,
    [switch]$LoadOnly,
    [switch]$RefreshInputs,
    [switch]$DryRun,
    [switch]$Rebuild,
    [switch]$RecoverFpga,
    [Alias('PythonExecutable')][string]$PythonExe='',
    [string]$OpenOcdExe='D:/Efinity/2026.1/debugger/openocd/bin/openocd.exe',
    [string]$GdbExe='D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gdb.exe'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$seconds=if($LoadOnly){0}else{$CaptureSeconds}
$variantRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$appRoot=[IO.Path]::GetFullPath((Join-Path $variantRoot '../..'))
$bsp=[IO.Path]::GetFullPath((Join-Path $appRoot '../../../../bsp/efinix/EfxSapphireSoc/openocd'))
$repo=Get-Item -LiteralPath $variantRoot
while($repo -and -not (Test-Path -LiteralPath (Join-Path $repo.FullName '.git'))){$repo=$repo.Parent}
if(-not $repo){throw 'Run this variant from a Git checkout; repository root was not found.'}
if(-not $PythonExe){
    foreach($candidate in @('C:/Users/Jimmy/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe','D:/Efinity/2026.1/python311/bin/python.exe')){
        if(Test-Path -LiteralPath $candidate -PathType Leaf){$PythonExe=$candidate;break}
    }
    if(-not $PythonExe){$found=Get-Command python -ErrorAction SilentlyContinue;if($found){$PythonExe=$found.Source}}
}
foreach($tool in @($PythonExe,$OpenOcdExe,$GdbExe,(Join-Path $bsp 'debug_ti.cfg'))){
    if(-not $tool -or -not (Test-Path -LiteralPath $tool -PathType Leaf)){throw ('Required tool/input missing: '+$tool)}
}
if($Rebuild){
    if($DryRun){& (Join-Path $PSScriptRoot 'build_fx_variant.ps1') -DryRun}
    else {& (Join-Path $PSScriptRoot 'build_fx_variant.ps1')}
    # A rebuild still has to match the recorded runtime_inputs hashes. A new
    # binary requires a new reviewed manifest, not a silently rewritten pin.
}
$loader=Join-Path $PSScriptRoot 'load_demo.py'
if(($Rebuild -or $RefreshInputs) -and -not $DryRun){
    & $PythonExe $loader --variant $variantRoot --write-manifest
    if($LASTEXITCODE -ne 0){throw 'Runtime input sealing failed; no board was accessed.'}
}
if($DryRun){
    & $PythonExe $loader --variant $variantRoot --port $Port --seconds $seconds --gdb $GdbExe --dry-run
    if($LASTEXITCODE -ne 0){throw 'Release input validation failed.'}
    return
}

# A COM number alone is insufficient with two identical FTDI devices attached.
# Trace the PnP ancestry to this exact previously verified physical USB10 board.
$ports=@(Get-PnpDevice -PresentOnly -Class Ports | Where-Object {$_.FriendlyName -match '\(COM11\)$'})
if($ports.Count -ne 1 -or $ports[0].InstanceId -notmatch '^FTDIBUS\\VID_0403\+PID_6011\+'){
    throw 'COM11 is not the expected FTDI board. No board was opened.'
}
$ancestry=@($ports[0].InstanceId)
$parent=$ports[0].InstanceId
for($i=0;$i -lt 6;$i++){
    $property=Get-PnpDeviceProperty -InstanceId $parent -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop
    if(-not $property.Data){break}
    $parent=[string]$property.Data;$ancestry+=@($parent)
    if($parent -eq 'USB\VID_0403&PID_6011\5&27B58322&0&10'){break}
}
if($ancestry -notcontains 'USB\VID_0403&PID_6011\5&27B58322&0&10'){
    throw 'COM11 is not bound to the verified USB1-10 FTDI parent. COM25/USB1-7 and other targets are not accessed.'
}
# Probe exclusive UART access before starting a debugger or halting the CPU.
$uart=New-Object System.IO.Ports.SerialPort($Port,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
try {$uart.DtrEnable=$false;$uart.RtsEnable=$false;$uart.Open()}
finally {if($uart.IsOpen){$uart.Close()};$uart.Dispose()}
foreach($portNumber in @(14333,14444,14666)){
    $listener=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback,$portNumber)
    try {$listener.Start()}
    catch {throw ('Private debugger port '+$portNumber+' is occupied; no process was terminated.')}
    finally {$listener.Stop()}
}
if($RecoverFpga){
    $taskBit=Join-Path $variantRoot 'delivery/ddr_demo_ti60_gpio6_uart.bit'
    if(-not (Test-Path -LiteralPath $taskBit -PathType Leaf)){throw 'Matching FPGA image is missing.'}
    if((Get-FileHash -LiteralPath $taskBit -Algorithm MD5).Hash -ne 'D59183E81A58833B1D550A91EBBE5E23'){throw 'FPGA image MD5 mismatch; no configuration attempted.'}
    & $PythonExe (Join-Path $PSScriptRoot 'program_fpga_ram.py') --bit $taskBit
    if($LASTEXITCODE -ne 0){throw 'Volatile FPGA configuration failed; no Flash operation was performed.'}
}
$stage=Join-Path (Join-Path $repo.FullName '06_Controller_Dependencies') ('sixlive_launch_'+[Guid]::NewGuid().ToString('N'))
if($stage -match '[^\x00-\x7f]|\s'){throw 'GDB requires ASCII staging. Use a checkout whose repository path contains only ASCII characters and no spaces.'}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
& $PythonExe $loader --variant $variantRoot --stage $stage --port $Port --seconds $seconds --gdb $GdbExe --prepare
if($LASTEXITCODE -ne 0){throw ('Release staging failed. No board was halted. See '+$stage)}
[ordered]@{uart=$Port;physical_usb='1-10';pnp_ancestry=$ancestry;protected_uart='COM25';protected_usb='1-7'} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'pnp_binding.json') -Encoding utf8
$cfg=(Join-Path $stage 'board_com11.cfg').Replace('\','/')
$arguments='-c "bindto 127.0.0.1" -c "gdb_port 14333" -c "telnet_port 14444" -c "tcl_port 14666" -f "'+$cfg+'" -f debug_ti.cfg'
$task=$null
try {
    $task=Start-Process -FilePath $OpenOcdExe -ArgumentList $arguments -WorkingDirectory $bsp -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $stage 'openocd.log') -RedirectStandardError (Join-Path $stage 'openocd.err')
    [ordered]@{pid=$task.Id;exe=[IO.Path]::GetFullPath($OpenOcdExe);started_at=$task.StartTime.ToUniversalTime().ToString('o');arguments=$arguments;working_directory=$bsp;uart='COM11';physical_usb='1-10';gdb_port=14333;telnet_port=14444;tcl_port=14666;flash_written=$false} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'openocd_identity.json') -Encoding utf8
    Start-Sleep -Seconds 2
    $task.Refresh()
    if($task.HasExited){throw ('Owned debugger failed to start; inspect '+(Join-Path $stage 'openocd.err'))}
    & $PythonExe $loader --variant $variantRoot --stage $stage --port $Port --seconds $seconds --gdb $GdbExe
    if($LASTEXITCODE -ne 0){throw ('RAM load/startup checks failed; preserve '+$stage)}
    Write-Host 'Independent matched-feature live six-case demo is running; COM11 is released; the demo remains active.'
    Write-Host ('Raw logs and identities: '+$stage)
    Write-Host 'RAM-only standalone demo: power cycling starts the previously installed Flash release.'
} finally {
    # Only this invocation's original Process object is eligible for shutdown.
    # No process-name search, foreign PID termination, or adapter-index fallback.
    if($task){
        $task.Refresh()
        if(-not $task.HasExited){
            try {
                $client=New-Object Net.Sockets.TcpClient('127.0.0.1',14444)
                try {$stream=$client.GetStream();$command=[Text.Encoding]::ASCII.GetBytes("shutdown`n");$stream.Write($command,0,$command.Length)}
                finally {$client.Close()}
                $null=$task.WaitForExit(3000)
            } catch {Write-Warning 'Owned debugger did not answer shutdown.'}
            $task.Refresh()
            if(-not $task.HasExited){$task.Kill();$null=$task.WaitForExit(3000)}
        }
        $task.Dispose()
    }
}
