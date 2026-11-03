# SPDX-License-Identifier: GPL-3.0-or-later
# Host-only: compile each real source with renamed public model symbols.
$ErrorActionPreference='Stop'
$v=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$old=[IO.Path]::GetFullPath((Join-Path $v '../cgame60h_1030'))
$workspace=$v
while(-not(Test-Path -LiteralPath (Join-Path $workspace '01_Hardware_Platform') -PathType Container)){
    $parent=Split-Path -Parent $workspace
    if(-not $parent -or $parent -eq $workspace){throw 'Workspace root was not found'}
    $workspace=$parent
}
# MinGW's native startup/CRT can truncate an executable path beyond MAX_PATH.
$out=Join-Path $workspace '06_Controller_Dependencies/host_model_equivalence'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$gcc='D:\mingw64\bin\gcc.exe'
$flags=@('-std=c11','-Wall','-Wextra','-Werror','-Os','-DPURE_BULLET_HOST_TEST')
& $gcc @flags ('-I'+$old+'/src') '-Dpure_bullet_test_model_init=baseline_model_init' '-Dpure_bullet_test_model_build=baseline_model_build' '-c' (Join-Path $old 'src/pure_bullet_test.c') '-o' (Join-Path $out 'baseline.o')
if($LASTEXITCODE -ne 0){throw 'baseline compile failed'}
& $gcc @flags ('-I'+$v+'/src') '-Dpure_bullet_test_model_init=candidate_model_init' '-Dpure_bullet_test_model_build=candidate_model_build' '-c' (Join-Path $v 'src/pure_bullet_test.c') '-o' (Join-Path $out 'candidate.o')
if($LASTEXITCODE -ne 0){throw 'candidate compile failed'}
$exe=Join-Path $out 'test_pure_model_equivalence.exe'
& $gcc @flags ('-I'+$v+'/src') (Join-Path $PSScriptRoot 'test_pure_model_equivalence.c') (Join-Path $out 'baseline.o') (Join-Path $out 'candidate.o') '-o' $exe
if($LASTEXITCODE -ne 0){throw 'equivalence harness compile failed'}
$log=Join-Path $v 'build/pure_model_equivalence_test.txt'
Copy-Item -LiteralPath (Join-Path $v 'assets/pure_scatter16.bin') -Destination (Join-Path $out 'scatter16.bin')
Push-Location $out
try {
    & '.\test_pure_model_equivalence.exe' 'scatter16.bin' 2>&1 | Tee-Object -FilePath $log
    if($LASTEXITCODE -ne 0){throw 'model equivalence failed'}
} finally { Pop-Location }
