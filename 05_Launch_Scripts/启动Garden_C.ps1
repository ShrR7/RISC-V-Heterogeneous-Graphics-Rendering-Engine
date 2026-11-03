# Team C: current single-Boss game and original2093 T (RAM load).
[CmdletBinding()]
param([string]$Port='COM11',[int]$Seconds=30,[switch]$Rebuild,[switch]$DryRun,[switch]$RecoverFpga)
$entry=Join-Path (Split-Path -Parent $PSScriptRoot) '01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/par/ddr_demo_ti60/embedded_sw/soc/software/standalone/application/gpuGardenDemo/variants/cclassic_1102/tools/run_game.ps1'
if(-not(Test-Path -LiteralPath $entry -PathType Leaf)){throw 'Current Team C release is missing.'}
& $entry @PSBoundParameters
