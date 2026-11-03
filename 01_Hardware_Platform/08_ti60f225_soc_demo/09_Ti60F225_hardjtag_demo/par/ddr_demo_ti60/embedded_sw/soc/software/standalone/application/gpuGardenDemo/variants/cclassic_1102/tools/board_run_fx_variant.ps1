# SPDX-License-Identifier: GPL-3.0-or-later
# Isolated Void art adapter. Never programs FPGA or Flash.
# Derived from the accepted C board_run_rtl8 safety/capture tool.
# Original source SHA256: 0b6ddc4aca087aeec6f4d75e046a51957739231da3ab70147f9a0a61505b4caf
# All log/load outputs remain in this variant; only recorded debugger
# ownership may be read from the original APP logs to replace its server.
param(
    [string]$Port = 'COM11',
    [int]$Seconds = 45,
    [string]$LogName = 'bringup.log',
    [string]$ElfFile = '',
    [string]$BitFile = '',
    [switch]$SkipAtlas,
    [switch]$ReuseCheckedAssets,
    [ValidatePattern('^t$|^(k)?m?[0-5]?$')][string]$InitialInput='',
    [switch]$CaptureOnly,
    [string]$ProbeCommand = '',
    [switch]$PrewarmOff,
    [switch]$InputTest,
    [switch]$UiInputTest,
    [switch]$FrameTimingTest,
    [switch]$AnimationProfile,
    [switch]$MotionProfile,
    [switch]$PureInputTest,
    [switch]$TransitionTest,
    [switch]$Acceptance600,
    [switch]$PreserveBoardState,
    [string]$PreservedFaultDirectory = '',
    [switch]$RelockOn,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if ($Seconds -le 0) { throw 'Seconds must be positive' }
# Always retain the validated RTL11 relock setting on a game download.
if (-not $CaptureOnly) { $RelockOn = $true }
if ($CaptureOnly -and ($PreserveBoardState -or $RelockOn)) { throw 'PreserveBoardState and RelockOn require an ELF download; CaptureOnly is read-only capture' }
if ($ProbeCommand -and (-not $CaptureOnly -or $ProbeCommand -ne 'b')) { throw 'ProbeCommand supports only b with CaptureOnly' }
if ($PrewarmOff -and $CaptureOnly) { throw 'PrewarmOff requires a halted diagnostic download' }
if (($InputTest -and $UiInputTest) -or ($FrameTimingTest -and ($InputTest -or $UiInputTest))) { throw 'Choose one input test profile' }
if ($UiInputTest -and $Seconds -lt 112) { throw 'UI input test requires at least 112 seconds' }
if ($FrameTimingTest -and $Seconds -lt 162) { throw 'Frame timing test requires at least 162 seconds' }
if ($AnimationProfile -and ($InputTest -or $UiInputTest -or $FrameTimingTest -or $CaptureOnly)) { throw 'AnimationProfile is an independent download/preview profile' }
if ($AnimationProfile -and $Seconds -lt 125) { throw 'AnimationProfile requires at least 125 seconds' }
if ($MotionProfile -and ($AnimationProfile -or $InputTest -or $UiInputTest -or $FrameTimingTest -or $CaptureOnly)) { throw 'MotionProfile is an independent download/preview profile' }
if ($MotionProfile -and $Seconds -lt 60) { throw 'MotionProfile requires at least 60 seconds' }
if ($TransitionTest -and ($CaptureOnly -or $InputTest -or $UiInputTest -or $FrameTimingTest -or $AnimationProfile -or $MotionProfile -or $InitialInput -or $ProbeCommand)) { throw 'TransitionTest is an independent download/capture profile' }
if ($PureInputTest -and ($CaptureOnly -or $InitialInput -or $ProbeCommand -or $TransitionTest -or $Acceptance600 -or $InputTest -or $UiInputTest -or $FrameTimingTest -or $AnimationProfile -or $MotionProfile)) { throw 'PureInputTest is an independent fresh-load capture profile' }
if ($PureInputTest -and $Seconds -lt 140) { throw 'PureInputTest requires at least 140 host seconds' }
if ($Acceptance600 -and -not $TransitionTest) { throw 'Acceptance600 requires TransitionTest' }
if ($Acceptance600 -and $Seconds -lt 660) { throw 'Acceptance600 requires at least 660 host seconds' }
if ($TransitionTest -and -not $Acceptance600) { $Seconds = [Math]::Min($Seconds,240) }
$variantDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$appDir = [IO.Path]::GetFullPath((Join-Path $variantDir '../..'))
$logDir = Join-Path $variantDir 'logs'
if ($LogName -ne [IO.Path]::GetFileName($LogName)) { throw 'LogName must be a filename' }
$logPath = Join-Path $logDir $LogName
$pidPath = Join-Path $logDir 'openocd.pid'
$ocdExe = 'D:\Efinity\2026.1\debugger\openocd\bin\openocd.exe'
$gdbExe = 'D:\Efinity\efinity-riscv-ide-2026.1\toolchain\bin\riscv-none-elf-gdb.exe'
$bspOcd = [IO.Path]::GetFullPath((Join-Path $appDir '../../../../bsp/efinix/EfxSapphireSoc/openocd'))
$elfPath = if ($ElfFile) { [IO.Path]::GetFullPath($ElfFile) } else { Join-Path $variantDir 'build/gardenStage9Game.elf' }
$atlasPath = Join-Path $variantDir 'assets/atlas.bin'
$mixPath = Join-Path $variantDir 'assets/pure_scatter16.bin'
$mixMetadataPath = Join-Path $variantDir 'assets/pure_scatter16.json'
$mixAddress = [uint32]0x009B0000
function Read-PureScatterAssetProof {
    param([string]$VariantPath,$BuildManifest=$null)
    function Mix-U32($Value){
        if([string]$Value -match '^0[xX]'){return [Convert]::ToUInt32(([string]$Value).Substring(2),16)}
        return [uint32]$Value
    }
    function Mix-Crc32([byte[]]$Bytes,[int]$Offset,[int]$Count){
        [uint32]$crc=[uint32]0xFFFFFFFFL
        for($ix=$Offset;$ix -lt $Offset+$Count;$ix++){
            $crc=$crc -bxor [uint32]$Bytes[$ix]
            for($bit=0;$bit -lt 8;$bit++){
                if($crc -band 1){$crc=($crc -shr 1) -bxor [uint32]0xEDB88320L}else{$crc=$crc -shr 1}
            }
        }
        return [uint32]($crc -bxor [uint32]0xFFFFFFFFL)
    }
    $blobPath=Join-Path $VariantPath 'assets/pure_scatter16.bin'
    $metadataPath=Join-Path $VariantPath 'assets/pure_scatter16.json'
    $metadata=Get-Content -LiteralPath $metadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $blob=[IO.File]::ReadAllBytes($blobPath)
    $blobHash=(Get-FileHash -LiteralPath $blobPath -Algorithm SHA256).Hash
    if($blob.Length -lt 22848 -or $blob.Length -ge 65536 -or [long]$metadata.blob_bytes -ne $blob.Length){throw 'Scatter asset dynamic length mismatch'}
    if($blobHash -ne $metadata.blob_sha256){throw 'Scatter asset SHA256 mismatch'}
    if((Mix-U32 $metadata.load_address) -ne 0x009B0000){throw 'Scatter metadata load address mismatch'}
    $h=@();for($i=0;$i -lt 16;$i++){$h+=[BitConverter]::ToUInt32($blob,4*$i)}
    if($h[0] -ne 0x32584D50 -or $h[1] -ne 2 -or $h[2] -ne $blob.Length -or $h[3] -ne $blob.Length-64){throw 'Scatter v2 header identity/length mismatch'}
    if($h[5] -ne 27 -or $h[6] -ne 91 -or $h[7] -ne 2730){throw 'Scatter tile/ray/plan count mismatch'}
    if($h[8] -ne 64 -or $h[9] -ne $h[8]+$h[5]*16 -or $h[10] -ne $h[9]+$h[6]*16 -or
       $h[11] -ne $h[10]+$h[7]*4 -or $h[12] -ne $h[11]+$h[5]*512 -or $h[12]+$h[13] -ne $h[2]){throw 'Scatter section offsets overlap or are not contiguous'}
    if($h[14] -ne (320 -bor (16 -shl 16)) -or $h[15] -ne 30){throw 'Scatter source/phase-slot header mismatch'}
    $payloadCrc=Mix-Crc32 $blob 64 ($blob.Length-64)
    if($payloadCrc -ne $h[4] -or $payloadCrc -ne (Mix-U32 $metadata.payload_crc32)){throw 'Scatter payload CRC mismatch'}
    if((Mix-Crc32 $blob 0 $blob.Length) -ne (Mix-U32 $metadata.blob_crc32)){throw 'Scatter whole blob CRC mismatch'}
    if([int]$metadata.count -ne 27 -or [int]$metadata.tile_count -ne 27 -or [int]$metadata.ray_count -ne 91 -or [int]$metadata.plan_count -ne 2730 -or [int]$metadata.payload_bytes -ne $h[3]){throw 'Scatter JSON counts mismatch'}
    if([int]$metadata.per_ray -ne 22 -or [int]$metadata.per_ray_min -ne 8 -or [int]$metadata.per_ray_max -ne 30 -or [int]$metadata.max_count -ne 2730 -or [int]$metadata.legacy_clear_local_rows -ne 91){throw 'Scatter capacity range/clear mismatch'}
    $metaWords=@();foreach($prop in $metadata.header.PSObject.Properties){$metaWords+=(Mix-U32 $prop.Value)}
    if($metaWords.Count -ne 16){throw 'Scatter JSON header word count mismatch'}
    for($i=0;$i -lt 16;$i++){if($metaWords[$i] -ne $h[$i]){throw 'Scatter JSON header differs from binary'}}
    if(@($metadata.styles).Count -ne 27 -or @($metadata.rays).Count -ne 91){throw 'Scatter JSON style/ray tables mismatch'}
    for($tile=0;$tile -lt 27;$tile++){
        $style=$metadata.styles[$tile];$off=$h[8]+16*$tile
        [uint32]$packed=([uint32]$style.tile_anchor[0]) -bor (([uint32]$style.tile_anchor[1]) -shl 8) -bor (([uint32]$style.source_width) -shl 16) -bor (([uint32]$style.source_height) -shl 24)
        if([BitConverter]::ToUInt32($blob,$off) -ne [uint32]$style.source_asset -or [BitConverter]::ToUInt32($blob,$off+4) -ne $packed -or
           [BitConverter]::ToUInt32($blob,$off+8) -ne (Mix-U32 $style.source_crc32) -or [BitConverter]::ToUInt32($blob,$off+12) -ne (Mix-U32 $style.tile_crc32)){throw 'Scatter tile metadata differs from binary'}
        if((Mix-Crc32 $blob ($h[11]+512*$tile) 512) -ne (Mix-U32 $style.tile_crc32)){throw 'Scatter tile pixels CRC mismatch'}
    }
    $rayFamilies=@();$rayTiles=@();$setupWeightSum=0
    for($ray=0;$ray -lt 91;$ray++){
        $off=$h[9]+16*$ray;$packed=[BitConverter]::ToUInt32($blob,$off+4)
        $tile=($packed -shr 16) -band 255;$family=($packed -shr 24) -band 255
        if($tile -ge 27 -or $family -ge 7 -or [BitConverter]::ToUInt32($blob,$off+8) -ne 0){throw 'Scatter ray metadata/fixed-TTL mismatch'}
        $style=$metadata.styles[$tile]
        $setup=[BitConverter]::ToUInt32($blob,$off+12)
        if(($setup -band 65535) -le 0 -or ($setup -band 65535) -gt 2000 -or ($setup -shr 16) -ge 65536){throw 'Scatter target planner weight/phase invalid'}
        if(($packed -band 255) -ne $style.tile_anchor[0] -or (($packed -shr 8) -band 255) -ne $style.tile_anchor[1]){throw 'Scatter ray/tile anchor mismatch'}
        if([BitConverter]::ToInt16($blob,$off) -eq 0 -and [BitConverter]::ToInt16($blob,$off+2) -eq 0){throw 'Scatter zero ray velocity'}
        $setupWeightSum+=($setup -band 65535);$rayFamilies+=$family;$rayTiles+=$tile
    }
    if($setupWeightSum -ne 2002){throw 'Scatter planner base weights must sum2002'}
    $families=New-Object int[] 7;$tiles=New-Object int[] 27;$rayCounts=New-Object int[] 91;$largestBirth=0
    for($slot=0;$slot -lt 2730;$slot++){
        $off=$h[10]+4*$slot;$ray=[BitConverter]::ToUInt16($blob,$off);$birth=[BitConverter]::ToUInt16($blob,$off+2)
        if($ray -ge 91 -or $birth -le 0 -or $birth -gt $h[15]){throw 'Scatter birth plan out of range'}
        $families[$rayFamilies[$ray]]++;$tiles[$rayTiles[$ray]]++;$rayCounts[$ray]++;$largestBirth=[Math]::Max($largestBirth,$birth)
    }
    if(@($metadata.family_counts).Count -ne 7 -or @($metadata.tile_counts).Count -ne 27 -or @($metadata.plan_counts).Count -ne 91){throw 'Scatter JSON plan summaries missing'}
    for($i=0;$i -lt 7;$i++){if($families[$i] -ne $metadata.family_counts[$i]){throw 'Scatter family JSON plan summary mismatch'}}
    for($i=0;$i -lt 27;$i++){if($tiles[$i] -ne $metadata.tile_counts[$i]){throw 'Scatter tile JSON plan summary mismatch'}}
    for($i=0;$i -lt 91;$i++){if($rayCounts[$i] -ne $metadata.plan_counts[$i]){throw 'Scatter ray JSON plan summary mismatch'}}
    if([int]$metadata.plan_frames -ne $h[15] -or [string]$metadata.despawn_policy -ne 'boundary' -or [string]$metadata.edge_policy -ne 'last_full_16x16_tile' -or [int]$metadata.fixed_ttl -ne 0){throw 'Scatter JSON phase/boundary policy mismatch'}
    if($largestBirth -ne $h[15]){throw 'Scatter phase-slot plan mismatch'}
    $cursor=[int]$h[12];$formatCount=0
    foreach($prop in ($metadata.format_strings.PSObject.Properties|Sort-Object {[int]$_.Value.offset})){
        $format=$prop.Value;$off=[int]$format.offset;$length=[int]$format.bytes
        if($off -ne $cursor -or $length -lt 2 -or $off+$length -gt $blob.Length -or $blob[$off+$length-1] -ne 0){throw 'Scatter format offsets/termination invalid'}
        for($j=0;$j -lt $length-1;$j++){$value=$blob[$off+$j];if($value -eq 0 -or ($value -lt 32 -and $value -ne 10 -and $value -ne 13) -or $value -gt 126){throw 'Scatter format is not printable ASCII'}}
        $formatBytes=New-Object byte[] $length;[Array]::Copy($blob,$off,$formatBytes,0,$length)
        $sha=[Security.Cryptography.SHA256]::Create();try{$formatHash=([BitConverter]::ToString($sha.ComputeHash($formatBytes))).Replace('-','')}finally{$sha.Dispose()}
        if($formatHash -ne $format.sha256){throw 'Scatter format SHA256 mismatch'}
        $formatText=[Text.Encoding]::ASCII.GetString($formatBytes,0,$length-1)
        $withoutSpec=[regex]::Replace($formatText,'%%|%[0-9]*[uxd]','')
        if($withoutSpec.Contains('%')){throw 'Scatter format has unsupported printf conversion'}
        $formatArgs=@{START=12;REPORT=41;WARMUP=5;PERIOD=8;PREP=10;FAIL=4;EXIT=4;LEFT=0;FOOTER=0;KEYS=0}
        if(-not $formatArgs.ContainsKey($prop.Name) -or [regex]::Matches($formatText,'%[0-9]*[uxd]').Count -ne $formatArgs[$prop.Name]){throw 'Scatter printf argument-count ABI mismatch'}
        $cursor+=$length;$formatCount++
    }
    if($cursor -ne $blob.Length -or $formatCount -ne 10){throw 'Scatter format section is incomplete'}
    if($metadata.source_atlas_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $VariantPath 'assets/atlas.bin') -Algorithm SHA256).Hash){throw 'Scatter source atlas SHA256 mismatch'}
    $start=[long]0x009B0000;$end=$start+$blob.Length
    if($end -gt 0x00A00000 -or $start -lt 0x009B0000){throw 'Pure mix asset escapes reserved DDR window'}
    $ranges=@(
        @{name='atlas';start=[long]0x005C4000;end=([long]0x005C4000+(Get-Item -LiteralPath (Join-Path $VariantPath 'assets/atlas.bin')).Length)},
        @{name='stage9_tiles';start=[long]0x00900000;end=([long]0x00900000+(Get-Item -LiteralPath (Join-Path $VariantPath 'assets/stage9_tiles.bin')).Length)},
        @{name='Stage9 descriptors/atlases';start=[long]0x00800000;end=[long]0x00850000},
        @{name='normal game runtime DDR';start=[long]0x00A00000;end=[long]0x01000000})
    if($null -ne $BuildManifest){
        if(-not $BuildManifest.load_ranges){throw 'ELF load ranges missing from build manifest'}
        foreach($range in $BuildManifest.load_ranges){$ranges+=@{name='ELF';start=[long]$range.start;end=[long]$range.end_exclusive}}
    }
    foreach($range in $ranges){if($start -lt $range.end -and $range.start -lt $end){throw ('Pure mix restore overlaps '+$range.name)}}
    return [pscustomobject][ordered]@{path=$blobPath;metadata_path=$metadataPath;sha256=$blobHash;
        metadata_sha256=(Get-FileHash -LiteralPath $metadataPath -Algorithm SHA256).Hash;
        bytes=$blob.Length;address='0x009B0000';end_exclusive=('0x{0:X8}' -f $end);checked_ranges=$ranges}
}
$baselinePidPath = Join-Path $appDir 'logs/openocd.pid'
$acceptedFxPidPath = Join-Path $appDir 'variants/neon_heading_precision_20261015/logs/openocd.pid'
$previousVariantPidPath = Join-Path $appDir 'variants/neon_variety_20261016/logs/openocd.pid'
$parentVariantPidPath = Join-Path $appDir 'variants/neon_damage_sum_20261023/logs/openocd.pid'
$probePidPath = Join-Path $appDir 'variants/batch_probe_20261021/logs/openocd.pid'
$capacityVariantPidPath = Join-Path $appDir 'variants/bullet_capacity_20261023/logs/openocd.pid'
$newCapPidPath = Join-Path $appDir 'variants/cap14_1025/logs/openocd.pid'
$newGamePidPath = Join-Path $appDir 'variants/game14_1025/logs/openocd.pid'
$newStage9PidPath = Join-Path $appDir 'variants/st9_1025/logs/openocd.pid'
$oldCapPidPath = Join-Path $appDir 'variants/bullet_capacity_20261023/logs/openocd.pid'
$previousRtl15PidPath = Join-Path $appDir 'variants/g9d_1025/logs/openocd.pid'
$previousRtl16PidPath = Join-Path $appDir 'variants/g9e_1026/logs/openocd.pid'
$previousP2hPidPath = Join-Path $appDir 'variants/p2h_1027/logs/openocd.pid'
$previousP2mPidPath = Join-Path $appDir 'variants/p2m_1027/logs/openocd.pid'
$previousP2aPidPath = Join-Path $appDir 'variants/p2a_1027/logs/openocd.pid'
$previousP2fPidPath = Join-Path $appDir 'variants/p2f_1026/logs/openocd.pid'
$previousP2gPidPath = Join-Path $appDir 'variants/p2g_1026/logs/openocd.pid'
$previousP2kPidPath = Join-Path $appDir 'variants/p2k_1026/logs/openocd.pid'
$previousP3dPidPath = Join-Path $appDir 'variants/p3d_1027/logs/openocd.pid'
$previousP1uPidPath = Join-Path $appDir 'variants/p1u_1027/logs/openocd.pid'
$previousP2uPidPath = Join-Path $appDir 'variants/p2u_1029/logs/openocd.pid'
$previousP2bPidPath = Join-Path $appDir 'variants/p2b_1029/logs/openocd.pid'
$previousP2bsPidPath = Join-Path $appDir 'variants/p2bs_1029/logs/openocd.pid'
$previousP2bcPidPath = Join-Path $appDir 'variants/p2bc_1029/logs/openocd.pid'
$previousP2bcoPidPath = Join-Path $appDir 'variants/p2bco_1029/logs/openocd.pid'
$previousP2bco7PidPath = Join-Path $appDir 'variants/p2bco7_1029/logs/openocd.pid'
$previousP2bco7cPidPath = Join-Path $appDir 'variants/p2bco7c_1029/logs/openocd.pid'
$previousP2bco7clPidPath = Join-Path $appDir 'variants/p2bco7cl_1029/logs/openocd.pid'
$previousP2bco6cPidPath = Join-Path $appDir 'variants/p2bco6c_1029/logs/openocd.pid'
$previousP2bco6csPidPath = Join-Path $appDir 'variants/p2bco6cs_1029/logs/openocd.pid'
$previousP2bco6cpPidPath = Join-Path $appDir 'variants/p2bco6cp_1029/logs/openocd.pid'
$previousPk6PidPath = Join-Path $appDir 'variants/pk6_1027/logs/openocd.pid'
$previousPk7PidPath = Join-Path $appDir 'variants/pk7_1027/logs/openocd.pid'
$previousPk8PidPath = Join-Path $appDir 'variants/pk8_1027/logs/openocd.pid'
$previousP2sPidPath = Join-Path $appDir 'variants/p2s_1027/logs/openocd.pid'
$previousG19PidPath = Join-Path $appDir 'variants/g19_1026/logs/openocd.pid'
$previousG9gPidPath = Join-Path $appDir 'variants/g9g_1026/logs/openocd.pid'
$previousClearPidPath = Join-Path $appDir 'variants/g9f_1026/logs/openocd.pid'
$oldCandidatePidPath = Join-Path $appDir 'variants/g9_1025/logs/openocd.pid'
$oldGamePidPath = Join-Path $appDir 'variants/neon_bullet_large_20261023/logs/openocd.pid'
$localPinnedBit = Join-Path $variantDir 'delivery/ddr_demo_ti60_rtl19.bit'
$projectBit = [IO.Path]::GetFullPath((Join-Path $appDir '../../../../../../outflow/ddr_demo_ti60.bit'))
$bitPath = if ($BitFile) { [IO.Path]::GetFullPath($BitFile) }
    elseif (Test-Path -LiteralPath $localPinnedBit -PathType Leaf) { $localPinnedBit }
    else { $projectBit }
$expectedBitMd5 = 'D59183E81A58833B1D550A91EBBE5E23'
if ($DryRun) {
    [ordered]@{variant=$variantDir;app=$appDir;elf=$elfPath;atlas=$atlasPath;
        pure_scatter_asset=$mixPath;pure_scatter_metadata=$mixMetadataPath;pure_scatter_restore_address='0x009B0000';pure_scatter_restore_requested=(-not [bool]$CaptureOnly);
        restore_address='0x005c4000';bit=$bitPath;expected_bit_md5=$expectedBitMd5;
        log=$logPath;port=$Port;seconds=$Seconds;transition_test=[bool]$TransitionTest;
        pure_input_test=[bool]$PureInputTest;acceptance600_requested=[bool]$Acceptance600;relock_on=[bool]$RelockOn;hardware_access=$false} | ConvertTo-Json -Depth 6
    return
}
if (Test-Path -LiteralPath $logPath) { throw 'Use a new LogName; an existing capture must not be overwritten' }
if (-not $CaptureOnly) {
    if (-not $elfPath.StartsWith($variantDir + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'ELF must belong to this isolated variant' }
    if ($elfPath.Length -ge 260) { throw 'GDB requires an ELF path below 260 characters; use start_garden.ps1 to stage the sealed ELF before reset.' }
    if ($SkipAtlas) { throw 'Use ReuseCheckedAssets only for this same program; both runtime CRC checks remain mandatory' }
    if ((Get-FileHash -LiteralPath $bitPath -Algorithm MD5).Hash -ne $expectedBitMd5) { throw 'GPIO6 UART image MD5 mismatch' }
    # Disk hash records provenance of the previously configured bit. It is not
    # an FPGA readback and does not claim to distinguish RTL10 from RTL11 IDs.
    $manifestPath = Join-Path $variantDir 'build/build_manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash -ne $manifest.elf_sha256) { throw 'Variant ELF changed after verified build' }
    if ((Get-FileHash -LiteralPath $atlasPath -Algorithm SHA256).Hash -ne $manifest.atlas_sha256) { throw 'Variant atlas changed after verified build' }
    $tilesPath = Join-Path $variantDir 'assets/stage9_tiles.bin'
    if ((Get-FileHash -LiteralPath $tilesPath -Algorithm SHA256).Hash -ne '6A73AC55168BAE6B84853F7D75FB57F960D63E52729235749610DF8D7F0B07DB') { throw 'Stage9 tile library changed' }
    # A new mixed test blob is loaded even when old atlas/tile reuse is requested.
    # Validate every restore range before opening UART or replacing a debugger.
    $mixProof=Read-PureScatterAssetProof -VariantPath $variantDir -BuildManifest $manifest
}
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$serial = New-Object IO.Ports.SerialPort $Port,115200,'None',8,'One'
# A halted/reloaded target may leave one old telemetry group queued while
# GDB loads RAM. Keep the old tail and complete new startup, rather than
# overflowing the default 4KiB receive queue before the capture loop starts.
$serial.ReadBufferSize = 65536
$serial.ReadTimeout = 100
$serial.WriteTimeout = 500
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$streamLog = $null
$hostLog = $null
$timingLog = $null
$timingBuffer = ''
$transitionLog = $null
$transitionBuffer = ''
$transitionExitCode = 0
$transitionState = @{startup_seen=$false;boss1_seen=$false;victory1_seen=$false;
    auto_round2_seen=$false;boss2_seen=$false;boss2_at=0.0;manual_r_sent=$false;
    round3_seen=$false;round3_at=0.0;round3_perf_count=0;fatal_seen=$false;last_device_ms=[long]0;
    serial_open_count=0;serial_error_count=0;reopen_count=0}
$pureLog=$null
$pureBuffer=''
$pureState=@{fresh_seen=$false;boot_banners=0;ready_seen=$false;ready_at=0.0;
    enter_sent=$false;pure_seen=$false;pure_at=0.0;pure_reports=0;generation=0;
    exit_sent=$false;exit_seen=$false;exit_at=0.0;normal_game_seen=$false;normal_lives=0;
    fatal_seen=$false;protocol_error=''}
function Write-PureEvent {
    param([string]$Type,[double]$HostSeconds,[long]$ByteOffset,[string]$Line,
          [IO.StreamWriter]$Writer,[hashtable]$Extra=@{})
    $row=[ordered]@{schema=1;type=$Type;host_monotonic_s=$HostSeconds;raw_byte_offset=$ByteOffset;line=$Line}
    foreach($key in $Extra.Keys){$row[$key]=$Extra[$key]}
    $Writer.WriteLine(($row|ConvertTo-Json -Compress -Depth 5))
}
function Write-PureChunk {
    param([string]$Chunk,[double]$HostSeconds,[long]$ByteOffset,[ref]$Buffer,
          [IO.StreamWriter]$Writer,[hashtable]$State)
    $Buffer.Value+=$Chunk
    while(($newline=$Buffer.Value.IndexOf("`n")) -ge 0){
        $line=$Buffer.Value.Substring(0,$newline).TrimEnd([char]"`r")
        $Buffer.Value=$Buffer.Value.Substring($newline+1)
        if($line -match '^GARDEN Ti60 cropped port '){
            $State.boot_banners++
            if($State.fresh_seen){$State.protocol_error='unexpected_second_boot'}
            $State.fresh_seen=$true
            Write-PureEvent 'rx' $HostSeconds $ByteOffset $line $Writer
            continue
        }
        # Retain old UART tail in raw, but it cannot arm this new profile.
        if(-not $State.fresh_seen){continue}
        if($line -match '^\[(PURE_FAIL|G9_FATAL|TRAP)\]' -or $line -match '^RESULT:.*FAIL'){
            $State.fatal_seen=$true
            Write-PureEvent 'fatal' $HostSeconds $ByteOffset $line $Writer
        } elseif($line -match '^\[(READY|PURE_WARMUP|PURE_START|PURE|PURE_EXIT|GAME|LIVE|INPUT_STATE|G9_PERF|G9_FRAME)\]'){
            Write-PureEvent 'rx' $HostSeconds $ByteOffset $line $Writer
        }
        if($line -match '^\[READY\]' -and -not $State.ready_seen){$State.ready_seen=$true;$State.ready_at=$HostSeconds}
        if($line -match '^\[PURE_START\].*\bgeneration=(\d+)\b'){
            if(-not $State.enter_sent -or $State.pure_seen){$State.protocol_error='unexpected_pure_start'}
            $State.pure_seen=$true;$State.pure_at=$HostSeconds;$State.generation=[int]$Matches[1]
        }
        if($line -match '^\[PURE\]' -and $State.pure_seen -and -not $State.exit_seen){$State.pure_reports++}
        if($line -match '^\[PURE_EXIT\]'){
            if(-not $State.exit_sent -or -not $State.pure_seen -or $State.exit_seen){$State.protocol_error='unexpected_pure_exit'}
            $State.exit_seen=$true;$State.exit_at=$HostSeconds
        }
        if($State.exit_seen -and $line -match '^\[GAME\]'){$State.normal_game_seen=$true}
        if($State.exit_seen -and $line -match '^\[LIVE\]'){$State.normal_lives++}
    }
}
function Test-PureReady {
    param([hashtable]$State,[double]$HostSeconds,[int]$SerialErrors)
    return $State.fresh_seen -and $State.boot_banners -eq 1 -and $State.ready_seen -and
        $State.enter_sent -and $State.pure_seen -and $State.pure_reports -ge 2 -and
        $State.exit_sent -and $State.exit_seen -and $State.normal_game_seen -and
        $State.normal_lives -ge 2 -and $HostSeconds-$State.exit_at -ge 6.0 -and
        -not $State.fatal_seen -and -not $State.protocol_error -and $SerialErrors -eq 0
}
$ocdProcess = $null
function Write-TransitionEvent {
    param([string]$Type,[double]$HostSeconds,[long]$ByteOffset,[string]$Line,
          [IO.StreamWriter]$Writer,[hashtable]$Extra=@{})
    $record=[ordered]@{schema=1;type=$Type;host_monotonic_s=$HostSeconds;
        raw_byte_offset=$ByteOffset;line=$Line}
    foreach($key in $Extra.Keys){$record[$key]=$Extra[$key]}
    $Writer.WriteLine(($record|ConvertTo-Json -Compress -Depth 5))
}
function Write-TransitionChunk {
    param([string]$Chunk,[double]$HostSeconds,[long]$ByteOffset,[ref]$Buffer,
          [IO.StreamWriter]$Writer,[hashtable]$State)
    $Buffer.Value += $Chunk
    while(($newline=$Buffer.Value.IndexOf("`n")) -ge 0){
        $line=$Buffer.Value.Substring(0,$newline).TrimEnd([char]"`r")
        $Buffer.Value=$Buffer.Value.Substring($newline+1)
        if($line -match '^\[G9_FATAL\]'){
            $State.fatal_seen=$true
            Write-TransitionEvent 'fatal' $HostSeconds $ByteOffset $line $Writer
        } elseif($line -match '^\[(RUN_SEED|GAME|INPUT_STATE|INPUT|CLEAR(?:_[A-Z0-9_]+)?|G9_CLEAR|G9_PERF)\]'){
            Write-TransitionEvent 'rx_transition' $HostSeconds $ByteOffset $line $Writer
        }
        if($line -match '^\[RUN_SEED\]\s+round=(\d+)\b'){
            $round=[int]$Matches[1]
            if($round -eq 1 -and -not $State.startup_seen){$State.startup_seen=$true;$State.last_device_ms=[long]0}
            if($round -eq 2 -and $State.victory1_seen -and -not $State.manual_r_sent){$State.auto_round2_seen=$true}
            if($round -eq 3 -and $State.manual_r_sent -and -not $State.round3_seen){
                $State.round3_seen=$true;$State.round3_at=$HostSeconds;$State.round3_perf_count=0
            }
        }
        if($line -match '^\[GAME\].*\bmode=(\d+)\b'){
            $mode=[int]$Matches[1]
            if($State.startup_seen -and -not $State.auto_round2_seen){
                if($mode -eq 1){$State.boss1_seen=$true}
                if($mode -eq 2 -and $State.boss1_seen){$State.victory1_seen=$true}
            }
            if($State.auto_round2_seen -and $mode -eq 1 -and -not $State.boss2_seen){
                $State.boss2_seen=$true;$State.boss2_at=$HostSeconds
            }
        }
        if($State.round3_seen -and $line -match '^\[G9_PERF\]'){$State.round3_perf_count++}
        # Use the latest complete device report, not host elapsed time or a
        # maximum over old UART data received before this fresh RUN_SEED.
        if($State.startup_seen -and $line -match '^\[LIVE\].*\bdevice_ms=(\d+)\b'){
            $State.last_device_ms=[long]$Matches[1]
        }
    }
}
function Test-TransitionReady {
    param([hashtable]$State,[double]$HostSeconds,[bool]$Require600)
    return $State.startup_seen -and $State.boss1_seen -and
        $State.victory1_seen -and $State.auto_round2_seen -and
        $State.boss2_seen -and $State.manual_r_sent -and
        $State.round3_seen -and $State.round3_perf_count -ge 3 -and
        $HostSeconds-$State.round3_at -ge 8.0 -and
        -not $State.fatal_seen -and $State.serial_error_count -eq 0 -and
        (-not $Require600 -or $State.last_device_ms -ge 600000)
}
function Read-CaptureSerial {
    param([IO.Ports.SerialPort]$SerialPort,[hashtable]$State,[bool]$CountErrors)
    try{return $SerialPort.ReadExisting()}
    catch{if($CountErrors){$State.serial_error_count++};throw}
}
function Write-TimingChunk {
    param([string]$Chunk, [double]$HostSeconds, [ref]$Buffer, [IO.StreamWriter]$Writer)
    # Complete lines from one USB/UART chunk share its receive timestamp. This
    # measures host receipt, including USB buffering, not the exact UART edge.
    $Buffer.Value += $Chunk
    while (($newline = $Buffer.Value.IndexOf("`n")) -ge 0) {
        $line = $Buffer.Value.Substring(0,$newline).TrimEnd([char]"`r")
        $Buffer.Value = $Buffer.Value.Substring($newline+1)
        if ($line -match '^(GARDEN Ti60 cropped port |DISPLAY(?: PHASE| READ AB)? PROBE:|\[(G9_INIT|G9_PERF|G9_FRAME|G9_FATAL|ART_CTRL|VOLLEY_CTRL|RANDOM_CTRL|RUN_SEED|PATTERN|FRAME_COST|WAVE|WAVE_CTRL|HAZARD|HAZARD_CTRL|ORDNANCE|ORDNANCE_CTRL|REWARD|REWARD_CTRL|STATE|SECOND|ACTOR_UI|ANIMATION|ANIMATION_CTRL|RHYTHM|RHYTHM_CTRL|COMBAT|COMBAT_CTRL|SCENE|SCENE_CTRL|FEEDBACK|FEEDBACK_CTRL|STAGE|STAGE_CTRL|FX|FX_CTRL|CAL|READY|LIVE|FRAME_PROFILE|FRAME_CTRL|PROBE_PHASE|READ_AB(?: META)?|DISPLAY_SEG|DISPLAY7(?:_AUX)?|RELOCK_AB|DRAW|FRAME|STATIC)\]|RESULT:)') {
            $record = [ordered]@{host_seconds=$HostSeconds;line=$line}
            $Writer.WriteLine(($record | ConvertTo-Json -Compress))
        }
    }
}
try {
    # Open first: a busy port must not cause a CPU reset.
    try{$serial.Open();$transitionState.serial_open_count++}
    catch{if($TransitionTest -or $PureInputTest){$transitionState.serial_error_count++};throw}
    $streamLog = New-Object IO.StreamWriter($logPath,$false,[Text.Encoding]::UTF8)
    $streamLog.AutoFlush = $true
    $timingLog = New-Object IO.StreamWriter(($logPath+'.timing.jsonl'),$false,[Text.Encoding]::UTF8)
    $timingLog.AutoFlush = $true
    if($PureInputTest){
        $pureLog=New-Object IO.StreamWriter(($logPath+'.pure.events.jsonl'),$false,[Text.Encoding]::UTF8)
        $pureLog.AutoFlush=$true
    }
    if($TransitionTest){
        $transitionLog=New-Object IO.StreamWriter(($logPath+'.events.jsonl'),$false,[Text.Encoding]::UTF8)
        $transitionLog.AutoFlush=$true
    }
    if ($InputTest -or $UiInputTest -or $FrameTimingTest -or $AnimationProfile -or $MotionProfile -or $ProbeCommand) {
        $hostLog = New-Object IO.StreamWriter(($logPath+'.host.log'),$false,[Text.Encoding]::UTF8)
        $hostLog.AutoFlush = $true
    }
    if (-not $CaptureOnly) {
        if (-not (Test-Path -LiteralPath $elfPath)) { throw 'Build the ELF first' }
        if (-not $SkipAtlas) {
            if (-not (Test-Path -LiteralPath $atlasPath)) { throw 'Generate atlas first' }
            if ((Get-Item -LiteralPath $atlasPath).Length -gt 0xBA000) { throw 'Atlas exceeds verified DDR window' }
        }
        # Replace only a server previously launched and recorded by this tool.
        foreach ($ownedPidPath in @($pidPath, (Join-Path $appDir 'variants/cjui_1031/logs/openocd.pid'), (Join-Path $appDir 'variants/crp2_1101/logs/openocd.pid'), (Join-Path $appDir 'variants/cpad_hw_1030/logs/openocd.pid'), $baselinePidPath, $acceptedFxPidPath, $previousVariantPidPath, $parentVariantPidPath, $probePidPath, $capacityVariantPidPath, $newCapPidPath, $newGamePidPath, $newStage9PidPath, $oldCapPidPath, $oldGamePidPath, $oldCandidatePidPath, $previousRtl15PidPath, $previousRtl16PidPath, $previousClearPidPath, $previousG9gPidPath, $previousG19PidPath, $previousP2kPidPath, $previousP2gPidPath, $previousP2fPidPath, $previousP2aPidPath, $previousP2mPidPath, $previousP2hPidPath, $previousP2sPidPath, $previousPk6PidPath, $previousPk7PidPath, $previousPk8PidPath, $previousP3dPidPath, $previousP1uPidPath, $previousP2uPidPath, $previousP2bPidPath, $previousP2bsPidPath, $previousP2bcPidPath, $previousP2bcoPidPath, $previousP2bco7PidPath, $previousP2bco7cPidPath, $previousP2bco7clPidPath, $previousP2bco6cPidPath, $previousP2bco6csPidPath, $previousP2bco6cpPidPath)) {
            if (Test-Path -LiteralPath $ownedPidPath) {
                $oldId = [int](Get-Content -LiteralPath $ownedPidPath -Raw)
                $oldProcess = Get-Process -Id $oldId -ErrorAction SilentlyContinue
                if ($oldProcess -and $oldProcess.Path -eq $ocdExe -and $oldProcess.StartTime -le (Get-Item -LiteralPath $ownedPidPath).LastWriteTime) {
                    Stop-Process -Id $oldId -Force
                    Start-Sleep -Milliseconds 700
                }
            }
        }
        $listener = Get-NetTCPConnection -LocalPort 13333,24444,26666 -State Listen -ErrorAction SilentlyContinue
        if ($listener) { throw 'An unowned debugger is using TCP 13333; it has been left running' }
        $ocdProcess = Start-Process -FilePath $ocdExe -ArgumentList '-c','"gdb_port 13333"','-c','"telnet_port 24444"','-c','"tcl_port 26666"','-f','ftdi_ti.cfg','-f','debug_ti.cfg' -WorkingDirectory $bspOcd -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logDir 'openocd.log') -RedirectStandardError (Join-Path $logDir 'openocd.err')
        [IO.File]::WriteAllText($pidPath,[string]$ocdProcess.Id)
        $connected = $false
        for ($i=0; $i -lt 25; $i++) {
            if ($ocdProcess.HasExited) { throw 'OpenOCD exited; see logs/openocd.err' }
            try {
                $tcp = New-Object Net.Sockets.TcpClient
                $tcp.Connect('127.0.0.1',13333)
                $tcp.Close()
                $connected = $true
                break
            } catch { Start-Sleep -Milliseconds 500 }
        }
        if (-not $connected) { throw 'OpenOCD did not open GDB port' }
        $gdbScript = Join-Path $logDir 'load.gdb'
        # GDB runs with APP as its working directory. A sealed ELF's absolute
        # Windows path can exceed GDB's 260-character limit even when PowerShell
        # can hash it; use the already-confined APP-relative path instead.
        $elfUnix = $elfPath.Substring($appDir.Length + 1).Replace('\','/')
        $atlasUnix = $atlasPath.Replace('\','/')
        # GDB's restore parser treats quotes as part of a binary filename.
        # Invoke GDB in the app directory and use a space-free relative path.
        $commands = @('set confirm off','set mem inaccessible-by-default off','set arch riscv:rv32','set remotetimeout 20','target extended-remote 127.0.0.1:13333')
        if ($PreserveBoardState) {
            # Preserve the aligned static ON epoch by avoiding debug reset.
            # Halt/load/MMIO reads still perturb timing; overhead is not zero.
            $commands += 'monitor halt'
        } else { $commands += @('monitor reset init','monitor reset halt') }
        $commands += @(('file "'+$elfUnix+'"'),'load')
        if (-not $ReuseCheckedAssets) { $commands += @('restore variants/cclassic_1102/assets/atlas.bin binary 0x005c4000','restore variants/cclassic_1102/assets/stage9_tiles.bin binary 0x00900000') }
        $commands += @(('printf "[HOST_PURE_SCATTER_RESTORE] phase=before address=0x009B0000 bytes='+$mixProof.bytes+' sha256='+$mixProof.sha256+'\n"'),
            'restore variants/cclassic_1102/assets/pure_scatter16.bin binary 0x009b0000',
            ('printf "[HOST_PURE_SCATTER_RESTORE] phase=after address=0x009B0000 bytes='+$mixProof.bytes+' sha256='+$mixProof.sha256+'\n"'))
        # RTL19 diagnostic identity: reads only, no REAR or clear_stats.
        $commands += @('set $c_bul9 = *(unsigned int*)0xf8100100',
            'set $c_bulrev = *(unsigned int*)0xf8100150',
            'set $c_fmis = *(unsigned int*)0xf8100180',
            'set $c_fmrev = *(unsigned int*)0xf810018c',
            'set $c_fmcap = *(unsigned int*)0xf8100230',
            'set $c_fmstat = *(unsigned int*)0xf8100184',
            'set $c_hstid = *(unsigned int*)0xf8100280',
            'set $c_hstshape = *(unsigned int*)0xf8100288',
            'set $c_hststat = *(unsigned int*)0xf8100284',
            'set $c_hstdrop = *(unsigned int*)0xf810028c',
            'printf "[HOST_RTL19_ID] BUL9=0x%08x REV=0x%08x FMIS=0x%08x FMREV=0x%08x CAP=0x%x SNAP=0x%x HST=0x%08x SHAPE=0x%08x HSTAT=0x%x DROP=%u\n", $c_bul9, $c_bulrev, $c_fmis, $c_fmrev, $c_fmcap, $c_fmstat, $c_hstid, $c_hstshape, $c_hststat, $c_hstdrop',
            'if $c_bul9 != 0x42554c39 || $c_bulrev != 0x00010007 || $c_fmis != 0x464d4953 || $c_fmrev != 0x00010002 || $c_fmcap != 15 || $c_hstid != 0x48535432 || $c_hstshape != 0x00400008',
            'echo RTL19 identity mismatch; CPU kept halted\n','quit 1','end',
            'if $c_fmstat != 0 || ($c_hststat & 1) != 0',
            'echo Existing first-fault snapshot present; preserve before any new run\n','quit 1','end')
        if ($PreservedFaultDirectory) {
            # A new run is allowed only after the exact current 40-word scene
            # has been exported. Verify all five binary backups by SHA, then
            # compare every frozen word on the actual board before REAR.
            $faultDir=[IO.Path]::GetFullPath($PreservedFaultDirectory)
            $fault=Get-Content -LiteralPath (Join-Path $faultDir 'result.json') -Raw | ConvertFrom-Json
            if($fault.writes -ne $false -or $fault.revision -ne 0x10007 -or @($fault.freeze_words).Count -ne 40){throw 'Invalid saved fault identity'}
            foreach($f in $fault.files.PSObject.Properties){
                if($f.Name -notmatch '^[a-z0-9_]+\.bin$'){throw 'Invalid fault backup name'}
                $backup=Join-Path $faultDir $f.Name
                if((Get-Item -LiteralPath $backup).Length -ne $f.Value.bytes -or
                   (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $f.Value.sha256){throw 'Fault backup checksum mismatch'}
            }
            # Remove only the default refusal tail, retaining ID checks above.
            $commands=$commands[0..($commands.Count-5)]
            $commands += @('if ($c_fmstat & 1) == 0 || ($c_fmstat & 4) != 0',
                'echo Saved fault not complete; CPU kept halted\n','quit 1','end')
            for($fi=0;$fi -lt 40;$fi++){
                $address=[Convert]::ToUInt64('F8100190',16)+[uint64]($fi*4)
                $commands += @((('if *(unsigned int*)0x{0:x} != 0x{1:x8}') -f $address,[uint64]$fault.freeze_words[$fi]),
                    'echo Current fault differs from verified backup; CPU kept halted\n','quit 1','end')
            }
            $commands += @('set {unsigned int}0xf8100188 = 0x52454152','monitor sleep 20',
                'printf "[HOST_SAVED_FAULT_REARM] words=40 matched=1 status=0x%x\n", *(unsigned int*)0xf8100184',
                'if *(unsigned int*)0xf8100184 != 0 || (*(unsigned int*)0xf8100284 & 1) != 0',
                'echo Rearm did not complete; CPU kept halted\n','quit 1','end')
        }
        if ($PrewarmOff) {
            # Explicit diagnostic A/B only. CPU is halted; check magic before
            # writing CTRL=0. No clear_all, power-cycle, FPGA or Flash write.
            $commands += @('if *(unsigned int*)0xf810006c != 0x44424731',
                'echo DBG1 missing; prewarm control left untouched\n','quit 1','end',
                'printf "[HOST_DBG_CTRL] before=0x%x\n", *(unsigned int*)0xf810005c',
                'set {unsigned int}0xf810005c = 0',
                'printf "[HOST_DBG_CTRL] after=0x%x\n", *(unsigned int*)0xf810005c',
                'if *(unsigned int*)0xf810005c != 0','quit 1','end')
        }
        if ($RelockOn) {
            # ELF and atlas loaded; verify identity before enabling relock.
            # CTRL=4 does not clear counters or re-arm the snapshot.
            $commands += @('set $rtl8_gpu_id = *(unsigned int*)0xf8100024',
                'set $rtl8_dbg_id = *(unsigned int*)0xf810006c',
                'printf "[HOST_RELOCK] phase=identity gpu=0x%08x dbg=0x%08x\n", $rtl8_gpu_id, $rtl8_dbg_id',
                'if $rtl8_gpu_id != 0x47505531',
                'echo GPU1 missing; CPU remains halted, CTRL left untouched\n','quit 1','end',
                'if $rtl8_dbg_id != 0x44424731',
                'echo DBG1 missing; CPU remains halted, CTRL left untouched\n','quit 1','end',
                'set $rtl8_ctrl_before = *(unsigned int*)0xf810005c',
                'set {unsigned int}0xf810005c = 4',
                'monitor sleep 40',
                'set $rtl8_ctrl_after = *(unsigned int*)0xf810005c',
                'set $rtl8_q_after = *(unsigned int*)0xf8100074',
                'printf "[HOST_RELOCK] phase=readback before=0x%x ctrl=0x%x q=0x%x\n", $rtl8_ctrl_before, $rtl8_ctrl_after, $rtl8_q_after',
                'if $rtl8_ctrl_after != 4',
                'echo Relock CTRL readback failed; CPU remains halted\n','quit 1','end',
                'if ($rtl8_q_after & 0x400) == 0',
                'echo Relock mirror bit10 missing; CPU remains halted\n','quit 1','end',
                'printf "[HOST_RELOCK] verified=1 gpu=0x%08x dbg=0x%08x ctrl=0x%x q=0x%x mirror10=1\n", $rtl8_gpu_id, $rtl8_dbg_id, $rtl8_ctrl_after, $rtl8_q_after')
        }
        $commands += @('info registers pc','monitor resume 0x1000','disconnect','quit')
        $gdbDepth=0
        foreach($gdbLine in $commands){
            if($gdbLine -match '^(if|while|define)\s'){$gdbDepth++}
            if($gdbLine -eq 'end'){$gdbDepth--;if($gdbDepth -lt 0){throw 'Unbalanced generated GDB script'}}
        }
        if($gdbDepth -ne 0){throw 'Unbalanced generated GDB script'}
        [IO.File]::WriteAllLines($gdbScript,$commands,[Text.Encoding]::ASCII)
        . (Join-Path $PSScriptRoot 'invoke_gdb.ps1')
        $loadLog=$logPath+'.load.log'
        $attempt='GDB attempt '+[DateTime]::UtcNow.ToString('o')+'; capture='+$LogName
        # Invalidate the shared log before launching so a failed start cannot
        # leave yesterday's successful load as evidence for the new attempt.
        [IO.File]::WriteAllText((Join-Path $logDir 'load.log'),$attempt+[Environment]::NewLine)
        [IO.File]::WriteAllText($loadLog,$attempt+[Environment]::NewLine)
        Copy-Item -LiteralPath $gdbScript -Destination ($logPath+'.load.gdb')
        $mixProof | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath ($logPath+'.scatter_restore.json') -Encoding UTF8
        $gdbResult=Invoke-GardenGdbBatch -Executable $gdbExe -WorkingDirectory $appDir -ScriptPath $gdbScript
        $gdbOutput=$gdbResult.output
        [IO.File]::WriteAllText(($logPath+'.gdb.stdout.log'),$gdbResult.stdout)
        [IO.File]::WriteAllText(($logPath+'.gdb.stderr.log'),$gdbResult.stderr)
        $captured=$attempt+[Environment]::NewLine+'[GDB_STDOUT]'+[Environment]::NewLine+$gdbResult.stdout+[Environment]::NewLine+'[GDB_STDERR]'+[Environment]::NewLine+$gdbResult.stderr
        [IO.File]::WriteAllText($loadLog,$captured)
        [IO.File]::WriteAllText((Join-Path $logDir 'load.log'),$captured)
        [ordered]@{native_exit_code=$gdbResult.exit_code;powershell=$PSVersionTable.PSVersion.ToString();
            working_directory=$appDir;script=$gdbScript;capture=$LogName;stderr_is_failure=$false} |
            ConvertTo-Json | Set-Content -LiteralPath ($logPath+'.gdb_result.json') -Encoding UTF8
        Assert-GardenGdbLoad -Result $gdbResult -RequireAtlas (-not $ReuseCheckedAssets) -RequireRelock ([bool]$RelockOn)
        if($gdbOutput -notmatch '(?m)^\[HOST_PURE_SCATTER_RESTORE\] phase=after address=0x009B0000 bytes=[0-9]+ sha256=') { throw 'Pure mix restore completion marker missing' }
        if($gdbOutput -notmatch '(?m)^Restoring binary file .*pure_scatter16\.bin.*'){throw 'Actual pure mix binary restore line missing'}
        if ($RelockOn) {
            if ($gdbOutput -notmatch '\[HOST_RELOCK\] verified=1') { throw 'Relock verification marker missing; see logs/load.log' }
            $gdbOutput -split '\r?\n' | Where-Object { $_ -match '^\[HOST_RELOCK\]' } | ForEach-Object { Write-Host $_ }
        }
        Write-Host $(if ($SkipAtlas) { 'Diagnostic ELF loaded; atlas skipped. Waiting for UART evidence.' } else { 'ELF + atlas loaded; resume requested. Waiting for application UART evidence.' })
    }
    # Resume the next phase of an already loaded diagnostic without a CPU reset.
    # Keep host commands separate from UART bytes, which can arrive mid-line.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if ($ProbeCommand) {
        $serial.Write($ProbeCommand)
        $hostLog.WriteLine("[HOST_PROBE_COMMAND] byte=$ProbeCommand host_seconds=$($watch.Elapsed.TotalSeconds)")
    }
    if ($InitialInput) {
        # Queue a bounded diagnostic command until the normal game poll loop.
        # Startup still validates both immutable DDR asset CRCs before use.
        $serial.Write($InitialInput)
        [IO.File]::WriteAllText(($logPath+'.initial_input.json'),(@{command=$InitialInput;reuse_checked_assets=[bool]$ReuseCheckedAssets;host_seconds=$watch.Elapsed.TotalSeconds}|ConvertTo-Json),[Text.Encoding]::UTF8)
    }
    $sent = @{}
    $schedule = @{}
    if ($InputTest -or $UiInputTest) {
        # Deterministic UART checks, with enough separation for edge events.
        $schedule = @{8='a';10='d';12='w';14='s';16='p';18='p';20='r';23='t';26='t';30='b';38='c';43='c'}
        if ($UiInputTest) {
            # Test all Alpha levels, both backends and actual pause state.
            # Finish in GPU + Alpha16 + auto demo before a fresh long run.
            $schedule += @{48='v';51='n';54='v';57='n';60='n';63='n';66='n';69='n';
                72='c';76='v';80='v';84='c';88='f';91='f';94='p';96='p';99='r';104='t';108='t'}
        }
    } elseif ($FrameTimingTest) {
        # Two verbose and two quiet windows. Restore logs and leave profiling
        # off. Gameplay keeps running; the windows need not have equal loads.
        $schedule = @{8='o';38='o';42='l';46='o';76='o';80='l';84='o';114='o';118='l';122='o';152='o';156='l'}
    } elseif ($MotionProfile) {
        # Measure the actual main frame stages through both first dense Boss
        # bursts. Stop the recorder before capture closes; no input/game rule
        # changes and no new periodic formatting while the window is open.
        $schedule = @{8='o';55='o'}
    } elseif ($AnimationProfile) {
        # Reuse the existing read-only phase timer. Keep automatic gameplay,
        # all normal reports, Alpha and the GPU unchanged; stop at the end.
        $schedule = @{8='o';110='o'}
    }
    $scheduledTimes = @($schedule.Keys | Sort-Object)
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        if ($scheduledTimes.Count) {
            foreach ($at in $scheduledTimes) {
                if ($watch.Elapsed.TotalSeconds -ge $at -and -not $sent.ContainsKey($at)) {
                    $serial.Write($schedule[$at]); $sent[$at]=$true
                    $marker = "`r`n[HOST_INPUT] $($schedule[$at]) at ${at}s`r`n"
                    # Keep host events separate: UART chunks may end mid-line.
                    $hostLog.Write($marker); Write-Host $marker.Trim()
                }
            }
        }
        $chunk = Read-CaptureSerial $serial $transitionState ([bool]($TransitionTest -or $PureInputTest))
        $receivedAt = $watch.Elapsed.TotalSeconds
        if ($chunk.Length) {
            $streamLog.Write($chunk)
            $streamLog.Flush()
            Write-TimingChunk $chunk $receivedAt ([ref]$timingBuffer) $timingLog
            if($TransitionTest){
                Write-TransitionChunk $chunk $receivedAt $streamLog.BaseStream.Position ([ref]$transitionBuffer) $transitionLog $transitionState
            }
            if($PureInputTest){Write-PureChunk $chunk $receivedAt $streamLog.BaseStream.Position ([ref]$pureBuffer) $pureLog $pureState}
            Write-Host -NoNewline $chunk
        }
        if($PureInputTest){
            if($pureState.fatal_seen -or $pureState.protocol_error){break}
            if($pureState.ready_seen -and -not $pureState.enter_sent -and $watch.Elapsed.TotalSeconds-$pureState.ready_at -ge 2.0){
                try{$serial.Write('t')}
                catch{$transitionState.serial_error_count++;$pureState.protocol_error='entry_tx_failed';break}
                $pureState.enter_sent=$true;$streamLog.Flush()
                Write-PureEvent 'tx' $watch.Elapsed.TotalSeconds $streamLog.BaseStream.Position '[HOST_PURE] enter_after_ready_2s' $pureLog @{tx_hex='74';tx_ascii='t';written=1;action='enter'}
            }
            if($pureState.pure_seen -and -not $pureState.exit_sent -and $watch.Elapsed.TotalSeconds-$pureState.pure_at -ge 80.0){
                try{$serial.Write('t')}
                catch{$transitionState.serial_error_count++;$pureState.protocol_error='exit_tx_failed';break}
                $pureState.exit_sent=$true;$streamLog.Flush()
                Write-PureEvent 'tx' $watch.Elapsed.TotalSeconds $streamLog.BaseStream.Position '[HOST_PURE] exit_after_pure_80s' $pureLog @{tx_hex='74';tx_ascii='t';written=1;action='exit'}
            }
            if(Test-PureReady $pureState $watch.Elapsed.TotalSeconds $transitionState.serial_error_count){break}
        }
        if($TransitionTest){
            if($transitionState.fatal_seen){break}
            if($transitionState.boss2_seen -and -not $transitionState.manual_r_sent -and
               $watch.Elapsed.TotalSeconds-$transitionState.boss2_at -ge 6.0){
                try{$serial.Write('r')}
                catch{$transitionState.serial_error_count++;throw}
                $transitionState.manual_r_sent=$true
                $streamLog.Flush()
                Write-TransitionEvent 'tx' $watch.Elapsed.TotalSeconds $streamLog.BaseStream.Position '[HOST_TRANSITION] manual_r_after_boss2_6s' $transitionLog @{tx_hex='72';tx_ascii='r';written=1}
            }
            if(Test-TransitionReady $transitionState $watch.Elapsed.TotalSeconds ([bool]$Acceptance600)){break}
        }
        Start-Sleep -Milliseconds 10
    }
    $tail = Read-CaptureSerial $serial $transitionState ([bool]($TransitionTest -or $PureInputTest))
    $receivedAt = $watch.Elapsed.TotalSeconds
    if ($tail.Length) {
        $streamLog.Write($tail)
        $streamLog.Flush()
        Write-TimingChunk $tail $receivedAt ([ref]$timingBuffer) $timingLog
        if($TransitionTest){Write-TransitionChunk $tail $receivedAt $streamLog.BaseStream.Position ([ref]$transitionBuffer) $transitionLog $transitionState}
        if($PureInputTest){Write-PureChunk $tail $receivedAt $streamLog.BaseStream.Position ([ref]$pureBuffer) $pureLog $pureState}
        Write-Host -NoNewline $tail
    }
    if($TransitionTest){
        # Raw UART is sealed before hashing; host commands never enter it.
        $streamLog.Flush();$rawBytes=$streamLog.BaseStream.Position
        $streamLog.Dispose();$streamLog=$null
        $rawSha=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
        $passed=Test-TransitionReady $transitionState $watch.Elapsed.TotalSeconds ([bool]$Acceptance600)
        $status=if($transitionState.fatal_seen){'fatal'}elseif($passed){'passed'}else{'duration_end'}
        $summary=@{status=$status;source_kind='board';capture_profile='TransitionTest';
            capture_start_host_monotonic_s=0;raw_bytes=$rawBytes;raw_sha256=$rawSha;
            acceptance600_requested=[bool]$Acceptance600;last_device_ms=$transitionState.last_device_ms}
        foreach($key in $transitionState.Keys){$summary[$key]=$transitionState[$key]}
        Write-TransitionEvent 'complete' $watch.Elapsed.TotalSeconds $rawBytes ('[HOST_TRANSITION] '+$status) $transitionLog $summary
        if(-not $passed){$transitionExitCode=1}
        Write-Host ('TransitionTest status='+$status+' -> '+$logPath+'.events.jsonl')
    }
    if($PureInputTest){
        $streamLog.Flush();$rawBytes=$streamLog.BaseStream.Position
        $streamLog.Dispose();$streamLog=$null
        $rawSha=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
        $passed=Test-PureReady $pureState $watch.Elapsed.TotalSeconds $transitionState.serial_error_count
        $status=if($pureState.fatal_seen){'fatal'}elseif($pureState.protocol_error){'protocol_error'}elseif($passed){'passed'}else{'duration_end'}
        $summary=@{status=$status;source_kind='board';capture_profile='PureInputTest';raw_bytes=$rawBytes;raw_sha256=$rawSha;
            capture_start_host_monotonic_s=0;serial_open_count=$transitionState.serial_open_count;
            serial_error_count=$transitionState.serial_error_count;reopen_count=$transitionState.reopen_count}
        foreach($key in $pureState.Keys){$summary[$key]=$pureState[$key]}
        Write-PureEvent 'complete' $watch.Elapsed.TotalSeconds $rawBytes ('[HOST_PURE] '+$status) $pureLog $summary
        if(-not $passed){$transitionExitCode=1}
        Write-Host ('PureInputTest status='+$status+' elapsed='+$watch.Elapsed.TotalSeconds+'s -> '+$logPath+'.pure.events.jsonl')
    }
    Write-Host "`nCaptured ${Seconds}s -> $logPath"
    Write-Host 'Target and this debugger stay running. Serial port is released for play.'
} catch {
    # A partial download may leave the CPU halted. Report failure; do not execute
    # partially loaded code. Only this tool's own debugger process is stopped.
    if ($ocdProcess -and -not $ocdProcess.HasExited) { Stop-Process -Id $ocdProcess.Id -Force }
    throw
} finally {
    try {
        if ($pureLog) { $pureLog.Dispose() }
        if ($hostLog) { $hostLog.Dispose() }
        if ($timingLog) { $timingLog.Dispose() }
        if ($transitionLog) { $transitionLog.Dispose() }
        if ($streamLog) { $streamLog.Dispose() }
    }
    finally {
        try { if ($serial.IsOpen) { $serial.Close() } }
        finally { $serial.Dispose() }
    }
}
if(($TransitionTest -or $PureInputTest) -and $transitionExitCode -ne 0){exit $transitionExitCode}
