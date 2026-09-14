<#
  Move builds and logs over USB/MTP on Windows.

      powershell -ExecutionPolicy Bypass -File tools\mtp-courier.ps1 pull
      powershell -ExecutionPolicy Bypass -File tools\mtp-courier.ps1 push
      powershell -ExecutionPolicy Bypass -File tools\mtp-courier.ps1        # both

  courier.sh does this job on Linux through `gio`, which is gvfs and cannot
  exist here. Windows exposes the same MTP device through the shell namespace
  instead, so this drives it with Shell.Application.

  Two things about MTP through the shell shape everything below:

    - CopyHere is ASYNCHRONOUS and returns immediately, reporting nothing. So
      every copy is followed by polling the destination until the size stops
      changing. A script that trusts CopyHere's return reports success for
      transfers that never happened.
    - CopyHere will not overwrite on MTP. The existing file is deleted first,
      and the delete is confirmed before the copy starts, because a failed
      delete silently becomes "Jouster (2).nro" -- the console keeps running
      the old build while everything here says it was updated.
#>
param(
    [ValidateSet('push','pull','both')] [string]$Action = 'both',
    # Which app to carry. Jouster is the default because it is the one with a
    # log to pull; -App Armory pushes the lobby and pulls nothing, since it
    # writes no log.
    [string]$App = 'Jouster',
    # Override the local .nro when it is not where this script would look.
    [string]$NroPath
)

$ErrorActionPreference = 'Stop'
$Root   = Split-Path -Parent $PSScriptRoot          # the jouster checkout
$Repos  = Split-Path -Parent $Root                  # the directory holding them all
$LogDir = Join-Path $Repos '_hardware-logs'

if ($NroPath) { $Nro = $NroPath }
elseif ($App -eq 'Jouster') { $Nro = Join-Path $Root 'game\Jouster.nro' }
else { $Nro = Join-Path $Repos "$($App.ToLower())\switch\$App.nro" }

$RemoteName = "$App.nro"

function Say($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Ok ($m) { Write-Host "ok  $m" -ForegroundColor Green }
function Bad($m) { Write-Host "!!  $m" -ForegroundColor Red }

# Silent delete. Without this the push stops on "permanently delete this
# file?" and waits for a click, once per build.
. (Join-Path $PSScriptRoot 'mtp-delete.ps1')

$shell = New-Object -ComObject Shell.Application

function Get-Device {
    $d = $shell.NameSpace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch' }
    if (-not $d) {
        Bad "no 'Nintendo Switch' under This PC."
        Bad "MTP is only up while the console runs its USB transfer app"
        Bad "(Haze, or hbmenu on builds that bundle it) -- not during a run."
        exit 1
    }
    $d
}

function Get-SdRoot {
    $sd = (Get-Device).GetFolder.Items() | Where-Object { $_.Name -eq 'SD Card' }
    if (-not $sd) { Bad "no 'SD Card' on the device"; exit 1 }
    $sd
}

function Get-FolderAt([string]$path) {
    $cur = Get-SdRoot
    if ($path) {
        foreach ($seg in $path.Split('/')) {
            if (-not $seg) { continue }
            $next = $cur.GetFolder.Items() | Where-Object { $_.Name -eq $seg }
            if (-not $next) { return $null }
            $cur = $next
        }
    }
    $cur.GetFolder
}

function Get-SizeOf($item) {
    if ($null -eq $item) { return -1 }
    try { return [int64]$item.ExtendedProperty('System.Size') } catch { return -1 }
}

# Poll until a copy settles. MTP reports no progress and no completion, so
# "settled" means present at the expected size, or the same size twice running.
function Wait-Settled($folder, [string]$name, [int64]$expect, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $last = -1
    $stable = 0
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 800
        $it = $folder.Items() | Where-Object { $_.Name -eq $name }
        $sz = Get-SizeOf $it
        if ($sz -ge 0 -and $sz -eq $last) { $stable++ } else { $stable = 0 }
        if ($expect -gt 0 -and $sz -eq $expect -and $stable -ge 1) { return $sz }
        if ($expect -le 0 -and $sz -gt 0 -and $stable -ge 2) { return $sz }
        $last = $sz
        Write-Host "." -NoNewline
    }
    return $last
}

# The build stamp, read out of the NRO itself rather than passed in, so what
# is reported cannot disagree with what is sent.
#
# Scanned in chunks rather than with Select-String: its -Encoding Byte is not
# a real option and the call silently threw, which is how the first push of a
# new build reported its stamp as "unknown". A 177MB ReadAllBytes would work
# but costs the memory for no reason -- the marker sits in .rodata and turns
# up in the first pass either way. The overlap covers a marker straddling a
# chunk boundary.
function Get-BuildStamp([string]$file) {
    $chunk   = 8MB
    $overlap = 128
    try {
        $fs = [System.IO.File]::OpenRead($file)
        $buf = New-Object byte[] $chunk
        $carry = ''
        while (($read = $fs.Read($buf, 0, $chunk)) -gt 0) {
            $text = $carry + [System.Text.Encoding]::ASCII.GetString($buf, 0, $read)
            $m = [regex]::Match($text, 'ARKCHEMY_BUILD ([A-Za-z]{3} [ 0-9]{1,2} \d{4} [0-9:]{8})')
            if ($m.Success) { $fs.Close(); return $m.Groups[1].Value }
            $carry = $text.Substring([Math]::Max(0, $text.Length - $overlap))
        }
        $fs.Close()
    } catch { }
    return 'unknown'
}

function Invoke-Pull {
    Say "pulling from the card"
    $jd = Get-FolderAt 'switch/Jouster'
    if ($null -eq $jd) { Bad "no /switch/Jouster on the card"; return }

    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    $stage = Join-Path $env:TEMP ("ark-mtp-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    $stageNs = $shell.NameSpace($stage)

    foreach ($name in @('game-results.log','run-tally.txt')) {
        $src = $jd.Items() | Where-Object { $_.Name -eq $name }
        if (-not $src) { Write-Host "    ($name is not on the card)"; continue }
        $want = Get-SizeOf $src
        Write-Host "    $name ($want bytes) " -NoNewline
        $stageNs.CopyHere($src, 16)
        $got = Wait-Settled $stageNs $name $want 900
        Write-Host ""

        $local = Join-Path $stage $name
        if (-not (Test-Path $local)) { Bad "$name did not arrive"; continue }
        if ($want -gt 0 -and $got -ne $want) { Bad "$name truncated -- $got of $want"; continue }

        if ($name -eq 'run-tally.txt') {
            Copy-Item $local (Join-Path $LogDir 'run-tally.txt') -Force
            $t = @(Get-Content $local)
            $o = @($t | Where-Object { $_ -match '^OK' }).Count
            $f = @($t | Where-Object { $_ -match '^FAIL' }).Count
            Ok "tally: $o ok, $f failed"
            continue
        }

        # The hash decides whether this is a new run, not the clock. Re-pulling
        # an unchanged log should be silent rather than adding another copy --
        # nine identical files landed in _hardware-logs on 2026-09-14 doing
        # exactly that.
        $h = (Get-FileHash $local -Algorithm SHA256).Hash
        $latest = Join-Path $LogDir 'latest.log'
        if ((Test-Path $latest) -and ((Get-FileHash $latest -Algorithm SHA256).Hash -eq $h)) {
            Ok "log unchanged since the last pull"
        } else {
            $dest = Join-Path $LogDir ((Get-Date -Format 'yyyy-MM-dd-HHmm') + '-game-results.log')
            Copy-Item $local $dest -Force
            Copy-Item $local $latest -Force
            Ok "new run pulled -- $got bytes -> $dest"
        }
    }
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}

function Invoke-Push {
    if (-not (Test-Path $Nro)) { Bad "no $Nro -- build it first"; return }
    $want  = (Get-Item $Nro).Length
    $stamp = Get-BuildStamp $Nro
    Say "pushing $RemoteName -- $stamp -- $want bytes"

    $sw = Get-FolderAt 'switch'
    if ($null -eq $sw) { Bad "no /switch on the card"; return }

    $existing = $sw.Items() | Where-Object { $_.Name -eq $RemoteName }
    if ($existing) {
        Write-Host "    removing the old copy" -NoNewline
        $gone = $false
        try {
            $gone = Remove-MtpPath -Segments @('SD Card','switch',$RemoteName)
        }
        catch {
            Write-Host ""
            Bad "silent delete failed: $($_.Exception.Message)"
        }
        Write-Host ""
        if (-not $gone) {
            Bad "could not delete the old $RemoteName."
            Bad "Copying now would create a second copy and the console would"
            Bad "keep running the old build. Delete it by hand and re-run."
            return
        }
    }

    Write-Host "    copying (177MB over USB takes a minute) " -NoNewline
    $sw.CopyHere($Nro, 16)
    $got = Wait-Settled $sw $RemoteName $want 1800
    Write-Host ""
    if ($got -ne $want) { Bad "push incomplete -- $got of $want bytes on the card"; return }
    Ok "$RemoteName on the card -- $got bytes -- $stamp"
    Write-Host "    confirm it ran from the next log's first line: $stamp"
}

switch ($Action) {
    'push' { Invoke-Push }
    'pull' { Invoke-Pull }
    'both' { Invoke-Push; Invoke-Pull }
}
