# The allocator hands out a block inside a block it already handed out

Run: build `Sep  6 2026 22:16:33`, `owner=0x45f35dc`.

## Two theories died first, both cheaply

**The write is not a stack spill.** `storewatch_r1_stackptr = 0x3ffff948` --
the main thread's stack, initialised to `PPC_MEM_SIZE - 256` at the very top
of guest memory. The manager at `0x45f3964` is nowhere near it.

**And it is not a worker stack either.** `THREADSTACK` puts both workers well
above the manager, and the two buffers are adjacent whichever end the `stack`
argument names:

```
[0 thread=0x6d480 stack=0x4653a00 size=65536 entry=0x21db584]
[1 thread=0x6dc88 stack=0x4663a08 size=65536 entry=0x21db584]
```

`0x4663a08 - 0x4653a00 = 0x10008`, so they are laid out back to back and
neither reading of the convention puts one over `0x45f3964`. The unverified
`stack_top = r7` assumption in `cafeos_coreinit_thread.h` is still unverified,
but it is not this bug.

## What the ownership record shows instead

```
OWNER target=0x45f35dc
  [0 @2042   ALLOC ret=0x45002e0 size=5242880 lr=0x2173170]   the arena
  [1 @160610 ALLOC ret=0x45f3510 size=512     lr=0x214b8a8]
  [2 @430577 ALLOC ret=0x45f35dc size=24      lr=0x21735cc]
```

* `0x45f3510` + 512 = `0x45f3710`
* `0x45f35dc` is **inside** that range, 204 bytes in

The allocator handed back a 24-byte block that sits inside a 512-byte block it
had handed back 270,000 calls earlier. Both allocations come from the same
pool through `igMemoryPool::reallocCommon`, and the ring recorded them itself.

## Which reframes the manager entirely

The manager at `0x45f3964` was never the thing being corrupted -- it is one of
several objects packed into a region the allocator is reissuing. The dumps
show the region churning through owners:

```
DUMP2 0x45f3964  frame 2940  00:045002e0 04:045002e0 ... 14:045f395c 18:00040006
                 frame 3060  00:0011ecd8 04:00800001 ... 14:045f395c 18:00040004
                 frame 3720  ZEROS
DUMP1 0x45f35dc  frame 3720  00:001403bc 04:00800001 08:000000bd 0c:00000008
                             10:00000020 14:045f37dc ...
```

The list at `0x45f35dc` is constructed in the **same frame** the manager is
zeroed. Two objects, one region.

That also explains why `store1` on `0x45f3964` sees the write arrive with
`lr=0x215dd0c` -- inside `igDataList::resizeAndSetCount`, whose only non-stack
store is `stw r31, 8(r30)`. With `r30 = 0x45f395c` that lands exactly on
`0x45f3964`. It is a list writing its own count field, into memory the
allocator had already given to the manager.

## The one thing still to establish

Whether `0x45f3510` was **freed** between call 160,610 and call 430,577.

* freed -> the overlap is a legal reuse, and the bug is that the manager was
  built in memory belonging to a block that was about to be released
* not freed -> the allocator reissued live memory, and the bug is in
  `reallocCommon` itself

The previous ring could not answer this. Matching a free on `ptr == target`
only sees the block's own address, and the block that matters here is the
*enclosing* one. Matching any free in the same 4 KB flooded all 16 slots with
neighbours before the interesting call, which is what the tightening fixed and
then over-fixed.

The matcher now also records a free that releases a block the ring has already
seen allocated -- precisely the set whose lifetimes decide this -- without
reopening the flood.

Build `Sep  6 2026 22:33:06`, md5 `65b46b3b485290389476e4042a62e8e0`, deployed
and hash-verified. `watch.cfg` unchanged: `owner=0x45f35dc`.
