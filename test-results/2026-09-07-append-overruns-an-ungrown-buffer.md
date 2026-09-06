# The manager is overwritten by igObjectList::append writing past its buffer

2026-09-07. This is the mechanism, proven from the register capture, and it
makes the manager zeroing a **symptom** rather than the cause.

## The instruction

`storewatch` on `0x45f3964` with the full register set:

```
value = 0x0    r30 = 0x0     <- matches, so the store's source is r30
lr    = 0x215dd0c            r29 = 0x45f35dc    r31 = 0x62 (98)
r1    = 0x3ffff948           r0  = 0x215dd0c
```

`lr = 0x215dd0c` is the return address into `igObjectList::append`, and LR is
**not restored by the caller**, so code running in `append` *after*
`resizeAndSetCount` returns still carries it. That is where the store is --
not inside `resizeAndSetCount`, which is what an earlier note assumed.

```
215dd0c: lwz  r10, 0x14(r29)    ; r10 = list->data  = 0x45f37dc
215dd10: slwi r9,  r31, 2       ; r9  = 98 * 4      = 392
215dd14: stwx r30, r9, r10      ; data[98] = r30
```

`0x45f37dc + 392 = 0x45f3964` -- **exactly the frame manager**, and `r30 = 0`
is the zero that lands there. Element 98 of the list is written on top of it.

## Why the buffer was never grown

`igDataList::setCapacity` gets its pool from a lookup and does not check it:

```
215dbbc: lwz   r3, [pool global]
215dbc0: bl    getMemoryPoolByIndex
215dbc4: mr    r6, r3               ; r6 = pool, NULL included
215dbd4: bl    reallocateFieldMemory(metafield, obj, count, r6)
215dbd8: cmpwi r3, 1
215dbdc: bne   0x215dbe4            ; on failure, skip the reload
215dbe0: lwz   r30, 0xc(r29)        ; success only
215dbe4: stw   r30, 0xc(r29)        ; capacity stored EITHER WAY
```

So a failed reallocation still writes the capacity, and `resizeAndSetCount`
then writes the count. The list believes it holds 99 elements in a buffer that
was never enlarged, and `append` walks off the end.

The log has been reporting this the whole time and it was never followed up:

```
mem: fail=1 free=1839 reuse=1714
```

**One** allocation failure across 4.4 million calls.

## The corrected causal chain

1. one allocation fails
2. `reallocateFieldMemory` returns non-1, so the buffer is not grown
3. capacity and count are stored regardless
4. `append` writes element 98 out of bounds, onto `0x45f3964`
5. the frame manager is zeroed
6. every later pool lookup through it returns NULL
7. LZMA cannot allocate its probability array, and the archive never drains

Steps 5-7 are what four sessions were spent investigating. They are
downstream. **The root cause is step 1.**

This also explains, at last, why the manager dumps looked the way they did:
populated at frame 3000, zeroed at 3660. It was correct until something else
overran it.

## What this retires

* the manager is not corrupted by an allocator bug -- `OWNER` was right
* it is not overrun by a `memmove` -- `WIPE` was right
* it is not freed -- `RELWINDOW` was right
* no list *allocation* overlaps it -- `OVERLAP` was right, the write is simply
  past the end of a block that was never resized

Every probe was correct. They were all looking downstream.

## Queued, no rebuild

```
owner=0x45f37dc    the list buffer -- what size was it actually allocated with?
store1=0x45f3964   keep the overrun capture
store2=0x45f35e8   [list+0xc], the capacity field
dump1=0x45f35dc    the list header: count at +0x08, capacity at +0x0c
dump2=0x45f3964
```

If the buffer's allocation ends before `0x45f3964`, the overrun is confirmed
outright, and the remaining question is the single failed allocation at
step 1 -- why it failed, and what it was for.
