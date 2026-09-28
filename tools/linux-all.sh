#!/bin/sh
# Build and deliver on Linux -- the whole path in one command, every tool in a
# container, so an immutable desktop (Bluefin, Silverblue) needs only podman,
# which it already has.
#
#     ./tools/linux-all.sh [path/to/tfbGame_cafe.rpx]
#
# Each stage is skipped when it is already done, so re-running is the normal
# way to rebuild:
#
#   1. conquertron beside this checkout (cloned if missing)
#   2. the dump: the argument, else $ARK_RPX, else the path remembered from
#      last time, else a search of your home directory. Remembered in
#      ~/.config/arkchemy/rpx.
#   3. the two build images (tools/containers/), rebuilt only when their
#      Containerfile changes
#   4. recomp, built from conquertron
#   5. the generated C, regenerated only when recomp itself changed -- the
#      hash of the recomp binary is kept in game/.generated-with
#   6. game/Jouster.nro
#   7. delivered with tools/courier.sh --once when the Switch is in hbmenu
#      with USB connected (ARK_DELIVER=none skips it)
#
# ARK_MEMCHECK=1 builds with conquertron's guest-memory range check.
# The GitHub workflow runs exactly this on a self-hosted Linux runner; see
# tools/setup-runner-linux.sh.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARK="$(dirname "$ROOT")"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/arkchemy"
cd "$ROOT"

say()  { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
ok()   { printf '\033[32mok  %s\033[0m\n' "$1"; }
warn() { printf '\033[33m!!  %s\033[0m\n' "$1"; }
die()  { printf '\033[31mxx  %s\033[0m\n' "$1"; exit 1; }

command -v podman >/dev/null || die "podman not found -- it ships with Bluefin/Silverblue; elsewhere install it first"

# ------------------------------------------------------------ 1. conquertron
CQ="${CONQUERTRON:-$ARK/conquertron}"
if [ ! -f "$CQ/include/ppc_runtime.h" ]; then
    say "cloning conquertron beside jouster, at $CQ"
    git clone -q https://github.com/Arkchemy/conquertron.git "$CQ"
fi
ok "conquertron at $CQ ($(git -C "$CQ" rev-parse --short HEAD 2>/dev/null || echo '?'))"

# ------------------------------------------------------------------- 2. dump
RPX="${1:-${ARK_RPX:-}}"
[ -n "$RPX" ] || RPX="$(cat "$CONF/rpx" 2>/dev/null || true)"
if [ -z "$RPX" ] || [ ! -f "$RPX" ]; then
    say "looking for tfbGame_cafe.rpx under $HOME"
    RPX="$(find "$HOME" -xdev -maxdepth 7 -name tfbGame_cafe.rpx -print 2>/dev/null | head -1 || true)"
fi
if [ -n "$RPX" ] && [ -f "$RPX" ]; then
    RPX="$(cd "$(dirname "$RPX")" && pwd)/$(basename "$RPX")"
    mkdir -p "$CONF" && printf '%s\n' "$RPX" > "$CONF/rpx"
    ok "dump at $RPX"
else
    RPX=""
    warn "no tfbGame_cafe.rpx found -- pass its path: tools/linux-all.sh /path/to/tfbGame_cafe.rpx"
fi

# ----------------------------------------------------------------- 3. images
image() {   # name, Containerfile -> prints the tag, building it if needed
    tag="localhost/arkchemy-$1:$(sha256sum "$2" | cut -c1-12)"
    if ! podman image exists "$tag"; then
        say "building the $1 image (once; cached after this)" >&2
        podman build -q -t "$tag" -f "$2" "$(dirname "$2")" >&2
    fi
    printf '%s\n' "$tag"
}
HOST_IMG="$(image host tools/containers/host.Containerfile)"
DKP_IMG="$(image devkitpro tools/containers/devkitpro.Containerfile)"
ok "images ready"

# Everything is mounted at its own path, so absolute paths in make's
# dependency files and in regenerate.sh mean the same thing inside and out.
# Only the two checkouts and the dump FILE are mounted: :z relabels a mount
# for SELinux recursively, and mounting a parent -- which can be $HOME --
# would relabel everything under it.
MOUNTS="-v $ROOT:$ROOT:z -v $CQ:$CQ:z"
[ -n "$RPX" ] && MOUNTS="$MOUNTS -v $RPX:$RPX:ro,z"
run() {  # image, command...
    img="$1"; shift
    # shellcheck disable=SC2086
    podman run --rm --userns=keep-id $MOUNTS -w "$ROOT" -e HOME=/tmp "$img" "$@"
}

# ------------------------------------------------------------------ 4. recomp
say "building recomp"
run "$HOST_IMG" sh -c "cmake -S '$CQ' -B '$CQ/build-podman' -DCMAKE_BUILD_TYPE=Release -DCAPSTONE_PREFIX=/opt/capstone >/dev/null && cmake --build '$CQ/build-podman' -j\"\$(nproc)\" | tail -1"
RECOMP="$CQ/build-podman/recomp"
[ -x "$RECOMP" ] || die "recomp did not build"
ok "recomp built"

# ------------------------------------------------------------ 5. generated C
want="$(sha256sum "$RECOMP" | cut -c1-16)"
have="$(cat game/.generated-with 2>/dev/null || true)"
if ls game/source/generated_*.c >/dev/null 2>&1 && [ "$want" = "$have" ]; then
    ok "generated C is current for this recomp"
elif [ -z "$RPX" ]; then
    if ls game/source/generated_*.c >/dev/null 2>&1; then
        warn "recomp changed but there is no dump to regenerate from -- building the existing generated C"
    else
        die "no generated C and no dump -- pass the path to tfbGame_cafe.rpx"
    fi
else
    say "regenerating the game's C from the dump (recomp changed) -- this takes a while"
    run "$HOST_IMG" env CONQUERTRON="$CQ" RECOMP="$RECOMP" game/regenerate.sh "$RPX"
    printf '%s\n' "$want" > game/.generated-with
    ok "regenerated"
fi

# ------------------------------------------------------------------- 6. build
# Idempotent, and cheap: also fixes a tree generated before regenerate.sh ran it
run "$HOST_IMG" python3 tools/native-overrides.py game
run "$HOST_IMG" python3 tools/probe-hooks.py game

# Dependency files carry absolute paths. A build directory made anywhere else
# (another machine, Windows) makes make demand files that do not exist here.
if [ -d game/build ] && grep -L "$ROOT" game/build/*.d 2>/dev/null | grep -q .; then
    warn "dependency files from another build location -- clearing game/build"
    rm -rf game/build
fi
say "building Jouster.nro (200-odd generated translation units -- give it a while)"
run "$DKP_IMG" bash -lc "export PATH=\$DEVKITPRO/devkitA64/bin:\$PATH; make -C game -j\$(nproc) CONQUERTRON='$CQ' ARK_MEMCHECK='${ARK_MEMCHECK:-0}'"
NRO="$ROOT/game/Jouster.nro"
[ -f "$NRO" ] || die "make finished but $NRO is not there"
stamp="$(grep -a -o 'ARKCHEMY_BUILD [A-Za-z0-9 :]*' "$NRO" | head -1 | sed 's/ARKCHEMY_BUILD //')"
ok "built $(wc -c < "$NRO") bytes -- stamp ${stamp:-unknown} -- $NRO"

# ----------------------------------------------------------------- 7. deliver
if [ "${ARK_DELIVER:-mtp}" = "none" ]; then
    ok "built, delivery skipped (ARK_DELIVER=none)"
    exit 0
fi
mkdir -p build/courier
cp "$NRO" build/courier/Jouster.nro
say "delivering over USB (the Switch must be in hbmenu with USB connected)"
out="$(tools/courier.sh --once 2>&1 || true)"
[ -n "$out" ] && printf '%s\n' "$out"
if printf '%s\n' "$out" | grep -q 'PUSH Jouster.*hash-verified'; then
    ok "on the Switch: switch/Jouster.nro"
else
    warn "not delivered yet -- open hbmenu with USB connected, then run: tools/courier.sh --once"
    warn "(or leave tools/courier.sh running; it delivers as soon as the Switch appears)"
    exit 3
fi
