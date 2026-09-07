# HEAPWALK: the block chain ends 1.45 MB into a 5 MB arena

Run of 2026-09-07, build `Sep  7 2026 05:18:18`.

```
HEAPWALK blocks=211 used=211/1451880b free=0/0b largest=0
         range=0x4500f50..0x4a002e0 stoppedAt=0x4663a04 status=1
```

## The walk checks out

Span minus used bytes is `1,452,724 - 1,451,880 = 844`, and `211 x 4 = 844`
exactly -- one 4-byte header per block, which is what `tlsf_walk_heap`'s
`next = block + 4 + size` implies. The walker is reading the chain correctly,
so its answer can be trusted.

## Neither hypothesis survives as stated

I had framed this as "free bytes near 3.8 MB means corruption, near zero means
the heap is really full". It came back **free = 0 blocks, 0 bytes** -- but not
because the arena is full. The chain simply *ends*:

```
arena declared   0x4500f50 .. 0x4a002e0    5,239,696 bytes
chain covers     0x4500f50 .. 0x4663a04    1,452,724 bytes
never walked     0x4663a04 .. 0x4a002e0    3,786,972 bytes
```

So the heap is entirely used up across the region it covers, and **3.79 MB
beyond the stopping point is not in the chain at all**. That is consistent
with every other reading: `fl_bitmap` 0, largest free block 0..21 bytes, 494
refusals. There is genuinely nothing left to hand out -- the question is why
the heap only ever covered 28% of what `tlsf_create` was given.

## The one word that decides it

The walk stops on `(word & ~3) == 0`. That test cannot tell two very
different things apart:

* `tlsf_create`'s sentinel -- size 0 with the used and prev_free bits, so the
  word reads **2 or 3**. The heap legitimately ends here, and the bug is in
  setup: `activate` passes `_address` and `_size` to `tlsf_create`, which
  should mean 5 MB, so something is shortening it.
* a **zeroed header** -- the word reads **0**. The chain has been cut, the
  free block that lived at the allocation frontier is gone, and the bug is a
  stray write.

Both produce exactly the output above. The address alone will not separate
them either, so the next build records the raw words rather than watching a
fixed address that may move between runs.

Note also `blocksAllocated` 267 against 211 walked, and `userAllocated`
1,451,373 against 1,451,880 walked. The byte totals agree to 507 bytes, which
is ordinary alignment padding, but 56 blocks are unaccounted for. If the chain
were cut rather than ended, blocks past the cut would be exactly what the walk
cannot see.

## Next

Build `Sep  7 2026 13:09:21`, md5 `62f80c4df62cc7388a27f1930b107410`, is on
the Switch. HEAPSTOP reports the four header words at the stopping point plus
the previous block and its size word. `watch.cfg` also watches `0x4663a08`
for stores and dumps `0x4663a04`, so if the word is zero the run names the pc
that wrote it.

* word reads 2 or 3 -> read `tlsf_create` (0x21f0510) and find what shortens
  the pool below the `_size` it is handed
* word reads 0 -> the store watch has the writer, and this becomes a
  stray-write hunt like the `0x45f3964` one
