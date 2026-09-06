# The default pool-frame manager is a snapshot taken ~1.1M calls too early

Run: build `Sep  6 2026 13:52:58`, 420 s, 6.2 MB log.
Verdict: **the overrun hypothesis is dead, and the real cause is ordering.**

## The probe that was queued came back empty, and that was the answer

```
WIPE n=0 <none>   -- x420 samples
```

The `WIPE` capture watched every `memmove`-style move whose destination range
covered the default frame manager at `0x45f3964`. Across the whole run it
never fired once. Nothing overruns that block. The "`igObjectList::append` ->
`resizeAndSetCount` -> the move at `0x2184e1c` wipes the manager" theory,
which the previous three sessions were built on, is wrong.

## This run got much further than any before it

Two things that had never happened now happen:

```
INFLATE lzma n=1 ret=0x1 | ok=1 fail=0
COMPLETION reached=1 nonnull=1 flagptr=0x34a0 *flag=0x1 jobs_run=1
```

LZMA is reached and the completion gate fires. Previously both were `n=0`.
`stwcx: ok=789145 fail=0` -- the atomics work now, where the job-queue
counters used to never increment at all. Threads start (`created=2 started=2`),
the ring is populated (`ring=[0x45f3a10,0x45f3a10]`), an archive header is
read (`head=[0x4947411a,...]`, that is `\x1aAGI`).

The stall is now one step later and much better defined.

## Where it actually stops

```
PROBS n=1 lc=0x3 lp=0x0 size=15980 bytes ptr=0x0 allocObj=0x3488 allocFn=0x216b6f4
LZMA err=0x2 (0=ok, 0xb=size mismatch) alloc=0x2
```

The 15,980-byte probability array is refused, so LZMA fails with alloc error 2
and the archive never drains. Same symptom as before -- but for a different
reason, and this run finally shows which.

## The manager consulted at the resolve is empty

```
LZWALK hits=1 mgr=0x45f3964 frame=0 frames=1 maxCount=0x0 slot28=0
```

At the moment LZMA resolves its pool it walks `0x45f3964`, which has one
frame whose pool table holds **zero** pools. Not "missing pool 28" -- missing
everything.

Pool 28 does exist, in a different manager:

```
POOL28  n=4 mgr=0x4502fb0 frame=1 topFrameCount=0x80 slot28=0x1
POOLREG bulkSetups=2 registrations=52 maxIndex=44 got28=2 indexMask(0..31)=0xfffffbff
```

## Why it is empty: the default was built before the registrations finished

```
POOLDEST [mgr=0x4500f58 n=7  @37349..38138]
         [mgr=0x4502fb0 n=45 @39877..1362525]
         || context=0x4400170 fallbackMgr(+0x18)=0x45f3964

ORDERING registrations @37349..1362525
         | setDefaultFrame [0 @160826 src=0x4502fb0 new=0x45f3000]
                           [1 @262720 src=0x4502fb0 new=0x45f3964]
```

All 52 registrations land in `0x4500f58` and `0x4502fb0`. `0x45f3964` receives
**none**. It was built at call 262,720 from `0x4502fb0`, and registrations into
`0x4502fb0` carried on until call 1,362,525 -- roughly 1.1 million calls after
the snapshot was taken. Whatever `setDefaultFrame` copies, it copied it too
early, and nothing propagated afterwards.

## And the workers only ever see that one

```
TLS slot0: sets=3 gets=17695 nullGets=15 lastSetValue=0x0
TLS SETS [0 val=0x4502878 lr=0x214ff64 thr=0xc17e8460]
         [1 val=0x0 lr=0x21db5a4 thr=0xc17e84d8]
         [2 val=0x0 lr=0x21db5a4 thr=0xc17e8550]
TLS NULLGET callers 0x2172bbc x4
```

Thread 0 gets a real value. The two worker threads are set to `0x0` from the
same site (`lr=0x21db5a4`), so `getCallingThread()` returns NULL for them and
the engine falls through to `fallbackMgr(+0x18) = 0x45f3964` -- the empty one.

## The chain, end to end

worker thread -> NULL TLS slot 0 -> fallback manager `0x45f3964` -> built at
call 262,720 and never updated -> pool table count 0 -> pool 28 invisible ->
15,980-byte request refused -> `LZMA err=2` -> archive never decompresses ->
boot parks at `sti_idx=113`.

## Structure, for reference

Read out of the frame walk in `generated_0159.c`:

```
mgr   + 0x00  vtable
mgr   + 0x08  frame stack
mgr   + 0x0c  top frame index
stack + 0x14  array of frame pointers
frame + 0x08  pool table
table + 0x08  pool count
table + 0x14  pools array          pools[28*4] is the one LZMA wants
```

## Queued next (watch.cfg, no rebuild)

```
store1=0x45f3964   does anything EVER write to the default after it is built?
store2=0x4502fb0   when do the registrations actually land?
dump1=0x45f3964    16 words: gives +8 (stack) and +0xc (top index)
dump2=0x4502fb0    same, to compare
```

The question it settles: do the two managers **share** a frame stack (`+8`
equal, so the default should have seen the later registrations and something
else is wrong) or hold **separate** ones (`+8` different, so `setDefaultFrame`
deep-copies and the fix is ordering or aliasing)?

`igRewrite8` cannot help here -- its `igMemoryPool` is a 45-line stub and it
models the filesystem side, not the allocator.
