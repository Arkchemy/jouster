# Cemu: retail's identical 5 MB pool keeps its whole arena

2026-09-07, retail SSA running under Cemu 2.6 at full JIT speed, memory read
through `/proc/<pid>/mem`. No gdb stub and no `--force-interpreter` -- the stub
has cost three sessions and is not needed for reads.

`jouster/tools/cemu-pools.py` scans for `__vtbl__Q2_4Core16igHeapMemoryPool`
(`0x100541dc`, from the retail symbol table) and walks each instance's physical
chain. Pools are found by vtable, never by translating a jouster address --
those do not correspond and return convincing false zeros.

## Four real heap pools, and one of them is ours

```
pool 0x115d9284  arena 0x115d9300 size 0x100000  blocks 8287  used 586,660    free 425,576    OK
pool 0x11602044  arena 0x11dd9f30 size 0x190000  blocks   38  used 219,240    free 1,415,816  OK
pool 0x116d9314  arena 0x116d9390 size 0x500000  blocks 1220  used 2,694,424  free 2,540,384  OK
```

The third is the same size as the one that fails in jouster -- `0x500000`,
5 MB, alignment 4.

```
              blocks         used         free    used+free   chain
  retail        1220    2,694,424    2,540,384    5,234,808   reaches sentinel
  jouster        211    1,451,880            0    1,451,880   3,786,964 short

  retail accounts for 99.8% of the arena; jouster for 27.7%
```

**Retail's chain spans its whole arena and always has free memory available.**
Ours does not, and has none.

## What this does and does not prove

It does **not** show that retail runs the same allocation sequence -- retail is
fully booted with 1,220 blocks while jouster stalls early with 211, so the
counts are not comparable.

What it establishes is the **invariant**: in the real game, a heap pool's
physical chain reaches its sentinel and `used + free` accounts for essentially
the entire arena. That is true of all three real pools here, at three different
sizes. So the allocator design is sound and the recompiled one diverges from
it. This is not a case of the engine legitimately abandoning part of an arena.

It also independently confirms the control-block size: retail's first block
header sits at `_address + 0xc70` exactly, the same offset taken from
`tlsf_walk_heap`, on a completely separate binary run.

`test-results/2026-09-07-retail-5mb-pool-profile.txt` has retail's first 40
block sizes as a reference profile to compare a jouster trace against.

## One false positive, deliberately not hidden

The scan also matched `0x115e03c4`, reporting `arena 0x00260060 size 0x1070004
align 0` with `userAllocated` of 291 MB against a 17 MB size. Alignment zero
and used-exceeds-size are both impossible for a live pool, so this is a stray
word matching the vtable value rather than a real object. The tool prints it
with its failed walk instead of filtering it, because a scanner that silently
drops what it cannot explain is how a real pool would get missed too.

## Next

Retail's behaviour is the oracle; the remaining work is on the jouster side.
The `TLSFTRACE` build (`Sep  7 2026 21:55:07`,
md5 `8d81cf5b084e846ee6d992c1f01fc87c`) is on the Switch with
`trace=0x45002e0` set, and `conquertron/hosttest/tlsf_replay.c` will replay the
captured sequence against the recompiled allocator here.
