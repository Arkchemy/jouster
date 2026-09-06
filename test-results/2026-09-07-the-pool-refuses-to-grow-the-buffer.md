# Root cause: the pool refuses small allocations, so the list never grows

2026-09-07, build `Sep  7 2026 00:25:12`. The chain is complete and every link
is measured.

## The buffer never grew

`owner=0x45f37dc` gives the list buffer's whole history. Entries matched on the
`ptr` argument are **reallocs**, not frees -- the reporter's label is wrong:

```
@432347  realloc(ptr=0x45f3654, size=32)   -> 0x45f37dc    32 bytes
@433173  realloc(ptr=0x45f37dc, size=64)   -> 0            REFUSED   x8
@433781  realloc(ptr=0x45f37dc, size=128)  -> 0            REFUSED   x12
```

The buffer is **32 bytes**, room for 8 elements. Twenty attempts to grow it to
64 and then 128 bytes were all refused. It never moved and never grew.

Meanwhile the list's count climbed to 98, and `igObjectList::append` wrote
element 98 at `buffer + 98*4` = `0x45f37dc + 392` = **`0x45f3964`** -- the
default pool-frame manager, 360 bytes past the end of a 32-byte block.

## Why the count climbed anyway

`igDataList::setCapacity` stores the capacity whether or not the grow
succeeded:

```
215dbd4: bl    reallocateFieldMemory
215dbd8: cmpwi r3, 1
215dbdc: bne   0x215dbe4        ; failure skips only the RELOAD
215dbe0: lwz   r30, 0xc(r29)    ; success path
215dbe4: stw   r30, 0xc(r29)    ; capacity written either way
```

`resizeAndSetCount` then writes the count. Nothing propagates the failure, so
the list believes it holds 99 elements in a buffer sized for 8.

## The complete chain

1. a 64-byte grow is refused by pool `0x4500274`
2. `reallocateFieldMemory` returns non-1, buffer unchanged at 32 bytes
3. capacity and count are stored regardless
4. `append` writes element 98 at `+392`, outside the block
5. that lands on `0x45f3964`, zeroing the frame manager
6. every later pool lookup through it returns NULL
7. LZMA cannot allocate its 15,980-byte probability array
8. block 1 of 35 never decompresses, the archive never drains, boot stalls

Steps 5 through 8 are what four sessions investigated. They are all downstream.

## The remaining question, and a second bug beside it

**Why does a 64-byte allocation fail in a 5 MB pool?** 301 of 589 allocations
from it are refused. At each refusal `[pool+0x18] = 4` and `[pool+0x50] = 0`,
the latter being a high-water mark that should not still be zero after 288
successes.

Separately, `ALLOCFAIL` caught six *fresh* allocations with impossible sizes
from the same caller, `igMemoryPool::allocatePoolMemory`:

```
@159235  size = 367,001,600   (350 MB)
@219696  size =  90,771,456   ( 87 MB)
@224450  size = 163,577,856   (156 MB)
@229200  size =  46,153,728   ( 44 MB)
@233950  size =  46,153,728   ( 44 MB)
@238664  size =     393,216   (384 KB)
```

Into a 5 MB arena. Those sizes are not plausible requests and point at a size
computation going wrong upstream -- `setCapacity` computes
`(capacity * elemSize) / virtualCall()`, which is exactly the shape that
produces garbage if any term is wrong.

Whether the huge requests and the refused 64-byte grows are the same bug or
two is not yet established.

## Note on the reporter

`OWNER` labels any entry matched on the `ptr` argument as `FREE`. That is
wrong -- `reallocCommon` takes `ptr` for reallocation too, and the entries
above with `size=64` and `size=128` are grows, not frees. The label should
distinguish `size == 0` (free) from `size != 0` (realloc).
