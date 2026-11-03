# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [switch]$Execute,
    [string]$Url='ftdi://0x0403:0x6011:0:ff/1',
    [string]$EfinityRoot='D:/Efinity/2026.1',
    [string]$PythonExe='',
    [string]$Out='',
    [ValidateRange(100000,6000000)][int]$Frequency=3000000
)
$ErrorActionPreference='Stop'
if(-not $PythonExe){
    $candidate=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    if(Test-Path -LiteralPath $candidate -PathType Leaf){$PythonExe=$candidate}
}
if(-not $PythonExe){
    $command=Get-Command python -ErrorAction SilentlyContinue
    if($command -and $command.Source -notmatch 'WindowsApps'){$PythonExe=$command.Source}
}
if(-not $PythonExe -or -not (Test-Path -LiteralPath $PythonExe -PathType Leaf)){
    throw 'Python 3.11+ was not found. Pass -PythonExe with the installed Python executable.'
}
$entry=Join-Path $PSScriptRoot 'tools/install_autoboot.py'
$pyArgs=@($entry,'--bundle',$PSScriptRoot,'--url',$Url,'--efinity',$EfinityRoot,'--frequency',([string]$Frequency))
if($Out){$pyArgs+=@('--out',$Out)}
if($Execute){$pyArgs+='--execute'}
& $PythonExe @pyArgs
if($LASTEXITCODE -ne 0){throw ('Garden Flash installation failed; exit '+$LASTEXITCODE)}
