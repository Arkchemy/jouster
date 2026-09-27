#!/usr/bin/env python3
"""Let hand-written native code replace recompiled guest functions.

Some guest functions are replaced with native code instead of being
recompiled: game/source/bink_ffmpeg.c's Bink* decode the game's movies with
ffmpeg. Both copies are called ppc_<name>, so without this step the link
fails with "multiple definition of ppc_BinkClose".

Any ppc_* function defined in a non-generated .c file in game/source/ keeps
its name. The recompiled definition of the same name is renamed
<name>__recompiled, and generated_decls.h gets a declaration for it. Only
definitions are renamed, so every call site, and ppc_dispatch, reaches the
native one. The build links with --gc-sections, so the renamed originals
cost nothing unless something calls them (a frame-for-frame diff against
the recompiled decoder would).

regenerate.sh runs this after splitting. It is idempotent, so it can also be
run on an existing set of generated files:

    python3 tools/native-overrides.py [game-dir]
"""
import glob
import os
import re
import sys

game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "game")
source = os.path.join(game, "source")
decls_path = os.path.join(game, "include", "generated_decls.h")

def_re = re.compile(r'^void (ppc_[A-Za-z0-9_]+)\(PpcContext \*ctx\)\s*\{', re.M)

native = {}
for path in sorted(glob.glob(os.path.join(source, "*.c"))):
    if os.path.basename(path).startswith("generated_"):
        continue
    with open(path, "r", errors="replace") as f:
        for name in def_re.findall(f.read()):
            native.setdefault(name, os.path.basename(path))

renamed = []
for path in sorted(glob.glob(os.path.join(source, "generated_*.c"))):
    with open(path, "r") as f:
        text = f.read()
    def repl(m):
        name = m.group(1)
        if name not in native:
            return m.group(0)
        renamed.append(name)
        return m.group(0).replace(name, name + "__recompiled", 1)
    new = def_re.sub(repl, text)
    if new != text:
        with open(path, "w") as f:
            f.write(new)

if os.path.exists(decls_path):
    with open(decls_path, "r") as f:
        decls = f.read()
    missing = [n for n in sorted(set(renamed)) if f"void {n}__recompiled(" not in decls]
    if missing:
        add = "".join(f"void {n}__recompiled(PpcContext *ctx);\n" for n in missing)
        decls = decls[: decls.rindex("#endif")] + add + decls[decls.rindex("#endif"):]
        with open(decls_path, "w") as f:
            f.write(decls)

for name in sorted(set(renamed)):
    print(f"  {name}: native in {native[name]}; recompiled one is now {name}__recompiled")
print(f"native overrides: {len(set(renamed))} recompiled function(s) renamed")
