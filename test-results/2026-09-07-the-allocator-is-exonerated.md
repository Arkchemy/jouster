# The ARM64 replay clears the allocator entirely

Run of 2026-09-07, build `Sep  7 2026 22:33:35`.

```
REPLAY ctrl=0x45002e0 entries=804 create->0x45002e0
REPLAY #216 DIVERGED op=1 a=0x40 b=65540 arm64->0x4653a40 recorded->0x4653a08
```

Replaying the captured 804-call sequence **on the Switch, on ARM64, through the
same recompiled allocator** returns `0x4653a40` -- identical to the x86-64 host,
and correctly 64-byte aligned. The live run returned `0x4653a08`.

So this is **not** an ARM64/x86-64 codegen difference, which was the leading
theory an hour ago. Same code, same architecture, same 804 inputs, correct
result. The allocator is clear on both platforms.

What is left is the outcome I listed third: **something outside
`tlsf_memalign`, `tlsf_free` and `tlsf_realloc` modifies the arena during the
real boot.** The replay does not reproduce it because the replay does not run
that something.

## The disassembly says what the damage does

`tlsf_memalign`'s leading split is guarded:

```
21f0800: addi r0, r9, 0x10     ; gap + 16, and gap here is 56, so 72
21f0804: rlwinm r7, r8, 0,0,0x1d ; blockSize
21f0808: cmplw r7, r0
21f080c: mr   r4, r12          ; r4 = the ORIGINAL block
21f0810: blt  0x21f0934        ; blockSize < 72 -> skip the split, keep r4
```

Skipping the split returns `block + 8` unchanged -- `0x4653a08`, unaligned.

A block being asked to hold 65,540 bytes cannot legitimately have a size below
72. So the block's size word was **already wrong before call #216**, which is
exactly what `SPLITVERIFY` reported for that same return: `bsz=0x0`.

Every symptom follows from that one corrupted word, in order: the size word is
zeroed, `tlsf_memalign` skips its leading split and hands back an unaligned
pointer into a zero-size block, `tlsf_block_size` returns 0, and
`mallocInternal` then writes its trailing size marker at `ptr - 4` -- the header
itself -- stamping `0x10000` over it. The tail of the arena is orphaned because
the chain now ends there.

## What the previous probes could not see

The store watch counted **seven** writes to `0x4653a04` but the loopwatch slots
keep only the most recent value, pc and lr. The last write is the size marker,
which is a consequence. The write that first zeroed the word is one of the
earlier six, and nothing has recorded it.

`BULKWRITE seen=0` already rules out `memset` and `memcpy` for that address, so
it is an ordinary instruction store from code that is not the allocator.

## Next

Build `Sep  7 2026 22:46:02`, md5 `4492366e96aef3c0a8e3f3e1643b631f`, is on the
Switch with `replay=0` and `store1=0x4653a04` still set.

**WRITELOG** records every write to the store-watch address in order -- value,
pc, lr and call count, up to twelve -- rather than only the last. `pc` is the
last function *entered*, so for a store made after a call returns it names the
callee rather than the writer; `lr` is the one to trust, exactly as it was `lr`
that identified `mallocInternal` earlier.

That should name the code that zeroes the header outright, and with the
allocator now excluded on two architectures, it is the only place left to look.
