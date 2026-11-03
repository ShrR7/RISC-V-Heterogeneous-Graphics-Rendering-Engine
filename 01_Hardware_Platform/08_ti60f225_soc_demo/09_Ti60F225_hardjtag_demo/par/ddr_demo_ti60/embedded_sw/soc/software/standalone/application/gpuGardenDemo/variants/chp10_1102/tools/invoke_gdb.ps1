# SPDX-License-Identifier: GPL-3.0-or-later
# Capture native streams without PowerShell 5.1 NativeCommandError semantics.
function Invoke-GardenGdbBatch {
    param(
        [Parameter(Mandatory=$true)][string]$Executable,
        [Parameter(Mandatory=$true)][string]$WorkingDirectory,
        [Parameter(Mandatory=$true)][string]$ScriptPath
    )
    $exe=[IO.Path]::GetFullPath($Executable)
    $cwd=[IO.Path]::GetFullPath($WorkingDirectory)
    $script=[IO.Path]::GetFullPath($ScriptPath)
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'GDB executable is missing.'}
    if(-not (Test-Path -LiteralPath $cwd -PathType Container)){throw 'GDB working directory is missing.'}
    if(-not (Test-Path -LiteralPath $script -PathType Leaf)){throw 'GDB command script is missing.'}
    if($script.Contains('"')){throw 'Invalid GDB script path.'}
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$exe
    $info.WorkingDirectory=$cwd
    # -x always receives one filename; Windows filenames cannot contain quotes.
    $info.Arguments='-batch -x "'+$script+'"'
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process
    $process.StartInfo=$info
    try {
        if(-not $process.Start()){throw 'GDB process could not start.'}
        # Drain both pipes concurrently: a full stderr pipe must not deadlock
        # a child whose stdout is also being captured.
        $stdoutTask=$process.StandardOutput.ReadToEndAsync()
        $stderrTask=$process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout=$stdoutTask.GetAwaiter().GetResult()
        $stderr=$stderrTask.GetAwaiter().GetResult()
        return [pscustomobject][ordered]@{
            exit_code=$process.ExitCode;stdout=$stdout;stderr=$stderr
            output=($stdout+[Environment]::NewLine+$stderr)
        }
    } finally {
        $process.Dispose()
    }
}

function Assert-GardenGdbLoad {
    param(
        [Parameter(Mandatory=$true)]$Result,
        [bool]$RequireAtlas=$true,
        [bool]$RequireRelock=$true
    )
    if($Result.exit_code -ne 0){throw ('GDB returned exit code '+$Result.exit_code+'; see this capture .load.log and .gdb.stderr.log.')}
    if($Result.stdout -notmatch '(?m)^Start address .*load size [0-9]+'){
        throw 'GDB ELF load completion marker is missing.'
    }
    if($RequireAtlas -and $Result.stdout -notmatch '(?m)^Restoring binary file '){
        throw 'GDB atlas restore marker is missing.'
    }
    if($RequireRelock -and $Result.stdout -notmatch '(?m)^\[HOST_RELOCK\] verified=1(?:\s|$)'){
        throw 'GDB GPU/DBG identity and relock verification marker is missing.'
    }
}
