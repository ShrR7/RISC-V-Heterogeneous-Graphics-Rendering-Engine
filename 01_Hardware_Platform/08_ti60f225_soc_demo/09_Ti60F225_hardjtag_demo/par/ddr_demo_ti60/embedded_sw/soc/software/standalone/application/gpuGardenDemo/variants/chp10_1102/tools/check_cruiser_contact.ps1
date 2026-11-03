# Serial-only real-contact check: retry, disable demo movement, observe real
# enemy damage, then retry into auto demo. No ELF/JTAG/FPGA/Flash operation.
[CmdletBinding()]
param([ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='COM11',[ValidateRange(20,90)][int]$Seconds=45,[string]$LogName='',[switch]$DryRun)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$variant=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $LogName){$LogName='hazard_contact_'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'.log'}
if($LogName -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*\.log$' -or [IO.Path]::GetFileName($LogName) -ne $LogName){throw 'Use a new simple log filename'}
$rawPath=Join-Path $variant ('logs/'+$LogName)
$hostPath=$rawPath+'.host.jsonl';$receiptPath=$rawPath+'.receipt.json'
foreach($path in @($rawPath,$hostPath,$receiptPath)){if(Test-Path -LiteralPath $path){throw 'Never overwrite a contact case'}}
function Bound-Path([string]$Relative){
    if([IO.Path]::IsPathRooted($Relative) -or ($Relative -split '[/\\]') -contains '..'){throw 'Binding must remain within the W variant'}
    $resolved=[IO.Path]::GetFullPath((Join-Path $variant $Relative))
    if(-not $resolved.StartsWith($variant+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Binding escaped W variant'}
    if(-not (Test-Path -LiteralPath $resolved -PathType Leaf)){throw ('Missing W binding: '+$Relative)}
    return $resolved
}
$manifest=Get-Content -LiteralPath (Join-Path $variant 'delivery_manifest.json') -Raw|ConvertFrom-Json
if($manifest.variant -ne 'neon_cruiser_20261016'){throw 'Wrong contact-check variant'}
$sourcePath=Bound-Path $manifest.source_manifest.path
if((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $manifest.source_manifest.sha256){throw 'W source manifest binding changed'}
$source=Get-Content -LiteralPath $sourcePath -Raw|ConvertFrom-Json
foreach($entry in $source.runtime_source_sha256.PSObject.Properties){
    if((Get-FileHash -LiteralPath (Bound-Path $entry.Name) -Algorithm SHA256).Hash -ne $entry.Value){throw 'Runtime source changed'}
}
foreach($artifact in @($manifest.elf,$manifest.atlas)){
    if((Get-FileHash -LiteralPath (Bound-Path $artifact.path) -Algorithm SHA256).Hash -ne $artifact.sha256){throw 'W ELF or atlas binding changed'}
}
if($DryRun){[ordered]@{port=$Port;seconds=$Seconds;raw=$rawPath;sequence='r,t; real contacts; r';ELF_SHA256=$manifest.elf.sha256;hardware_access=$false}|ConvertTo-Json;return}
$script:portHandle=$null;$script:rawWriter=$null;$script:hostWriter=$null
$script:clock=[Diagnostics.Stopwatch]::StartNew();$script:buffer='';$script:line=0
$script:records=New-Object 'Collections.Generic.List[object]'
$passed=$false;$failure=$null;$restored=$false;$opened=$false;$sent=$false;$proof=$null;$finalAck=$null
function Event([string]$Name,$Data){
    if($script:hostWriter){$script:hostWriter.WriteLine((([ordered]@{event=$Name;utc=[DateTime]::UtcNow.ToString('o');seconds=$script:clock.Elapsed.TotalSeconds;data=$Data})|ConvertTo-Json -Depth 8 -Compress));$script:hostWriter.Flush()}
}
function Pump {
    $chunk=$script:portHandle.ReadExisting()
    if(-not $chunk.Length){return}
    $script:rawWriter.Write($chunk);$script:rawWriter.Flush();$script:buffer+=$chunk
    while($script:buffer.Contains("`n")){
        $cut=$script:buffer.IndexOf("`n");$text=$script:buffer.Substring(0,$cut).TrimEnd("`r")
        $script:buffer=$script:buffer.Substring($cut+1);$script:line++
        if($text -match '\bFAIL\b|\bTRAP\b'){throw ('Device fatal: '+$text)}
        if($text -notmatch '^\[(INPUT_STATE|REWARD|ORDNANCE|HAZARD|LIVE)\]'){continue}
        $kind=$Matches[1];$fields=@{}
        foreach($part in [regex]::Matches($text,'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)')){
            $value=0L;$token=$part.Groups[2].Value
            if([long]::TryParse($token,[ref]$value)){$fields[$part.Groups[1].Value]=$value}else{$fields[$part.Groups[1].Value]=$token}
        }
        $script:records.Add([pscustomobject]@{line=$script:line;kind=$kind;fields=$fields})
    }
}
function Key([string]$Byte){
    if($Byte -notmatch '^[rt]$'){throw 'Contact check sends only one raw R or T character'}
    Pump;$after=$script:line;$script:portHandle.Write($Byte);$script:sent=$true
    Event 'send' ([ordered]@{key=$Byte;after_raw_line=$after;single_raw_character=$true})
    $until=$script:clock.Elapsed.TotalSeconds+8.0
    while($script:clock.Elapsed.TotalSeconds -lt $until){
        Pump
        foreach($r in $script:records){if($r.line -gt $after -and $r.kind -eq 'INPUT_STATE' -and $r.fields.key -eq $Byte){Event 'ack' $r;return $r}}
        Start-Sleep -Milliseconds 15
    }
    throw ('Missing device acknowledgement for '+$Byte)
}
try{
    $script:portHandle=New-Object IO.Ports.SerialPort $Port,115200,'None',8,'One'
    $script:portHandle.ReadBufferSize=65536;$script:portHandle.WriteTimeout=500
    $script:portHandle.Open();$opened=$true
    New-Item -ItemType Directory -Path (Split-Path $rawPath -Parent) -Force|Out-Null
    $encoding=New-Object Text.UTF8Encoding $false
    $script:rawWriter=New-Object IO.StreamWriter ([IO.File]::Open($rawPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)),$encoding
    $script:hostWriter=New-Object IO.StreamWriter ([IO.File]::Open($hostPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)),$encoding
    Event 'start' ([ordered]@{ELF_SHA256=$manifest.elf.sha256;identity='HOST_PROVENANCE_NOT_DEVICE_READBACK';no_reload=$true})
    $begin=Key 'r'
    if($begin.fields.tick -gt 7 -or $begin.fields.mode -ne 0 -or $begin.fields.demo -ne 1 -or $begin.fields.paused -ne 0 -or $begin.fields.lives -ne 3 -or $begin.fields.bombs -ne 2 -or $begin.fields.cpu -ne 0 -or $begin.fields.alpha -ne 1 -or $begin.fields.panel_alpha -ne 16 -or $begin.fields.slow -ne 0){throw 'Initial state is not verified fresh GPU/Alpha16/slow0 demo'}
    $manual=Key 't'
    if($manual.fields.demo -ne 0 -or $manual.fields.paused -ne 0){throw 'Did not disable automatic movement'}
    while($script:clock.Elapsed.TotalSeconds -lt $Seconds){
        Pump
        $hazards=@($script:records|Where-Object{$_.line -gt $manual.line -and $_.kind -eq 'HAZARD'})
        foreach($h in $hazards){
            $ordnance=@($script:records|Where-Object{$_.line -gt $manual.line -and $_.line -lt $h.line -and $_.kind -eq 'ORDNANCE' -and $_.fields.tick -eq $h.fields.tick})
            if($h.fields.armed_contacts -lt 1 -and (-not $ordnance.Count -or $ordnance[-1].fields.swept_hits -lt 1)){continue}
            $reward=@($script:records|Where-Object{$_.line -gt $manual.line -and $_.line -lt $h.line -and $_.kind -eq 'REWARD' -and $_.fields.tick -eq $h.fields.tick})
            if($reward.Count -and $reward[-1].fields.hits -ge 2){
                $proof=[ordered]@{hazard=$h;reward=$reward[-1];ordnance=$ordnance};$passed=$true;Event 'real_contact_proof' $proof;break
            }
        }
        if($passed){break}
        Start-Sleep -Milliseconds 15
    }
    if(-not $passed){throw 'No sufficient real enemy contact/damage pair before timeout'}
}catch{$failure=$_.Exception.Message;Event 'failure' @{message=$failure};Write-Warning $failure}
finally{
    try{
        if($opened -and $sent -and $script:rawWriter){
            try{
                $finalAck=Key 'r'
                $restored=$finalAck.fields.tick -le 7 -and $finalAck.fields.mode -eq 0 -and $finalAck.fields.demo -eq 1 -and $finalAck.fields.paused -eq 0 -and $finalAck.fields.bombs -eq 2 -and $finalAck.fields.lives -eq 3 -and $finalAck.fields.cpu -eq 0 -and $finalAck.fields.alpha -eq 1 -and $finalAck.fields.panel_alpha -eq 16 -and $finalAck.fields.slow -eq 0
                $until=$script:clock.Elapsed.TotalSeconds+3
                while($script:clock.Elapsed.TotalSeconds -lt $until){Pump;Start-Sleep -Milliseconds 15}
                Event 'restored' @{verified=$restored;state=$finalAck}
            }catch{$failure=([string]$failure)+'; recovery: '+$_.Exception.Message}
        }
    }finally{
        try{if($script:rawWriter){$script:rawWriter.Dispose()}}finally{
            try{if($script:hostWriter){$script:hostWriter.Dispose()}}finally{
                if($script:portHandle){try{if($script:portHandle.IsOpen){$script:portHandle.Close()}}finally{$script:portHandle.Dispose()}}
                $script:clock.Stop()
            }
        }
    }
}
if(Test-Path -LiteralPath $rawPath){
    $receipt=[ordered]@{status=$(if($passed -and $restored -and -not $failure){'DEVICE_CONTACT_STEPS_COMPLETE_AUDIT_REQUIRED'}else{'FAIL'});
        elapsed_host_seconds=$script:clock.Elapsed.TotalSeconds;proof=$proof;restored=$restored;final_ack=$finalAck;failure=$failure;
        raw_sha256=(Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash;host_sha256=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash;
        elf_sha256=$manifest.elf.sha256;serial_released=$true;FPGA_reconfigured=$false;Flash_written=$false;
        real_contact_proof_observed=[bool]$passed;
        limits='Requires at least two cumulative device hits and same-tick armed-contact or swept-hit evidence. Not an exact per-hit causal-frame or all-collision proof; no fake damage or HP writes.'}
    $receiptStream=[IO.File]::Open($receiptPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $receiptWriter=New-Object IO.StreamWriter $receiptStream,(New-Object Text.UTF8Encoding $false)
    try{$receiptWriter.WriteLine(($receipt|ConvertTo-Json -Depth 10))}finally{$receiptWriter.Dispose()}
    Write-Host ($receipt|ConvertTo-Json -Depth 5)
}
if(-not ($passed -and $restored -and -not $failure)){throw 'Contact check incomplete; inspect preserved receipt'}
