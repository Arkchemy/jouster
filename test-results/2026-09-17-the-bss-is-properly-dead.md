# The BSS is properly dead

Build `Sep 17 2026 20:56:50`, interval `256`.

    log flush interval: 256 line(s), stream buffered 32KB on the heap
    RUNRATE hostframes=9300 elapsed=239463ms hostfps=38
    guest calls 2,546,141   (10,609/sec)
    tally  flush=256 frames=9319 draws=0 modules=2

The buffer is on the heap, one pointer in BSS instead of 32KB, and the boot is
still broken. This time the measurement addressed the hypothesis directly, so
the BSS is dead for real.

Guest rate across every run since the break, regardless of what logging did:

    10,479 /sec   buffered, 32KB BSS
    10,476 /sec   buffered, 32KB BSS, 8ms yield
    10,226 /sec   unbuffered, flush per line
    10,609 /sec   buffered, heap
    ---
    16,146 /sec   last good build

A consistent ~35% deficit that has survived four different logging
configurations. (Worth a caveat: once the boot takes a different path, calls
per second is not a clean speed measure, because the call density of the code
it is looping in is different. It is a symptom, not a metric.)

## What is left

The regression is in `main.c` and `self_update.c` between `bf8f23b` and HEAD,
and within that:

  * flush cadence -- ruled out by a run that paid the cost back
  * buffered stream -- ruled out by a run with `setvbuf` skipped
  * the BSS array -- ruled out by this run
  * `checkpoint_flush` -- only fires from the deko3d error sink, and no run
    since has logged a `[DKDEBUG]` line

Leaving the two harness changes -- the wall-clock run bound and the 8ms yield
-- and the merged PR's config read and tally signature.

## The bisect

Build `Sep 17 2026 21:09:22` removes **both harness changes at once**: the loop
is frame-bounded again, exactly as the last good build had it, and the yield is
commented out. Everything else stays at HEAD.

That halves the remaining space in one run:

  * boot comes back -- the cause is the wall-clock bound or the yield, and two
    more runs separate them.
  * boot stays broken -- the cause is in the PR's config read or tally change,
    which is a small and very readable diff.

The yield costs nothing to remove: it was measured doing nothing for the guest,
2,514,226 calls against 2,510,759 without it.

## Note on the earlier reasoning

"Both postdate the first bad build, so neither can be the original cause" is
sound as far as it goes, and it is why these two were left until last. But it
assumes a single cause throughout, and the 256KB build and the current one have
had several intervening changes. If the harness turns out to be responsible,
that assumption was carrying more weight than it could bear.
