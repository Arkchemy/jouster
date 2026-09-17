# Status 4 lands after completion, not instead of it

Build `Sep 17 2026 16:14:31`.

    SSHIST
     [0x4503700: 2@0x2155a14 2@0x2155b24 4@0x215566c 1@0x216b0b4 2@0x216835c
                 1@0x216b0b4 2@0x2168be4 2@0x2155b24]
     [0x452cfc8: 1@0x2155c14 2@0x2155bac 1@0x2155c14 2@0x2155bac
                 1@0x2155c14 2@0x2155bac 1@0x2155c14 2@0x2155bac]
     [0x4503730: 2@0x2155a14 4@0x216abcc 2@0x216aba8 1@0x216b0b4 2@0x2168be4
                 0@0x2168774 0@0x2168774 1@0x216b0b4]
     [0x4503760: 0@0x2168774 1@0x216b0b4 2@0x216835c 2@0x2155a14 4@0x216abcc
                 2@0x216aba8 1@0x216b0b4 2@0x2168be4]

Of the two cases this was written to separate, it is the second.

**Every 4 is immediately preceded by a 2.** Not one item goes 1 -> 4. The read
reaches status 2 and is then marked 4, so nothing here is a read that failed.

Three further things fall out of the sequences:

  * Every 4 is followed by a 2 again, from `lr=0x216aba8` -- 0x24 bytes below
    the `0x216abcc` that wrote the 4. Two writes in one function, so 4 is a
    step inside an operation, not a terminal error state.
  * Item `0x4503730` is written 0 twice, by `lr=0x2168774`. Items are being
    reset and reused. They are pooled.
  * Item `0x452cfc8` alternates 1,2,1,2 for its whole life and never sees a 4.
    Whatever 4 means, it is not universal.

`isFileWorkFinished` tests `_status > 2` and calls anything above it finished.
On a pooled item that has already completed and moved on, that test is being
applied to a byte describing a different operation.

## The attribution this was built on is wrong

`IGZWORK` sits on both failure branches of `isFileWorkFinished` -- the NULL
`_fileWorkItem` arm and the `status > 2` arm, at `0x219f3bc` -- and recorded
nothing for an entire run while `IGZSTATE` reported `ret=0x1` six times.

The probe is on the path; the generated code shows it at the label both
branches reach. So whatever produces that `ret=0x1` is not those branches, and
the note that had IGZSTATE's return coming from `isFileWorkFinished` is
unsupported. Every conclusion resting on it, including the status-4 reasoning
above, is provisional until something re-establishes where that return comes
from.

## The reading the counts actually support

    IGZSTATE  6 loads: Failed, Finished, Failed, Finished, Failed, ...
    IGZLOAD   entries=6 | igArchive::open rets: 0x0 0x0 0x0
    STREAMLOAD calls=3: permanent0/bootstrap, item0/legal, permanent0/global

Six loads, three streams, three opens, three finishes, and the failures strictly
alternate with the successes.

That is the exact signature of try-then-fall-back: each stream load attempts one
location, fails, attempts another, succeeds. On that reading the three failures
are routine probing, there is no loading bug, and the engine is simply still in
early boot -- bootstrap, legal, global, with no level ever requested.

It is equally the signature of three streams each failing once for a real reason
and succeeding on a retry that hides it. The counts cannot separate those.

## What is being measured next

Build `Sep 17 2026 16:40:24`.

`IGZSTATE` now records which stream load each igz load belongs to.

  * Two loads against the same stream -- try-then-fall-back, the failures are
    routine, and the graphics being black is only that no level has loaded yet.
  * One load per stream, three streams, six loads -- the pairing is something
    else and the failures are real.
