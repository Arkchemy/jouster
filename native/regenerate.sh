#!/bin/sh
# Regenerates switch/native/source/generated_t*.c from testdata/*.c via the
# recomp tool, for the on-hardware test suite (source/main.c). Re-run this
# whenever recomp or the underlying testdata/*.c files change.
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
cd "$(dirname "$0")/../.."

RECOMP="${RECOMP:-recomp/build/recomp}"
OUTDIR=switch/native/source
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
}

gen_test() {
    # gen_test <prefix> <src.c> <opt> <fn-names...>
    prefix="$1"; src="$2"; opt="$3"; shift 3
    testdata/build_ppc.sh "$src" "$WORK/${prefix}.o" "$opt" >/dev/null
    "$RECOMP" "$WORK/${prefix}.o" -o "$OUTDIR/generated_${prefix}.c"
    rename_syms "$OUTDIR/generated_${prefix}.c" "$prefix" "$@"
    echo "wrote $OUTDIR/generated_${prefix}.c"
}

gen_test t1_arithmetic testdata/arithmetic.c   -O0 add compute
gen_test t2_floating   testdata/floating.c     -O0 compute
gen_test t3_loop       testdata/loop_counted.c -O2 sumn
gen_test t4_rodata     testdata/rodata_table.c -O2 classify
gen_test t5_fnptr      testdata/fnptr.c        -O0 add mul compute

cp recomp/include/ppc_runtime.h switch/native/include/
echo "done"
