# It is the code, and the only candidate left is the BSS

Build `Sep 17 2026 20:46:40` -- `main.c` and `self_update.c` at `bf8f23b`,
against an unchanged conquertron.

    OK  build=Sep_17_2026_20-46-40 frames=14400 draws=287 modules=7

**It boots.** 287 draws, seven shader modules, a full-length run.

So the environment is fine and the regression is in the code, in 212 lines of
two files. That is the first thing today that has been established by
elimination rather than guessed at, and it was worth the run.

## What survives in those 212 lines

    static char g_log_buf[32 * 1024];
    static unsigned g_log_flush_lines = 256u;
    static void checkpoint_flush(void) { ... }
    setvbuf(g_log, g_log_buf, _IOFBF, sizeof(g_log_buf));
    const uint64_t g_test_deadline_ns = ...
    while (appletMainLoop() && arkchemy_gx2_host_ticks() < g_test_deadline_ns)
    svcSleepThread(8000000ULL);

Of these:

  * the **flush cadence** is ruled out -- a run that paid the 54 seconds back
    stayed broken
  * the **buffered stream** is ruled out -- a run with `setvbuf` skipped
    entirely stayed broken
  * the **wall-clock bound** and the **8ms yield** both postdate the first bad
    build, so neither can be the original cause
  * `checkpoint_flush` only fires from the deko3d error sink, and no run since
    has logged a single `[DKDEBUG]` line

What is left is `static char g_log_buf[32 * 1024]` itself. **A static array
reserves its BSS whether or not anything ever calls `setvbuf` on it**, so it
was present, untouched, in both runs that were supposed to have removed
logging from the picture.

## An apology to a hypothesis I killed twice

This is the BSS idea, and it has now been declared dead twice on evidence that
never addressed it.

The first time rested on 256KB and 32KB behaving identically -- which rules out
size-dependence between two non-zero values and says nothing about the array
existing. The second rested on "logging is exonerated", which was true of the
flushing and the stream and not of the declaration.

`PPC_MEM_SIZE` is one gigabyte and the guest arena is a static array of exactly
that, so this NRO's BSS is a gigabyte before anything else is in it. 32KB
against that is 0.003%, which is exactly why it kept being waved away. A
gigabyte sitting at the edge of what the loader can satisfy does not care about
the ratio.

## The test

Build `Sep 17 2026 20:56:50`, interval `256`. The buffer is `malloc`'d: one
pointer in BSS instead of 32KB, and only when a buffered stream is wanted. The
first log line reports which it got.

  * boot comes back -- the BSS is the cause, confirmed, and the 60% logging win
    comes with it. The wall-clock bound and the yield are exonerated at the same
    time, since both are in this build.
  * boot stays broken -- the BSS is finally, properly dead, and the remaining
    suspects are the wall-clock bound and the yield, which bisect in two runs.

## Seven

The one that keeps mattering: a hypothesis is only dead when a measurement has
addressed *it*, not when a measurement of something nearby came back negative.
That has now cost two separate detours on the same idea.
