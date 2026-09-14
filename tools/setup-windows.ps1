<#
  One command to make this repo buildable on Windows.

      powershell -ExecutionPolicy Bypass -File tools\setup-windows.ps1

  Installs devkitPro and the libraries game/Makefile links against, then
  PROVES the result can build rather than reporting success because a download
  finished. Safe to re-run; every stage is skipped if already done.

  Everything below was established by doing it on 2026-09-14, not from the
  documentation, because four of the six steps do not behave as documented:

    - The installer's /VERYSILENT default installs devkitARM and the GBA
      libraries, NOT the Switch toolchain. Running it silently and trusting it
      leaves you with a complete-looking install and no aarch64 compiler.
      devkitA64 has to come from pacman afterwards.
    - pacman is at msys2\usr\bin\pacman.exe. There is no tools\bin\dkp-pacman.
    - make is at msys2\usr\bin\make.exe. There is no tools\bin\make.
    - `pacman -S switch-portlibs` does not install the group. The individual
      packages do install, so they are named explicitly.
    - libdeko3d.a lives in libnx\lib, not portlibs\switch\lib, even though it
      is a portlib in every other respect.
#>

$ErrorActionPreference = 'Stop'

$DKP        = 'C:\devkitPro'
$InstallerV = '3.0.3'
$InstallerU = "https://github.com/devkitPro/installer/releases/download/v$InstallerV/devkitProUpdater-$InstallerV.exe"

$Pacman = "$DKP\msys2\usr\bin\pacman.exe"
$Bash   = "$DKP\msys2\usr\bin\bash.exe"
$Gcc    = "$DKP\devkitA64\bin\aarch64-none-elf-gcc.exe"

# switch-dev carries devkitA64, libnx, deko3d, uam and switch-tools. The six
# portlibs are the ones game/Makefile links; their dependencies come along.
$Packages = @('switch-dev',
              'switch-bzip2','switch-curl','switch-dav1d',
              'switch-ffmpeg','switch-mbedtls','switch-zlib')

function Say($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Bad($m) { Write-Host "!!  $m" -ForegroundColor Red }
function Ok ($m) { Write-Host "ok  $m" -ForegroundColor Green }

# ------------------------------------------------------- 1. devkitPro bootstrap
if (Test-Path $Pacman) {
    Ok "devkitPro bootstrap already present at $DKP"
} else {
    Say "downloading devkitProUpdater $InstallerV"
    $exe = Join-Path $env:TEMP "devkitProUpdater-$InstallerV.exe"
    if (-not (Test-Path $exe)) {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $InstallerU -OutFile $exe -UseBasicParsing
    }

    # Refuse to run an installer that is not signed by devkitPro.
    $sig = Get-AuthenticodeSignature $exe
    if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'devkitpro') {
        Bad "installer signature is '$($sig.Status)' from '$($sig.SignerCertificate.Subject)'"
        Bad "refusing to run it. Delete $exe and try again."
        exit 1
    }
    Ok "installer signature valid -- $($sig.SignerCertificate.Subject)"

    Say "running the installer -- approve the UAC prompt"
    Start-Process -FilePath $exe -Verb RunAs `
        -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=$DKP" -Wait

    if (-not (Test-Path $Pacman)) {
        Bad "no pacman at $Pacman after the installer exited."
        Bad "If you cancelled the UAC prompt, re-run this script."
        exit 1
    }
    Ok "bootstrap installed"
}

# ------------------------------------------------------------ 2. the toolchain
if (Test-Path $Gcc) {
    Ok "devkitA64 already installed"
} else {
    Say "installing the Switch toolchain and portlibs (a few hundred MB)"
    & $Bash -lc "pacman -Sy --noconfirm" | Out-Null
    & $Bash -lc ("pacman -S --needed --noconfirm " + ($Packages -join ' '))
    if (-not (Test-Path $Gcc)) { Bad "devkitA64 still missing after pacman"; exit 1 }
    Ok "toolchain installed"
}

# Re-run the package line even when gcc was already there: it is --needed, so
# it is a no-op when everything is current and it repairs a partial install.
& $Bash -lc ("pacman -S --needed --noconfirm " + ($Packages -join ' ')) | Out-Null

# ----------------------------------------------------------------- 3. verify
Say "checking every library game/Makefile links"
$port  = "$DKP\portlibs\switch\lib"
$libnx = "$DKP\libnx\lib"
$need  = 'deko3d','curl','mbedtls','mbedx509','mbedcrypto','avformat','avcodec',
         'swresample','swscale','avutil','dav1d','bz2','z','nx'

$missing = @()
foreach ($n in $need) {
    $file = "lib$n.a"
    $where = @($port, $libnx) | Where-Object { Test-Path (Join-Path $_ $file) } | Select-Object -First 1
    if ($where) { Ok ("-l$n").PadRight(14) + $where }
    else        { Bad "-l$n  ($file is in neither $port nor $libnx)"; $missing += $n }
}

foreach ($t in @("$DKP\tools\bin\elf2nro.exe", "$DKP\tools\bin\uam.exe", "$DKP\msys2\usr\bin\make.exe")) {
    if (Test-Path $t) { Ok (Split-Path -Leaf $t) } else { Bad "missing tool: $t"; $missing += (Split-Path -Leaf $t) }
}

Write-Host ""
if ($missing.Count -gt 0) {
    Bad "$($missing.Count) missing: $($missing -join ', ')"
    Bad "Search for the package carrying one with:  $Bash -lc 'pacman -Ss <name>'"
    exit 1
}

Ok "everything the build needs is present"
Write-Host ""
Say "build with:"
Write-Host "    ./tools/windows-all.sh" -ForegroundColor White
