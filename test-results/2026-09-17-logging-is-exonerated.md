# Logging is exonerated

Build `Sep 17 2026 20:39:33`, `log-flush-lines.txt` = 1.

    log flush interval: 1 line(s), stream UNBUFFERED
    tally: flush=1 frames=7270 draws=0 modules=2

No `setvbuf`, no buffered stream, a flush on every line -- byte for byte the
logging behaviour of every run before 18:15:43. **Still broken.**

So the whole logging line of investigation is closed:

  * the flush cadence is not the cause (the previous run, which restored the
    54-second cost and stayed broken)
  * the buffered stream is not the cause (this run, which removed it entirely)

## What is left, and what has never been questioned

The break began at build `18:15:43`. Both changes that came after it -- the
wall-clock run bound and the 8ms yield -- postdate the first bad run, so
neither can be the original cause. Logging is now out. That leaves very little
of my own work in the frame, and it leaves one assumption that has been carried
all day without examination: **that the code is what changed at all.**

Since the last good build, `conquertron` has not moved a commit, and `jouster`
has changed exactly two files that reach the binary: `main.c` and
`self_update.c`.

## The test

Build `Sep 17 2026 20:46:40` is `main.c` and `self_update.c` checked out at
`bf8f23b` -- the exact sources of the last run that booted properly -- against
an unchanged conquertron. The working tree was restored to HEAD immediately
afterwards; only the NRO on the card is the old one.

  * boot comes back -- the cause is in those two files, the search space is 192
    plus 20 lines, and it bisects cleanly from here.
  * boot stays broken -- **nothing in the source is responsible.** Something in
    the environment changed between 17:33 and 18:15: the SD card's contents,
    the game data, a config file, or the console's own state. That is a
    completely different search, and five wrong calls today have all been
    inside an assumption that never held.

## Six

A stuck A button. BSS pressure. A shortened run. Thread starvation. A
controlled test that held the wrong thing constant. And now, possibly, the
assumption that the regression is in the code.

The instinct that keeps failing is not the theorising. It is treating the
most recently changed thing as the most likely thing, and then testing
variations of it instead of testing whether it is involved at all.
