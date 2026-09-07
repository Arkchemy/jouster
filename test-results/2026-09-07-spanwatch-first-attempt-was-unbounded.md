# SPANWATCH, first attempt: the walk ran off the end and the result is unusable

Run of 2026-09-07, build `Sep  7 2026 16:57:51`.

```
SPANWATCH n=2
 [ctrl=0x4400270 walks=17297 max=0x4500268 cur=0x4500268 drop(@0 0x0 blocks=0 by -)]
 [ctrl=0x45002e0 walks=1608  max=0x8fd7fec cur=0x8fd5c74
                             drop(@204398 0x4653a00 blocks=203 by memalign)]
```

## The flaw, which is mine

To avoid needing the arena size at the `tlsf_*` hooks I replaced the arena
bound with a high-water mark. That was wrong. `max=0x8fd7fec` is **73,235,724
bytes past the end of an arena that ends at `0x4a002e0`** -- once the chain is
damaged the walk runs clean past the sentinel and wanders through unrelated
memory until it happens on a zero word.

With the high-water mark set from a runaway, every healthy walk afterwards
reads as "below max", so the drop it reports may be nothing more than the
first walk after a runaway. **`drop(@204398 ... by memalign)` cannot be
trusted, and I am not going to build on it.**

The other pool is the control that shows the mechanism is otherwise sound:
`ctrl=0x4400270` has `max = cur = 0x4500268`, exactly `end - 8`, its sentinel,
and no drop at all.

## Worth keeping from it

The runaway is itself a real observation. A walk bounded only by "stay within
1 GB of the control block" left the arena, which means that at some point the
damaged chain contains a size word large enough to jump past the sentinel.
HEAPWALK never saw this because it is bounded by the arena end and stops at
`0x4663a04`. Whether that bogus size is a cause or another symptom is not yet
known, but it is new.

## Next

Build `Sep  7 2026 17:13:13`, md5 `bad436d566113bccb077c74132bf1ada`, is on
the Switch.

SPANWATCH now learns each arena's end once from `reallocCommon`, which knows
`psize`, and caches it per control block; hooks that do not know it skip until
the row exists. The test is back to the honest one -- a walk reaching
`end - 8` found the sentinel and the chain is whole -- rather than a
high-water mark that a corrupt walk can poison.

A walk that leaves the arena is now counted as a **runaway** and discarded
rather than recorded, so a bogus size can no longer manufacture a drop. The
report shows the runaway count alongside the drop, so if runaways are frequent
that is visible rather than silently shaping the answer.
