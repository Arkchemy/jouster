# deko3d names it: a GPU page fault reading address zero

Run of 2026-09-14 21:54, build `Sep 14 2026 21:43:58` — the first build linked
against debug deko3d with a working `cbDebug`.

## What deko3d said

```
[DKDEBUG] ... 'dkCmdBufBarrier': Queue (0) entered error state
[DKDEBUG] ...   timestamp: 2307610258985
[DKDEBUG] ...   info32: 31
[DKDEBUG] ...   info16: 0
[DKDEBUG] ...   status: 65535
[DKDEBUG] ...   --
[DKDEBUG] ...   GPU page fault (info 0x00000d00)
[DKDEBUG] ...   Address: 0x0000000000
[DKDEBUG] ...   Access type: Read
[DKDEBUG] deko3d raised DkResult_Fail (1) in 'dkQueueSubmitCommands':
          attempt to submit commands to a queue in error state
```

**The GPU faulted reading address 0.** The queue went into an error state, and
the next `dkQueueSubmitCommands` refused and aborted the process.

## The abort is a consequence, not the cause

`2026-09-14-the-intermittent-failure-is-a-deko3d-abort.md` left this open:

> either the submit thread died early and the main loop carried on for twelve
> thousand frames — which `svcBreak` should not permit — or the engine stalled
> early for an unrelated reason and the submit thread died much later.

**The second.** The ten `[DKDEBUG]` lines land at log line 20,027 of 20,429 —
the very end of the run, at main frame ~12,780. The engine's own stall is much
earlier and separate. The `GX2Flush` abort in the crash reports was never the
thing that stopped the engine; it is the queue refusing work it had already
given up on, some time after a GPU fault nobody could see.

That also retires the suspicion about `ImageLayout::calcLevelOffset` in the
symbolised trace. The fault has nothing to do with `GX2Flush` — that is merely
the next submit after a queue that was already dead.

## A null GPU address is bound to something

`Address: 0x0000000000`, `Access type: Read`. The GPU was told to read from a
buffer or descriptor whose address is zero. Candidates, in the order worth
checking:

- a vertex or index buffer bound with a null `DkGpuAddr`
- a uniform buffer bound at zero (`dkCmdBufBindUniformBuffer` with an unset
  address — note `mode=0` on every shader bound means uniform *registers*, so
  a mis-set one would be exactly this shape)
- a texture or sampler descriptor set whose GPU address never got filled
- a render target or depth buffer view over an image with no memory bound

`info 0x00000d00` is Nvidia's fault descriptor and narrows which engine
faulted; it has not been decoded yet.

## This run is not the usual failure

Worth flagging before anyone treats the fault as *the* explanation for the
826-call runs:

| | usual failure | this run |
| --- | --- | --- |
| GX2 calls / entry points | 826 / 56 | **997 / 71** |
| shader modules | 2 | **7** |
| draws attempted | 0 | **2** |
| main frame at the end | ~12,400 | ~12,780 |

This run got substantially further — all seven shaders, real draws — and then
died of a GPU fault. Whether the 826/2-shader runs die of the *same* fault is
not established; they were on builds whose crash reports show the same
`0x367`, but `0x367` is `DkResult_Fail` and names nothing. **One run with the
debug library is not a rate.** The tally reads `0 ok, 2 failed` so far.

## Two counting bugs found while reading this

**`drawn` was double-counted.** `ark_draw_ex` incremented `g_ark_draw_done`
twice — once inside the `ARK_REC_DRAW_DEFERRED` block and once unconditionally
below it — so every draw figure this project has recorded is exactly twice the
truth. The runs written up as **489 and 513 draws really made 245 and 257.**
This run's `tried=2 drawn=4` is what exposed it: `drawn` is a subset of `tried`
by construction and cannot exceed it. Fixed by deleting the inner increment.

**The log called the fault a success.** deko3d reports the detail of a failure
as a run of separate callback calls carrying `DkResult_Success`, with only the
last carrying the real result. The first version of the sink printed each as
"deko3d raised DkResult_Success", so the GPU page fault — the one line that
matters — read as nine successes followed by one failure. Informational lines
are now indented under their context instead.

## Next

1. **Find the null address.** deko3d's debug build validates on bind, so the
   cheapest next step is to check whether any `dkCmdBufBind*` call in the shim
   can pass a zero `DkGpuAddr`, and log the address at each bind site rather
   than inferring it. The fault is a read, which rules out render targets being
   written.
2. **Decode `info 0x00000d00`** against Nvidia's fault-info layout to learn
   which engine and which client faulted. That narrows four candidates to one.
3. **Run it several times.** The tally is running; the question of whether the
   826-call runs share this cause needs a handful of runs on the debug library,
   not one.
