#!/bin/sh
# Watch the endpoint for logs the console uploaded, and summarise each new one.
#
#     log-watch.sh            # poll forever
#     log-watch.sh --once     # single pass, for a cron or a monitor
#
# Prints one line per new log with the numbers worth waking up for, so a
# notification carries the answer rather than just the news that an answer
# exists. Anything already downloaded is skipped, so this is safe to poll.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENDPOINT="${ARK_ENDPOINT:-https://arkchemy-logs.vercel.app}"
STORE="${ARK_WEBLOGS:-$ROOT/../_hardware-logs/web}"
INTERVAL="${ARK_INTERVAL:-60}"
mkdir -p "$STORE"

summarise() {
    log="$1"
    build="$(grep -am1 'starting -- build' "$log" | sed 's/.*build //')"
    draws="$(grep -a '^GX2LISTS' "$log" | tail -1 | grep -o 'GX2DrawEx x[0-9]*' | head -1)"
    shd="$(grep -a '^SHADERMOD' "$log" | tail -1 | grep -o 'distinct=[0-9]* loaded=[0-9]*')"
    draw="$(grep -a '^DRAWPATH' "$log" | tail -1 | grep -o 'tried=[0-9]* drawn=[0-9]*')"
    bad="$(grep -a '^DRAWPATH' "$log" | tail -1 | grep -o 'badfmt=[0-9]*')"
    end="$(tail -1 "$log" | cut -c1-40)"
    printf 'WEBLOG %s | %s | %s | %s %s | end: %s\n' \
        "$build" "$draws" "$shd" "$draw" "$bad" "$end"
}

pass() {
    # The JSON, not the text listing: that date field contains spaces and
    # whitespace-splitting it silently produced nothing at all.
    curl -fsS "$ENDPOINT/api/logs" 2>/dev/null | python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for row in d.get("logs", []):
    if row.get("build") == "selftest":
        continue
    print(row["build"] + "\t" + row["url"])
' | while IFS="$(printf '\t')" read -r build url; do
        [ -n "$url" ] || continue
        name="$(printf '%s' "$url" | sed 's|.*/||; s|\.log\.gz$||')"
        dest="$STORE/$build-$name.log"
        [ -f "$dest" ] && continue
        curl -fsS "$url" -o "$dest.gz" 2>/dev/null || continue
        gunzip -f "$dest.gz" 2>/dev/null || { rm -f "$dest.gz"; continue; }
        summarise "$dest"
    done
}

if [ "${1:-}" = "--once" ]; then pass; exit 0; fi
while :; do pass; sleep "$INTERVAL"; done
