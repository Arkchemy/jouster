#!/bin/sh
# Moves builds and logs between this machine and the Switch, by itself.
#
# The Switch's MTP mount is only up while it sits in hbmenu: launching any
# homebrew drops it, returning restores it. That makes the mount a free
# run-start/run-finish signal, and it makes writing to the card safe by
# construction -- you cannot write mid-run, because there is nothing to write
# to. This script leans on that rather than trying to detect the game itself.
#
# Every stdout line is one event, so this can be driven by a watcher that
# turns lines into notifications. It is deliberately quiet otherwise: a poll
# that finds nothing new prints nothing.
#
# Drop files in $DROP and they land on the card the next time the Switch is in
# hbmenu, verified by hash:
#
#   $DROP/<Name>.nro    ->  switch/<Name>.nro        (Jouster.nro, Armory.nro)
#   $DROP/sd/<path>     ->  <card>/<path>            (anything else, e.g.
#                                                     sd/switch/Armory/catalogue.tsv)
#
# Pulled logs are kept in _hardware-logs/ with a timestamp, plus latest.log.
# `pending.nro` is still accepted and still means Jouster, because that is what
# the previous convention was and a silently ignored drop would be worse than
# a slightly redundant rule.
#
#   ./courier.sh            poll forever
#   ./courier.sh --once     one pass, then exit
#
# Verify card copies by HASH, never by size: consecutive builds are routinely
# byte-identical in size because the ROM blob dominates (176MB), so size
# cannot tell a fresh .nro from a stale one.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOGDIR="${ARK_LOGDIR:-$ROOT/../_hardware-logs}"
DROP="${ARK_DROP:-$ROOT/build/courier}"
STATE="${ARK_STATE:-$HOME/.cache/arkchemy-courier}"
INTERVAL="${ARK_INTERVAL:-20}"

CARD_GLOB='/run/user/*/gvfs/mtp:host=Nintendo_Nintendo_Switch_*'
REMOTE_LOG='switch/Jouster/game-results.log'
REMOTE_NRO='switch/Jouster.nro'

mkdir -p "$LOGDIR" "$DROP" "$STATE"

card_root() {
    for c in $CARD_GLOB; do
        [ -d "$c/SD Card" ] || continue
        # A stale gvfs entry can linger after the Switch launches something;
        # only a directory that actually lists counts as mounted.
        ls "$c/SD Card" >/dev/null 2>&1 || continue
        printf '%s/SD Card\n' "$c"
        return 0
    done
    return 1
}

hash_of() { md5sum "$1" 2>/dev/null | cut -d' ' -f1; }

pull_log() {
    card="$1"
    tmp="$STATE/pull.log"
    gio copy "$card/$REMOTE_LOG" "$tmp" 2>/dev/null || return 0
    h="$(hash_of "$tmp")"
    [ -n "$h" ] || return 0
    old="$(cat "$STATE/log.md5" 2>/dev/null || true)"
    [ "$h" = "$old" ] && return 0
    build="$(grep -m1 -o 'build [A-Z][a-z][a-z] *[0-9]* [0-9]* [0-9:]*' "$tmp" || echo 'build unknown')"
    dest="$LOGDIR/$(date +%Y-%m-%d-%H%M)-game-results.log"
    cp "$tmp" "$dest"
    cp "$tmp" "$LOGDIR/latest.log"
    printf '%s\n' "$h" > "$STATE/log.md5"
    echo "LOG new run pulled -- $build -- $(wc -c < "$dest") bytes -> $dest"
}

# Copy one file and prove it arrived. Never trusts the copy: a read-back and a
# hash compare, because size cannot tell a fresh .nro from a stale one when the
# 176MB ROM blob dominates and consecutive builds land on the same byte count.
push_one() {
    card="$1"; src="$2"; dest="$3"; label="$4"
    want="$(hash_of "$src")"
    destdir="$(dirname "$dest")"
    if [ "$destdir" != "." ]; then
        ls "$card/$destdir" >/dev/null 2>&1 || gio mkdir -p "$card/$destdir" 2>/dev/null || true
    fi
    gio copy "$src" "$card/$dest" 2>/dev/null || {
        echo "PUSH $label FAILED (copy error) -- will retry next poll"; return 1; }
    gio copy "$card/$dest" "$STATE/verify.bin" 2>/dev/null || {
        echo "PUSH $label unverified (read-back failed) -- will retry next poll"; return 1; }
    got="$(hash_of "$STATE/verify.bin")"
    rm -f "$STATE/verify.bin"
    if [ "$want" = "$got" ]; then
        echo "PUSH $label on the card and hash-verified -- $want"
        return 0
    fi
    echo "PUSH $label MISMATCH -- wanted $want got $got -- left in the drop, will retry"
    return 1
}

push_nro() {
    card="$1"
    stamp="$(date +%Y%m%d-%H%M%S)"

    # Historical name: pending.nro has always meant Jouster.
    if [ -f "$DROP/pending.nro" ]; then
        if push_one "$card" "$DROP/pending.nro" "$REMOTE_NRO" "Jouster"; then
            mv "$DROP/pending.nro" "$DROP/sent-$stamp-Jouster.nro"
        fi
    fi

    # Any other .nro goes to switch/<its own name>.
    for f in "$DROP"/*.nro; do
        [ -f "$f" ] || continue
        base="$(basename "$f")"
        case "$base" in pending.nro|sent-*) continue ;; esac
        if push_one "$card" "$f" "switch/$base" "${base%.nro}"; then
            mv "$f" "$DROP/sent-$stamp-$base"
        fi
    done

    # Data files, mirrored under their own path.
    if [ -d "$DROP/sd" ]; then
        find "$DROP/sd" -type f 2>/dev/null | while read -r f; do
            rel="${f#$DROP/sd/}"
            if push_one "$card" "$f" "$rel" "$rel"; then
                rm -f "$f"
            fi
        done
    fi
}

# Always returns 0. Under `set -e` a bare `[ -f x ] && ...` as the last
# command of a branch takes the whole script down the first time the file is
# absent -- which is exactly how the first version of this died on its second
# poll, when the Switch went away and the "mounted" marker had already been
# removed. Explicit `if` blocks and an explicit `return 0`.
pass() {
    if card="$(card_root)"; then
        if [ ! -f "$STATE/mounted" ]; then
            echo "SWITCH in hbmenu (MTP up)"
            : > "$STATE/mounted"
        fi
        pull_log "$card"
        push_nro "$card"
    else
        if [ -f "$STATE/mounted" ]; then
            echo "SWITCH busy or unplugged (MTP down) -- a run may be in progress"
            rm -f "$STATE/mounted"
        fi
    fi
    return 0
}

if [ "${1:-}" = "--once" ]; then pass; exit 0; fi
while :; do pass; sleep "$INTERVAL"; done
