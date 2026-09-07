# The replay reproduces it: same code, same input, different answer

2026-09-07. Device build `Sep  7 2026 21:55:07`; host replay via
`conquertron/hosttest`.

```
TLSFTRACE ctrl=0x45002e0 entries=804 dropped=0
  589 memalign, 194 realloc, 21 free
```

Replayed against the same recompiled allocator on this machine:

```
#216 DIVERGED  op=1 a=0x40 b=65540  host->0x04653a40  device->0x04653a08
   chain: 206 blocks, span 0x04a002d8
```

**Calls 1 through 215 return byte-identical results**, so the heap state going
into call 216 is the same on both. Then the same call --
`tlsf_memalign(align=64, size=65540)` -- returns:

* **host** `0x04653a40`, which is 64-byte aligned, chain still whole
* **device** `0x04653a08`, which is `8 mod 64`, and the tail is orphaned

The difference is exactly `0x38`, the 56-byte alignment gap. The host performs
the leading split at `0x21f07fc`; the device does not, which means it computed
`gap == 0` at `0x21f07c0` from the same pointer and the same alignment.

That also finally explains the block count. HEAPSPAN measured 204 blocks before
and after, which never fitted a leading split -- a split adds a block. It fits
perfectly if the split simply did not happen.

## What has been eliminated

* **Not the allocator's logic.** The same C, given the same 215-call history,
  gets it right here.
* **Not the missing marker write.** `mallocInternal` writes a trailing size
  marker after each allocation; the replay now does the same, and the
  divergence is unchanged.
* **Not optimisation-dependent UB.** Identical at `-O0`, `-O1`, `-O2`, `-O3`,
  and the device builds at `-O0` as well.
* **Not the engine abandoning the arena legitimately.** Retail's identically
  sized pool keeps 99.8% of its arena accounted for, measured under Cemu.

What is left is that the same generated C produces different behaviour when
compiled for ARM64 rather than x86-64.

## Testing that directly

There is no `aarch64-linux-gnu-gcc` and no qemu on this machine, only
devkitA64, which targets bare metal -- so the replay cannot be run for ARM64
here. It can be run *on the Switch*.

Build `Sep  7 2026 22:21:26`, md5 `c3dbb8177139ad35a945d1bec5c520a9`, adds
`replay=1` to `watch.cfg`. It reads `tlsf-trace.bin`, runs the same 804 calls
through the same recompiled allocator on ARM64, and reports the first call
whose result differs from the recording. It runs before `ppc_init_globals` so
the arena is pristine, and zeroes what it touched afterwards so the real boot
is unaffected.

Three outcomes, all informative:

* **diverges at #216 the same way** -- confirms an ARM64/x86-64 difference in
  the generated code, and the failing call is pinned down to a handful of
  instructions
* **no divergence at all** -- the replay is faithful and something *outside*
  these three functions modifies the arena during the real boot, which is a
  much narrower search than before
* **diverges somewhere else** -- the trace or the replay is not capturing
  everything, and that is worth knowing before drawing conclusions from it

The trace itself is committed as `2026-09-07-tlsf-trace.bin` so the host result
is reproducible without another hardware run.
