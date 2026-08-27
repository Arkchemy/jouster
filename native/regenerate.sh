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

rename_fns() {
    # rename_fns <file> <prefix> <name...> -- the program's own functions.
    # These take the *shared* prefix: for a multi-object test the caller
    # in one file has to keep resolving to the callee in the other.
    rn_file="$1"; rn_prefix="$2"; shift 2
    for rn_name in "$@"; do
        sed -i "s/\bppc_${rn_name}\b/${rn_prefix}_${rn_name}/g" "$rn_file"
    done
}

rename_boilerplate() {
    # rename_boilerplate <file> <prefix> -- the three functions recomp
    # emits into every translation unit. These take a *per-file* prefix,
    # since two files belonging to the same test still each define their
    # own copy. ppc_run_static_initializers in particular is emitted by
    # the current recompiler and not by the one that produced the files
    # committed before 2026-08-27, which is why the collision only
    # appeared once they were regenerated.
    bp_file="$1"; bp_prefix="$2"
    [ -f "$bp_file" ] || { echo "rename_boilerplate: no such file: $bp_file" >&2; exit 1; }
    sed -i "s/\bppc_init_globals\b/${bp_prefix}_init_globals/g" "$bp_file"
    sed -i "s/\bppc_dispatch\b/${bp_prefix}_dispatch/g" "$bp_file"
    sed -i "s/\bppc_run_static_initializers\b/${bp_prefix}_run_static_initializers/g" "$bp_file"
}

gen_test() {
    # gen_test <prefix> <testdata-name> <opt> <fn-names...>
    gt_prefix="$1"; gt_name="$2"; gt_opt="$3"; shift 3
    (cd "$BLASTER" && testdata/build_ppc.sh "testdata/$gt_name.c" "$WORK/${gt_prefix}.o" "$gt_opt" >/dev/null)
    "$RECOMP" "$WORK/${gt_prefix}.o" -o "$OUTDIR/generated_${gt_prefix}.c"
    rename_fns "$OUTDIR/generated_${gt_prefix}.c" "$gt_prefix" "$@"
    rename_boilerplate "$OUTDIR/generated_${gt_prefix}.c" "$gt_prefix"
    echo "wrote $OUTDIR/generated_${gt_prefix}.c"
}

gen_multifile() {
    # gen_multifile <prefix> -- testdata/multifile_{a,b}.c, the one test
    # made of two separate objects: b's compute() calls a's helper().
    # Mirrors verify.sh's own pipeline, --extern-globals on the first
    # object included. Both files share the function prefix so that call
    # keeps resolving, but each gets its own boilerplate prefix.
    gm_prefix="$1"
    (cd "$BLASTER" && testdata/build_ppc.sh testdata/multifile_a.c "$WORK/${gm_prefix}_a.o" -O0 >/dev/null)
    (cd "$BLASTER" && testdata/build_ppc.sh testdata/multifile_b.c "$WORK/${gm_prefix}_b.o" -O0 >/dev/null)
    "$RECOMP" --extern-globals "$WORK/${gm_prefix}_a.o" -o "$OUTDIR/generated_${gm_prefix}_a.c"
    "$RECOMP" "$WORK/${gm_prefix}_b.o" -o "$OUTDIR/generated_${gm_prefix}_b.c"
    rename_fns "$OUTDIR/generated_${gm_prefix}_a.c" "$gm_prefix" helper compute
    rename_fns "$OUTDIR/generated_${gm_prefix}_b.c" "$gm_prefix" helper compute
    rename_boilerplate "$OUTDIR/generated_${gm_prefix}_a.c" "${gm_prefix}_a"
    rename_boilerplate "$OUTDIR/generated_${gm_prefix}_b.c" "${gm_prefix}_b"
    echo "wrote $OUTDIR/generated_${gm_prefix}_{a,b}.c"
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

# Everything else blaster's verify.sh covers under QEMU. Added
# 2026-08-27 after noticing the on-hardware suite ran nine of the
# twenty-three programs verify.sh runs -- the console was checking a
# minority of what the emulator checks. Optimisation levels match
# verify.sh's own pipelines.
gen_test t10_bitops            bitops            -O0 compute
gen_test t11_rotate            rotate            -O1 compute
gen_test t12_carry             carry             -O0 compute
gen_test t13_division          division          -O0 compute
gen_test t14_indexed           indexed           -O0 compute
gen_test t15_fcmp              fcmp              -O0 compute
gen_test t16_mulhw             mulhw             -O1 compute
gen_test t17_double            double            -O0 compute
gen_test t18_misc_bitops       misc_bitops       -O0 compute
gen_test t19_mixed_double      mixed_double      -O0 compute
gen_test t20_globals           globals           -O0 compute
gen_test t21_multifunc_globals multifunc_globals -O0 bump read_shared compute
gen_test t22_manyargs          manyargs          -O0 sum9 compute
gen_multifile t23_multifile

cp "$CONQUERTRON/include/ppc_runtime.h" "$NATIVE_ROOT/include/"
echo "done"
