# Bramble's first full-game build

The real, complete recompiled Skylanders: Spyro's Adventure -- not a
test program, the actual game -- built into one real Switch `.nro` for
the first time.

## What this is (and isn't) yet

Calls the game's real, complete entry point
(`ppc_bramble_game_entry`, see `recomp`'s own `--entry-alias`) on a
real background thread, while `main.c`'s own main thread shows a real,
independent progress indicator (pulsing screen color + periodic
SD-card log checkpoints) -- deliberately *not* relying on the
recompiled game's own draw calls, since those are still real, honest
no-ops (shader translation doesn't exist yet -- see
`cafeos_gx2.h`'s "Real shader/draw-call pipeline" comment). This is a
smoke test: does the actual, complete game entry point run at all,
without crashing/hanging, now that every real GX2 import has a shim
and the 10 remaining genuinely-unhandled real instructions (see git
history) are honestly stubbed rather than blocking the build.

Real logs land on the SD card at `sdmc:/switch/Bramble/game-results.log`
(periodic checkpoints, `ppc_unhandled_stub` hits) and
`sdmc:/switch/Bramble/game-exception-dump.log` (only written on a real,
unhandled hardware exception -- full register dump + which frame the
main thread was on).

## Building

Needs the real, legally-dumped `tfbGame_cafe.rpx` this project
targets -- not included, not distributable (see the repo's own
`LICENSE`). Regenerate the real, machine-generated game source first:

```sh
./regenerate.sh /path/to/tfbGame_cafe.rpx
```

This produces `source/generated_*.c` (~213 files, split from one real
`recomp` run to keep each individual `gcc` invocation's own compile-time
memory use bounded -- a single 308MB/8.5M-line translation unit
exhausted a deliberate 5.5GB safety cap in real testing) and
`include/generated_decls.h`. Both are deliberately gitignored --
they're derived game output, not this project's own source.

Then build normally:

```sh
export DEVKITPRO=/opt/devkitpro   # or wherever your devkitPro install lives
make
```

## Why the shim headers gained a `cafeos_state.c`

Every `cafeos_*.h` shim header's own persistent state (`g_bramble_gx2`
and similar) used to be `static` -- correct for a header-only library
included by exactly one translation unit (which is all
`switch/native/` and `switch/gx2_test/` ever needed), but wrong once
the actual game's size forced a many-file split: each file would have
gotten its own private, unsynchronized copy of that state. Those
globals are now real `extern` declarations in their own headers, with
the one, real, shared definition of each living in
`recomp/include/cafeos_state.c` -- compiled and linked into every
project that uses these headers now, including `switch/native/` and
`switch/gx2_test/`, even though they only ever needed a single
translation unit themselves.
