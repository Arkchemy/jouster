#!/bin/sh
# Re-applies probe patches to freshly generated C. Run after regenerate.sh.
set -e
cd "$(dirname "$0")"
SRC="../source"
applied=0
for p in *.patch; do
    [ -e "$p" ] || continue
    if patch -s -p0 -N -d "$SRC" < "$p" 2>/dev/null; then
        echo "applied $p"; applied=$((applied+1))
    elif patch -s -p0 -R --dry-run -d "$SRC" < "$p" >/dev/null 2>&1; then
        echo "already applied $p"
        # The failed forward attempt above leaves .rej/.orig behind even
        # though nothing was wrong; clear them so they cannot end up
        # committed or mistaken for a real conflict.
        rm -f "$SRC"/*.rej "$SRC"/*.orig
    else
        echo "FAILED to apply $p -- the generated code moved; re-make the patch" >&2
        exit 1
    fi
done
echo "$applied patch(es) applied"
