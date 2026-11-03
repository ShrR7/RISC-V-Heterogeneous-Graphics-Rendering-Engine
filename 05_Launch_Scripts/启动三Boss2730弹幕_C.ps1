# Run the clean-FPS CBoss pressure variant in RAM, then hold its2730-bullet scene.
# No source rebuild, FPGA Flash write, ESP32 access or foreign process kill.
[CmdletBinding()]
param(
    [ValidateSet('COM11')][string]$Port='COM11',
    [ValidateRange(15,3600)][int]$Seconds=30,
    [switch]$LoadOnly,
    [switch]$DryRun,
    [switch]$RecoverFpga,
    [Alias('PythonExecutable')][string]$PythonExe=''
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$relative='01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/par/ddr_demo_ti60/embedded_sw/soc/software/standalone/application/gpuGardenDemo/variants/cboss_hud_1103'
$variant=Join-Path $repoRoot $relative
if(-not $PythonExe){
    foreach($candidate in @('C:/Users/Jimmy/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe','D:/Efinity/2026.1/python311/bin/python.exe')){
        if(Test-Path -LiteralPath $candidate -PathType Leaf){$PythonExe=$candidate;break}
    }
    if(-not $PythonExe){$found=Get-Command python -ErrorAction SilentlyContinue;if($found){$PythonExe=$found.Source}}
}
if(-not $PythonExe){throw 'A local Python interpreter is required.'}
$entry=Join-Path $variant 'tools/run_2730_demo.ps1'
& $entry -Port $Port -LoadOnly -DryRun:$DryRun -RecoverFpga:$RecoverFpga -PythonExe $PythonExe
if($DryRun){
    [ordered]@{next_action='measure_three_boss';scene='triple';slots=30;bullets=2730;seconds=$Seconds;hold=$true;hardware_access=$false}|ConvertTo-Json
    return
}
if($LoadOnly){return}
# Repeat the physical binding check before opening UART for the scene command.
$ports=@(Get-PnpDevice -PresentOnly -Class Ports | Where-Object {$_.FriendlyName -match '\(COM11\)$'})
if($ports.Count -ne 1 -or $ports[0].InstanceId -notmatch '^FTDIBUS\\VID_0403\+PID_6011\+'){
    throw 'COM11 no longer identifies the expected FTDI; no scene command sent.'
}
$parent=$ports[0].InstanceId;$bound=$false
for($i=0;$i -lt 6;$i++){
    $property=Get-PnpDeviceProperty -InstanceId $parent -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop
    if(-not $property.Data){break}
    $parent=[string]$property.Data
    if($parent -eq 'USB\VID_0403&PID_6011\5&27B58322&0&10'){$bound=$true;break}
}
if(-not $bound){throw 'COM11 is not the verified USB1-10 target; COM25 is protected.'}
$capture=Join-Path (Join-Path $repoRoot '06_Controller_Dependencies') ('boss2730_measure_'+[Guid]::NewGuid().ToString('N'))
& $PythonExe (Join-Path $variant 'tools/measure_three_boss.py') --port $Port --scene triple --slots 30 --seconds $Seconds --hold --out $capture
if($LASTEXITCODE -ne 0){throw ('Scene command/capture failed; preserve '+$capture)}
Write-Host ('Three Boss2730 scene remains running; COM11 released. Evidence: '+$capture)
