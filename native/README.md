# Milestone 2, real version: recompiled code running as actual libnx homebrew

Unlike `switch/src/start.s` (the hand-rolled, libnx-free "does anything
boot at all" test), this is a genuine devkitA64/libnx project. It needs a
real devkitPro install — `sudo pacman -S switch-dev` on Fedora, or the
platform-appropriate equivalent — but since it depends on nothing beyond
`libnx` itself (no portlibs), it builds fine directly in this project's
dev sandbox too, now that the `devkitA64`+`libnx` toolchain already
installed there is usable (devkitpro.org's package mirror, needed for
*additional* libraries, is still blocked from the sandbox — that only
affects projects, like Viridite, that need portlibs).

## What it does

`source/main.c` is a standard libnx console app (via `consoleInit`) that
runs **five** of Arkchemy's recompiler test programs — not just one —
against real ARM64 Switch hardware, checking each against the exact same
known-correct values `tools/verify.sh` already checks under QEMU-ARM64:

- `t1_arithmetic` (`testdata/arithmetic.c`) — integer arithmetic/calls.
- `t2_floating` (`testdata/floating.c`) — single-precision FP + rodata
  constant resolution.
- `t3_loop` (`testdata/loop_counted.c`) — `mtctr`/`bdnz` counted-loop
  branches.
- `t4_rodata` (`testdata/rodata_table.c`) — a compiler-generated
  switch-statement lookup table, indexed at runtime: the real read-only-
  data addressing bug this project found and fixed, run here on real
  hardware rather than just QEMU.
- `t5_fnptr` (`testdata/fnptr.c`) — `mtctr`/`bctrl` indirect calls through
  a function pointer.

QEMU is a good proxy for "does the recompiled code compute the right
answer," but it's still an emulator — this is the actual target hardware,
running actual recompiler output, across several distinct instruction
categories, not just one integer-arithmetic program.

**Logging, not just an on-screen result.** Per the project's own Notion
plan ("Tooling & Development Approach" — self-instrumented automated
testing), the app writes a checkpointed log to
`sdmc:/switch/Jouster/test-results.log` as it runs, flushed after every
line — not just a summary at exit. If the app freezes or crashes partway
through a future, more ambitious test, the last-written checkpoint in that
log narrows down where, without needing a human to guess blind. The same
checkpoints also print to the console in real time.

## Regenerating the test programs

`source/generated_t*.c` and `include/ppc_runtime.h` are copied/generated
from `recomp`'s output, not hand-written. Each test's generated C defines
functions under the same names every time (`compute`, `init_globals`,
`dispatch`, ...), which collide once more than one test is linked into the
same binary — `regenerate.sh` handles this by renaming every generated
symbol to a per-test prefix (`t1_arithmetic_compute`, `t2_floating_compute`,
etc.), leaving the shared runtime helpers in `ppc_runtime.h`
(`ppc_load_u32` and friends) untouched. Re-run it whenever `recomp` or the
underlying `testdata/*.c` files change:

```sh
switch/native/regenerate.sh
```

## Building

```sh
export DEVKITPRO=/opt/devkitpro   # if not already set by the installer
make
```

Output: `Arkchemy.nro`. Copy it to `/switch/Jouster/` on your SD
card (alongside, or replacing, `switch/build/arkchemy_poc.nro` from the
libnx-free attempt). Drop what happens in `switch/test-results/`, and grab
`sdmc:/switch/Jouster/test-results.log` off the SD card for the full
checkpoint trail.
