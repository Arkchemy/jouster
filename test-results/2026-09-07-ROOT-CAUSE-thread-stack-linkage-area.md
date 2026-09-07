# Root cause: a worker thread's prologue wrote 4 bytes past its stack

Found 2026-09-07 from build `Sep  7 2026 23:00:39`. **Fix built as
`Sep  7 2026 23:17:20`, md5 `0d9914ea601d75dc3a63d99e0aa0ae56`, not yet
verified on hardware.**

## The measurement that settled it

```
ALLOCRACE peak=1 overlaps=0 threads=1 [t0=0xb9605fe0]

WRITELOG n=7
 [0 @204257 val=0x3ac8d4 lr=0x21f07a4 thr=0xb9605fe0]
 [1 @204257 val=0x3ac8d5 lr=0x21f07a4 thr=0xb9605fe0]
 [2 @204257 val=0x3ac8d7 lr=0x21f07a4 thr=0xb9605fe0]
 [3 @204257 val=0x3ac8d5 lr=0x21f07a4 thr=0xb9605fe0]
 [4 @204267 val=0x0      lr=0x0       thr=0x40ff9d00]   <-- the corruption
 [5 @204392 val=0x0      lr=0x21f07a4 thr=0xb9605fe0]
 [6 @204399 val=0x10000  lr=0x217cc58 thr=0xb9605fe0]
```

`ALLOCRACE` killed the race theory outright -- only ever **one** thread inside
`tlsf_*`. But the per-thread context pointer added to `WRITELOG` shows entry 4
comes from a **different thread** that never went through the allocator at all.

## The mechanism, exactly

`jqWorkerThread`'s prologue:

```
21db584: mflr r0            ; r0 = lr = 0, nothing has called this thread
21db588: stwu r1, -0x10(r1)
21db59c: stw  r0, 0x14(r1)  ; = old_r1 + 4
```

`stw r0, 0x14(r1)` after `stwu r1, -0x10(r1)` writes at **old_r1 + 4** -- above
the incoming stack pointer, in the *caller's* frame. That is the PowerPC EABI
linkage area, and it is the caller's job to provide it. At thread start there is
no caller, so the OS must, and real `OSCreateThread` does.

Our shim did not:

```c
ctx.r[1] = te->stack_top;      /* the raw stack argument, unadjusted */
```

The worker's stack buffer is `0x4643a00..0x4653a00` -- HEAPTRAIL's block
`0x46439f8 size=0x10004` ends at exactly `0x4653a00`. With `r1 = 0x4653a00`,
that prologue store landed at `0x4653a04`: **four bytes past the end of the
buffer, on the size word of the free TLSF block that began there.** And because
`lr` is 0 on a fresh thread, the value written was zero.

Everything since has been downstream of that one store:

1. the free block at `0x4653a00` now claims size 0
2. `tlsf_memalign` finds it, and its split guard `blockSize >= gap + 16` fails
3. so the leading split is skipped and it returns `block + 8` -- unaligned
4. `tlsf_block_size` reads the zero and returns 0
5. `mallocInternal` writes its trailing size marker at `ptr + 0 - 4`, the header
   itself, stamping `0x10000` over it
6. the chain now ends there, orphaning 3,786,964 bytes of a 5 MB arena
7. later allocations fail, LZMA cannot get its 15,980-byte probability array,
   block 1 of 35 never decompresses, the archive never drains, boot stalls

## Why every earlier probe missed it

* **The allocator was never at fault**, so probes aimed at it found nothing.
  The captured 804-call trace replays correctly on x86-64 *and* on ARM64.
* **It is not a race**, though it looked like one -- a single stray write from
  a second thread, not concurrent access. `ALLOCRACE` was built to test that
  and disproved it.
* **`pc` could not name it.** `g_ppc_current_pc` is one global shared by every
  thread. Only the per-thread context pointer distinguished the writer.
* **The store watch reported `hits=7` but only the last write's details**, and
  the last write was the size marker -- a consequence. `WRITELOG` keeping all
  seven is what exposed the thread mismatch.

## The fix

`arkchemy_thread_trampoline` now reserves the linkage area and terminates the
back chain, as the real OS does:

```c
sp = (te->stack_top - 16u) & ~0xFu;
ppc_store_u32(&ctx, sp, 0u);   /* back chain terminator */
ctx.r[1] = sp;
```

Sixteen bytes rather than the ABI minimum of eight: the stack wants 8-byte
alignment and some prologues assume 16, and the cost is 16 bytes per thread.

Audited the other stack setup: the main thread uses `PPC_MEM_SIZE - 256`, which
has ample headroom and is unaffected. `arkchemy_thread_trampoline` was the only
site.

## Not yet verified

The fix is built and on the Switch but has not been run. What to check next:

* `WRITELOG` should show entries 0-3 and then **nothing** -- no zero at
  `0x4653a04`
* `HEAPWALK` should reach the sentinel at `0x4a002d8` with free space, rather
  than stopping at `0x4663a04` with `free=0`
* `SPANWATCH` should report no drop for `ctrl=0x45002e0`
* `SPLITVERIFY` should report `n=0`
* and then the real question: whether block 1 of 35 decompresses and the
  archive drains, which is the first milestone actually beyond this bug

If the arena stays whole but boot still stalls, the stall has simply moved on
to whatever is next, and that is progress rather than a failed fix.
