# Buffering isolated, two causes confirmed

Build `Sep 17 2026 21:09:22` -- frame-bounded loop, no yield. Same binary,
hash `79d8ac6c`, run three times with one text file changed between them.

    flush=256  frames=14400 draws=0   modules=2
    flush=1    frames=14400 draws=284 modules=7
    flush=1    frames=14400 draws=345 modules=7

**Buffering the log stops the game booting.** One variable, identical binaries,
reproduced twice on the good side.

And the second cause is confirmed by the same data: build `20:39:33` was truly
unbuffered and still broken, and its only other differences were the wall-clock
run bound and the 8ms yield. So there are two, independently sufficient.

## Why this took nine builds

Every test between 18:15 and 21:09 changed two things, because the reference
build was nine builds back and everything since carried at least one of the two
causes. "Exonerated" was written three times for things that were not:

  * the flush-cadence test left `setvbuf` on
  * the unbuffered test left the harness changes in
  * the BSS test left the harness changes in

The fix was to stop reasoning forward and lay the runs out as a table. Two rows
then disagreed in a way only two causes could explain, and the deciding run
needed no rebuild at all.

## Restored

Build `Sep 17 2026 22:45:26`. The default interval is `1` -- unbuffered, flushed
per line -- and the harness stays frame-bounded with no yield. That is the
configuration that boots, and it is now the one a card with no config file gets.

The 60% saving is given up for it, deliberately. A correct run is worth more
than a fast broken one, and the card override keeps the experiment repeatable.

## The open question, which is a real one

Why does a buffered stdio stream stop a recompiled PowerPC game from booting?

Nothing about `checkpoint()` should reach the guest. The most promising lead is
that the guest thread calls `checkpoint()` too, so the per-line `fflush` is a
blocking syscall on *that* thread, not only the harness's -- and an 8ms sleep in
the main loop measurably did not substitute for it (2,514,226 guest calls
against 2,510,759 without).

That points at the recompiled code depending on being descheduled somewhere,
which would be a real bug in the runtime rather than in the logging. Worth
chasing properly, and worth not chasing by guesswork.
