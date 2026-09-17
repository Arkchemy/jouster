#!/bin/sh
# Install, build, and deliver -- the whole Windows path in one command.
#
#     ./tools/windows-all.sh [switch-ip]
#
# Each stage is skipped if it is already done, so this is safe to re-run and
# is the normal way to rebuild: a second run with the toolchain already in
# place goes straight to make.
#
# The console address is optional. Given one, it is remembered in
# tools/switch-address.txt and used for every later run. Without one, this
# still installs and builds, and says what to run to deliver.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

say()  { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
ok()   { printf '\033[32mok  %s\033[0m\n' "$1"; }
warn() { printf '\033[33m!!  %s\033[0m\n' "$1"; }

[ -n "${1:-}" ] && printf '%s\n' "$1" > tools/switch-address.txt

# ---------------------------------------------------------------- 1. toolchain
# $DEVKITPRO is tested, not trusted. devkitPro's Windows installer sets it to
# /opt/devkitpro as a user environment variable, which is correct inside its
# own msys2 and meaningless in Git Bash, where that path sits under the Git
# installation and does not exist. This script used to take the value on faith
# and stop with "devkitA64 still missing" on a machine whose toolchain was
# complete and working -- setup-windows.ps1, which hardcodes C:\devkitPro, had
# just finished confirming every library and tool in the same run. Found
# 2026-09-17, on the first build after a fresh Git Bash.
#
# So try each candidate and keep the first one that actually holds a compiler.
# A Windows-style path is converted first, since -x cannot read one.
dkp_has_gcc() {
    [ -n "$1" ] && [ -x "$1/devkitA64/bin/aarch64-none-elf-gcc.exe" ]
}
dkp_unix() {
    case "$1" in
        [A-Za-z]:[\\/]*) cygpath -u "$1" 2>/dev/null || printf '%s\n' "$1" ;;
        *)                 printf '%s\n' "$1" ;;
    esac
}
dkp_find() {
    for cand in "${DEVKITPRO:-}" /c/devkitPro /c/devkitpro; do
        [ -n "$cand" ] || continue
        cand="$(dkp_unix "$cand")"
        if dkp_has_gcc "$cand"; then printf '%s\n' "$cand"; return 0; fi
    done
    return 1
}

DKP="$(dkp_find || true)"
if [ -n "$DKP" ]; then
    ok "devkitPro already installed at $DKP"
else
    say "installing devkitPro -- tick 'Switch Development' in the installer"
    powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$ROOT/tools/setup-windows.ps1")"
    DKP="$(dkp_find || true)"
    [ -n "$DKP" ] || {
        warn "devkitA64 still missing -- stopping here rather than failing at the link step"
        warn "looked in: ${DEVKITPRO:-(DEVKITPRO unset)}, /c/devkitPro, /c/devkitpro"
        exit 1; }
    ok "devkitPro installed at $DKP"
fi

export DEVKITPRO="$DKP"
# make ships in devkitPro's own msys2, not in tools/bin.
MAKE="$DKP/msys2/usr/bin/make.exe"
[ -x "$MAKE" ] || MAKE="$(command -v make 2>/dev/null || true)"
[ -n "$MAKE" ] && [ -x "$MAKE" ] || { warn "no make found under $DKP/msys2/usr/bin"; exit 1; }

# Dependency files carry ABSOLUTE paths. A build directory copied from another
# machine makes make demand source files at paths that do not exist here, and
# it says so in terms of a missing .c rather than a stale .d:
#
#   No rule to make target '/home/aaron/.../game/source/bink_ffmpeg.c'
#
# Clearing them is the fix and costs a full rebuild, so it happens only when
# a foreign path is actually present rather than on every run.
if [ -d game/build ] && grep -lq "$(printf '/home/')" game/build/*.d 2>/dev/null; then
    n=$(grep -l "$(printf '/home/')" game/build/*.d 2>/dev/null | wc -l)
    warn "$n dependency files reference another machine's paths -- clearing game/build"
    rm -rf game/build
fi

# -------------------------------------------------------------------- 2. build
# Deliberately not conditional on the NRO existing: the whole reason this run
# exists is that the NRO on disk is older than the source. make decides.
say "building (this is 200-odd generated translation units -- give it a while)"
"$MAKE" -C game -j"$(nproc 2>/dev/null || echo 4)"

NRO="$ROOT/game/Jouster.nro"
[ -f "$NRO" ] || { warn "make finished but $NRO is not there"; exit 1; }
stamp="$(grep -a -o 'ARKCHEMY_BUILD [A-Za-z0-9 :]*' "$NRO" | head -1 | sed 's/ARKCHEMY_BUILD //')"
ok "built $(wc -c < "$NRO") bytes -- stamp ${stamp:-unknown}"

# ----------------------------------------------------------------- 3. deliver
# MTP over USB is the default: it needs no network on the console, which is
# the situation this machine is actually in. ARK_DELIVER=ftp switches to
# ftp-courier.sh for when the Switch has wifi and you would rather not plug
# it in.
case "${ARK_DELIVER:-mtp}" in
ftp)
    if [ ! -f tools/switch-address.txt ]; then
        say "built, not delivered"
        warn "ARK_DELIVER=ftp but no console address. Start ftpd, then run:"
        echo ""
        echo "    ./tools/windows-all.sh <switch-ip>"
        echo ""
        exit 0
    fi
    say "delivering over ftp to $(tr -d ' \t\r\n' < tools/switch-address.txt)"
    if ! ./tools/ftp-courier.sh; then
        warn "delivery failed -- the build is fine at game/Jouster.nro"
        warn "start ftpd on the console and re-run: ./tools/ftp-courier.sh"
        exit 1
    fi
    ;;
*)
    say "delivering over USB/MTP -- the console must be in its transfer app"
    if ! powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$ROOT/tools/mtp-courier.ps1")" both; then
        warn "delivery failed -- the build is fine at game/Jouster.nro"
        warn "put the console in Haze/hbmenu and re-run:"
        warn "  powershell -ExecutionPolicy Bypass -File tools\mtp-courier.ps1"
        exit 1
    fi
    ;;
esac
ok "done -- launch Jouster on the console"
