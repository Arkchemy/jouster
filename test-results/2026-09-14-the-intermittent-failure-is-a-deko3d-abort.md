# The 826 failure is a deko3d abort, not a stall

Run of 2026-09-14 21:17, build `Sep 14 2026 21:05:04` — the first build made
on Windows, and the first run of it.

## What it is

The process is killed by `svcBreak`, called from deko3d, from inside
`GX2Flush`'s shim, on the graphics submit thread. Atmosphère's crash report
gives the whole chain, symbolised against the ELF of this exact build:

```
Result: 0x367 (2359-0001)     Type: User Break     Break Size: 0x4

svcBreak
dk::detail::RaiseError(DkResult)
dk::detail::ImageLayout::calcLevelOffset(unsigned int) const
ppc_import_gx2_GX2Flush                   cafeos_gx2.h:1342
Gfx::igPlatformVisualContext::submitEndDraw          generated_0171.c:36903
Gfx::igPlatformVisualContext::submitProcessMessage   generated_0171.c:37123
Gfx::igPlatformVisualContext::submitThreadFunction   generated_0171.c:37292
ppc_igCafeThreadFunction__4Core
arkchemy_thread_trampoline                cafeos_coreinit_thread.h:191
__thread_entry / _EntryWrap
```

`cafeos_gx2.h:1342` is the submit itself:

```c
dkQueueSubmitCommands(g_arkchemy_gx2.queue, dkCmdBufFinishList(g_arkchemy_gx2.cmdbuf));
```

`Break Size: 0x4` and the break address landing inside the crashed thread's
own stack region is `diagAbortWithResult`'s signature — it passes the address
of a 4-byte Result. The stack dump confirms the value: at the break address
`0x60066b3c` the bytes are `67 03 00 00`, little-endian `0x367`, matching the
report's own header.

## What this corrects

`2026-09-14-the-same-build-fails-two-runs-in-five.md` says of the failed runs:

> It is **not hung.** The main frame counter is still advancing where the log
> stops ... It is the *engine* that is stuck, not the app.

The first half is right and the conclusion drawn from it is wrong. The app is
not stuck either — **it is killed.** The main thread really is still counting
frames at the point the log ends, because the thread that dies is a different
one: the graphics submit thread. `svcBreak` then takes the whole process down,
which is why the log stops mid-stream with no exit line and why `run-open.txt`
is left unclosed.

That was worth getting wrong slowly. The log alone cannot distinguish "stopped
running" from "was killed", and every run on record was read only through the
log. The crash reports were on the card the entire time — 207 of them.

## It is the recurring mode, not a one-off

The previous crash report, from the 2026-09-14 01:38 build, carries the **same
`Result 0x367`** and a stack trace of the same depth and shape — the offsets
differ only as two builds of the same code differ (`0x9a90468` against
`0x9a91f38`, `0x7e4c7c0` against `0x7e4cca4`).

So the intermittent failure survived a complete change of build environment:
different machine, different OS, different devkitPro install, freshly compiled
from cleared objects. It reproduced on that build's **first run**. Whatever
this is, it is not the toolchain and not a stale artifact.

## What is solid and what is not

**Solid.** deko3d raised a `DkResult` and aborted the process, from the submit
inside `GX2Flush`, on the submit thread. Twice, across two builds.

**Not solid.** The frame attributed to `ImageLayout::calcLevelOffset` should be
treated with suspicion. `GX2Flush` calls `dkCmdBufFinishList` and
`dkQueueSubmitCommands`, neither of which has any reason to reach an image
layout calculation, and deko3d here is a release build where identical code is
merged and cold paths are outlined. The symbol is what that address belongs
to; it need not be what actually ran. **Do not start looking at image layouts
on the strength of that line alone.**

Nor is the timing established. The GX2 census stops at 826 calls while the main
loop reaches frame ~12,400 of 14,400, and those two facts have not been
reconciled: either the submit thread died early and the main loop carried on
for twelve thousand frames — which `svcBreak` should not permit — or the
engine stalled early for an unrelated reason and the submit thread died much
later. Deciding that comes first, because it determines whether the abort is
the cause of the failure or a consequence of it.

## The callback did nothing, because release deko3d cannot call it

Build `Sep 14 2026 21:26:26` installed `cbDebug` correctly, crashed in exactly
the same way, and produced **not one `[DKDEBUG]` line**.

The two deko3d libraries do not expose the same function:

| | symbol |
| --- | --- |
| `libdeko3d.a` (release) | `dk::detail::RaiseError(DkResult)` |
| `libdeko3dd.a` (debug) | `dk::detail::RaiseError(DkResult, char const*)` |

Release has **no message parameter at all**. There is nothing to hand a debug
callback, so it is never called, and `DkDeviceMaker::cbDebug` is dead weight in
a release build. The crash report settles which was linked without any
guessing: its stack trace names the one-argument form.

Release also carries far less validation — 6 call sites reaching `RaiseError`
against debug's 17. Those missing eleven are the checks that would say *which*
object was wrong.

So `game/Makefile` now links `-ldeko3dd` via `DEKO3D_LIB ?= deko3dd`, and the
ELF was checked for the two-argument symbol before the build was pushed rather
than after. `DEKO3D_LIB=deko3d` returns to release once this is understood;
validation is not free.

**This is the same mistake as the `dkCmdBufDraw` bisection, caught earlier.**
An installed callback that never fires looks exactly like a callback that fired
and had nothing to say. One more run would have "confirmed" that deko3d
declines to explain itself.

## A correction to this document

It said 207 crash reports. **147** of those are `.log` reports; the rest are
the `.jpg` screenshots Atmosphère saves alongside each one. 207 was the item
count of the directory, quoted without checking what was in it — a small
version of exactly the error the rest of this file is about.

## Next

1. ~~**Decode the result properly.**~~ Done. `deko3d.h` gives
   `DkResult_Success, DkResult_Fail, DkResult_Timeout, …` in that order, so
   description 1 is `DkResult_Fail` — deko3d's generic failure, which names
   nothing by itself. That is what makes item 3 the whole game.
2. **Reconcile the timing.** Print a frame number and the GX2 call count from
   the submit thread on every flush. One run then says whether the abort
   happens at call 826 or at frame 12,400, and those imply completely different
   bugs.
3. ~~**Install deko3d's debug callback.**~~ Done, and then made to actually
   fire by linking the debug deko3d — see above. Build `Sep 14 2026 21:43:58`.
   `DkDeviceMaker.cbDebug` is set before `dkDeviceCreate`, which is the only
   point it can be — every later call belongs to that device. deko3d hands the
   callback `context` (the entry point) and `message` (what was wrong with
   it), and `RaiseError` calls it before aborting, so the next failure names
   itself.

   It logs through `checkpoint()`, which `fflush`es every line. That detail is
   the whole mechanism: a buffered write here would die with the process and
   leave exactly the unexplained crash report this exists to prevent. Grep a
   log for `[DKDEBUG]`.

   The sink is an `extern` function pointer defined in `cafeos_state.c`, for
   the same reason `g_ppc_unhandled_log` is — a `static` in this header gives
   each of the 200-odd generated translation units its own NULL copy that
   `main.c` never sets, which is a bug this project has already had once.

Until the timing is settled, the ~40% failure rate in the sibling write-up
stands as a measurement, but its description of the failure as a stall does
not.
