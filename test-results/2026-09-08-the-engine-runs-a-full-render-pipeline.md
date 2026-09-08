# GX2CENSUS, keyed properly: a complete render pipeline

Run of 2026-09-08, build `Sep  8 2026 07:31:55`.

```
GX2CENSUS total=134606 distinct=53 over=0
  GX2Invalidate                x15837
  GX2SetViewport               x13139
  GX2SetScissor                x13139
  GX2CopyDisplayList           x13139
  GX2SetPixelShader             x9195
  GX2SetVertexShader            x7882
  GX2SetContextState            x6572
  GX2CalcSurfaceSizeAndAlignment x5300
  GX2InitColorBufferRegs        x5274
  GX2SetAlphaToMask             x5258
  GX2InitSamplerXYFilter        x5256
  GX2InitSamplerZMFilter        x5256
  GX2InitSamplerClamping        x5256
  GX2SetClearDepthStencil       x3942
```

`over=0` and `distinct=53`: with the census keyed on the imported function
rather than the call site, every call is attributed and nothing is dropped. The
previous run's 24-slot ring attributed about 110 of 133,069.

This is not a renderer being poked at. It is a **per-frame pipeline**: render
target and viewport set up, scissor applied, display lists copied, vertex and
pixel shaders bound, context state switched, samplers configured, depth-stencil
cleared. The three counts sitting together at exactly 13,139 -- viewport,
scissor, display list copy -- are one sequence running once per frame.

The heap stays healthy under all of it: three pools, all reaching their
sentinels, no drops, `SPLITVERIFY n=0`, archive still drained.

## The question this does not answer

None of the draw or present entry points appear in the top fourteen, and a
busiest-first list will never surface them -- a draw call can sit far below a
state setter and still be the only thing that matters. The shims exist:
`GX2DrawEx`, `GX2DrawIndexedEx`, `GX2DrawDone`, `GX2BeginDisplayListEx`,
`GX2EndDisplayList`, `GX2Flush`, `GX2SwapScanBuffers`, `GX2GetSwapStatus`,
`GX2SetSwapInterval`.

Whether any of them is ever called decides what the graphics work actually is.

## Next

Build `Sep  8 2026 07:56:46`, md5 `d4e6dfc8cecce4eaf94094d64a0c91e5`, is on the
Switch. **GX2DRAW** reports that fixed set by name regardless of rank,
**including zeros**.

Counts are summed by name rather than read per index, because several GX2 entry
points are hooked by two shim definitions in `cafeos_gx2.h` and reading a single
index would undercount them.

* **zero draws** -- the engine builds complete render state every frame and
  never submits it. Something gates submission, and finding that gate is the
  next job rather than shader translation.
* **nonzero draws** -- the pipeline is complete end to end, and what stands
  between this and a picture is the shaders themselves plus real hardware
  behind the shims. That is the R600-to-Maxwell wall, and it would then be the
  main remaining problem rather than one of several.
