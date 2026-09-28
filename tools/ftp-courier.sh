#!/bin/sh
# Move builds and logs over FTP, for machines where the MTP courier cannot run.
#
# courier.sh drives the Switch's MTP mount through `gio`, which is gvfs and so
# Linux-only. This does the same two jobs against ftpd running on the console,
# using curl, which exists everywhere. It is not a replacement for courier.sh
# on Linux -- MTP needs no homebrew running on the console, and ftpd does.
#
#   ./ftp-courier.sh push          send game/Jouster.nro
#   ./ftp-courier.sh pull          fetch the log into _hardware-logs/
#   ./ftp-courier.sh               push, then pull
#   ./ftp-courier.sh push --verify re-download and hash after sending
#
# The console's address comes from $ARK_SWITCH, or tools/switch-address.txt,
# in that order. Port defaults to ftpd's 5000.
#
#   echo 192.168.1.42 > tools/switch-address.txt
#
# Start ftpd on the console first: it is a normal homebrew app, so launching it
# ENDS any run in progress and the loop rig will not be relaunching while it is
# up. That is the trade against MTP -- you get a reliable transfer from any OS,
# but the console is doing nothing else while you use it.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOGDIR="${ARK_LOGDIR:-$ROOT/../_hardware-logs}"
NRO="${ARK_NRO:-$ROOT/game/Jouster.nro}"
PORT="${ARK_SWITCH_PORT:-5000}"

# Validate the subcommand before resolving anything. A typo used to report
# "no console address", which sends you to configure a thing that was never
# the problem.
case "${1:-both}" in
    push|pull|both) ;;
    *) echo "usage: $0 [push [--verify] | pull]" >&2; exit 1 ;;
esac

addr="${ARK_SWITCH:-}"
if [ -z "$addr" ] && [ -f "$ROOT/tools/switch-address.txt" ]; then
    addr="$(tr -d ' \t\r\n' < "$ROOT/tools/switch-address.txt")"
fi
[ -n "$addr" ] || {
    echo "no console address -- set ARK_SWITCH=<ip> or write tools/switch-address.txt" >&2
    exit 1; }

BASE="ftp://$addr:$PORT"

# The build stamp, read out of the NRO rather than passed in, exactly as
# publish-build.sh does it -- so what is reported cannot disagree with what is
# sent. `strings` is not present on MSYS, and grep -a does the same job here.
stamp_of() {
    grep -a -o 'ARKCHEMY_BUILD [A-Za-z0-9 :]*' "$1" 2>/dev/null | head -1 \
        | sed 's/ARKCHEMY_BUILD //'
}

reachable() {
    curl -s --max-time 8 --list-only "$BASE/" >/dev/null 2>&1 && return 0
    echo "cannot reach $BASE -- is ftpd running on the console?" >&2
    return 1
}

do_push() {
    [ -f "$NRO" ] || { echo "no $NRO -- build it first" >&2; return 1; }
    reachable || return 1

    st="$(stamp_of "$NRO")"
    sz="$(wc -c < "$NRO")"
    echo "PUSH Jouster.nro -- ${st:-no stamp found} -- $sz bytes"

    curl -s --max-time 1800 -T "$NRO" "$BASE/switch/Jouster.nro" || {
        echo "PUSH FAILED -- the console may have left ftpd" >&2; return 1; }

    # Size is a weak check and this project has been bitten by trusting it:
    # consecutive builds are routinely byte-identical in size because the
    # 176MB ROM blob dominates. It catches a truncated transfer and nothing
    # else, which is why --verify exists and why the stamp is printed.
    got="$(curl -s --max-time 30 --head "$BASE/switch/Jouster.nro" 2>/dev/null \
           | grep -i '^Content-Length:' | tr -dc '0-9')"
    if [ -n "$got" ] && [ "$got" != "$sz" ]; then
        echo "PUSH TRUNCATED -- sent $sz, console has $got" >&2; return 1
    fi
    echo "PUSH complete -- $sz bytes on the card"

    if [ "${1:-}" = "--verify" ]; then
        echo "VERIFY re-downloading to hash (this is the whole file again)"
        tmp="$(mktemp)"
        curl -s --max-time 1800 -o "$tmp" "$BASE/switch/Jouster.nro"
        a="$(sha256sum "$NRO" | cut -d' ' -f1)"
        b="$(sha256sum "$tmp"  | cut -d' ' -f1)"
        rm -f "$tmp"
        [ "$a" = "$b" ] && echo "VERIFY ok -- $a" \
                        || { echo "VERIFY MISMATCH -- $a vs $b" >&2; return 1; }
    else
        echo "VERIFY skipped -- confirm from the next log's first line: ${st:-?}"
    fi
}

do_pull() {
    reachable || return 1
    mkdir -p "$LOGDIR"
    tmp="$(mktemp)"
    if ! curl -s --max-time 600 -o "$tmp" \
              "$BASE/switch/Jouster/game-results.log" 2>/dev/null; then
        rm -f "$tmp"; echo "no log on the console yet"; return 0
    fi
    [ -s "$tmp" ] || { rm -f "$tmp"; echo "no log on the console yet"; return 0; }

    # Same rule as courier.sh: never overwrite a distinct run. The hash decides
    # whether this is new, not the timestamp -- a re-pull of an unchanged log
    # should be silent rather than filling the directory with duplicates, which
    # is exactly what the MTP courier's timestamped names did on 2026-09-14.
    h="$(sha256sum "$tmp" | cut -d' ' -f1)"
    if [ -f "$LOGDIR/latest.log" ] \
       && [ "$h" = "$(sha256sum "$LOGDIR/latest.log" | cut -d' ' -f1)" ]; then
        rm -f "$tmp"; echo "log unchanged since the last pull"; return 0
    fi
    dest="$LOGDIR/$(date +%Y-%m-%d-%H%M)-game-results.log"
    cp "$tmp" "$dest"; cp "$tmp" "$LOGDIR/latest.log"; rm -f "$tmp"
    echo "LOG new run pulled -- $(wc -c < "$dest") bytes -> $dest"

    # The tally is the point of pulling at all now: one line per run, and the
    # failure lines are written by the run AFTER the one that failed.
    if curl -s --max-time 30 -o "$LOGDIR/run-tally.txt" \
            "$BASE/switch/Jouster/run-tally.txt" 2>/dev/null \
       && [ -s "$LOGDIR/run-tally.txt" ]; then
        ok=$(grep -c '^OK'   "$LOGDIR/run-tally.txt" 2>/dev/null || echo 0)
        no=$(grep -c '^FAIL' "$LOGDIR/run-tally.txt" 2>/dev/null || echo 0)
        echo "TALLY $ok ok, $no failed -> $LOGDIR/run-tally.txt"
    fi
}

case "${1:-both}" in
    push) shift 2>/dev/null || true; do_push "${1:-}" ;;
    pull) do_pull ;;
    both) do_push "" ; do_pull ;;
    *)    echo "usage: $0 [push [--verify] | pull]" >&2; exit 1 ;;
esac
