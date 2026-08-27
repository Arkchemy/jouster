#!/bin/sh
# Builds the Milestone 2 "does it boot" NRO -- see src/start.s for what this
# actually is (and isn't) and why. Output: build/arkchemy_poc.nro
#
# Paths here are relative to this repository's own root (where this script
# lives). They used to be relative to the parent of an old monorepo's
# `switch/` subdirectory, which is where this whole tree lived before the
# org split -- so every path below resolved to nothing once it was published
# as a standalone repo, exactly the same pre-split breakage already fixed in
# game/Makefile and gx2_test/Makefile.
set -e
cd "$(dirname "$0")"

ZIG="${ZIG:-$HOME/devtools/zig/zig}"
SWITCH_TOOLS="${SWITCH_TOOLS:-$HOME/devtools/switch-tools/bin}"
OUT_DIR="build"
mkdir -p "$OUT_DIR"

echo "== Assembling + linking =="
"$ZIG" cc -target aarch64-freestanding-none -nostdlib -static \
    -Wl,-T,link.ld -o "$OUT_DIR/arkchemy_poc.elf" src/start.s

echo "== Sanity-checking program header shape (elf2nro requires exactly =="
echo "== 3 PT_LOAD segments + a 4th for bss sizing) =="
readelf -l "$OUT_DIR/arkchemy_poc.elf" | grep -A1 "^  LOAD" || true

echo "== Building control.nacp =="
"$SWITCH_TOOLS/nacptool" --create "Arkchemy PoC" "Arkchemy project" "0.0.1" "$OUT_DIR/control.nacp"

echo "== Packaging NRO =="
"$SWITCH_TOOLS/elf2nro" "$OUT_DIR/arkchemy_poc.elf" "$OUT_DIR/arkchemy_poc.nro" \
    --nacp="$OUT_DIR/control.nacp"

echo "== Done: $OUT_DIR/arkchemy_poc.nro =="
ls -la "$OUT_DIR/arkchemy_poc.nro"
