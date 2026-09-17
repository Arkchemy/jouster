# The controlled test was not controlled

Build `Sep 17 2026 20:31:40`, `log-flush-lines.txt` set to `1`.

    log flush interval: 1 line(s)
    LOGCOST lines=14111 log=54616ms flush=54068ms
    RUNRATE hostframes=7260 elapsed=238718ms hostfps=30
    GPUCNT  frames=1
    guest calls 2,454,134   (10,226/sec)
    tally   flush=1 frames=7299 draws=0 modules=2

The flush cost came back -- 54 seconds of a 240 second run -- so the setting
took effect. And the boot is still broken.

Guest rate across every run since the change, regardless of flushing:

    18:32:42  buffered    10,479 calls/sec
    18:45:11  buffered    10,476 calls/sec
    20:31:40  flush=1     10,226 calls/sec
    ---
    17:33:11  (last good) 16,146 calls/sec

## The test did not test what it claimed

`setvbuf` is called unconditionally. The card setting controls when
`checkpoint()` calls `fflush`, and nothing else. So interval 1 restored the
flush *cadence* while leaving the stream fully buffered against a 32KB static
array -- the buffer was present in both halves and only the `fflush` calls
moved.

What that run actually established is narrower than intended, and still worth
having: **the flush cadence is not the cause.** Paying 54 seconds of SD writes
back did not bring the boot back.

## And the BSS hypothesis was never dead

The note that killed it rested on 256KB and 32KB behaving identically. That
rules out size-dependence between those two values. It says nothing about the
buffer existing at all, and `static char g_log_buf[32 * 1024]` has been in
every build since 18:15:43 including both halves of the "controlled" test.

Writing "the BSS hypothesis is dead" on that evidence was wrong, and it is the
fifth wrong call today. The others were at least killed by a measurement that
addressed them; this one was retired by a measurement that did not.

## What is being tested next

Build `Sep 17 2026 20:39:33`. Interval 1 now means genuinely unbuffered: no
`setvbuf`, no static buffer in play, a flush on every line -- exactly what
every run before 18:15:43 did. The first log line says which it got, so a run
can still never be read without knowing.

  * boot comes back -- the buffer's existence is the cause, and with 1GB of
    guest arena already in BSS the mechanism is worth finding properly rather
    than worked around.
  * boot stays broken -- logging is fully exonerated and what remains is the
    wall-clock run bound, which is mine and revertible.

## Five misses

A stuck A button. BSS pressure. A shortened run. Thread starvation. And a
controlled test that held two things constant except it held one of them
wrong.

Each was a coherent story about a real number. The failure mode is not the
theorising -- it is calling a hypothesis dead on evidence that never touched
it, which is what happened twice today: once to the BSS, and once to the
buffering itself.
