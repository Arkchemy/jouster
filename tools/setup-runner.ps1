<#
  Registers this machine as the build runner for Arkchemy/jouster, so a push
  builds the NRO here and couriers it to the console without anyone typing
  anything.

      powershell -ExecutionPolicy Bypass -File tools\setup-runner.ps1

  Run it once. It is idempotent: every stage is skipped if it is already done,
  so re-running after a failure picks up where it stopped.

  What this actually sets up, stated plainly because a build machine that runs
  code on a push deserves to be understood before it is installed:

    - A GitHub Actions runner, in C:\actions-runner\jouster, registered to
      Arkchemy/jouster only. It polls GitHub for jobs; nothing dials in, and no
      port is opened. Remove it any time from Settings -> Actions -> Runners,
      or by deleting the scheduled task and that folder.
    - It runs the workflow in .github/workflows/build.yml, which builds in your
      EXISTING jouster checkout so the generated sources and the incremental
      build directory are reused. It will not touch uncommitted work: the
      checkout step refuses rather than resetting.
    - This repository is public. A self-hosted runner executes what the
      workflow says, so build.yml deliberately has no pull_request trigger and
      you should leave fork-PR workflows requiring approval under
      Settings -> Actions. Without that, a stranger's PR could run code here.

  Deliberately NOT installed as a Windows service. A service runs in session 0,
  and mtp-courier.ps1 drives the Explorer shell's COM namespace to talk to the
  console over USB, which needs a real interactive session. So the runner is a
  scheduled task that starts at logon and runs as you, which also means it
  needs no administrator rights to install.
#>

$ErrorActionPreference = 'Stop'

$Repo      = 'Arkchemy/jouster'
$RunnerDir = 'C:\actions-runner\jouster'
$TaskName  = 'Arkchemy jouster runner'
$Labels    = 'self-hosted,windows,arkchemy'

function Say  ($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Ok   ($m) { Write-Host "ok  $m"   -ForegroundColor Green }
function Warn ($m) { Write-Host "!!  $m"   -ForegroundColor Yellow }
function Bad  ($m) { Write-Host "!!  $m"   -ForegroundColor Red }

# ------------------------------------------------------------ 1. the checkout
# The workflow builds in your existing working copy, so we have to find it.
# Looked for in the order that is most likely to be right, and confirmed by
# files that only this repository has rather than by the folder's name.
function Test-JousterRepo ($p) {
    if (-not $p) { return $false }
    return (Test-Path (Join-Path $p '.git')) -and
           (Test-Path (Join-Path $p 'game\Makefile')) -and
           (Test-Path (Join-Path $p 'tools\windows-all.sh'))
}

Say 'locating your jouster checkout'
$candidates = @()
if ($PSScriptRoot) { $candidates += (Split-Path $PSScriptRoot -Parent) }
$candidates += (Get-Location).Path
$candidates += "$env:USERPROFILE\Documents\Github\Arkchemy\jouster"
$candidates += "$env:USERPROFILE\Documents\GitHub\Arkchemy\jouster"

$RepoPath = $candidates | Where-Object { Test-JousterRepo $_ } | Select-Object -First 1
while (-not $RepoPath) {
    Warn 'could not find it automatically'
    $typed = Read-Host 'Full path to your jouster folder'
    if (Test-JousterRepo $typed) { $RepoPath = (Resolve-Path $typed).Path }
    else { Bad "that does not look like jouster (no .git, game\Makefile and tools\windows-all.sh)" }
}
$RepoPath = (Resolve-Path $RepoPath).Path
Ok "jouster at $RepoPath"

# Git Bash is what the workflow runs its steps in, and it cannot cd into a
# backslash path. Forward slashes work in both.
$RepoPathUnix = $RepoPath -replace '\\', '/'

# ------------------------------------------------------------------- 2. gh
# gh is not required, but it turns three manual steps into none: fetching a
# registration token, and setting the ARK_REPO variable the workflow reads.
$HasGh = $false
if (Get-Command gh -ErrorAction SilentlyContinue) {
    gh auth status 2>&1 | Out-Null
    $HasGh = ($LASTEXITCODE -eq 0)
}
if ($HasGh) { Ok 'gh is installed and signed in' }
else { Warn 'gh not available or not signed in -- you will be asked to paste one token' }

# --------------------------------------------------------------- 3. the runner
if (Test-Path (Join-Path $RunnerDir '.runner')) {
    Ok "runner already configured in $RunnerDir"
} else {
    Say 'downloading the GitHub Actions runner'
    New-Item -ItemType Directory -Force -Path $RunnerDir | Out-Null

    $Version = $null
    try {
        $rel = Invoke-RestMethod -UseBasicParsing 'https://api.github.com/repos/actions/runner/releases/latest'
        $Version = $rel.tag_name.TrimStart('v')
    } catch {
        # An unauthenticated API call can be rate-limited. Not a reason to stop.
        Warn "could not read the latest release ($($_.Exception.Message)) -- pinning a known-good one"
        $Version = '2.328.0'
    }
    $Zip = Join-Path $env:TEMP "actions-runner-win-x64-$Version.zip"
    $Url = "https://github.com/actions/runner/releases/download/v$Version/actions-runner-win-x64-$Version.zip"
    if (-not (Test-Path $Zip)) { Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Zip }
    Ok "runner $Version downloaded"

    Say 'unpacking'
    Expand-Archive -Path $Zip -DestinationPath $RunnerDir -Force
    Ok "unpacked into $RunnerDir"

    Say 'registering with GitHub'
    $Token = $null
    if ($HasGh) {
        $Token = (gh api -X POST "repos/$Repo/actions/runners/registration-token" --jq .token)
        if ($LASTEXITCODE -ne 0) { $Token = $null; Warn 'gh could not get a registration token' }
    }
    if (-not $Token) {
        $page = "https://github.com/$Repo/settings/actions/runners/new?arch=x64&os=win"
        Warn 'opening the runner page -- copy the token out of the ./config.cmd line it shows'
        Start-Process $page
        $Token = Read-Host 'Registration token'
    }

    Push-Location $RunnerDir
    try {
        & .\config.cmd --url "https://github.com/$Repo" --token $Token `
            --name "$env:COMPUTERNAME-jouster" --labels $Labels `
            --work '_work' --unattended --replace
        if ($LASTEXITCODE -ne 0) { throw "config.cmd failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
    Ok 'registered'
}

# ------------------------------------------------------------- 4. ARK_REPO
Say 'telling the workflow where to build'
if ($HasGh) {
    gh variable set ARK_REPO --repo $Repo --body $RepoPathUnix
    if ($LASTEXITCODE -eq 0) { Ok "ARK_REPO = $RepoPathUnix" }
    else { Warn "could not set it -- add ARK_REPO = $RepoPathUnix under Settings -> Secrets and variables -> Actions -> Variables" }
} else {
    Warn "add a repository variable ARK_REPO = $RepoPathUnix"
    Warn "  Settings -> Secrets and variables -> Actions -> Variables -> New variable"
}

# --------------------------------------------------------- 5. start, and keep
$RunCmd = Join-Path $RunnerDir 'run.cmd'
Say 'starting it, and setting it to start at logon'
schtasks /Create /TN "$TaskName" /TR "`"$RunCmd`"" /SC ONLOGON /RL LIMITED /F | Out-Null
if ($LASTEXITCODE -eq 0) { Ok "scheduled task '$TaskName' created" }
else { Warn 'could not create the scheduled task -- the runner will still run until you close its window' }

$already = Get-Process -Name 'Runner.Listener' -ErrorAction SilentlyContinue
if ($already) {
    Ok 'runner is already listening'
} else {
    Start-Process -FilePath $RunCmd -WorkingDirectory $RunnerDir
    Start-Sleep -Seconds 5
    if (Get-Process -Name 'Runner.Listener' -ErrorAction SilentlyContinue) { Ok 'runner is listening' }
    else { Warn "runner did not come up -- run it by hand once to see why: $RunCmd" }
}

Write-Host ''
Ok 'done'
Write-Host ''
Write-Host '  Every push to jouster now builds here and couriers to the console.' -ForegroundColor Cyan
Write-Host "  Watch a run at https://github.com/$Repo/actions" -ForegroundColor Cyan
Write-Host "  Remove it from Settings -> Actions -> Runners, then delete $RunnerDir" -ForegroundColor Cyan
Write-Host ''
