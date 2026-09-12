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
# Drop a finished build at $DROP/pending.nro and it lands on the card the
# next time the Switch is in hbmenu, verified by hash. Pulled logs are kept
# in _hardware-logs/ with a timestamp, plus a stable latest.log.
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

push_nro() {
    card="$1"
    [ -f "$DROP/pending.nro" ] || return 0
    want="$(hash_of "$DROP/pending.nro")"
    gio copy "$DROP/pending.nro" "$card/$REMOTE_NRO" 2>/dev/null || {
        echo "BUILD push FAILED (copy error) -- will retry next poll"; return 0; }
    gio copy "$card/$REMOTE_NRO" "$STATE/verify.nro" 2>/dev/null || {
        echo "BUILD push unverified (read-back failed) -- will retry next poll"; return 0; }
    got="$(hash_of "$STATE/verify.nro")"
    rm -f "$STATE/verify.nro"
    if [ "$want" = "$got" ]; then
        mv "$DROP/pending.nro" "$DROP/sent-$(date +%Y%m%d-%H%M%S)-$want.nro"
        echo "BUILD on the card and hash-verified -- $want -- ready to launch"
    else
        echo "BUILD push MISMATCH -- wanted $want got $got -- left pending, will retry"
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
