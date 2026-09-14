#!/bin/sh
# Publish a built NRO so consoles can update themselves over wifi.
#
#     publish-build.sh jouster            # or: armory, or both
#
# Uploads straight to Vercel Blob rather than through the API, because the
# serverless function caps a request body at about 4.5MB and Jouster is 176MB.
# Blob takes it directly; the manifest endpoint then reads the store and tells
# the console what the newest build is.
#
# The build stamp in the filename is the same __DATE__ __TIME__ string the
# binary prints on its first log line, read back out of the NRO itself rather
# than passed in -- so what is published cannot disagree with what will run.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENDPOINT="${ARK_ENDPOINT:-https://arkchemy-logs.vercel.app}"
cd "$ROOT/tools/log-endpoint"      # where the Blob token is linked

# `vercel blob` refuses to guess when both an OIDC token and a read-write
# token are present in the environment, and `vercel link` leaves both in
# .env.local. Pass the read-write one explicitly rather than unsetting things
# and hoping.
RW_TOKEN="$(grep -m1 '^BLOB_READ_WRITE_TOKEN=' .env.local | cut -d= -f2- | tr -d '\"')"
[ -n "$RW_TOKEN" ] || { echo "no BLOB_READ_WRITE_TOKEN in .env.local -- run: npx vercel env pull" >&2; exit 1; }

publish_one() {
    app="$1"; nro="$2"
    [ -f "$nro" ] || { echo "no $nro -- build it first" >&2; return 1; }

    stamp="$(strings -n 12 "$nro" | grep -m1 "ARKCHEMY_BUILD " \
             | sed 's/.*ARKCHEMY_BUILD //' | tr ' :' '_-')"
    [ -n "$stamp" ] || { echo "no build stamp in $nro" >&2; return 1; }

    echo "$app: $stamp ($(du -h "$nro" | cut -f1))"
    npx --yes vercel blob put "$nro" --rw-token "$RW_TOKEN" \
        --pathname "builds/$app/$stamp.nro" --access public --allow-overwrite true --add-random-suffix false --multipart true \
        >/dev/null
    echo "$app: published"
}

for app in "${@:-jouster}"; do
    case "$app" in
        jouster) publish_one jouster "$ROOT/game/Jouster.nro" ;;
        armory)  publish_one armory  "$ROOT/../armory/switch/Armory.nro" ;;
        *) echo "unknown app: $app" >&2; exit 1 ;;
    esac
done

echo
echo "manifest now says:"
curl -s "$ENDPOINT/api/manifest" | head -c 600
echo
