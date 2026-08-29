#!/bin/sh
# Resolve conquertron, the recompiler whose headers jouster compiles against.
#
# conquertron supplies ppc_runtime.h, the cafeos_*.h Cafe OS shims and
# cafeos_state.c. The generated C is meaningless without them, so a jouster
# checkout on its own cannot build -- which is exactly what happened after the
# org split, when the hardcoded monorepo path stopped existing and nobody who
# cloned the published repo could build a single file.
#
# Resolution order, deliberately in this order:
#
#   1. $CONQUERTRON set explicitly            -> use it, never touch the network
#   2. a sibling ../conquertron checkout      -> use it, never touch the network
#   3. otherwise                              -> fetch into deps/conquertron
#
# 2 comes before 3 on purpose. conquertron and jouster are developed together;
# nearly every recompiler bug found so far was found from a jouster symptom and
# fixed in conquertron in the same sitting. If a fetched copy silently shadowed
# a local checkout, those edits would compile into nothing and the next
# hardware run would quietly test the wrong code. A developer with a sibling
# checkout gets their own tree, always.
#
# Prints the resolved directory on stdout. All chatter goes to stderr so the
# Makefile can capture the path cleanly.
set -e

here=$(CD=$(dirname "$0") && cd "$CD" && pwd)
root=$(dirname "$here")
lock="$root/conquertron.lock"
vendor="$root/deps/conquertron"
url="${CONQUERTRON_URL:-https://github.com/Arkchemy/conquertron.git}"

usable() { [ -f "$1/include/ppc_runtime.h" ]; }

# 1. explicit override
if [ -n "$CONQUERTRON" ]; then
    if usable "$CONQUERTRON"; then printf '%s\n' "$(cd "$CONQUERTRON" && pwd)"; exit 0; fi
    echo "fetch-conquertron: CONQUERTRON=$CONQUERTRON has no include/ppc_runtime.h" >&2
    exit 1
fi

# 2. sibling checkout wins, so local recompiler work is what actually builds
sibling="$root/../conquertron"
if usable "$sibling"; then printf '%s\n' "$(cd "$sibling" && pwd)"; exit 0; fi

# 3. vendored copy. Already present is the common case: no network, no delay,
#    and the build still works on a plane.
if usable "$vendor"; then printf '%s\n' "$(cd "$vendor" && pwd)"; exit 0; fi

echo "fetch-conquertron: no conquertron found, fetching into deps/conquertron" >&2
command -v git >/dev/null 2>&1 || { echo "fetch-conquertron: git not installed, and no local conquertron to fall back on" >&2; exit 1; }

mkdir -p "$root/deps"
if ! git clone --quiet "$url" "$vendor" >&2; then
    echo "fetch-conquertron: clone of $url failed (offline?). Clone conquertron next to this repo, or set CONQUERTRON=/path/to/conquertron" >&2
    exit 1
fi

# Pin to the recorded commit when there is one, so a build is reproducible
# rather than "whatever main happened to be this morning".
if [ -f "$lock" ]; then
    ref=$(sed -n 's/^commit[[:space:]]*//p' "$lock" | head -1)
    if [ -n "$ref" ]; then
        git -C "$vendor" checkout --quiet "$ref" 2>/dev/null \
            || echo "fetch-conquertron: pinned commit $ref not found, staying on default branch" >&2
    fi
fi

usable "$vendor" || { echo "fetch-conquertron: fetched copy has no include/ppc_runtime.h" >&2; exit 1; }
printf '%s\n' "$(cd "$vendor" && pwd)"
