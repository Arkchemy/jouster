# The log was 60% of the run

Build `Sep 17 2026 17:33:11`.

    LOGCOST lines=27854 log=114060ms flush=109547ms
    TIMING  frames=103 frame_avg=1784ms
            | copyup=7105ms texup=8821ms setcbup=196ms waitidle=803ms

103 frames at 1784ms is 183,752ms.

    flush   109,547ms   59.6%
    log     114,060ms   62.1%   (the flush is 96% of it)
    uploads  16,925ms    9.2%
    ------------------------------
    accounted            71.3%

**3.93 milliseconds per line, 27,854 lines.** Almost two thirds of every run
has been spent flushing the log to the SD card one line at a time.

Not a share of the problem. The problem. And it was present in every
measurement this project has ever taken, including all of yesterday's and
today's, because it is the instrumentation.

## What this invalidates

`frame_avg=1784ms` is not the game's frame time. It is the game's frame time
plus 60% overhead from watching it. The 0.38 fps figure from the previous note
is wrong in the same way, and so is every timing number in this project's
history.

It also settles something that never fitted. The video path was measurably
improved on 2026-09-15 -- a per-pixel 64-bit divide replaced by a lookup table,
a 3.7MB memset hoisted out of the frame loop -- and the reported frame count did
not move. That was put down to the decoder playing every frame regardless of
rate. The real reason is that the work being optimised was a few percent of a
frame dominated by a fixed per-line logging cost.

What stands: the upload loops are still 9%, which was measured against the same
inflated total and is therefore an over-estimate of their true share, not an
under-estimate. Rewriting them would still have been wasted work.

## Fix

Build `Sep 17 2026 18:15:43`.

The log gets a 256KB buffer and flushes every 256 lines instead of every one --
27,854 flushes become about 109.

The per-line flush was there for a real reason, stated in its own comment: a
buffered write is lost with the process, and the whole point of the log is to
survive the crash it is describing. That is preserved where it actually
matters:

  * `gx2_debug_log_sink` flushes explicitly. deko3d's `RaiseError` is
    `[[noreturn]]`, so its non-zero call is the last thing that happens before
    the process dies -- and those `[DKDEBUG]` lines are the ones that found the
    GPU page fault and the queue death.
  * the unhandled-exception handler already flushed `g_log` explicitly, twice.

What the per-line flush bought beyond those two paths was the last few hundred
lines of a kill that runs no handler at all, and the run tally already covers
that case by design: a failed run cannot file its own report, so the next run
records it from `run-open.txt`. 256 lines is about two dump blocks, which bounds
what such a kill can lose.

## What to read next

  * `LOGCOST flush=` should fall by roughly two orders of magnitude.
  * `TIMING frame_avg=` will drop, and that new figure is the first honest
    measurement of the game's frame rate this project has had.
  * `GPUCNT frames=` and `INPUT vpadreads=` should rise together, since both
    count the game's own frames.

Only then is it worth asking where the remaining time goes. The recompiled code
is the obvious candidate and it is the largest piece of work left, so it should
be started against a number that means something.
