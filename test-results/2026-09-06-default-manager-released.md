# The default frame manager is released while the workers still use it

Run of 2026-09-06, build Sep 6 2026 01:46:54, 25,200 frames. This is the
reading the previous run's `watch.cfg` was queued for, and it changes the
diagnosis from "stale snapshot" to "freed object".

## What the dumps say

    DUMP1 0x45f3964  00:00000000 04:00000000 08:00000000 ... 3c:00000000
    DUMP2 0x4502fb0  00:0011ecd8 04:00800001 08:04502fc8 0c:00000003
                     10:00000010 14:00000018 18:0011efe8 ...

`0x4502fb0`, the manager holding all 45 pools, is healthy and self-consistent:
`+8` points at `+0x18`, its own embedded frame stack, exactly as expected.

`0x45f3964`, the default the workers read, is **entirely zero for the first
0x40 bytes -- including its vtable at +0**. Not partially stale. Blank.

## Who blanks it, and when

The store watch on `0x45f397c` (the copy's embedded frame stack) caught the
whole life of the field:

    [0] @1       = 0x0
    [1] @204125  = 0x40006   pc=0x21f04f0  lr=0x217cc58
    [2] @261946  = 0x40004   pc=0x21f04f0  lr=0x217ced4
    [3] @441068  = 0x0       pc=0x215d194  lr=0x227f364

`0x215d194` is **`igObject::Release`**, reached from a countdown loop:

    227f360: bl 0x215d194        ; igObject::Release
    227f364: addic. r26, r26, -1

So the object is refcount-released at call 441,068 and its storage wiped. The
timeline against `ORDERING`:

    @262720  setDefaultFrame creates the copy 0x45f3964 (copyDeep of 0x4502fb0)
    @441068  igObject::Release drops it and the memory is zeroed
    ..1362525 pool registrations continue into 0x4502fb0
    later    workers read 0x45f3964 -> all zeros -> slot28=0 -> LZMA pool NULL

`CHAIN mgr=0x45f3964 stack=0x0 count=0x0 pools=0x0` and
`LZWALK mgr=0x45f3964 slot28=0` are both just this dead object being read.

## Why this is the boot stall

`LZMAPOOL resolved=0x0` -> LZMA cannot allocate its 15,980-byte probability
array -> `decompressBatch` never completes -> the archive work item never
drains. Index 28 exists the whole time in 0x4502fb0
(`POOL28 slot28=0x1`, `POOLREG got28=2`); nothing ever looks there, because
`getCallingThread()` returns NULL on the workers (the game clears TLS slot 0
deliberately -- see 2026-09-06-pool-manager-snapshot.md) and the fallback they
use has been freed.

## The question this leaves

Whoever stores 0x45f3964 as the default does not hold a reference to it, or an
extra Release is being issued. Retail must either retain it, never copy at all,
or keep the workers off the fallback path entirely.

Next probe (queued, no rebuild):

    store1=0x45f3968   # +4 -- the refcount/flags word (healthy peer reads 0x00800001)
    store2=0x45f3964   # +0 -- the vtable, to timestamp the destruct
    dump1=0x45f3964
    dump2=0x4502fb0

That gives the retain/release sequence and says whether the count is
over-released or never taken.
