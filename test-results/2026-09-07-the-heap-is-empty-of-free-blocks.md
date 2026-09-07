# POOLWHY: three pools, three different failures

Run of 2026-09-07, build `Sep  7 2026 04:52:29`.

```
POOLWHY n=3
 [0x44001a8 igCafeSystemMemoryPool refused=1   small=0   free=0..0  used=188773012/482344444 blocks=15  ctrl+8=0x0        fl=0x0 last=367001600]
 [0xf80788c igBlockMemoryPool      refused=13  small=4   free=0..0  used=0/0                 blocks=0   ctrl+8=0x0        fl=0x0 last=80]
 [0x4500274 igHeapMemoryPool       refused=494 small=353 free=0..21 used=1451373/5242880     blocks=267 ctrl+8=0x45002e0  fl=0x0 last=124]
```

Every prediction from the last note held: the class is `igHeapMemoryPool`,
`used` is 1,451,373 of 5,242,880, `blocks` is 267, and the control block's
`+0x08` reads back as `0x45002e0` -- the `block_null` self-link `tlsf_create`
writes, which confirms the layout and makes `+0x10` the `fl_bitmap`.

`fl_bitmap` is **0**, and the largest free block the pool reported at a
refusal never exceeded **21 bytes**.

## The other two rows correct an earlier guess

I had the 350 MB request going into the 5 MB arena. It does not.
`last=367001600` is on **`0x44001a8`, `igCafeSystemMemoryPool`** -- a 460 MB
pool with 180 MB already used. A 350 MB request on top of that is simply too
big, and it is refused once and never again. That is a plausible thing for a
Wii U title to ask for and a sane refusal, not the bug.

`0xf80788c` is an `igBlockMemoryPool` with **`_size` of 0** -- never given any
memory, so all 13 of its allocations fail. A separate, smaller problem.

That leaves `0x4500274` alone as the boot blocker, with 494 refusals of which
353 are 256 bytes or smaller.

## Not corruption, on the present evidence

The store watch names the writer of the `fl_bitmap`:

```
storewatch_writer_pc  hits=597  last=0x21effc0   -> search_suitable_block
storewatch_lr                   last=0x21f00d0   -> block_locate_free+0x80
```

Both are ordinary TLSF internals, and the bitmap is written 597 times across
the run. This is a heap in normal use whose free lists drain, not a stray
pointer scribbling on the control block. The final control dump is the
textbook empty state rather than garbage:

```
DUMP1 0x45002e0  00:0 04:0 08:045002e0 0c:045002e0 10:0 14:0 ... 3c:0
```

`block_null` self-linked, every `sl_bitmap` word zero.

The symbols settle what this allocator is, with no inference:
`tlsf_create`, `tlsf_memalign`, `tlsf_realloc`, `tlsf_free`,
`tlsf_block_size`, `tlsf_largest_free_block_size`, `tlsf_walk_heap`,
`tlsf_check_heap`. There is no `tlsf_add_pool`, so this is the older
`tlsf_create(mem, bytes)` that takes the whole region -- and
`igHeapMemoryPool::activate` (0x217cad4) passes it `_address` and `_size`
directly:

```
217caf8: lwz r3, 0x10(r30)   ; _address = 0x45002e0
217cafc: lwz r4, 0x14(r30)   ; _size    = 0x500000
217cb00: bl  0x21f0510       ; tlsf_create
217cb08: stw r3, 0x54(r30)   ; heap handle
```

So the arena really is 5 MB. A 5 MB heap holds 1.45 MB of tracked live data
and has no free block larger than 21 bytes. Those cannot both be innocent.

## The discriminator, and the next build

Two readings remain, and one measurement separates them:

* **the free memory is there but unlinked** -- physically free bytes near
  3.8 MB while `fl_bitmap` is 0, which is free-list damage after all
* **the heap really is full** -- physically free bytes near zero, and
  `userAllocated` is under-reporting. `igMemoryPool` has
  `mallocUntracked`/`freeUntracked` and
  `mallocInternalUntracked`/`freeInternalUntracked`, which skip
  `updateStatistics` by design, so a caller using those would consume the
  arena without moving the counter that says 27.7%

`tlsf_walk_heap` (0x21f021c) gives the physical layout exactly, so the walk
needs no guessing:

```
21f024c: addi r30, r3, 0xc70    ; first header = control + 0xc70
21f0288: lwz  r0, 4(r30)        ; +4 is size|flags
21f028c: rlwinm. r4, r0, 0, 0, 0x1d   ; size = word & ~3, zero terminates
21f0258: clrlwi r9, r0, 0x1f    ; bit 0 = free
21f0278: add  r12, r8, r10      ; next = block + 8 + size ...
21f027c: addi r30, r12, -4      ; ... - 4
```

Build `Sep  7 2026 05:18:18`, md5 `a464aba1b12e9efa7fa9b910524bef99`, is on
the Switch. HEAPWALK walks that chain once, at the first refusal from a heap
pool, and tallies used and free blocks and bytes. It is read-only and bounded
three ways -- a 40,000 block cap, an arena range check, and the terminator --
so a damaged chain cannot spin or read outside the pool.

`watch.cfg` also dumps `0x4500f50`, the first block header, so the raw words
are there to check the walk against.

`used + free` should account for the arena. Whichever side it lands on names
the next thing to fix.
