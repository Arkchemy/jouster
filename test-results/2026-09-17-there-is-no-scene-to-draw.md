# There is no scene to draw

Build `Sep 17 2026 08:45:13`. Two readings, and together they end the graphics
investigation.

## Every draw in the run is a full-screen quad

    DRAWSIZE quad=287 small=0 mid=0 big=0 max=4

287 draws. Every one four vertices. The largest draw in a four-minute run is
four vertices.

No scene geometry is ever submitted. DRAWIN already showed the ones it sampled
to be pixel-space quads with orthographic matrices and texture coordinates --
composites, not models -- and this says all 287 are.

So the black screen is not a graphics fault. It is a composite chain running
correctly over a scene nobody drew.

That closes out the chain of five bugs found since 2026-09-16 -- the tiling
gap, the deferred use-after-free, the shared render-target slot, the ignored
scan target, the re-uploaded render target. Each was real and each is fixed,
and what they were hiding is that there is nothing to look at yet.

## The remaining texture misses are not a shim bug

    TEXUP    [1024x576 pitch=1024 mips=1 tile=1 addr=0x1000100 FLAT 0x00000000]
             [1024x576 pitch=1024 mips=1 tile=1 addr=0x2d42100 FLAT 0x00000000]
    SURFKEYS [#2 1024x576 pitch=1024 addr=0x1948100]
             [#3 1024x576 pitch=1024 addr=0x1dc9100]

The key was not subtly mismatched. The addresses are entirely different:
`0x1000100` and `0x2d42100` against `0x1948100` and `0x1dc9100`. Same size,
same pitch, different memory.

These are genuine CPU-side textures that have never been a colour target, so
they were never rendered into and correctly are not in the cache. They read
zero because their content was never loaded. Nothing to fix here; the fix that
served the other 196 from the GPU's own image was right and is complete for
what it covers.

## The frontier is loading, and it has moved a long way

    STREAMLOAD calls=3 [permanent0/bootstrap] [item0/legal] [permanent0/global]
    ARCHNAME   permanent/bootstrap.bld  item/legal.bld
               permanent/global.arc     permanent/global.bld
    INFLATE    lzma n=139 ret=0x0 | ok=139 fail=0
    ARCHPUMP   updateArchiveSystem=147 startBlockRead=145 decompressBatch=137
    ADDWORK CALLS n=8
      [0 size=0x0] [1 size=0x0] [2 off=0x0 size=0x800]
      [3 off=0x800 size=0xe78] [4 off=0x1678 size=0x3a3c] [5 off=0x50b4 ...]

Three archives opened, 139 successful LZMA decompressions, and reads whose
offsets advance sequentially with real sizes. `ADDWORK size=0 is the whole
stall`, the note this project carried for weeks, is out of date: only the first
two calls have size 0 and the rest are reading properly.

## Where it stops

    IGZSTATE update calls=6 max=kStateFailed(9)
      [kStateFailed(9) ret=0x1 iters=1] [kStateFinished(7) ret=0x0 iters=5]
      [kStateFailed(9) ret=0x1 iters=1] [kStateFinished(7) ret=0x0 iters=5]
      [kStateFailed(9) ret=0x1 iters=1]
    IGZTRACE states [call0: 0] [call1: 0 1 3 5 7] [call2: 0] [call3: 0 1 3 5 7]
    SETSTATUS [0x4503700=4 lr=0x215566c] [0x4503730=4 lr=0x216abcc] ...

Three of six igz loads finish. Three fail on their first iteration without ever
leaving state 0. And work items are being written status 4, which
`isFileWorkFinished` treats as finished-with-error, since its own disassembly
branches on `<= 2 is fine`.

`IGZWORK` came back `<none>` while `IGZSTATE` reports `ret=0x1` from exactly
that function, which is a contradiction in the instrumentation and worth
remembering before either probe is trusted further.

## What is being measured next

Build `Sep 17 2026 16:14:31`.

`SSHIST` records each work item's full status sequence with the lr that set
each value. The aggregate cannot distinguish the two cases and they need
opposite fixes:

  * `1 -> 4` -- the read genuinely failed, and the work is upstream in the
    archive.
  * `1 -> 2 -> 4` -- the read completed and the item was then marked something
    else, so the loader is reading a byte from an item that has already been
    recycled. That is a timing fix in the loader and nothing to do with the
    archive at all.
