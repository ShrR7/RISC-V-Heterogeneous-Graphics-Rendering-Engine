# SPDX-License-Identifier: GPL-3.0-or-later
# Build-only. Every generated file belongs to this isolated art variant.
# No UART, JTAG, reset, FPGA configuration, DDR or Flash access is performed.
[CmdletBinding()]
param([switch]$DryRun, [string]$FixedSeed="")
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$variantRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$appRoot = [IO.Path]::GetFullPath((Join-Path $variantRoot '../..'))
$sourceRoot = Join-Path $variantRoot 'src'
$buildRoot = Join-Path $variantRoot 'build'
$projectName = 'gardenStage9Game'
$toolRoot = 'D:\Efinity\efinity-riscv-ide-2026.1\toolchain\bin'
$makeExe = 'D:\mingw64\bin\mingw32-make.exe'
$nativeShell = $env:ComSpec
$relativeVariant = 'variants/cclassic_1102'
$relativeBuild = $relativeVariant + '/build'
$relativeSrc = $relativeVariant + '/src'
$savedMakeShell = $env:MAKESHELL
$savedPath = $env:PATH

if (-not $variantRoot.StartsWith($appRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Variant must be contained in APP'
}
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'garden_assets.c') -PathType Leaf)) {
    throw 'Generate the isolated variant source and assets before building'
}
$sourceFiles = @(Get-ChildItem -LiteralPath $sourceRoot -Filter '*.c' -File | Where-Object { $_.Name -ne 'void_display_counter.c' } | Sort-Object Name)
if (-not $sourceFiles.Count) { throw 'No variant C sources found' }
# Use the compact cumulative display counters; the old verbose wrapper would
# exhaust on-chip program RAM after adding the game/session/stress features.
$sourceArguments = @($sourceFiles | ForEach-Object { $relativeSrc + '/' + $_.Name })
$sourceArguments += '../../common/start.S', '../../common/trap.S'
$sources = $sourceArguments -join ' '
$makeArguments = @('-f', ($relativeVariant + '/tools/Makefile.fx'),
    'BSP=efinix/EfxSapphireSoc', 'DEBUG=no', '-B', '-j4', ('SHELL=' + $nativeShell),
    ('OBJDIR=' + $relativeBuild), ('PROJ_NAME=' + $projectName),
    ('VARIANT_SRC=' + $relativeSrc), ('SRCS=' + $sources))

if ($FixedSeed) {
    if ($FixedSeed -notmatch '^0x[0-9a-fA-F]{1,8}$') { throw 'FixedSeed must be a uint32 hexadecimal literal' }
    $makeArguments += ('CFLAGS_ARGS=-DGARDEN_FIXED_RUN_SEED=' + $FixedSeed + 'U')
}

if ($DryRun) {
    [ordered]@{app=$appRoot;variant=$variantRoot;make=$makeExe;arguments=$makeArguments;
        elf=(Join-Path $buildRoot ($projectName + '.elf'));hardware_access=$false} | ConvertTo-Json -Depth 6
    return
}

# Guard baseline source/atlas/production and accepted ELF identities. The
# builder cannot silently overwrite or relink one of these protected inputs.
$protected = @(Get-ChildItem -LiteralPath (Join-Path $appRoot 'src') -File)
foreach ($item in @('assets/atlas.bin','build/gpuGardenDemo.elf','build/diag_game_cnt_rtl7/gardenDisplayCounterRtl7.elf')) {
    $protectedPath = Join-Path $appRoot $item
    if (Test-Path -LiteralPath $protectedPath -PathType Leaf) { $protected += Get-Item -LiteralPath $protectedPath }
}
$beforeHashes = @{}
foreach ($item in $protected) { $beforeHashes[$item.FullName] = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash }
New-Item -ItemType Directory -Path (Join-Path $buildRoot 'obj_files') -Force | Out-Null
$buildLog = Join-Path $buildRoot 'build.log'
[IO.File]::WriteAllText($buildLog, "Isolated Void art variant build`r`n", [Text.Encoding]::UTF8)
function Invoke-VariantNative {
    param([string]$NativeExe, [string[]]$NativeArguments)
    $savedError = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $output = & $NativeExe @NativeArguments 2>&1; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $savedError }
    foreach ($line in $output) {
        Write-Host $line.ToString()
        Add-Content -LiteralPath $buildLog -Encoding UTF8 -Value $line.ToString()
    }
    if ($code -ne 0) { throw ('Command exit {0}: {1}' -f $code, $NativeExe) }
    return $output
}
Push-Location $appRoot
try {
    foreach ($tool in @($makeExe, $nativeShell, (Join-Path $toolRoot 'riscv-none-elf-gcc.exe'),
        (Join-Path $toolRoot 'riscv-none-elf-readelf.exe'))) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw ('Required tool missing: ' + $tool) }
    }
    $env:MAKESHELL = $nativeShell
    $env:PATH = $toolRoot + ';D:\mingw64\bin;' + $env:PATH
    Invoke-VariantNative $makeExe $makeArguments | Out-Null
    $elf = Join-Path $buildRoot ($projectName + '.elf')
    Invoke-VariantNative (Join-Path $toolRoot 'riscv-none-elf-size.exe') @($elf) | Out-Null
    $headers = @(Invoke-VariantNative (Join-Path $toolRoot 'riscv-none-elf-readelf.exe') @('-l', '--wide', $elf))
    $segments = [regex]::Matches(($headers -join "`n"),
        '(?m)^\s*LOAD\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)')
    if (-not $segments.Count) { throw 'No ELF LOAD segment found' }
    $ranges = @()
    foreach ($segment in $segments) {
        $start = [Convert]::ToUInt64($segment.Groups[1].Value, 16)
        $end = $start + [Convert]::ToUInt64($segment.Groups[2].Value, 16)
        if ($start -lt 0x1000 -or $end -gt 0x20000) { throw 'ELF exceeds program RAM 0x1000..0x20000' }
        $ranges += [ordered]@{start=$start;end_exclusive=$end}
    }
    foreach ($path in $beforeHashes.Keys) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $beforeHashes[$path]) {
            throw ('Protected baseline input changed: ' + $path)
        }
    }
    $manifest = [ordered]@{variant='cclassic_1102';source_files=$sourceArguments;
        flags=('-Os -Wall -Wextra -Werror -flto '+$(if($FixedSeed){'-DGARDEN_FIXED_RUN_SEED='+$FixedSeed+'U'}else{''}));fixed_first_seed=$FixedSeed;elf=$elf;elf_sha256=(Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash;
        load_ranges=$ranges;protected_baseline_unchanged=$true;hardware_access=$false}
    $atlas = Join-Path $variantRoot 'assets/atlas.bin'
    if (Test-Path -LiteralPath $atlas -PathType Leaf) {
        if ((Get-Item -LiteralPath $atlas).Length -gt 0xBA000) { throw 'Variant atlas exceeds the verified DDR window' }
        $manifest['atlas_sha256'] = (Get-FileHash -LiteralPath $atlas -Algorithm SHA256).Hash
        $manifest['atlas_bytes'] = (Get-Item -LiteralPath $atlas).Length
    }
    [IO.File]::WriteAllText((Join-Path $buildRoot 'build_manifest.json'), ($manifest | ConvertTo-Json -Depth 8), [Text.Encoding]::UTF8)
    Write-Host ('BUILD PASS: ' + $elf)
} finally {
    Pop-Location
    $env:MAKESHELL = $savedMakeShell
    $env:PATH = $savedPath
}
