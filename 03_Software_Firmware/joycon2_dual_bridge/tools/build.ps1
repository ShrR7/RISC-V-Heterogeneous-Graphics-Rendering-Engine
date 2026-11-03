# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [ValidateRange(1,8)][int]$Jobs=4,
    [string]$ArduinoCli='D:\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
    [string]$PythonExe='',
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$sdk=Join-Path $workspace '06_Controller_Dependencies/j2sdk'
$deps=Join-Path $workspace '06_Controller_Dependencies/joycon2_dual_20261031/deps'
$sketch=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../joycon2_dual_bridge'))
$cli=$ArduinoCli
$prepare=Join-Path $PSScriptRoot 'prepare_sdk.py'
if(-not $PythonExe){$PythonExe=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'}
if(-not(Test-Path -LiteralPath (Join-Path $sdk 'arduino-cli.yaml'))){throw 'Run 03_Software_Firmware/joycon2_dual_bridge/tools/prepare_sdk.py with local prerequisites and --execute first'}
if($DryRun){
    [ordered]@{hardware_access=$false;network_access=$false;compile=$false;compiler=$cli;
        sdk=$sdk;libraries=$deps;sketch=$sketch;fqbn='esp32:esp32:esp32:FlashSize=4M,FlashMode=dio,FlashFreq=40,CPUFreq=240';
        nimble='2.5.2 pinned compile tree in tools/sdk_archives.json';max_connections=2}|ConvertTo-Json
    return
}
if(-not(Test-Path -LiteralPath $cli -PathType Leaf)){throw 'Arduino CLI is missing; pass -ArduinoCli.'}
if(-not(Test-Path -LiteralPath $PythonExe -PathType Leaf)){throw 'Python 3.11+ is missing; pass -PythonExe.'}
& $PythonExe $prepare --check-build-inputs
if($LASTEXITCODE -ne 0){throw 'Pinned local NimBLE/SDK input check failed; no compilation started'}
$savedTemp=$env:TEMP;$savedTmp=$env:TMP
try {
    $env:TEMP=Join-Path $sdk 'temp';$env:TMP=$env:TEMP
    & $cli compile --jobs $Jobs --config-file (Join-Path $sdk 'arduino-cli.yaml') `
        --fqbn 'esp32:esp32:esp32:FlashSize=4M,FlashMode=dio,FlashFreq=40,CPUFreq=240' `
        --build-path (Join-Path $sdk 'build') --output-dir (Join-Path $sdk 'output') `
        --libraries $deps --warnings all `
        --build-property 'compiler.cpp.extra_flags=-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=2' `
        --build-property 'compiler.c.extra_flags=-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=2' $sketch
    if($LASTEXITCODE -ne 0){throw 'ESP32 bridge build failed'}
} finally {$env:TEMP=$savedTemp;$env:TMP=$savedTmp}
