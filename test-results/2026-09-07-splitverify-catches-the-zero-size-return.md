# SPLITVERIFY catches it, and the harness says the allocator is innocent alone

Run of 2026-09-07, build `Sep  7 2026 20:08:36`.

```
SPLITVERIFY calls=8650 n=1
 [@204396 lr=0x217cc4c size=65540 align=64 ret=0x4653a08 blk=0x4653a00 bsz=0x0 succ=0x4653a04]
```

With the predicate corrected from `__bsz &&` to `__bsz == 0 ||`, the case it
had been skipping shows up on the first run: **`tlsf_memalign` returned a
pointer whose own block size word is zero.**

Note `size=65540`, not 65536. `igHeapMemoryPool` adds 4 to the caller's request
for its trailing size marker, so the allocator sees `0x10004`. The first
version of the host harness tested 65536 and found nothing partly for that
reason.

Everything downstream now follows without any further assumption:
`mallocInternal` computes its marker address as `ptr + tlsf_block_size(ptr) - 4`,
`tlsf_block_size` reads that zero size word and returns 0, so the marker lands
at `ptr - 4` -- the block's own header -- and stamps `0x10000` over it. The
`0x10000` chased all week is a size marker in the wrong place, not a block size.

## The allocator is correct in isolation

`conquertron/hosttest` runs the recompiled `igTlsfWrapper` natively. On a fresh
5 MB arena at the real address, with the real request:

```
memalign(align=64, size=65540) -> ptr=0x04500f80  block size 0x10006
align     4 .. 4096 sweep: pointer correctly aligned every time,
                    block size 0x10004, chain reaches the sentinel, nothing orphaned
```

So `tlsf_memalign` does not mistranslate. The fault needs the specific heap
state built by the ~200 allocations before it, which no synthetic sequence is
going to reconstruct by guessing.

## Capture the real sequence instead

Build `Sep  7 2026 21:55:07`, md5 `8d81cf5b084e846ee6d992c1f01fc87c`, is on the
Switch.

**TLSFTRACE** records every `tlsf_memalign`, `tlsf_free` and `tlsf_realloc`
call on one pool -- named by `trace=0x45002e0` in `watch.cfg`, now set -- as
`{op, a, b, ret}`, and writes them to `sdmc:/switch/Jouster/tlsf-trace.bin` at
report time. Binary rather than log text: 8,192 entries is 128 KB, which would
swamp the log and be miserable to parse back. Dropped entries are counted and
reported, so an overflowing trace says so instead of silently truncating.

**`conquertron/hosttest/tlsf_replay.c`** replays that file against the
recompiled allocator on this machine, checking after *every* call both that the
return matches what the device recorded and that the chain still reaches the
sentinel. It stops at the first divergence or the first orphaning and prints
the state.

```
SRC=tlsf_replay.c sh conquertron/hosttest/run.sh /path/to/tlsf-trace.bin
```

That turns the remaining question from a ten-minute hardware round trip into a
millisecond one that can be bisected and stepped in a debugger. Two outcomes,
both useful:

* **it reproduces** -- the allocator really does diverge given this history,
  and the failing call is identified with its full prior state available
* **it does not** -- the recorded sequence is not sufficient, which means
  something outside these three functions is touching the arena, and that is a
  much narrower search than the one this started with
