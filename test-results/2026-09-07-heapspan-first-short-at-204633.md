# HEAPSPAN: the chain is already short at call 204,633

Run of 2026-09-07, build `Sep  7 2026 13:54:30`.

```
HEAPSPAN walks=9553 arenaEnd=0x4500270 lastSpan=0x4500268/8058b
         | whole until: span=0x4500268 blocks=8058
         | first short: call=204633 span=0x4663a04 blocks=204
```

## A flaw in the probe, and what survives it

`arenaEnd=0x4500270` is not this pool's arena end, which is `0x4a002e0`. The
hook fires for **every** `igHeapMemoryPool`, and a second one -- 8,058 blocks
in a small arena -- overwrote the shared slots. So `arenaEnd`, `lastSpan` and
`whole until` all describe the wrong pool and are worthless here.

The `first short` record does survive, because it is written once by whichever
pool goes short first, and its span `0x4663a04` matches the stop point
HEAPWALK and HEAPSTOP both found. That is our pool.

**At call 204,633 the chain already ended at `0x4663a04` with 204 blocks.**
The final state is 211 blocks ending at the same address, so the seven blocks
allocated afterwards went into holes left by frees rather than extending the
frontier -- consistent with `free=1839` in the same run.

## Context from OWNER

```
[0 @2042 ALLOC ret=0x45002e0 ptr=0x0 size=5242880 lr=0x2173170]
```

The 5 MB arena itself is allocated at call 2,042 by
`igMemoryPool::allocatePoolMemory`, so the pool exists very early. And
`allocbucket [0]@110636 buf=0x4593c40 pool=0x4500274` shows a healthy
allocation at call 110,636 landing about 600 KB into the arena, well before
the frontier reached `0x4663a04`.

So the tail was lost somewhere between the pool's first use and call 204,633.
Whether it was ever whole cannot be read from this run, because the slot that
would say so belongs to the other pool.

## Next

Build `Sep  7 2026 14:09:40`, md5 `b3a7bc5ab1493ca85d3245b42c632765`, is on
the Switch with two fixes.

**Keyed by control address.** Up to four heap pools each get their own row, so
one cannot overwrite another's history. Each row keeps its first walk, its
last walk that still reached the sentinel, and its first that did not. A
`lastGood` of zero will mean the pool was never observed whole, which is
itself an answer.

**The culprit's arguments.** The walk runs at `reallocCommon`'s entry, before
the call does any work, so `lastGood` and `firstShort` are consecutive calls on
the same pool and the damage is done by whatever the `lastGood` call went on
to do. That row now records its `ptr`, `size` and `lr`, which names the
allocation directly rather than leaving a call number to be matched up
afterwards.
