# The yield changed nothing

Build `Sep 17 2026 18:45:11` -- the main loop yielding 8ms explicitly.

    RUNRATE hostframes=9300 elapsed=239605ms hostfps=38
    LOGCOST lines=17979 log=1918ms flush=573ms
    GPUCNT  frames=1
    INPUT   vpadreads=0
    guest calls 2,514,226

                        18:32:42     18:45:11
    host fps                  56           38
    guest calls        2,510,759    2,514,226
    guest calls/sec       10,479       10,476

The host loop slowed by a third, exactly as the 8ms sleep intends, and the
guest did **the same work at the same rate** -- 0.14% apart, which is noise.

So the guest thread is not competing with the main thread for CPU, and the
scheduling explanation is wrong. That is four in a row.

## What is left standing

Four explanations have now been proposed and killed by measurement: a stuck A
button, BSS pressure, a shortened run, and thread starvation. What has not
been tested is the plain claim underneath all of them -- that buffering the log
is what changed the guest's behaviour at all.

It looks obvious. The last good run and the first bad one differ by it. But
two more changes have landed since (the wall-clock run bound and the 8ms
yield), and this project's record today is four confident readings of a number
and four misses, so "obvious" has not been worth much.

## The experiment

A PR merged at 20:22 put the flush interval on the card --
`sdmc:/switch/Jouster/log-flush-lines.txt`, read before the first checkpoint so
one setting covers the whole run, and echoed as the log's first line so a run
can never be read without knowing which setting produced it.

That turns this into a proper controlled test: **the same binary, one variable,
set from a text file.** No rebuild between the two halves, so nothing else can
drift.

Build `Sep 17 2026 20:31:40` is on the card with the interval set to **1** --
a flush on every line, the behaviour of every run before 18:15:43.

  * boot comes back -- buffering is the cause, confirmed against the same
    binary, and the question becomes *why* a log write affects the guest's
    path. That is a real finding and probably a real bug.
  * boot stays broken -- buffering is exonerated and the cause is in the
    wall-clock run bound or the yield, both of which are mine and both of which
    can be reverted cleanly.

Either way this is the first time today the question has been asked with only
one thing moving.
