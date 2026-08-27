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
runs **nine** of Arkchemy's recompiler test programs — not just one —
against real ARM64 Switch hardware, checking each against the exact same
known-correct values blaster's `verify.sh` already checks under QEMU-ARM64:

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

The four below were added 2026-08-27. Each covers something found in real
code rather than invented for a test, and each had only ever run under
QEMU until then:

- `t6_andi_lwzu` (`testdata/andi_lwzu.c`, -O1) — `andi.`/`lwzu`, found
  missing while recompiling a genuine Wii U homebrew `.rpx`.
- `t7_cond_return` (`testdata/cond_return.c`, -O1) — conditional return
  (`blelr` and friends), same origin.
- `t8_addis_frsp` (`testdata/addis_frsp.c`, -O1) — `addis`/`frsp`, same
  origin; the only test here returning a double.
- `t9_bss_large` (`testdata/bss_large.c`) — the real oversized-`.bss`
  address-assignment bug from the actual Skylanders binary, where every
  global past the first 256 bytes silently aliased the next section.

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
./regenerate.sh
```

It expects `blaster` and `conquertron` cloned beside this repo (override
with `BLASTER=` / `CONQUERTRON=`) and a built `conquertron/build/recomp`.
Two things worth knowing before running it:

- The generated files committed before 2026-08-27 came from an older
  recompiler and an older `ppc_runtime.h` (a pinned 4MB copy, ~240 lines
  behind). Regenerating pulls in the current header, whose full-game
  default arena is 1GB — nine of those will not load on a console, so
  `Makefile` overrides `PPC_MEM_SIZE` back to 4MB.
- blaster's `build_ppc.sh` now pins `powerpc-freestanding-eabihf`. Plain
  `eabi` leaves the float ABI to the installed zig's default, and newer
  zig picks soft float, which turns every FP operation into a libgcc
  helper the recompiler emits as an unresolved extern — and removes the
  `frsp` instruction `t8` exists to cover.

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
