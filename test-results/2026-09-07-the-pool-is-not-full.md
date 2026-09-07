# The pool is 28% full and refusing 64 bytes

2026-09-07. No new hardware run: everything below comes from reading the
binary and re-reading `DUMP1` in the log of build `Sep  7 2026 01:56:17`,
which had already dumped the pool without my noticing what the words were.

## Reading the allocator first

`igMemoryPool::reallocCommon` (0x216f89c, 1232 bytes) decides everything:

* `[pool+0x08]` gates the whole function -- zero returns NULL immediately
* `size == 0` is the **free** path (0x216f964), a separate tail
* allocation starts at 0x216fa98 and fails if the alignment is zero or not a
  power of two, then clamps it up to `[pool+0x18]`
* the real work is virtual: `vtable+0xf4` `reallocInternal` for a grow,
  `vtable+0x104` `mallocInternalUntracked` for a fresh block
* both failure branches land on a shared tail that calls `vtable+0xcc`
  `getLargestFreeBlockSize` before returning

That last point is what makes the next probe free -- the engine already
computes "do you have room?" on the way out of a refusal.

## Two corrections to what I wrote yesterday

**`[pool+0x50]` is not a high-water mark.** The tail writes it only when the
new value is *lower* (0x216fd40 `bge` skips the store), so it is
`_largestFreeBlockSizeMinimum`, a low-water mark. Reading 0 after 288
successful allocations is not the anomaly I called it.

**And it is fed by a virtual that is usually a constant.**
`igMemoryPool::getLargestFreeBlockSize` (0x21733fc) is two instructions:

```
21733fc: li  r3, 0
2173400: blr
```

Only `igHeapMemoryPool` overrides it. So for any other pool class that field
is 0 by construction. My "should not still be zero" was an observation about
nothing.

## The offsets, proven rather than assumed

`igMemoryPool::updateStatistics` (0x2173648) writes them itself:

```
+0x2c blocksAllocated      +0x30 peakBlocksAllocated
+0x34 userAllocated        +0x38 peakUserAllocated
+0x3c totalAllocated       +0x40 peakTotalAllocated
```

each peak maintained as `max`. These match igRewrite8's field order, but they
are taken from this binary, not from it -- the point of failure last time was
trusting a later game's numbering.

## The pool is an igHeapMemoryPool, and it is not full

`DUMP1 0x4500274` gives `+0x00 = 0x0010b55c` as the vtable.
`find_synth_addr` maps that synthetic address to real `0x100541dc`, which is
exactly the vtable the symbol table gives for `igHeapMemoryPool` (its
`+0xf4` slot is `reallocInternal__Q2_4Core16igHeapMemoryPool`). Two
independent routes, same answer.

So `getLargestFreeBlockSize` on this pool is real, and the accounting reads:

```
_size            0x00500000   5,242,880     the 5 MB arena
userAllocated    0x0016256d   1,451,373     27.7% of it
peakUserAllocated 0x00162659  1,451,609     it was never fuller than this
blocksAllocated  0x0000010b         267     live blocks
```

**3.79 MB free, and it refuses a 64-byte grow.** The peak matters more than
the snapshot: the pool never rose above 28% at any point in the run, so
"exhausted" is not available as an explanation, then or now.

## What that leaves

An `igHeapMemoryPool` is TLSF-backed -- `_address` (`+0x10`) is
`0x045002e0`, a `control_t` whose `block_null` is self-linked at `+8/+0x0c`
exactly as `tlsf_create` leaves it. Its `fl_bitmap` was read as **0** in an
earlier session and dismissed with "allocations out of that pool are
succeeding, so the zero bitmap is not the stall. Do not chase it."

That dismissal does not survive the timeline. `fl_bitmap == 0` means no free
block is indexed in any size class, so nothing can be served. It is
consistent with the successes *and* the refusals if the bitmap became zero
partway through: 288 allocations succeed, the free lists are lost, the
remaining 301 fail. The refusals do cluster late (@433173 onward against
successes from @110636).

Note the ordering, because it rules out the obvious circular story: the first
refusal is at call 433,173 and the list overrun that lands on `0x45f3964`
is at call 440,612. **The heap was already refusing before the overrun**, so
the corruption downstream of the refusal cannot be its cause. Something
breaks this heap earlier.

The leading suspect is the 350 MB request at call 159,235 -- well before both.
A TLSF size-to-index mapping is only valid up to the heap's maximum block
size; a request far above it indexes outside `sl_bitmap`/`blocks` unless the
implementation guards for it, and what sits beyond those arrays is the rest
of the control block. That is a hypothesis, not a finding.

## The probe

Build `Sep  7 2026 04:52:29`, md5 `02d1b90e76f08419910af158962ec4f5`, on the
Switch. POOLWHY hooks the shared failure tail at 0x216fd38, where `r31 == 0`
identifies a refusal and `r3` is the largest free block the pool just
reported. It adds no call of its own. Per refusing pool it records the class
(from `vtable+0xf4`, real .text addresses, unlike the relocated vtable
pointer), the largest free block, bytes used against arena size, live blocks,
and the control block's `+0x08` self-link and `+0x10` fl_bitmap.

`watch.cfg` watches `0x45002f0` (the fl_bitmap) for stores and dumps the
control block, so the run says both *that* the bitmap goes to zero and *which
pc* wrote it.

Predicted, so the run can falsify it: `free=0..0`, `fl=0x0`, `used` around
1.4 M against `_size` 5,242,880, class `igHeapMemoryPool`. If instead
`free` comes back large while allocations still fail, the fault is above the
heap, in `reallocInternal`'s own size handling, and the next place to look is
the `getMemorySize`/rounding at 0x217cd20.
