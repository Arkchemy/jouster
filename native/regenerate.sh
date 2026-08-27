#!/bin/sh
# Regenerates native/source/generated_t*.c from blaster's testdata/*.c via
# the recomp tool, for the on-hardware test suite (source/main.c). Re-run
# this whenever conquertron or the underlying testdata/*.c files change.
#
# Real issue found 2026-08-27: this script carried the same pre-split path
# bug already fixed in game/Makefile, gx2_test/Makefile, build.sh and
# blaster's verify.sh -- it cd'd two levels above itself and then
# referenced recomp/, switch/native/ and testdata/, which only resolve in
# the old monorepo. After the org split none of them existed, so the
# script could not run at all from a fresh clone. It now stays in this
# repo and locates its two siblings by variable:
#
#     some-dir/
#       blaster/        <- testdata/*.c and testdata/build_ppc.sh
#       conquertron/    <- the recompiler and ppc_runtime.h
#       jouster/        <- you are here
#
# Override either if your checkout differs:
#     CONQUERTRON=/path/to/conquertron BLASTER=/path/to/blaster ./regenerate.sh
#
# Each test program's generated C defines functions under the SAME names
# every time (main.cpp always emits e.g. ppc_compute, ppc_init_globals,
# ppc_dispatch) -- fine when verify.sh builds one test at a time, but this
# app links several tests into one binary, so those names collide across
# files. Renaming every generated ppc_<name> symbol to a per-test t<N>_
# prefix (via sed, targeting only the exact names recomp is known to emit
# for that test -- never a blind "ppc_" replace, which would also mangle
# calls to the shared runtime helpers in ppc_runtime.h, e.g. ppc_load_u32)
# keeps each test's functions unique while leaving the runtime alone.
set -e
cd "$(dirname "$0")"
NATIVE_ROOT="$PWD"

CONQUERTRON="${CONQUERTRON:-$NATIVE_ROOT/../../conquertron}"
BLASTER="${BLASTER:-$NATIVE_ROOT/../../blaster}"
RECOMP="${RECOMP:-$CONQUERTRON/build/recomp}"

if [ ! -x "$RECOMP" ]; then
    echo "error: recomp not found/executable at $RECOMP" >&2
    echo "       build it first: cmake -S \"$CONQUERTRON\" -B \"$CONQUERTRON/build\" && cmake --build \"$CONQUERTRON/build\"" >&2
    exit 1
fi
if [ ! -x "$BLASTER/testdata/build_ppc.sh" ]; then
    echo "error: blaster's testdata not found at $BLASTER/testdata" >&2
    echo "       clone https://github.com/Arkchemy/blaster next to this repo," >&2
    echo "       or run: BLASTER=/path/to/blaster $0" >&2
    exit 1
fi

OUTDIR="$NATIVE_ROOT/source"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

rename_syms() {
    # rename_syms <file> <prefix> <name...>
    file="$1"; prefix="$2"; shift 2
    for name in "$@"; do
        sed -i "s/\bppc_${name}\b/${prefix}_${name}/g" "$file"
    done
    sed -i "s/\bppc_init_globals\b/${prefix}_init_globals/g" "$file"
    sed -i "s/\bppc_dispatch\b/${prefix}_dispatch/g" "$file"
    # Emitted into every translation unit by the current recompiler, so
    # it collides across files exactly like the three above. The
    # generated files committed before 2026-08-27 predate it, which is
    # why this only started failing to link once they were regenerated.
    sed -i "s/\bppc_run_static_initializers\b/${prefix}_run_static_initializers/g" "$file"
}

gen_test() {
    # gen_test <prefix> <testdata-name> <opt> <fn-names...>
    prefix="$1"; name="$2"; opt="$3"; shift 3
    (cd "$BLASTER" && testdata/build_ppc.sh "testdata/$name.c" "$WORK/${prefix}.o" "$opt" >/dev/null)
    "$RECOMP" "$WORK/${prefix}.o" -o "$OUTDIR/generated_${prefix}.c"
    rename_syms "$OUTDIR/generated_${prefix}.c" "$prefix" "$@"
    echo "wrote $OUTDIR/generated_${prefix}.c"
}

gen_test t1_arithmetic arithmetic   -O0 add compute
gen_test t2_floating   floating     -O0 compute
gen_test t3_loop       loop_counted -O2 sumn
gen_test t4_rodata     rodata_table -O2 classify
gen_test t5_fnptr      fnptr        -O0 add mul compute

# The four below cover instructions and a memory-layout bug found in real
# code rather than invented for a test: three came out of recompiling a
# genuine Wii U homebrew .rpx (vgmoose/wiiu-space) and one out of the real
# Skylanders binary's oversized .bss. blaster's verify.sh has run all four
# under QEMU since 2026-08-27; until now none had ever run on the actual
# console. Optimisation levels match verify.sh's own run_pipeline calls --
# at -O0 the instructions three of these exist to cover do not appear at
# all.
gen_test t6_andi_lwzu   andi_lwzu   -O1 compute
gen_test t7_cond_return cond_return -O1 guarded
gen_test t8_addis_frsp  addis_frsp  -O1 compute
gen_test t9_bss_large   bss_large   -O0 fill_and_check

cp "$CONQUERTRON/include/ppc_runtime.h" "$NATIVE_ROOT/include/"
echo "done"
