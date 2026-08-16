// Real, minimal, dedicated test for the gx2 shim's actual deko3d-backed
// GX2* functions -- kept as its own separate .nro, not merged into
// switch/native/'s existing text-console-based recompiler test suite,
// since mixing a real deko3d swapchain into the same native window
// libnx's own consoleInit() already claims for text output is a real
// integration risk this hasn't been checked yet, and this project
// doesn't want to put the already-hardware-verified main suite at risk
// to find out. No console output here -- same "checkpointed SD-card
// log" approach switch/native/source/main.c already uses instead (see
// its own comment on why: if this freezes/crashes partway through,
// whatever ran before that point is still on the SD card afterward).
//
// This calls the real ppc_import_gx2_* shim functions through the same
// PpcContext-based calling convention real recompiled code would use
// (not a shortcut/bypass), then -- for the state-setting functions --
// inspects `g_bramble_gx2`'s own shadow-state fields directly (visible
// here since this .c file is the one real translation unit that
// #includes cafeos_gx2.h, and its `static` file-scope globals are
// therefore this app's actual live instance, not an opaque copy) to
// confirm each real GX2 call actually produced the real deko3d state
// it claims to, not just that it ran without crashing. This is the
// same kind of real assertion-based checking switch/native/'s
// checkResult() does against known-correct ground truth, applied here
// to GX2 state instead of recompiled arithmetic results.
//
// After the state-setter self-test, the original visual clear-color
// loop still runs, now reporting the self-test's own result: solid
// GREEN means every check passed, solid RED means at least one
// failed (check the log for which) -- real, visible, on-hardware
// confirmation that the GX2->deko3d bridge this project built
// actually produces pixels on a real console, readable at a glance
// without needing the SD card log for the common case. Runs until +
// is pressed.
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>

#include "ppc_runtime.h"
#include "cafeos_gx2.h"

static FILE *g_log;
static int g_pass_count, g_fail_count;

// Appends to the SD-card log, flushed after every line -- no console
// output (see file comment on why this .nro doesn't use consoleInit()).
static void checkpoint(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
    fflush(g_log);
}

static void checkBool(const char *name, int got, int want) {
    int pass = (got == want);
    if (pass) g_pass_count++; else g_fail_count++;
    checkpoint("[%s] got=%d want=%d -- %s", name, got, want, pass ? "PASS" : "FAIL");
}

static void checkU64(const char *name, uint64_t got, uint64_t want) {
    int pass = (got == want);
    if (pass) g_pass_count++; else g_fail_count++;
    checkpoint("[%s] got=%llu want=%llu -- %s", name, (unsigned long long)got, (unsigned long long)want, pass ? "PASS" : "FAIL");
}

// Real self-test: exercises every real deko3d-backed GX2Set*/GX2Get*
// this project has implemented so far via the same PpcContext calling
// convention real recompiled code uses, then checks the real resulting
// shadow/deko3d state (or return value) against a known-correct
// expectation -- not just "did it crash."
static void run_state_selftest(PpcContext *ctx) {
    checkpoint("=== GX2 state-setter self-test ===");

    // GX2SetColorControl(rop3=SET(0xF0), targetBlendEnable=0x01,
    // multiWriteEnable=0, colorWriteEnable=1) -- real GX2LogicOp SET
    // (raw 0xF0, index 15) maps to DkLogicOp_Set (15) per
    // bramble_gx2_logic_op_to_dk's own table.
    ctx->r[3] = 0xF0; ctx->r[4] = 0x01; ctx->r[5] = 0; ctx->r[6] = 1;
    ppc_import_gx2_GX2SetColorControl(ctx);
    checkBool("GX2SetColorControl.logicOp", (int)g_bramble_gx2.color_state.logicOp, (int)DkLogicOp_Set);
    checkBool("GX2SetColorControl.blendEnableMask", (int)g_bramble_gx2.color_state.blendEnableMask, 0x01);
    checkBool("GX2SetColorControl.write_enable", (int)g_bramble_gx2.color_write_enable, 1);

    // GX2SetAlphaTest(alphaTest=TRUE, func=GREATER(4), ref=0.5) -- real
    // shadow-state fix under test: this must NOT reset the logicOp
    // GX2SetColorControl just set above (that was the actual bug this
    // shared shadow-state refactor fixed).
    ctx->r[3] = 1; ctx->r[4] = 4; ctx->f[1] = 0.5;
    ppc_import_gx2_GX2SetAlphaTest(ctx);
    checkBool("GX2SetAlphaTest.alphaCompareOp", (int)g_bramble_gx2.color_state.alphaCompareOp, (int)DkCompareOp_Greater);
    checkBool("GX2SetAlphaTest preserves prior logicOp", (int)g_bramble_gx2.color_state.logicOp, (int)DkLogicOp_Set);

    // GX2SetTargetChannelMasks(mask0=RGBA(15), mask1..7=R(1)) -- real
    // precedence-fix under test: channel_masks is now a source of truth
    // independent of GX2SetColorControl's color_write_enable, combined
    // at bind time by bramble_gx2_rebind_color_write_state instead of
    // either call overwriting the other's setting outright.
    ctx->r[3] = 15; ctx->r[4] = 1; ctx->r[5] = 1; ctx->r[6] = 1;
    ctx->r[7] = 1; ctx->r[8] = 1; ctx->r[9] = 1; ctx->r[10] = 1;
    ppc_import_gx2_GX2SetTargetChannelMasks(ctx);
    checkBool("GX2SetTargetChannelMasks[0]", (int)(g_bramble_gx2.channel_masks & 0xF), 15);
    checkBool("GX2SetTargetChannelMasks[1]", (int)((g_bramble_gx2.channel_masks >> 4) & 0xF), 1);

    // Real regression test for the precedence bug the shadow-state
    // fix addressed: calling GX2SetColorControl again afterward (e.g.
    // just to toggle blend for an unrelated reason, a real, plausible
    // per-draw pattern) must NOT reset the narrower per-channel mask
    // GX2SetTargetChannelMasks just set on target 1.
    ctx->r[3] = 0xF0; ctx->r[4] = 0x01; ctx->r[5] = 0; ctx->r[6] = 1;
    ppc_import_gx2_GX2SetColorControl(ctx);
    checkBool("GX2SetColorControl preserves target 1's channel mask", (int)((g_bramble_gx2.channel_masks >> 4) & 0xF), 1);

    // GX2SetPolygonControl(frontFace=CCW(0), cullFront=1, cullBack=0,
    // polyMode=1, polyModeFront=TRIANGLE(2), polyModeBack=TRIANGLE(2),
    // polyOffsetFrontEnable=0, polyOffsetBackEnable=0), 9th stack arg
    // polyOffsetParaEnable=0 at r1+8. r1 is a real *guest* address --
    // an offset into ctx->shared->mem (see ppc_load_u32/ppc_store_u32
    // in ppc_runtime.h), not a real host pointer -- so this uses a
    // fixed scratch offset into that arena, the same real addressing
    // model recompiled code itself uses for its stack, not a host
    // `uint8_t[]`.
    {
        ctx->r[1] = 0x1000;
        ppc_store_u32(ctx, ctx->r[1] + 8, 0);
        ctx->r[3] = 0; ctx->r[4] = 1; ctx->r[5] = 0; ctx->r[6] = 1;
        ctx->r[7] = 2; ctx->r[8] = 2; ctx->r[9] = 0; ctx->r[10] = 0;
        ppc_import_gx2_GX2SetPolygonControl(ctx);
    }
    checkBool("GX2SetPolygonControl.cullMode", (int)g_bramble_gx2.rasterizer_state.cullMode, (int)DkFace_Front);
    checkBool("GX2SetPolygonControl.polygonModeFront", (int)g_bramble_gx2.rasterizer_state.polygonModeFront, (int)DkPolygonMode_Fill);

    // GX2SetRasterizerClipControl(rasterizer=1, zclipEnable=0) -- real
    // shadow-state fix under test: must not reset the cullMode
    // GX2SetPolygonControl just set above. zclipEnable=0 ->
    // depthClampEnable=1 (the documented inverse mapping).
    ctx->r[3] = 1; ctx->r[4] = 0;
    ppc_import_gx2_GX2SetRasterizerClipControl(ctx);
    checkBool("GX2SetRasterizerClipControl.depthClampEnable", (int)g_bramble_gx2.rasterizer_state.depthClampEnable, 1);
    checkBool("GX2SetRasterizerClipControl preserves prior cullMode", (int)g_bramble_gx2.rasterizer_state.cullMode, (int)DkFace_Front);

    // GX2SetAlphaToMask(alphaToMask=1, mode=DITHER_90(2)) -- any
    // nonzero mode maps to dither=1 per the documented collapse.
    ctx->r[3] = 1; ctx->r[4] = 2;
    ppc_import_gx2_GX2SetAlphaToMask(ctx);
    checkBool("GX2SetAlphaToMask.alphaToCoverageEnable", (int)g_bramble_gx2.multisample_state.alphaToCoverageEnable, 1);
    checkBool("GX2SetAlphaToMask.alphaToCoverageDither", (int)g_bramble_gx2.multisample_state.alphaToCoverageDither, 1);

    // GX2SetDepthStencilControl(depthTest=1, depthWrite=1,
    // depthCompare=LESS(1), stencilTest=1, backfaceStencil=0,
    // frontStencilFunc=ALWAYS(7), frontStencilZPass=KEEP(0),
    // frontStencilZFail=KEEP(0)), 5 stack args (frontStencilFail,
    // backStencilFunc, backStencilZPass, backStencilZFail,
    // backStencilFail) at r1+8/12/16/20/24, all KEEP/ALWAYS(0/7).
    {
        ctx->r[1] = 0x1000; /* same real guest-arena scratch offset as above */
        ppc_store_u32(ctx, ctx->r[1] + 8, 0);
        ppc_store_u32(ctx, ctx->r[1] + 12, 7);
        ppc_store_u32(ctx, ctx->r[1] + 16, 0);
        ppc_store_u32(ctx, ctx->r[1] + 20, 0);
        ppc_store_u32(ctx, ctx->r[1] + 24, 0);
        ctx->r[3] = 1; ctx->r[4] = 1; ctx->r[5] = 1; ctx->r[6] = 1;
        ctx->r[7] = 0; ctx->r[8] = 7; ctx->r[9] = 0; ctx->r[10] = 0;
        ppc_import_gx2_GX2SetDepthStencilControl(ctx);
    }
    checkBool("GX2SetDepthStencilControl.depthCompareOp", (int)g_bramble_gx2.depth_stencil_state.depthCompareOp, (int)DkCompareOp_Less);
    checkBool("GX2SetDepthStencilControl.stencilTestEnable", (int)g_bramble_gx2.depth_stencil_state.stencilTestEnable, 1);

    // GX2SetDepthOnlyControl(depthTest=0, depthWrite=0,
    // depthCompare=ALWAYS(7)) -- real bugfix under test: must NOT
    // force-disable the stencilTestEnable GX2SetDepthStencilControl
    // just set above.
    ctx->r[3] = 0; ctx->r[4] = 0; ctx->r[5] = 7;
    ppc_import_gx2_GX2SetDepthOnlyControl(ctx);
    checkBool("GX2SetDepthOnlyControl.depthCompareOp", (int)g_bramble_gx2.depth_stencil_state.depthCompareOp, (int)DkCompareOp_Always);
    checkBool("GX2SetDepthOnlyControl preserves prior stencilTestEnable", (int)g_bramble_gx2.depth_stencil_state.stencilTestEnable, 1);

    // GX2GetDisplayListWriteStatus() -- always FALSE, honestly (no
    // display-list recording implemented yet).
    ppc_import_gx2_GX2GetDisplayListWriteStatus(ctx);
    checkBool("GX2GetDisplayListWriteStatus", (int)ctx->r[3], 0);

    // GX2Flush()/GX2GetLastSubmittedTimeStamp()/GX2DrawDone(): a real
    // submit must advance submitted_timestamp, and GX2DrawDone must
    // return TRUE and catch retired_timestamp up to it.
    uint64_t before = g_bramble_gx2.submitted_timestamp;
    ppc_import_gx2_GX2Flush(ctx);
    checkBool("GX2Flush advances submitted_timestamp", g_bramble_gx2.submitted_timestamp > before, 1);
    ppc_import_gx2_GX2GetLastSubmittedTimeStamp(ctx);
    uint64_t last_submitted = ((uint64_t)ctx->r[3] << 32) | (uint64_t)ctx->r[4];
    checkU64("GX2GetLastSubmittedTimeStamp matches shadow state", last_submitted, g_bramble_gx2.submitted_timestamp);
    ppc_import_gx2_GX2DrawDone(ctx);
    checkBool("GX2DrawDone returns TRUE", (int)ctx->r[3], 1);
    ppc_import_gx2_GX2GetRetiredTimeStamp(ctx);
    uint64_t last_retired = ((uint64_t)ctx->r[3] << 32) | (uint64_t)ctx->r[4];
    checkU64("GX2GetRetiredTimeStamp caught up after GX2DrawDone", last_retired, g_bramble_gx2.submitted_timestamp);

    // GX2WaitTimeStamp(time=retired_timestamp) -- already retired, must
    // return TRUE without needing a real wait.
    uint64_t retired = g_bramble_gx2.retired_timestamp;
    ctx->r[3] = (uint32_t)(retired >> 32);
    ctx->r[4] = (uint32_t)retired;
    ppc_import_gx2_GX2WaitTimeStamp(ctx);
    checkBool("GX2WaitTimeStamp(already-retired) returns TRUE", (int)ctx->r[3], 1);

    // GX2WaitForVsync() -- real, safe to call with nothing pending;
    // just confirms it doesn't hang/crash (no return value to check).
    ppc_import_gx2_GX2WaitForVsync(ctx);
    checkpoint("[GX2WaitForVsync] returned -- PASS (no hang/crash)");

    // The functions below are all backend-independent (pure guest-memory
    // writes, no deko3d call, no g_bramble_gx2 shadow state involved) --
    // unlike everything above, these were only verified analytically on
    // host before now (see docs/phase1d_import_surface.md); this is
    // their first real on-hardware exercise, at 0x2000 to avoid the
    // 0x1000 scratch region already used above.

    // GX2InitSampler(sampler@0x2000, clampMode=MIRROR(1),
    // filterMode=LINEAR(1)) -- real bit-packed defaults per Cemu's
    // GX2InitSampler body: CLAMP_X/Y/Z=1, XY_MAG/MIN_FILTER=1,
    // Z_FILTER/MIP_FILTER=POINT(1), TEX_ARRAY_OVERRIDE=1, WORD1's
    // MAX_LOD=0x3FF, WORD2's TYPE=1.
    ctx->r[3] = 0x2000; ctx->r[4] = 1; ctx->r[5] = 1;
    ppc_import_gx2_GX2InitSampler(ctx);
    {
        uint32_t w0 = ppc_load_u32(ctx, 0x2000);
        uint32_t w1 = ppc_load_u32(ctx, 0x2004);
        uint32_t w2 = ppc_load_u32(ctx, 0x2008);
        checkBool("GX2InitSampler.CLAMP_X", (int)(w0 & 0x7), 1);
        checkBool("GX2InitSampler.XY_MAG_FILTER", (int)((w0 >> 9) & 0x7), 1);
        checkBool("GX2InitSampler.TEX_ARRAY_OVERRIDE", (int)((w0 >> 25) & 0x1), 1);
        checkBool("GX2InitSampler.MAX_LOD", (int)((w1 >> 10) & 0x3FF), 0x3FF);
        checkBool("GX2InitSampler.WORD2_TYPE", (int)((w2 >> 31) & 0x1), 1);
    }

    // GX2InitSamplerLOD(sampler@0x2000, minLod=2.5, maxLod=12.0,
    // lodBias=-1.5) -- real fixed-point encoding: floor(value*64),
    // clamped. Overwrites WORD1 set by GX2InitSampler above (real
    // behavior -- each Init* function owns whichever fields it touches,
    // same as GX2SetColorControl/GX2SetAlphaTest's shared-state
    // pattern, just with no cross-function field sharing here since
    // GX2InitSamplerLOD fully replaces WORD1 in one write).
    ctx->r[3] = 0x2000; ctx->f[1] = 2.5; ctx->f[2] = 12.0; ctx->f[3] = -1.5;
    ppc_import_gx2_GX2InitSamplerLOD(ctx);
    {
        uint32_t w1 = ppc_load_u32(ctx, 0x2004);
        checkBool("GX2InitSamplerLOD.MIN_LOD", (int)(w1 & 0x3FF), (int)(2.5 * 64));
        checkBool("GX2InitSamplerLOD.MAX_LOD", (int)((w1 >> 10) & 0x3FF), (int)(12.0 * 64));
    }

    // GX2InitSamplerDepthCompare(sampler@0x2000, GEQUAL(6)) -- raw
    // field write, no DkCompareOp translation (see its own comment).
    ctx->r[3] = 0x2000; ctx->r[4] = 6;
    ppc_import_gx2_GX2InitSamplerDepthCompare(ctx);
    checkBool("GX2InitSamplerDepthCompare", (int)((ppc_load_u32(ctx, 0x2000) >> 26) & 0x7), 6);

    // GX2SetClearDepthStencil(depthBuffer@0x3000, depth=0.25,
    // stencil=77) -- real WUT_CHECK_OFFSET-confirmed struct writes at
    // 0x88/0x8C.
    ctx->r[3] = 0x3000; ctx->f[1] = 0.25; ctx->r[4] = 77;
    ppc_import_gx2_GX2SetClearDepthStencil(ctx);
    checkBool("GX2SetClearDepthStencil.depthClear", ppc_load_f32(ctx, 0x3000 + 0x88) == 0.25f, 1);
    checkBool("GX2SetClearDepthStencil.stencilClear", (int)ppc_load_u32(ctx, 0x3000 + 0x8C), 77);

    // GX2CalcDepthBufferHiZInfo(depthBuffer@0x3000, sizeOut@0x4000,
    // alignOut@0x4004) -- real Cemu HLE behavior is fixed 0x1000/0x100
    // constants, matched exactly (see its own comment).
    ctx->r[3] = 0x3000; ctx->r[4] = 0x4000; ctx->r[5] = 0x4004;
    ppc_import_gx2_GX2CalcDepthBufferHiZInfo(ctx);
    checkBool("GX2CalcDepthBufferHiZInfo.size", (int)ppc_load_u32(ctx, 0x4000), 0x1000);
    checkBool("GX2CalcDepthBufferHiZInfo.align", (int)ppc_load_u32(ctx, 0x4004), 0x100);

    // GX2SetPixelSampler(sampler@0x2000, index=0) / GX2SetVertexSampler
    // (sampler@0x5000, index=0) -- real deko3d descriptor push + bind.
    // The real DkSamplerDescriptor byte encoding is opaque (deko3d's
    // own internal format, not documented/decodable independently), so
    // this can't assert specific bytes the way the guest-memory struct
    // checks above do -- what real hardware *can* confirm is (a) the
    // real dkMemBlockCreate/dkCmdBufPushData/
    // dkCmdBufBindSamplerDescriptorSet calls this needs all succeed
    // without crashing (the real, meaningful risk in this function --
    // wrong memory flags/alignment/size would show up here), and (b)
    // the real pixel/vertex namespace separation actually lands two
    // different real samplers in two different, non-overlapping real
    // descriptor slots, not the same one. Two deliberately different
    // GX2Samplers (0x2000: MIRROR/LINEAR from earlier in this test;
    // 0x5000: freshly WRAP/POINT) at pixel index 0 and vertex index 0
    // should end up at real byte offsets 0 and
    // 18*sizeof(DkSamplerDescriptor) in the pool, with different real
    // encoded bytes (deko3d's own encoding, whatever it is, of two
    // different real DkSampler configurations should not collide).
    // GX2Flush+GX2DrawDone (already exercised above) make sure the
    // real dkCmdBufPushData writes actually land in CPU-visible memory
    // before reading it back here, since GX2Set*Sampler only records
    // into the persistent command buffer, same as every other real
    // GX2Set* state function in this file.
    {
        ctx->r[3] = 0x5000; ctx->r[4] = 0; ctx->r[5] = 0; /* GX2InitSampler(sampler@0x5000, WRAP(0), POINT(0)) */
        ppc_import_gx2_GX2InitSampler(ctx);

        ctx->r[3] = 0x2000; ctx->r[4] = 0; /* GX2SetPixelSampler(sampler@0x2000, index=0) */
        ppc_import_gx2_GX2SetPixelSampler(ctx);
        ctx->r[3] = 0x5000; ctx->r[4] = 0; /* GX2SetVertexSampler(sampler@0x5000, index=0) */
        ppc_import_gx2_GX2SetVertexSampler(ctx);

        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);

        uint8_t *pool = (uint8_t *)dkMemBlockGetCpuAddr(g_bramble_gx2.sampler_descriptor_mem_block);
        uint8_t *pixel_slot = pool + 0 * sizeof(DkSamplerDescriptor);
        uint8_t *vertex_slot = pool + BRAMBLE_GX2_SAMPLER_VERTEX_BASE * sizeof(DkSamplerDescriptor);
        int slots_differ = memcmp(pixel_slot, vertex_slot, sizeof(DkSamplerDescriptor)) != 0;
        checkBool("GX2SetPixelSampler/GX2SetVertexSampler write to different real descriptor slots", slots_differ, 1);

        int pixel_slot_nonzero = 0, i;
        for (i = 0; i < (int)sizeof(DkSamplerDescriptor); i++) {
            if (pixel_slot[i] != 0) { pixel_slot_nonzero = 1; break; }
        }
        checkBool("GX2SetPixelSampler wrote real, non-zero descriptor bytes", pixel_slot_nonzero, 1);
    }

    // GX2SetPixelSamplerBorderColor(index=0, ...) + GX2InitSamplerBorderType
    // (VARIABLE(3)) -- real border-color register storage feeding a
    // real sampler's decode at bind time. Two samplers with otherwise
    // identical bits (both freshly WRAP/POINT at 0x5000) but different
    // real border colors and BORDER_COLOR_TYPE=VARIABLE should produce
    // two different real descriptors at the same pixel slot when bound
    // one after the other -- same "can't assert specific bytes, can
    // assert they differ" reasoning as the pixel/vertex check above.
    {
        uint8_t before[sizeof(DkSamplerDescriptor)], after[sizeof(DkSamplerDescriptor)];
        uint8_t *pool, *pixel_slot;

        ctx->r[3] = 0; /* GX2SetPixelSamplerBorderColor(index=0, r=1,g=0,b=0,a=1) -- real index arg is r3, NOT a sampler pointer */
        ctx->f[1] = 1.0; ctx->f[2] = 0.0; ctx->f[3] = 0.0; ctx->f[4] = 1.0;
        ppc_import_gx2_GX2SetPixelSamplerBorderColor(ctx);
        ctx->r[3] = 0x5000; ctx->r[4] = 3; /* GX2InitSamplerBorderType(sampler@0x5000, VARIABLE(3)) */
        ppc_import_gx2_GX2InitSamplerBorderType(ctx);
        ctx->r[3] = 0x5000; ctx->r[4] = 0; /* GX2SetPixelSampler(sampler@0x5000, index=0) */
        ppc_import_gx2_GX2SetPixelSampler(ctx);
        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);
        pool = (uint8_t *)dkMemBlockGetCpuAddr(g_bramble_gx2.sampler_descriptor_mem_block);
        pixel_slot = pool + 0 * sizeof(DkSamplerDescriptor);
        memcpy(before, pixel_slot, sizeof(before));

        ctx->r[3] = 0; /* GX2SetPixelSamplerBorderColor(index=0, r=0,g=1,b=0,a=1) -- different color, real index arg is r3 */
        ctx->f[1] = 0.0; ctx->f[2] = 1.0; ctx->f[3] = 0.0; ctx->f[4] = 1.0;
        ppc_import_gx2_GX2SetPixelSamplerBorderColor(ctx);
        ctx->r[3] = 0x5000; ctx->r[4] = 0; /* GX2SetPixelSampler(sampler@0x5000, index=0) -- re-bind so the new color takes effect */
        ppc_import_gx2_GX2SetPixelSampler(ctx);
        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);
        memcpy(after, pixel_slot, sizeof(after));

        checkBool("GX2SetPixelSamplerBorderColor changes the real bound descriptor", memcmp(before, after, sizeof(before)) != 0, 1);
    }

    // GX2CalcSurfaceSizeAndAlignment(surface@0x6000) -- real, bounded
    // AMD tiling-math port (mip 0, real linear-only tile modes; see
    // its own file comment for the real scope). Same real hand-traced
    // values already confirmed on host (docs/phase1d_import_surface.md);
    // this is their first real on-hardware exercise.
    {
        uint32_t addr = 0x6000;
        // TM_LINEAR_SPECIAL(16), DIM_2D(1), 256x128, RGBA8(0x1a), AA1X --
        // real hand-traced expected: imageSize=131072, alignment=1, pitch=256.
        ppc_store_u32(ctx, addr + 0x00, 1);   // dim
        ppc_store_u32(ctx, addr + 0x04, 256); // width
        ppc_store_u32(ctx, addr + 0x08, 128); // height
        ppc_store_u32(ctx, addr + 0x0C, 1);   // depth
        ppc_store_u32(ctx, addr + 0x10, 1);   // mipLevels
        ppc_store_u32(ctx, addr + 0x14, 0x1a);// format
        ppc_store_u32(ctx, addr + 0x18, 0);   // aa
        ppc_store_u32(ctx, addr + 0x30, 16);  // tileMode = TM_LINEAR_SPECIAL
        ctx->r[3] = addr;
        ppc_import_gx2_GX2CalcSurfaceSizeAndAlignment(ctx);
        checkBool("GX2CalcSurfaceSizeAndAlignment(LINEAR_SPECIAL).imageSize", (int)ppc_load_u32(ctx, addr + 0x20), 131072);
        checkBool("GX2CalcSurfaceSizeAndAlignment(LINEAR_SPECIAL).pitch", (int)ppc_load_u32(ctx, addr + 0x3C), 256);

        // Same surface, TM_LINEAR_ALIGNED(1) explicit -- real hand-traced
        // expected: imageSize=131072, alignment=256, pitch=256.
        ppc_store_u32(ctx, addr + 0x30, 1);
        ctx->r[3] = addr;
        ppc_import_gx2_GX2CalcSurfaceSizeAndAlignment(ctx);
        checkBool("GX2CalcSurfaceSizeAndAlignment(LINEAR_ALIGNED).imageSize", (int)ppc_load_u32(ctx, addr + 0x20), 131072);
        checkBool("GX2CalcSurfaceSizeAndAlignment(LINEAR_ALIGNED).alignment", (int)ppc_load_u32(ctx, addr + 0x38), 256);
    }

    // GX2SetColorBuffer(colorBuffer@0x7000, target=0) -- real deko3d
    // image + memory block creation, plus a real guest-memory-to-GPU
    // pixel copy. A small 4x4 RGBA8 pattern is written into guest
    // memory first (a distinct byte value per pixel), then read back
    // from the real, actual CPU-visible DkMemBlock the copy wrote into
    // -- unlike the sampler descriptor checks above (whose encoding is
    // opaque), pitch-linear RGBA8 image bytes are real, plain,
    // directly-readable pixel data, so this can assert exact values,
    // not just "did something change".
    {
        uint32_t addr = 0x7000;
        uint32_t pixel_addr = 0x7100;
        uint32_t w = 4, h = 4, x, y;
        uint32_t dest_stride = (4u * w + 127u) & ~127u; // real deko3d UsageRender pitch-linear formula

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint8_t v = (uint8_t)(y * w + x);
                uint32_t p = pixel_addr + (y * w + x) * 4;
                ppc_store_u8(ctx, p + 0, v);
                ppc_store_u8(ctx, p + 1, (uint8_t)(v + 1));
                ppc_store_u8(ctx, p + 2, (uint8_t)(v + 2));
                ppc_store_u8(ctx, p + 3, (uint8_t)(v + 3));
            }
        }

        ppc_store_u32(ctx, addr + 0x00, 1);      // dim = DIM_2D
        ppc_store_u32(ctx, addr + 0x04, w);      // width
        ppc_store_u32(ctx, addr + 0x08, h);      // height
        ppc_store_u32(ctx, addr + 0x10, 1);      // mipLevels
        ppc_store_u32(ctx, addr + 0x14, 0x1a);   // format = UNORM_R8_G8_B8_A8
        ppc_store_u32(ctx, addr + 0x30, 16);     // tileMode = TM_LINEAR_SPECIAL
        ppc_store_u32(ctx, addr + 0x3C, w);      // pitch (== width, no extra padding for this small test)
        ppc_store_u32(ctx, addr + 0x24, pixel_addr); // image pointer

        ctx->r[3] = addr; ctx->r[4] = 0; // target 0
        ppc_import_gx2_GX2SetColorBuffer(ctx);

        checkBool("GX2SetColorBuffer bound target 0", g_bramble_gx2.color_target_bound[0], 1);
        if (g_bramble_gx2.color_target_bound[0]) {
            uint8_t *dest = (uint8_t *)dkMemBlockGetCpuAddr(g_bramble_gx2.color_target_mem_block[0]);
            int pixels_match = 1;
            for (y = 0; y < h && pixels_match; y++) {
                for (x = 0; x < w && pixels_match; x++) {
                    uint8_t v = (uint8_t)(y * w + x);
                    uint8_t *px = dest + y * dest_stride + x * 4;
                    if (px[0] != v || px[1] != (uint8_t)(v + 1) || px[2] != (uint8_t)(v + 2) || px[3] != (uint8_t)(v + 3)) {
                        pixels_match = 0;
                    }
                }
            }
            checkBool("GX2SetColorBuffer copied real pixel data correctly", pixels_match, 1);
        }

        // Re-binding the same target must not leak the previous real
        // DkMemBlock -- calling it again should still leave exactly
        // one real, valid, bound resource at this slot.
        ctx->r[3] = addr; ctx->r[4] = 0;
        ppc_import_gx2_GX2SetColorBuffer(ctx);
        checkBool("GX2SetColorBuffer re-bind still bound", g_bramble_gx2.color_target_bound[0], 1);
    }

    // GX2SetDepthBuffer(depthBuffer@0x8000) -- real block-linear depth
    // image + real staging buffer + a real, recorded (not yet
    // submitted) dkCmdBufCopyBufferToImage GPU blit. Unlike
    // GX2SetColorBuffer's pitch-linear image (whose bytes are plain,
    // directly-readable pixel data), the real depth image itself is
    // block-linear (real GPU-swizzled) -- reading it back byte-for-
    // byte isn't meaningful without implementing the real deswizzle
    // algorithm, not attempted here. What real hardware *can* confirm:
    // (a) the real dkMemBlockCreate/dkImageInitialize/
    // dkCmdBufCopyBufferToImage calls all succeed without crashing
    // (the real, meaningful risk -- wrong memory flags/format/
    // alignment would show up here), and (b) the real staging buffer
    // -- a plain, tightly-packed linear copy target, same reasoning as
    // GX2SetColorBuffer's own image -- received the exact real guest
    // pixel bytes written to it.
    {
        uint32_t addr = 0x8000;
        uint32_t pixel_addr = 0x8100;
        uint32_t w = 4, h = 4, x, y;

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint8_t v = (uint8_t)(100 + y * w + x);
                uint32_t p = pixel_addr + (y * w + x) * 4;
                ppc_store_u8(ctx, p + 0, v);
                ppc_store_u8(ctx, p + 1, (uint8_t)(v + 1));
                ppc_store_u8(ctx, p + 2, (uint8_t)(v + 2));
                ppc_store_u8(ctx, p + 3, (uint8_t)(v + 3));
            }
        }

        ppc_store_u32(ctx, addr + 0x00, 1);      // dim = DIM_2D
        ppc_store_u32(ctx, addr + 0x04, w);      // width
        ppc_store_u32(ctx, addr + 0x08, h);      // height
        ppc_store_u32(ctx, addr + 0x10, 1);      // mipLevels
        ppc_store_u32(ctx, addr + 0x14, 0x11);   // format = UNORM_R24_X8
        ppc_store_u32(ctx, addr + 0x30, 16);     // tileMode = TM_LINEAR_SPECIAL
        ppc_store_u32(ctx, addr + 0x3C, w);      // pitch
        ppc_store_u32(ctx, addr + 0x24, pixel_addr); // image pointer

        ctx->r[3] = addr;
        ppc_import_gx2_GX2SetDepthBuffer(ctx);

        checkBool("GX2SetDepthBuffer bound", g_bramble_gx2.depth_target_bound, 1);
        if (g_bramble_gx2.depth_target_bound) {
            uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(g_bramble_gx2.depth_target_staging_mem_block);
            int pixels_match = 1;
            for (y = 0; y < h && pixels_match; y++) {
                for (x = 0; x < w && pixels_match; x++) {
                    uint8_t v = (uint8_t)(100 + y * w + x);
                    uint8_t *px = staging + (y * w + x) * 4;
                    if (px[0] != v || px[1] != (uint8_t)(v + 1) || px[2] != (uint8_t)(v + 2) || px[3] != (uint8_t)(v + 3)) {
                        pixels_match = 0;
                    }
                }
            }
            checkBool("GX2SetDepthBuffer staging buffer has correct real pixel data", pixels_match, 1);
        }

        // Real, recorded (not submitted) GPU commands from this call
        // (GX2SetColorBuffer's own pixel-copy above plus this
        // function's dkCmdBufCopyBufferToImage) need to actually
        // submit and complete without hanging/crashing -- confirmed
        // via the same real GX2Flush/GX2DrawDone path already
        // exercised earlier in this test.
        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);
        checkpoint("[GX2SetDepthBuffer's recorded GPU blit] submitted+completed -- PASS (no hang/crash)");
    }

    // GX2SetPixelTexture(texture@0x9000, unit=0) / GX2SetVertexTexture
    // (texture@0x9000, unit=0) -- real block-linear texture image +
    // staging buffer bridge (same real design/verification reasoning
    // as GX2SetDepthBuffer above, not GX2SetColorBuffer's pitch-linear
    // one -- see bramble_gx2_set_texture's own comment), plus a real
    // image descriptor push + dkMakeTextureHandle + dkCmdBufBindTextures.
    // GX2Texture's own `surface` member sits at the same offset 0 as
    // GX2ColorBuffer/GX2DepthBuffer's, so this reuses the identical
    // field layout already used above.
    {
        uint32_t addr = 0x9000;
        uint32_t pixel_addr = 0x9100;
        uint32_t w = 4, h = 4, x, y;

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint8_t v = (uint8_t)(200 + y * w + x);
                uint32_t p = pixel_addr + (y * w + x) * 4;
                ppc_store_u8(ctx, p + 0, v);
                ppc_store_u8(ctx, p + 1, (uint8_t)(v + 1));
                ppc_store_u8(ctx, p + 2, (uint8_t)(v + 2));
                ppc_store_u8(ctx, p + 3, (uint8_t)(v + 3));
            }
        }

        ppc_store_u32(ctx, addr + 0x00, 1);      // dim = DIM_2D
        ppc_store_u32(ctx, addr + 0x04, w);      // width
        ppc_store_u32(ctx, addr + 0x08, h);      // height
        ppc_store_u32(ctx, addr + 0x10, 1);      // mipLevels
        ppc_store_u32(ctx, addr + 0x14, 0x1a);   // format = UNORM_R8_G8_B8_A8
        ppc_store_u32(ctx, addr + 0x30, 16);     // tileMode = TM_LINEAR_SPECIAL
        ppc_store_u32(ctx, addr + 0x3C, w);      // pitch
        ppc_store_u32(ctx, addr + 0x24, pixel_addr); // image pointer

        ctx->r[3] = addr; ctx->r[4] = 0; // unit 0
        ppc_import_gx2_GX2SetPixelTexture(ctx);

        checkBool("GX2SetPixelTexture bound unit 0", g_bramble_gx2.texture_bound[BRAMBLE_GX2_SAMPLER_PIXEL_BASE + 0], 1);
        if (g_bramble_gx2.texture_bound[BRAMBLE_GX2_SAMPLER_PIXEL_BASE + 0]) {
            uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(g_bramble_gx2.texture_staging_mem_block[BRAMBLE_GX2_SAMPLER_PIXEL_BASE + 0]);
            int pixels_match = 1;
            for (y = 0; y < h && pixels_match; y++) {
                for (x = 0; x < w && pixels_match; x++) {
                    uint8_t v = (uint8_t)(200 + y * w + x);
                    uint8_t *px = staging + (y * w + x) * 4;
                    if (px[0] != v || px[1] != (uint8_t)(v + 1) || px[2] != (uint8_t)(v + 2) || px[3] != (uint8_t)(v + 3)) {
                        pixels_match = 0;
                    }
                }
            }
            checkBool("GX2SetPixelTexture staging buffer has correct real pixel data", pixels_match, 1);
        }

        // GX2SetVertexTexture uses a distinct, non-overlapping real
        // slot range (BRAMBLE_GX2_SAMPLER_VERTEX_BASE) -- confirm
        // binding unit 0 here doesn't disturb the pixel-stage binding
        // made just above.
        ctx->r[3] = addr; ctx->r[4] = 0; // unit 0
        ppc_import_gx2_GX2SetVertexTexture(ctx);
        checkBool("GX2SetVertexTexture bound unit 0 (distinct slot)", g_bramble_gx2.texture_bound[BRAMBLE_GX2_SAMPLER_VERTEX_BASE + 0], 1);
        checkBool("GX2SetPixelTexture's own slot still bound after GX2SetVertexTexture", g_bramble_gx2.texture_bound[BRAMBLE_GX2_SAMPLER_PIXEL_BASE + 0], 1);

        // Real, recorded GPU commands from both calls above (staging
        // copy + image descriptor push + texture bind) need to
        // actually submit and complete without hanging/crashing.
        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);
        checkpoint("[GX2SetPixelTexture/GX2SetVertexTexture] submitted+completed -- PASS (no hang/crash)");
    }

    checkpoint("=== self-test done: %d passed, %d failed ===", g_pass_count, g_fail_count);
}

#define GX2TEST_SWAP_COUNT_ADDR 0x2000u
#define GX2TEST_FLIP_COUNT_ADDR 0x2004u

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Bramble", 0777);
    g_log = fopen("sdmc:/switch/Bramble/gx2-test-results.log", "w");

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    static PpcContext ctx;
    static PpcSharedMemory shared;
    ctx.shared = &shared;

    // void GX2Init(uint32_t *attributes) -- real args ignored by this
    // shim's current implementation (see its own comment), r3 left 0.
    ppc_import_gx2_GX2Init(&ctx);
    checkpoint("GX2Init done");

    run_state_selftest(&ctx);

    // GX2GetSwapStatus/swap_count+flip_count real increment check, done
    // here (not in run_state_selftest) since it needs a real
    // GX2SwapScanBuffers call, which needs a real acquired framebuffer
    // -- GX2ClearColor below acquires one. Out-pointers are real *guest*
    // addresses (offsets into ctx->shared->mem, see ppc_store_u32 in
    // ppc_runtime.h), so results are read back via ppc_load_u32 at that
    // same offset, not by reading a host variable's address directly.
    uint32_t swap_before, flip_before;
    {
        ctx.r[3] = GX2TEST_SWAP_COUNT_ADDR;
        ctx.r[4] = GX2TEST_FLIP_COUNT_ADDR;
        ctx.r[5] = 0; ctx.r[6] = 0;
        ppc_import_gx2_GX2GetSwapStatus(&ctx);
        swap_before = ppc_load_u32(&ctx, GX2TEST_SWAP_COUNT_ADDR);
        flip_before = ppc_load_u32(&ctx, GX2TEST_FLIP_COUNT_ADDR);
    }

    // The self-test result is now visible from across the room, not just
    // in the log file: green means every check passed, red means at
    // least one failed -- a real, at-a-glance readout of the same
    // g_fail_count the log already reports, using the one real output
    // this .nro has (see file comment on why there's no on-screen text).
    int selftest_ok = (g_fail_count == 0);

    // Real, fixed number of frames to hold the result color on screen
    // before exiting on its own -- long enough to be seen/photographed
    // (~2 seconds at a real 60fps present rate) without needing a
    // physical + press, since this now runs unattended as part of a
    // test cycle. + still exits early if held/pressed sooner.
    #define GX2TEST_AUTO_EXIT_FRAMES 120

    int frame = 0;
    while (appletMainLoop() && frame < GX2TEST_AUTO_EXIT_FRAMES) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // void GX2ClearColor(GX2ColorBuffer *colorBuffer, float red,
        // float green, float blue, float alpha) -- real PPC ABI: r3 is
        // the (currently-ignored, see the shim's own comment)
        // colorBuffer pointer, the 4 floats go in f1-f4.
        ctx.r[3] = 0;
        if (selftest_ok) {
            ctx.f[1] = 0.10; ctx.f[2] = 0.70; ctx.f[3] = 0.20; /* green -- all checks passed */
        } else {
            ctx.f[1] = 0.70; ctx.f[2] = 0.10; ctx.f[3] = 0.10; /* red -- at least one check failed, check the log */
        }
        ctx.f[4] = 1.0;
        ppc_import_gx2_GX2ClearColor(&ctx);

        // void GX2SwapScanBuffers(void) -- presents the frame.
        ppc_import_gx2_GX2SwapScanBuffers(&ctx);

        if (frame == 0) {
            ctx.r[3] = GX2TEST_SWAP_COUNT_ADDR;
            ctx.r[4] = GX2TEST_FLIP_COUNT_ADDR;
            ctx.r[5] = 0; ctx.r[6] = 0;
            ppc_import_gx2_GX2GetSwapStatus(&ctx);
            uint32_t swap_after = ppc_load_u32(&ctx, GX2TEST_SWAP_COUNT_ADDR);
            uint32_t flip_after = ppc_load_u32(&ctx, GX2TEST_FLIP_COUNT_ADDR);
            checkBool("GX2GetSwapStatus.swapCount advanced after 1 real swap", swap_after > swap_before, 1);
            checkBool("GX2GetSwapStatus.flipCount advanced after 1 real swap", flip_after > flip_before, 1);
            checkpoint("first real frame swapped -- screen should now be %s", selftest_ok ? "green (all self-test checks passed)" : "red (a self-test check failed -- see log above)");
        }
        frame++;
    }

    checkpoint("exiting after %d frames, %d self-test checks passed, %d failed", frame, g_pass_count, g_fail_count);
    ppc_import_gx2_GX2Shutdown(&ctx);
    if (g_log) { fclose(g_log); g_log = NULL; }
    return 0;
}
