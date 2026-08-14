# Milestone 2, real version: recompiled code running as actual libnx homebrew

Unlike `switch/src/start.s` (the hand-rolled, libnx-free "does anything
boot at all" test built entirely from this project's dev sandbox, which
can't reach devkitPro), this is a genuine devkitA64/libnx project. It needs
a real devkitPro install (`sudo pacman -S switch-dev` on Fedora, or the
platform-appropriate equivalent) — see the main project README and the
Notion project log for how to get that installed if `DEVKITPRO` isn't
already set.

## What it does

`source/main.c` is a standard libnx console app (via `consoleInit`) that
calls `ppc_compute()` — the actual output of running Bramble's
recompiler (`recomp/`) against `testdata/arithmetic.c` — and prints the
result on screen, comparing it against the known-correct value (**260**,
per `tools/verify.sh`, which checks the same generated code on the host and
under QEMU-ARM64).

This is the real end-to-end validation the project needs: not just "did
something boot," but "does recompiled PowerPC code, running as actual
Switch homebrew, produce the correct result." If the screen shows `MATCH`,
that's the first real proof the whole pipeline (disassemble → recover
functions → emit C → compile for ARM64 → run on real Switch hardware)
works, closing out the plan's "Recommended First Step."

`source/generated.c` and `include/ppc_runtime.h` are copied in from
`recomp`'s output, not hand-written — see the top-level `README.md` for how
they're produced. If `testdata/arithmetic.c` or `recomp` changes, regenerate
with:

```sh
testdata/build_ppc.sh /tmp/arithmetic_ppc.o
recomp/build/recomp /tmp/arithmetic_ppc.o -o switch/native/source/generated.c
cp recomp/include/ppc_runtime.h switch/native/include/
```

## Building

```sh
export DEVKITPRO=/opt/devkitpro   # if not already set by the installer
make
```

Output: `Bramble.nro`. Copy it to `/switch/Bramble/` on your SD
card (alongside, or replacing, `switch/build/bramble_poc.nro` from the
libnx-free attempt). Drop what happens in `switch/test-results/`.
