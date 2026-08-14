#!/bin/sh
# Builds the Milestone 2 "does it boot" NRO -- see switch/src/start.s for
# what this actually is (and isn't) and why. Output: switch/build/bramble_poc.nro
set -e
cd "$(dirname "$0")/.."

ZIG="${ZIG:-$HOME/devtools/zig/zig}"
SWITCH_TOOLS="${SWITCH_TOOLS:-$HOME/devtools/switch-tools/bin}"
OUT_DIR="switch/build"
mkdir -p "$OUT_DIR"

echo "== Assembling + linking =="
"$ZIG" cc -target aarch64-freestanding-none -nostdlib -static \
    -Wl,-T,switch/link.ld -o "$OUT_DIR/bramble_poc.elf" switch/src/start.s

echo "== Sanity-checking program header shape (elf2nro requires exactly =="
echo "== 3 PT_LOAD segments + a 4th for bss sizing) =="
readelf -l "$OUT_DIR/bramble_poc.elf" | grep -A1 "^  LOAD" || true

echo "== Building control.nacp =="
"$SWITCH_TOOLS/nacptool" --create "Bramble PoC" "Bramble project" "0.0.1" "$OUT_DIR/control.nacp"

echo "== Packaging NRO =="
"$SWITCH_TOOLS/elf2nro" "$OUT_DIR/bramble_poc.elf" "$OUT_DIR/bramble_poc.nro" \
    --nacp="$OUT_DIR/control.nacp"

echo "== Done: $OUT_DIR/bramble_poc.nro =="
ls -la "$OUT_DIR/bramble_poc.nro"
