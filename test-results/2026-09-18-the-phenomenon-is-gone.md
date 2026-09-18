# The phenomenon is gone

Same binary `Sep 18 2026 11:21:11`, one text file changed between runs.

    0        newlib default   boots   12,807 draws
    4096     setvbuf          boots   12,785 draws
    16384    setvbuf          boots   12,633 draws
    32768    setvbuf          boots   12,607 draws   <-- the "bad" point

**32KB boots.** The configuration that would not get past early boot six times
yesterday now runs a full 14,400 frames with `modules=7` and a draw count
within 2% of every other point in the sweep.

There is no threshold. There is nothing left to bisect. Something changed
between yesterday evening and this morning that resolved it entirely.

## The decision that saved the runs

The obvious next step after 16K booted was 24,576 — the midpoint. Taking it
would have meant bisecting a good point measured today against a bad point
measured yesterday **on different code**, and the answer would have been a
threshold that does not exist.

Since the last failing run this binary has gained `-O2`, block reads replacing
the per-pixel upload loops, fence-rotated command memory, a 512-slot profiler
and thread attribution, and the guest has gone from 0.28 fps to 18.2. Treating
a measurement from before all of that as still valid is exactly the error that
produced three wrong "exonerated" verdicts on 2026-09-17: conclusions from runs
where two things differed.

Re-establishing the known-bad point cost one run and returned the best
available result.

## What fixed it is not known

Honestly: unknown, and worth saying rather than picking a plausible culprit.
The candidates are `-O2`, the block reads, the fence rotation, or some
interaction. Two observations that cut against the memory-pressure reading the
previous note reached for:

  * The fence rotation **added** two extra 64KB command memory blocks. The
    build now allocates more, not less, and works.
  * The 512-slot profiler added another few kilobytes. Also fine.

So "the host heap is at its limit and tens of kilobytes matter" does not
survive either. That reading was two hours old and is already retired.

It could be found by bisecting the four builds since. It is not obviously worth
the runs — but it is worth writing down that a phenomenon which blocked 39% of
every run for three days disappeared without being understood, because if it
returns, this is the note that says nobody knows why it left.

## What it is worth, immediately

`LOGCOST log=138775ms` on the last run — 39% of it, the largest single cost
since Tuesday, and the reason the log has been written a line at a time
straight to an SD card.

Set now: **buffer 32768, flush every 256 lines.** About 30,600 flushes become
roughly 120. If it holds, most of that 138 seconds comes back and the run gets
faster than any change to the graphics path has managed.
