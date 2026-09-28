#!/bin/sh
# Publish a built NRO as a GitHub release asset, and update the plain-text
# manifest the console reads.
#
#     publish-github.sh jouster [armory]
#
# Everything lives on GitHub: the asset on a release, the manifest as a file
# in the repo served through raw.githubusercontent.com. No server to keep
# running, no egress bill, and -- the part that matters on the console side --
# no credentials on the SD card, because both URLs are public reads.
#
# The manifest stays three "key=value" lines rather than becoming the releases
# API's JSON. The console parses it with strstr; handing it JSON would mean
# hand-rolling a parser on a device where a mis-parse means downloading the
# wrong 170MB, and the releases API is not a stable enough shape to justify
# that.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${ARK_REPO:-Arkchemy/jouster}"
BRANCH="${ARK_BRANCH:-main}"
TOKEN="$(sed -n 's|https://[^:]*:\([^@]*\)@github.com|\1|p' "$HOME/.git-credentials" | head -1)"
[ -n "$TOKEN" ] || { echo "no GitHub token in ~/.git-credentials" >&2; exit 1; }

api() { curl -sS -H "Authorization: Bearer $TOKEN" \
                 -H "Accept: application/vnd.github+json" "$@"; }

publish_one() {
    app="$1"; nro="$2"
    [ -f "$nro" ] || { echo "no $nro -- build it first" >&2; return 1; }

    stamp="$(strings -n 12 "$nro" | grep -m1 "ARKCHEMY_BUILD " \
             | sed 's/.*ARKCHEMY_BUILD //' | tr ' :' '_-')"
    [ -n "$stamp" ] || { echo "no build stamp in $nro" >&2; return 1; }
    tag="$app-$stamp"
    size="$(wc -c < "$nro")"
    echo "$app: $stamp ($(du -h "$nro" | cut -f1))"

    # A release per build. Already exists? Reuse it, so re-publishing the same
    # binary is not an error.
    rel="$(api "https://api.github.com/repos/$REPO/releases/tags/$tag" \
           | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get("id",""))')"
    if [ -z "$rel" ]; then
        rel="$(api -X POST "https://api.github.com/repos/$REPO/releases" \
               -d "{\"tag_name\":\"$tag\",\"name\":\"$tag\",\"body\":\"Automated build $stamp\",\"prerelease\":true}" \
               | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get("id",""))')"
    fi
    [ -n "$rel" ] || { echo "$app: could not create a release" >&2; return 1; }

    # Replace the asset if this tag already has one.
    old="$(api "https://api.github.com/repos/$REPO/releases/$rel/assets" \
           | python3 -c 'import sys,json;print("\n".join(str(a["id"]) for a in json.load(sys.stdin)))')"
    for id in $old; do api -X DELETE "https://api.github.com/repos/$REPO/releases/assets/$id" >/dev/null; done

    url="$(curl -sS -H "Authorization: Bearer $TOKEN" \
           -H "Content-Type: application/octet-stream" \
           --data-binary @"$nro" \
           "https://uploads.github.com/repos/$REPO/releases/$rel/assets?name=$app.nro" \
           | python3 -c 'import sys,json;print(json.load(sys.stdin).get("browser_download_url",""))')"
    [ -n "$url" ] || { echo "$app: asset upload failed" >&2; return 1; }

    # The manifest, committed to the repo so raw.githubusercontent serves it.
    body="$(printf 'build=%s\nsize=%s\nurl=%s\n' "$stamp" "$size" "$url" | base64 -w0)"
    path="builds/$app.txt"
    sha="$(api "https://api.github.com/repos/$REPO/contents/$path?ref=$BRANCH" \
           | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get("sha","") if isinstance(d,dict) else "")')"
    payload="{\"message\":\"$app: $stamp\",\"content\":\"$body\",\"branch\":\"$BRANCH\""
    [ -n "$sha" ] && payload="$payload,\"sha\":\"$sha\""
    payload="$payload}"
    api -X PUT "https://api.github.com/repos/$REPO/contents/$path" -d "$payload" >/dev/null

    echo "$app: published -> $url"
    echo "$app: manifest -> https://raw.githubusercontent.com/$REPO/$BRANCH/$path"
}

for app in "${@:-jouster}"; do
    case "$app" in
        jouster) publish_one jouster "$ROOT/game/Jouster.nro" ;;
        armory)  publish_one armory  "$ROOT/../armory/switch/Armory.nro" ;;
        *) echo "unknown app: $app" >&2; exit 1 ;;
    esac
done
