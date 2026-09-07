# The huge requests are real, and they are Wii U VRAM

2026-09-07, build `Sep  7 2026 01:27:19`.

## Where the sizes come from

With `BIGREQ` reading the correct register, every request of 1 MB or more and
who asked for it:

| call | size | caller |
|---|---|---|
| 2009 | 1 MB | `igMemoryContext::initializePool` |
| 2037 | **5 MB** | `igMemoryContext::initializePool` |
| 2065 | 2 MB | `igMemoryContext::initializePool` |
| 39759 | 172 MB | `igMemoryContext::configure` |
| 158719 | 32 MB | `igCafeSystemMemory::enableVRAM` |
| 159230 | **350 MB** | `igCafeSystemMemory::enableVRAM` |
| 215061 | 1.6 MB | (2005148) |
| 215469 | 14 MB | (2004e2c) |

The 5 MB at call 2037 is our arena, and the game asked for exactly that, so
the pool is the size it was meant to be. `enableVRAM` is Wii U graphics
memory -- 32 MB is MEM1, and the 350 MB is a MEM2 graphics reservation. On
hardware those come from somewhere other than a 5 MB game pool.

The 350 MB request at call 159230 is the same allocation `ALLOCFAIL` catches
refused at call 159235.

## The pool is fine; the routing is the suspect

`dump1` on the pool object itself:

```
+0x00  0010b55c   metaobject
+0x04  00000003   refcount / pool, packed
+0x08  01000000   16 MB
+0x10  045002e0   arena base -- matches the 5 MB allocation exactly
+0x14  00500000   arena size = 5 MB
+0x18  00000004   alignment -- the "lim=4" seen at every refusal
```

Nothing malformed. It is a correctly built 5 MB pool.

So the question is no longer "why is this pool broken" but **why is
everything being allocated out of it**. The igz that boot loads declares eight
pools by name -- `Default, Image, Vertex, Audio, AnimationData, VertexObject,
String, Text` (see `blaster/IGZ.md`) -- and the runtime registers 52 pools
across two managers. If pool resolution collapses onto one, every allocation
lands in the 5 MB `Default` arena, it fills, and small requests start failing.

That matches the shape of what is measured: **288 of 589 allocations succeed
and 301 fail**, all at the same exit, and the failures include 64-byte grows
that a healthy 5 MB pool would never refuse.

## Next

Count allocations per pool. If nearly everything resolves to `0x4500274`
rather than spreading across the registered pools, the funnel is the bug and
the exhaustion, the refused grow, the overrun, the zeroed manager and the
undrained archive are all one cause.

This also rejoins the earlier thread rather than replacing it: pool index 28
resolving through an empty manager, and everything resolving to one pool, are
the same class of failure -- pool lookup not returning what it should.
