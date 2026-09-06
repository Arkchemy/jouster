# The manager is never freed and never reissued: the write is out of bounds

Run: build `Sep  6 2026 21:50:26`, OWNER hooked into
`igMemoryPool::reallocCommon` with the free matcher tightened.

## The complete ownership timeline of 0x45f3964

```
OWNER target=0x45f3964 n=2
  [0 @2042   ALLOC ret=0x45002e0 size=5242880 lr=0x2173170]   the pool's arena
  [1 @261936 ALLOC ret=0x45f3964 size=20      lr=0x21735cc]   the manager
```

Two entries. That is the whole history of that memory across 4.4 million
calls:

* the pool's 5 MB arena is allocated at call 2,042
* a **20-byte** block at `0x45f3964` is allocated at call 261,936, immediately
  before `setDefaultFrame` builds the manager into it at 262,720
* **no free**, ever
* **no second allocation** covering it, ever

## Which kills all three remaining theories

| theory | needed | seen |
|---|---|---|
| double allocation | two covering ALLOCs | one |
| pool rewind and reuse | a second ALLOC after the rewind | none |
| legitimate free, missing re-registration | a FREE of the block | none |

Nobody but the manager ever owns this memory. Yet at call 440,609 three words
of zero are written straight through it from `lr=0x215dd0c`, the instruction
after `igObjectList::append` calls `igDataList::resizeAndSetCount`, with the
buffer base in `r29=0x45f35dc`.

**So the write is simply out of bounds.** The list being resized owns a
different block; its zero-fill runs past the end of that block and into the
manager's 20 bytes. `OVERLAP` agreeing there is no overlap is consistent with
this rather than against it: the list's *allocation* genuinely does not cover
`0x45f3964`, which is exactly what makes the write an overrun.

The manager sits `0x45f3964 - 0x45f35dc = 0x388` = **904 bytes** past the
buffer base.

## Why the earlier probes all missed it

Each was looking for a different bug:

* `WIPE` watched `memmove`-style moves. This is a direct store, not a move.
* `RELWINDOW` watched releases. There is no release.
* `OVERLAP` watched allocations overlapping a live block. There is no second
  allocation.
* the first `OWNER` hooked `igMemory::mallocAligned`, which serves almost
  nothing here -- 3 calls against 1,839 frees.

All four were sound tests of hypotheses that were all wrong. The evidence only
became legible once the ownership history was complete enough to show that
*nothing happens to this memory at all*.

## Queued next: how big is the buffer that overran?

`watch.cfg` only, no rebuild, using the `owner=` knob added for exactly this:

```
owner=0x45f35dc    the ALLOC that produced the list buffer, with its size
store1=0x45f3964   the zeroing, to keep confirming the call number
store2=0x4400188   context+0x18, in case the fallback is ever repointed
dump1=0x45f35dc    the buffer
dump2=0x45f3964    the manager
```

If that allocation's `base + size` does not reach `0x45f3964`, the overrun is
proven outright and the bug is in the zero-fill's bound -- a wrong count or a
wrong element size in the recompiled `setCount`, which is a class of error the
recompiler can introduce (a mis-emitted `lha` sign-extension has already been
found once in this project).

`r31 = 0x62` = 98 at the moment of the write, which is a plausible element
count; 904 bytes at 4 bytes each would be element 226. If the buffer really
holds 98 elements, the loop is running roughly twice too far.
