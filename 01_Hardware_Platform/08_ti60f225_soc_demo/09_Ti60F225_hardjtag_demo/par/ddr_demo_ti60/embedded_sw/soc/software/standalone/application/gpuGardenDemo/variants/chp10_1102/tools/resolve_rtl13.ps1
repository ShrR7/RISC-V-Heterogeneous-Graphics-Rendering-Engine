# SPDX-License-Identifier: GPL-3.0-or-later
# Read-only accepted-image resolution. Dot-source to call Resolve-Rtl13Bit.
function Resolve-Rtl13Bit {
    param([Parameter(Mandatory=$true)][string]$AppDir, [string]$BitFile='')
    $expectedMD5='D59183E81A58833B1D550A91EBBE5E23'
    $expectedBytes=2776119L
    $app=[IO.Path]::GetFullPath($AppDir)
    $project=[IO.Path]::GetFullPath((Join-Path $app '../../../../../../'))
    $candidates=if($BitFile) {
        @([pscustomobject]@{path=[IO.Path]::GetFullPath($BitFile);origin='explicit'})
    } else {
        @([pscustomobject]@{path=(Join-Path $project 'outflow/ddr_demo_ti60.bit');origin='project/outflow'},
          [pscustomobject]@{path=(Join-Path $app 'variants/chp10_1102/delivery/ddr_demo_ti60_gpio6_uart.bit');origin='accepted APP cache'})
    }
    $rejected=@()
    foreach($candidate in $candidates) {
        if(-not (Test-Path -LiteralPath $candidate.path -PathType Leaf)) {
            $rejected+="$($candidate.origin): missing $($candidate.path)"
            continue
        }
        $bytes=(Get-Item -LiteralPath $candidate.path -ErrorAction Stop).Length
        if($bytes -ne $expectedBytes) {
            $rejected+="$($candidate.origin): bytes=$bytes, expected=$expectedBytes"
            continue
        }
        $md5=(Get-FileHash -LiteralPath $candidate.path -Algorithm MD5 -ErrorAction Stop).Hash
        if($md5 -ne $expectedMD5) {
            $rejected+="$($candidate.origin): MD5=$md5, expected=$expectedMD5"
            continue
        }
        return [pscustomobject][ordered]@{path=$candidate.path;md5=$md5;bytes=$bytes;origin=$candidate.origin}
    }
    throw ('No matching candidate RTL19 image. '+($rejected -join '; '))
}

