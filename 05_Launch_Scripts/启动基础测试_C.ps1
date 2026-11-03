# Dedicated RAM-only basic CPU/GPU comparison. Does not write either Flash.
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
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$relative='01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/par/ddr_demo_ti60/embedded_sw/soc/software/standalone/application/gpuGardenDemo/variants/cbasic_min_1103/tools/run_demo.ps1'
$entry=Join-Path $repoRoot $relative
if(-not (Test-Path -LiteralPath $entry -PathType Leaf)){throw 'The cbasic_min_1103 RAM loader is missing.'}
& $entry @PSBoundParameters
