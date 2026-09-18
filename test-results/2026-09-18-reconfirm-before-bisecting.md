# 16K boots too — and the known-bad point needs re-confirming

Same binary `Sep 18 2026 11:21:11`, one text file changed between runs.

    0        newlib default   boots   12,807 draws
    4096     setvbuf          boots   12,785 draws
    16384    setvbuf          boots   12,633 draws
    32768    ?

Both sweep points boot, healthily, with `modules=7` each time. So the
threshold — if there is one — sits between 16,384 and 32,768.

## Why the next run is 32768 and not 24576

The obvious bisection step is the midpoint. It is the wrong move, because the
32KB failure has **never been observed on this binary**.

Every run that failed at 32KB was on a build from yesterday:

    18:15:43   256KB BSS buffer
    18:25:22   32KB BSS buffer
    18:32:42   + wall-clock run bound
    18:45:11   + 8ms yield
    20:31:40   + card-driven flush interval
    20:56:50   32KB heap buffer

Since then this binary has gained `-O2`, block reads in place of the per-pixel
upload loops, fence-rotated command memory, a 512-slot profiler and thread
attribution. The guest now runs at 18.2 fps rather than 0.28, and the whole
memory and timing picture around it has moved.

Bisecting between a good point measured today and a bad point measured
yesterday, on different code, is bisecting across two variables. That is the
exact mistake that cost most of last night — three "exonerated" verdicts from
runs that changed two things at once — and it is not worth repeating on the
same phenomenon.

So: 32768 on the current binary, to establish that the bad point is still bad.

  * still fails — the threshold is real and lives between 16K and 32K, and the
    bisection can proceed on solid ground.
  * **boots** — then something fixed since yesterday has already resolved it,
    the log can be buffered properly, and 39% of every run comes back. The
    likeliest candidate is the block-read change, which removed a large amount
    of per-frame CPU work, or `-O2` shifting allocation behaviour.

The second outcome is genuinely possible and would be the best result
available, which is another reason not to assume the old measurement still
holds.
