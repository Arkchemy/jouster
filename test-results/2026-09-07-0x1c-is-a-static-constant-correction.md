# Correction: 0x1C is a static constant, not an unresolved igz pointer

**This supersedes `2026-09-06-igz-pointer-fixup-not-applied.md`, which was
wrong.** That file concluded the boot fails because an igz pointer was never
fixed up. It was not.

## What the store watch caught

Watching the pool-index global at `0x66eec` (421612):

```
storewatch_value  = 0x1c    hits=1 @call 241
storewatch_lr     = 0x0                 no return address yet
storewatch_r1     = 0x3fffff00          the INITIAL stack pointer
writer_pc         = 0x2183b38
```

One write, at call **241**, with `r1` still at `PPC_MEM_SIZE - 256` and no LR.
That is static initialisation, before anything has pushed a frame.

`0x2183b38` is `__sti__26_igCoreMetaSource_cpp_3_cpp` -- a C++ static
initialiser. It fills a table with consecutive ordinals:

```
[421500] = 0    [421504] = 1    [421508] = 2 ...
```

and `(421612 - 421500) / 4 = 28`, so `[421612] = 28`. **The global is
supposed to hold 28.** It is a compile-time constant, correct on arrival, and
never touched again.

## So the earlier reasoning was wrong

bone's point about `0000001C` being a packed igz pointer (pool 0, offset 0x1C)
is true of igz pointers. Our value is not one -- it is a static enum constant
that happens to have the same numeric value. Reading across from his
observation to our global was the mistake, and it is the *same* mistake as
comparing against `EMemoryPoolID` earlier: seeing a small integer and
assuming which space it belongs to.

The igz format work stands on its own and is verified (`blaster/IGZ.md`).
`IGZFIXUP n=0` is also still true. But the fixup pass not running is a
consequence of the boot dying earlier, not its cause.

## Which puts the original diagnosis back

Pool 28 is legitimate, and it **is** registered:

```
POOLREG   registrations=52 maxIndex=44 got28=2
POOL28    mgr=0x4502fb0 frame=1 topFrameCount=0x80 slot28=0x1
```

But the manager the LZMA path walks is empty:

```
LZWALK    mgr=0x45f3964 frames=1 maxCount=0x0 slot28=0
POOLDEST  [mgr=0x4500f58 n=7] [mgr=0x4502fb0 n=45]
          context=0x4400170  fallbackMgr(+0x18)=0x45f3964
```

52 registrations went to `0x4500f58` and `0x4502fb0`. The fallback manager
`0x45f3964` received **none**, and that is the one workers reach because their
TLS slot 0 is NULL.

So the question is the one from the start of the investigation, now with
everything else eliminated: **why do the registrations land in one manager
while the consumers read another?**

## What is genuinely settled

* the value 28 is correct and static -- stop looking at it
* pool 28 exists and is registered -- stop looking for a missing registration
* nothing is corrupted -- five probes, all correctly null
* no allocator bug -- alloc/free/alloc is clean and ordered
* not a stack collision -- r1 is at the top of memory
* not a bootstrap teardown -- no teardown ever runs
* not an igz fixup failure -- that is downstream

What is left is the manager mismatch, and it has been the answer since the
first log.
