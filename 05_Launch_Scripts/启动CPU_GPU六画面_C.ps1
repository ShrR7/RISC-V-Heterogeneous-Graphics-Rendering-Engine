# Six exclusive full-screen cases. Loads RAM only; does not write Flash.
[CmdletBinding()]
param(
    [ValidateSet('COM11')][string]$Port='COM11',
    [ValidateRange(1,3600)][int]$CaptureSeconds=140,
    [switch]$LoadOnly,
    [switch]$RecoverFpga,
    [switch]$Rebuild,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
$taskRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskEntry=Join-Path $taskRepo '01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/par/ddr_demo_ti60/embedded_sw/soc/software/standalone/application/gpuGardenDemo/variants/csix_1103/tools/run_demo.ps1'
& $taskEntry -Port $Port -CaptureSeconds $CaptureSeconds -LoadOnly:$LoadOnly -Rebuild:$Rebuild -DryRun:$DryRun -RecoverFpga:$RecoverFpga
