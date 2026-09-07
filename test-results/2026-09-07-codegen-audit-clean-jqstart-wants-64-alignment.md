# The caller wants 64-byte alignment, and the codegen audit comes back clean

2026-09-07, static work while the SPLITVERIFY build waits to run.

## What jqStart actually asks for

`Core::jqStart+0x19c` is the call HEAPSPAN caught losing the tail:

```
21db71c: lwz r3, -0x2a30(r30)   ; the pool
21db720: lis r4, 1              ; size  = 0x10000 = 65536
21db724: li  r5, 0x40           ; align = 64
21db72c: bl  0x216fd6c          ; igMemoryPool::mallocAligned
```

So it is a **64-byte-aligned** 64 KB allocation, not a plain malloc. That
explains the size anomaly noted last time: the other 64 KB blocks in HEAPTRAIL
carry block size `0x10004`, this one `0x10000`, because it went through
`tlsf_memalign`'s alignment path rather than the ordinary one.

## The codegen audit is clean

Since the retail game boots, an orphaned block points at a translation bug, so
every unusual instruction in `tlsf_memalign` was checked against PowerPC
semantics rather than assumed:

| instruction | risk | conquertron emits | verdict |
|---|---|---|---|
| `rlwinm r5,r5,0,0x1f,0x1d` | MB > ME, mask wraps | `& 0xFFFFFFFD` | correct |
| `rlwinm r0,r0,0,0,0x1e` | mask to bit 30 | `& 0xFFFFFFFE` | correct |
| `lwzu r0, 4(r11)` | base register updates | loads `r11+4`, then sets `r11` | correct |
| `srawi` + `addze` | signed /4 via `XER[CA]` | `ppc_srawi` sets `xer_ca`, `ppc_addze` consumes it | correct |
| `slw r10,r10,r29` | PPC uses 6 bits, 0 for >= 32 | `(sh & 0x20) ? 0 : (v << (sh & 0x1F))` | correct |
| `cntlzw` | zero input | returns 32 | correct |
| `andc`, `nor` | mask construction | `& ~x`, `~(x\|x)` | correct |

`ppc_srawi` even carries a comment recording that its carry was once missing
and was fixed, which is the same bug class, already caught.

So the arithmetic in this function is not where the tail goes missing. That is
a useful negative: it moves attention from the translation to the logic and
the data flowing through it.

## A discrepancy the reading cannot resolve

The block whose size became `0x10000` is at header `0x4653a00`, so its user
pointer is `0x4653a08`.

```
0x4653a08 mod 64 = 8      -- not 64-byte aligned
align_ptr would give 0x4653a40, a gap of 56
```

A gap of 56 is well past the 16-byte minimum at `0x21f07cc`, so the leading
split at `0x21f07fc` should have run and added a block. But HEAPSPAN measured
**204 blocks before and 204 after**.

Those two facts do not fit together. Either the returned block is not the one
the walk ends on, or the alignment was not honoured, or the leading split ran
without producing the block the count would imply. Reading further has not
separated them, and this session has already had to withdraw two inferences
drawn that way -- the `[pool+0x50]` high-water claim and the reading of
`hits=0` as "never written".

## Waiting on

Build `Sep  7 2026 16:14:13`, md5 `eb608afd882de6fc8ef1637fa9381dd5`, is on
the Switch. SPLITVERIFY records the returned pointer, the requested size and
alignment, the block it came from, and its successor, for every allocation
whose block ends up with no physical successor. That answers all three
possibilities directly:

* if `ret` is 64-byte aligned and far from `0x4653a08`, the walk ends on a
  different block than assumed
* if `ret` is `0x4653a08`, the alignment was not honoured and the leading
  split is where to look
* the recorded `size` and `align` confirm whether the alignment path ran at all
