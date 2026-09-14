# The null address is not a null pointer — it is a draw with no state

Found by reading, 2026-09-14, from the fault deko3d reported in build
`Sep 14 2026 21:43:58` and reproduced identically in `21:57:19`.

## The sequence

`ark_draw_ex` runs on the **graphics submit thread**. It binds everything a
draw needs into `g_arkchemy_gx2.cmdbuf` — vertex buffers, the constant file for
both stages, shaders, attribute state, buffer state — and then, under
`ARK_REC_DRAW_DEFERRED`, does *not* record the draw. It queues the parameters.

`ark_draw_record_queued` drains that queue and calls `dkCmdBufDraw`. It is
called from `main.c`'s own loop, on the **main thread**.

Between those two points, on the graphics thread:

| | |
| --- | --- |
| `GX2Flush` — `cafeos_gx2.h:1381` | `dkCmdBufFinishList` + `dkQueueSubmitCommands`: submits the state **with no draw in it** |
| `GX2SwapScanBuffers` — `cafeos_gx2.h:1352` | `dkCmdBufClear`: **wipes the command buffer** |

So the draw was recorded into a command buffer that had been finished,
submitted and cleared, with nothing bound to it at all. No vertex buffer, no
uniform buffer, no shaders.

A draw with no vertex buffer bound fetches its vertices from address 0. A
shader with no uniform buffer bound reads its constants from address 0. That is
deko3d's report, exactly:

```
GPU page fault (info 0x00000d00)
Address: 0x0000000000
Access type: Read
```

**There is no null pointer to find.** Nothing in the shim computes a bad
address. Every bind is correct and every address it binds is valid — they are
simply not in the command buffer any more by the time the draw runs.

## A second bug in the same arrangement

Two threads record into one `DkCmdBuf`. deko3d command buffers are not
thread-safe, so this is a data race on the command stream independently of the
ordering problem, and it has been there for as long as the deferral has.

## Why the deferral existed, and why it should not have

To avoid calling `dkCmdBufDraw` from inside the GX2 shim header, which eight
builds "established" stops the boot. That finding was bisected **one run per
build** against a rig since measured to fail about two runs in five — see
`2026-09-14-the-same-build-fails-two-runs-in-five.md`. It never held.

The workaround's own comment came close to seeing it:

> everything else this function binds is state, and state is cheap to rebind,
> while a draw is the thing that has to sit in the right place in the stream

That is right, and then the draw was moved to a different thread and a
different point in the stream from the state it belongs to.

## The change

`ARK_REC_DRAW 1`, `ARK_REC_DRAW_DEFERRED 0` — the draw is recorded inline,
immediately after the state it needs, on the thread that bound it. Build
`Sep 14 2026 22:22:15`.

One variable. Two outcomes, and both are worth having:

* **The fault goes and draws land.** The deferral was the bug, the
  `dkCmdBufDraw` finding is dead, and the draw path is correct for the first
  time.
* **The boot breaks.** Then there really is something about that call from this
  header, and it can be investigated with a run tally and a debug deko3d that
  reports what it objects to — neither of which existed when the question was
  first asked.

Either way it is now measured over several runs rather than decided by one.
The tally reads `0 ok, 3 failed` going in.

## What this does not explain

The 826-call runs with two shader modules and zero draws. Those cannot be this
bug — a draw that never happens cannot fault. They remain unexplained, and
`run-tally.txt` is the way to find out how often they still occur now that
every run's outcome is recorded.
