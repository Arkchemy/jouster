# The workers read a stale snapshot of the pool-frame manager

Run of 2026-09-06, 25,200 frames, build Sep 5 2026 13:22:50.

## What changed since the last run

Allocation is no longer the problem. `mem: fail=1 free=1839 reuse=1714` and
`mallocret: calls=3 bad=0 lastret=0x45bb244 lastsize=216000` — a 216 KB
request now succeeds. The earlier "301 failures on pool 0x4500274" state is
gone; boot reaches `static_init=1`, `sti_idx=113` of 114.

## The actual stall

`LZMAPOOL n=1 poolGlobal=0x1c resolved=0x0` — pool index 28 resolves to NULL,
so LZMA cannot allocate its probability array (`PROBS size=15980 ptr=0x0`),
decompression never completes, and the archive work item never drains.

Index 28 **does** exist:

    POOL28   mgr=0x4502fb0 frame=1 topFrameCount=0x80 slot28=0x1
    POOLREG  registrations=52 maxIndex=44 got28=2 indexMask=0xfffffbff

but not in the manager the resolve actually reads:

    LZWALK   mgr=0x45f3964 frame=0 frames=1 maxCount=0x0 slot28=0
    CHAIN    mgr=0x45f3964 stack=0x0(count=0x0) arr=0x0 pools=0x0

## Why the workers read the wrong manager

`getCallingThread()` is `OSGetThreadSpecific(0)`. The disassembly at 0x21db584
is unambiguous:

    21db58c: li  r3, 0        ; slot 0
    21db594: mr  r31, r4
    21db598: mr  r4, r3       ; value = 0
    21db5a0: bl  0x2589dd0    ; OSSetThreadSpecific(0, NULL)

The game **deliberately clears TLS slot 0** on each worker thread — this is not
a shim bug. Confirmed by the counters: of three sets, one carries a real value
on the main thread and two write NULL on the two worker threads.

    TLS slot0: sets=3 gets=17695 nullGets=15
    TLS SETS [0 val=0x4502878 lr=0x214ff64 thr=0x78788fa8]
             [1 val=0x0       lr=0x21db5a4 thr=0x78789020]
             [2 val=0x0       lr=0x21db5a4 thr=0x78789098]

With NULL in the slot the engine falls back to the default frame manager, so
the fallback has to hold the same pools. It does not:

    SETPOOLS [0 target=0x4502fb0 default=0x45f3964 MISMATCH]
             [1 target=0x4502fb0 default=0x45f3964 MISMATCH]
    POOLDEST [mgr=0x4500f58 n=7 @37349..38138]
             [mgr=0x4502fb0 n=45 @39877..1362524]  context=0x4400170
             fallbackMgr(+0x18)=0x45f3964 -- workers can only see the fallback

## The ordering

    ORDERING registrations @37349..1362524
             setDefaultFrame [0 @160826 src=0x4502fb0 new=0x45f3000]
                             [1 @262719 src=0x4502fb0 new=0x45f3964]

The default manager is a **copyDeep snapshot taken at call 262719**, while pool
registrations run from call 37,349 all the way to 1,362,524. Every pool
registered after the snapshot — index 28 among them — lands in 0x4502fb0 and is
invisible to the copy the workers read.

`COPY[1]` shows the snapshot did carry a stack and array at copy time
(`new=0x45f3964 stack=0x46439c8 arr=0x4500fc4`), yet `CHAIN` now reads
`stack=0x0 count=0x0`. So the copy is not merely stale, its frame stack has
since been zeroed. That is the next thing to catch.

## Next probe (queued in watch.cfg, no rebuild needed)

    store1=0x45f397c   # 0x45f3964 + 0x18, the copied manager's frame stack
    store2=0x4500274   # keep the pool ledger
    dump1=0x45f3964    # the copied manager itself
    dump2=0x4502fb0    # the manager that actually holds the 45 pools

Comparing the two managers side by side says whether the fix is to keep the
default in sync with later registrations, or to stop taking a snapshot at all.

## Not the cause

`0x45002e0` is a TLSF `control_t` — `block_null` self-linked at +8/+0x0c is
what `tlsf_create` writes, and the pool points at it from +0x10. Its
`fl_bitmap` reads 0, which looks like an empty heap, but allocations out of
that pool are succeeding, so the zero bitmap is not the stall. Do not chase it.
