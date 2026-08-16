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

    int frame = 0;
    while (appletMainLoop()) {
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
