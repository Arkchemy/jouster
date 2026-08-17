#!/bin/sh
# Regenerates switch/game/source/generated_*.c and
# switch/game/include/generated_decls.h from a real, legally-dumped
# tfbGame_cafe.rpx via the recomp tool -- these are real, machine-
# generated game output (not this project's own source), deliberately
# excluded from git (see .gitignore and LICENSE's own "no
# redistribution of generated game output" clause), so this script is
# how you actually produce them locally from your own legal dump.
#
# Splits the ~8.5M-line/308MB single-file recomp output into many
# smaller .c files (~40,000 lines each, ~213 files for the actual real
# Skylanders: Spyro's Adventure binary) purely to keep each individual
# `gcc` invocation's own compile-time memory use bounded -- a real,
# single-file compile of the whole thing exhausted a deliberate 5.5GB
# safety cap in real testing. `ppc_init_globals` (the one real function
# that initializes every byte of the game's static data, alone millions
# of lines by itself) is further split into many smaller
# `ppc_init_globals_NNNN` sub-functions plus a thin wrapper, for the
# same real reason.
#
# Usage: ./regenerate.sh <path-to-tfbGame_cafe.rpx>
set -e
cd "$(dirname "$0")/../.."

RECOMP="${RECOMP:-recomp/build/recomp}"
RPX="$1"
if [ -z "$RPX" ]; then
    echo "usage: $0 <path-to-tfbGame_cafe.rpx>" >&2
    exit 1
fi

OUT_SOURCE="switch/game/source"
OUT_INCLUDE="switch/game/include"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

rm -f "$OUT_SOURCE"/generated_*.c "$OUT_INCLUDE"/generated_decls.h

echo "running recomp against $RPX (this takes a while, ~8.5M lines of output)..."
# recomp exits 2 (not 0) when it hits real, known-unhandled instructions --
# that's the expected, normal case for this specific real game binary (see
# ppc_unhandled_stub's own comment in ppc_runtime.h), not a real failure:
# it still writes the complete file before returning that code, and the
# very next step below patches exactly those sites. Only a genuinely
# different, unexpected exit code should actually stop this script.
set +e
"$RECOMP" --entry-alias bramble_game_entry "$RPX" -o "$WORK/full.c"
recomp_status=$?
set -e
if [ "$recomp_status" -ne 0 ] && [ "$recomp_status" -ne 2 ]; then
    echo "recomp failed with unexpected exit code $recomp_status" >&2
    exit "$recomp_status"
fi

echo "patching remaining real #error sites into honest, logged ppc_unhandled_stub calls..."
python3 - "$WORK/full.c" << 'PYEOF'
import re, sys
path = sys.argv[1]
with open(path, "r") as f:
    content = f.read()
pattern = re.compile(r'#error "([^"]*)"')
count = [0]
def repl(m):
    count[0] += 1
    msg = m.group(1).replace('"', '\\"')
    return f'ppc_unhandled_stub(ctx, "{msg}");'
content = pattern.sub(repl, content)
with open(path, "w") as f:
    f.write(content)
print(f"  replaced {count[0]} error site(s)")
PYEOF

echo "splitting into chunk files..."
python3 - "$WORK/full.c" "$OUT_SOURCE" "$OUT_INCLUDE/generated_decls.h" << 'PYEOF'
import re, sys

src_path, out_dir, decl_path = sys.argv[1], sys.argv[2], sys.argv[3]

with open(src_path, "r") as f:
    lines = f.readlines()

func_def_re = re.compile(r'^void (ppc_[A-Za-z0-9_]+)\(PpcContext \*ctx\) \{$')
dispatch_def_re = re.compile(r'^void (ppc_dispatch)\(PpcContext \*ctx, uint32_t addr\) \{$')
first_def_idx = None
for i, line in enumerate(lines):
    if func_def_re.match(line) or dispatch_def_re.match(line):
        first_def_idx = i
        break

decls = lines[:first_def_idx]
body_lines = lines[first_def_idx:]

TARGET_LINES = 40000
INIT_GLOBALS_SLICE = 5000

chunk_idx = 0
chunk_lines = []
chunk_line_count = 0
extra_decls = []

SHIM_INCLUDES = '''#include "cafeos_coreinit.h"
#include "cafeos_coreinit_atomic.h"
#include "cafeos_coreinit_dynload.h"
#include "cafeos_coreinit_fs.h"
#include "cafeos_coreinit_im.h"
#include "cafeos_coreinit_libc.h"
#include "cafeos_coreinit_log.h"
#include "cafeos_coreinit_mcp.h"
#include "cafeos_coreinit_mem.h"
#include "cafeos_coreinit_misc.h"
#include "cafeos_coreinit_sync.h"
#include "cafeos_coreinit_thread.h"
#include "cafeos_ghs_runtime.h"
#include "cafeos_gx2.h"
#include "cafeos_nn_ac.h"
#include "cafeos_nn_act.h"
#include "cafeos_nn_save.h"
#include "cafeos_nsyshid.h"
#include "cafeos_nsysnet.h"
#include "cafeos_padscore.h"
#include "cafeos_proc_ui.h"
#include "cafeos_snd_core.h"
#include "cafeos_snd_user.h"
#include "cafeos_sysapp.h"
#include "cafeos_vpad.h"
'''

def flush_chunk():
    global chunk_idx, chunk_lines, chunk_line_count
    if not chunk_lines:
        return
    path = f"{out_dir}/generated_{chunk_idx:04d}.c"
    with open(path, "w") as f:
        f.write('#include "ppc_runtime.h"\n')
        f.write(SHIM_INCLUDES)
        f.write('#include "generated_decls.h"\n\n')
        f.writelines(chunk_lines)
    chunk_idx += 1
    chunk_lines = []
    chunk_line_count = 0

def add_lines(ls):
    global chunk_line_count
    chunk_lines.extend(ls)
    chunk_line_count += len(ls)
    if chunk_line_count >= TARGET_LINES:
        flush_chunk()

i = 0
n = len(body_lines)
func_count = 0

while i < n:
    line = body_lines[i]
    m = func_def_re.match(line)
    md = dispatch_def_re.match(line)
    if not m and not md:
        i += 1
        continue
    fname = "ppc_dispatch" if md else m.group(1)
    func_lines = [line]
    depth = line.count('{') - line.count('}')
    i += 1
    while i < n and depth > 0:
        l = body_lines[i]
        func_lines.append(l)
        depth += l.count('{') - l.count('}')
        i += 1
    func_count += 1

    if fname == "ppc_init_globals":
        stmt_lines = func_lines[1:-1]
        stmts = [l for l in stmt_lines if l.strip()]
        sub_idx = 0
        wrapper_calls = []
        for start in range(0, len(stmts), INIT_GLOBALS_SLICE):
            slice_stmts = stmts[start:start + INIT_GLOBALS_SLICE]
            sub_name = f"ppc_init_globals_{sub_idx:04d}"
            sub_lines = [f"void {sub_name}(PpcContext *ctx) {{\n"] + slice_stmts + ["}\n"]
            add_lines(sub_lines)
            extra_decls.append(f"void {sub_name}(PpcContext *ctx);\n")
            wrapper_calls.append(f"  {sub_name}(ctx);\n")
            sub_idx += 1
        wrapper = ["void ppc_init_globals(PpcContext *ctx) {\n"] + wrapper_calls + ["}\n"]
        add_lines(wrapper)
        continue

    add_lines(func_lines)

flush_chunk()

with open(decl_path, "w") as f:
    f.write("#ifndef BRAMBLE_GAME_GENERATED_DECLS_H\n#define BRAMBLE_GAME_GENERATED_DECLS_H\n")
    f.writelines(decls)
    f.writelines(extra_decls)
    f.write("#endif\n")

print(f"functions: {func_count}, chunks: {chunk_idx}, init_globals sub-funcs: {len(extra_decls)}")
PYEOF

echo "done."
