# The matrix says there are two causes

Build `Sep 17 2026 21:09:22` -- frame-bounded loop, no yield, buffered on the
heap.

    tally  flush=256 frames=14400 draws=0 modules=2

The harness is byte-for-byte the last good build's again, and it is still
broken. So the wall-clock bound and the 8ms yield are exonerated as well.

At which point it is worth laying every run out together instead of reasoning
forward one hypothesis at a time.

    build      loop bound   yield   logging                    result
    bf8f23b    frames       no      unbuffered, per-line       GOOD
    18:15:43   frames       no      buffered, 256KB BSS        bad
    18:25:22   frames       no      buffered, 32KB BSS         bad
    18:32:42   wall clock   no      buffered, 32KB BSS         bad
    18:45:11   wall clock   8ms     buffered, 32KB BSS         bad
    20:31:40   wall clock   8ms     per-line, setvbuf STILL ON bad
    20:39:33   wall clock   8ms     truly unbuffered           bad
    20:56:50   wall clock   8ms     buffered, heap             bad
    21:09:22   frames       no      buffered, heap             bad

Two rows matter.

**18:15:43** differs from the good build by buffering alone -- same loop, no
yield -- and it is bad. That says buffering breaks it.

**20:39:33** is truly unbuffered and still bad -- which is why logging was
written off. But that run also carried the wall-clock bound and the yield, so
it never tested buffering in isolation. It tested unbuffered-plus-harness.

Those two are only consistent if there are **two independent causes**: one in
the buffering, one in the harness. Every run since 18:15:43 has had at least
one of them, which is why every single-hypothesis test came back negative and
why "exonerated" has been claimed three times in a row for things that were not.

## The test, and it is free

The one combination never run is frame-bounded, no yield, *and* unbuffered --
the good build's configuration in both respects at once.

That needs no rebuild. Build `21:09:22` is already on the card and reads the
interval from `log-flush-lines.txt`, which is now set to `1` -- which in this
build also skips `setvbuf` entirely. **Same binary, same hash
(`79d8ac6c...`), one file changed.**

  * boot comes back -- two causes confirmed, and the buffering one is real and
    isolated. The 60% logging win then has to be bought some other way, and
    knowing *why* a buffered stdio stream affects the guest is worth having.
  * boot stays broken -- both causes are somewhere else entirely, and the
    remaining diff is the PR's config read and tally signature, which is small
    enough to read line by line.

## What went wrong in the method

Three "exonerated" calls, each from a run that changed two things. The flush
cadence test left `setvbuf` on. The unbuffered test left the harness changes
in. The BSS test left the harness changes in too.

One variable per run is the rule this project already had, and it was being
broken every time by the things that were *already* different rather than the
thing being changed. A control is not a control unless everything else matches
the reference build, and the reference here was nine builds back.
