# The upload loops are not the bottleneck

Build `Sep 17 2026 17:08:30`.

    TIMING frames=96 frame_avg=1890ms
           | copyup=6630ms texup=8292ms setcbup=196ms waitidle=765ms

96 frames at 1890ms is 181,440ms of frame time. The four instrumented
candidates come to 15,883ms.

**8.8%.**

Every one of them was a plausible suspect and all four together are a rounding
error. Removing the per-pixel upload loops and the queue wait entirely --
not optimising them, deleting them -- would take 1890ms to 1725ms. 0.38 fps
becomes 0.42 fps.

This is exactly why it was measured. The byte-at-a-time `ppc_load_u8` loops
were the obvious answer, they are genuinely ugly, and a day could have gone
into rewriting them for 10%.

## Where the other 91% is not yet known

But there is one candidate that has been in every measurement this project has
ever taken, and was never itself measured.

`checkpoint()` calls `fflush(g_log)` on **every line**, and a run writes about
27,500 lines to an SD card. An unbuffered SD write is on the order of
milliseconds. 27,500 of them at even 5ms is 137 seconds, against a 181 second
run.

If that is where the time goes then the frame rate being measured is
substantially the cost of measuring it, and every timing figure in this
project's history -- including `frame_avg=1890ms` above -- is inflated by its
own instrumentation.

That would also explain something that never quite fitted: the video path was
made measurably faster on 2026-09-15 (a per-pixel 64-bit divide replaced with
a lookup table, a 3.7MB memset moved out of the frame loop) and the reported
frame count did not move at all. The note at the time put that down to the
decoder playing every frame regardless of rate. A fixed per-line logging cost
swamping the work explains it at least as well.

## What is being measured next

Build `Sep 17 2026 17:33:11`.

`LOGCOST lines= log= flush=` -- time inside `checkpoint()`, and inside its
`fflush` alone, against the same frame total.

The two are separated because they need different fixes. Formatting is cheap
and the write is not; if `flush` dominates, buffering the log and flushing on
dump boundaries costs nothing and changes everything. If `log` is large but
`flush` is small, the formatting itself is the problem and the dumps are simply
too big.

And if neither is significant, the cost is in the recompiled code itself, which
is the largest piece of work this project has left and should not be started
on a guess.
