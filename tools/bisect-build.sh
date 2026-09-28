#!/bin/sh
# Build Jouster against a chosen conquertron and send it to the Switch, to
# find which conquertron change a hardware regression came from.
#
#     tools/bisect-build.sh <variant> <path-to-tfbGame_cafe.rpx>
#
#   head     conquertron as checked out beside jouster
#   sep20    conquertron at a7b0bb3 (2026-09-20): recompiler and runtime
#            headers both from before the changes of 2026-09-24..27
#   nosync   head, with the two sync-shim changes of 2026-09-24 taken out
#            (02fed45 AUTO events + FS queue lock, 7665c01 OSWaitSemaphore
#            slices) -- everything else, recompiler included, as head
#
# Found necessary 2026-09-28: a build of head reached no file read at all,
# where the build of 2026-09-18 drew 12,807 times. Comparing sep20 with head
# splits "a conquertron change" from "a jouster change"; nosync then splits
# the sync shims from the recompiler.
#
# Runs on the host (Bluefin): the recompiler builds in the distrobox `ark`
# (Capstone in ~/devtools/capstone-install), the game in the devkitPro
# container. The variant's conquertron is a git worktree beside jouster,
# $ARK/cq-<variant>, so the container can see it. Regenerating replaces
# game/source/generated_*.c; run `bisect-build.sh head` to go back.
set -e
V="$1"; RPX="$2"
[ -n "$V" ] && [ -f "$RPX" ] || { echo "usage: $0 head|sep20|nosync <tfbGame_cafe.rpx>" >&2; exit 2; }
JO="$(cd "$(dirname "$0")/.." && pwd)"
ARK="$(dirname "$JO")"
CQ="$ARK/conquertron"
case "$V" in
    head)   DIR="$CQ" ;;
    sep20|nosync)
        DIR="$ARK/cq-$V"
        if [ ! -d "$DIR" ]; then
            if [ "$V" = sep20 ]; then
                git -C "$CQ" worktree add -f --detach "$DIR" a7b0bb3
            else
                git -C "$CQ" worktree add -f --detach "$DIR" HEAD
                git -C "$DIR" revert --no-commit 7665c01 02fed45
            fi
        fi ;;
    *) echo "unknown variant $V" >&2; exit 2 ;;
esac
echo "== $V: conquertron at $DIR ($(git -C "$DIR" log -1 --format='%h %ad %s' --date=short))"

echo "== recompiler and regenerate (distrobox ark)"
distrobox enter ark -- sh -c '
    set -e
    cmake -S "$1" -B "$1/build-bisect" -DCMAKE_BUILD_TYPE=Release -DCAPSTONE_PREFIX="$HOME/devtools/capstone-install" >/dev/null
    cmake --build "$1/build-bisect" -j"$(nproc)" --target recomp >/dev/null
    CONQUERTRON="$1" RECOMP="$1/build-bisect/recomp" "$2/game/regenerate.sh" "$3" 2>&1 | tail -4
    python3 "$2/tools/native-overrides.py" "$2/game"
' sh "$DIR" "$JO" "$RPX"

echo "== build (devkitPro container)"
rm -rf "$JO/game/build" "$JO/game/Jouster.nro"   # a different conquertron: nothing from the last build may be reused
podman run --rm -v "$ARK":/work:z -w /work/jouster/game docker.io/devkitpro/devkita64 bash -lc \
    "dkp-pacman -Sy --noconfirm --needed switch-bzip2 switch-curl switch-dav1d switch-ffmpeg switch-mbedtls switch-zlib >/dev/null; make -j\$(nproc) CONQUERTRON=/work/${DIR#"$ARK"/} 2>&1 | grep -E 'error|linking|built' || true"
[ -f "$JO/game/Jouster.nro" ] || { echo "build failed" >&2; exit 1; }

echo "== deliver (Switch in hbmenu, USB connected)"
mkdir -p "$JO/build/courier"
cp "$JO/game/Jouster.nro" "$JO/build/courier/"
"$JO/tools/courier.sh" --once
echo "== $V sent. Run it, then pull the log with: tools/courier.sh --once"
