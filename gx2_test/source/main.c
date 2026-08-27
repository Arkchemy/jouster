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
// inspects `g_arkchemy_gx2`'s own shadow-state fields directly (visible
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
#include <math.h>
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

// Real, current frame number in the main per-frame loop below -- a plain
// global (not behind a lock/atomic: this is single-threaded, and the
// real libnx exception handler below runs on the *same* thread, at the
// exact point a fault occurred, so it always sees this variable's real,
// last-written value, not a stale or torn one). Exists so a real crash
// mid-loop (an actual unhandled hardware exception, not caught by any
// of the self-test's own checkpoint()-based logging, which only runs
// before this loop even starts) still records *which frame* it died on.
static volatile int g_current_frame = -1;

// Real, official libnx exception-handling hook (see devkitPro's own
// `exception-handler` example, `__libnx_exception_handler`) -- a real,
// user-installable fallback for genuine unhandled CPU exceptions
// (illegal instruction, bad memory access, etc.), running *before* the
// process is actually killed, so real diagnostic info can still be
// written to the SD card even when Atmosphère's own fatal-error/crash-
// report path (already used elsewhere in this project to diagnose real
// hardware bugs) is the only other place that info would otherwise
// land. Writes to its own, separate, dedicated log file -- deliberately
// not reusing `g_log`/`checkpoint()`, since a real fault could plausibly
// happen while `g_log` itself is mid-write; a fresh `fopen` here doesn't
// depend on that file's own state being consistent. Real register dump
// fields/names confirmed against libnx's own real
// `arm/thread_context.h` (`ThreadExceptionDump`), same struct/field set
// as devkitPro's own official example. */
alignas(16) static u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    FILE *f = fopen("sdmc:/switch/Jouster/exception-dump.log", "w");
    int i;
    if (!f) return;

    fprintf(f, "real, unhandled hardware exception caught by this .nro's own fallback handler\n");
    fprintf(f, "current frame at time of fault: %d\n", g_current_frame);
    fprintf(f, "self-test result so far: %d passed, %d failed\n", g_pass_count, g_fail_count);
    fprintf(f, "error_desc: 0x%x\n", ctx->error_desc);
    for (i = 0; i < 29; i++) fprintf(f, "[X%d]: 0x%lx\n", i, (unsigned long)ctx->cpu_gprs[i].x);
    fprintf(f, "fp: 0x%lx\n", (unsigned long)ctx->fp.x);
    fprintf(f, "lr: 0x%lx\n", (unsigned long)ctx->lr.x);
    fprintf(f, "sp: 0x%lx\n", (unsigned long)ctx->sp.x);
    fprintf(f, "pc: 0x%lx\n", (unsigned long)ctx->pc.x);
    fprintf(f, "pstate: 0x%x\n", ctx->pstate);
    fprintf(f, "esr: 0x%x\n", ctx->esr);
    fprintf(f, "far: 0x%lx\n", (unsigned long)ctx->far.x);
    fclose(f);

    // Also append the same summary to the main log, if it's still in a
    // writable state -- real, best-effort, not depended on (the dedicated
    // file above is the one guaranteed-real record).
    if (g_log) {
        fprintf(g_log, "*** UNHANDLED EXCEPTION at frame %d -- see exception-dump.log for full register state ***\n", g_current_frame);
        fflush(g_log);
    }
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
    // arkchemy_gx2_logic_op_to_dk's own table.
    ctx->r[3] = 0xF0; ctx->r[4] = 0x01; ctx->r[5] = 0; ctx->r[6] = 1;
    ppc_import_gx2_GX2SetColorControl(ctx);
    checkBool("GX2SetColorControl.logicOp", (int)g_arkchemy_gx2.color_state.logicOp, (int)DkLogicOp_Set);
    checkBool("GX2SetColorControl.blendEnableMask", (int)g_arkchemy_gx2.color_state.blendEnableMask, 0x01);
    checkBool("GX2SetColorControl.write_enable", (int)g_arkchemy_gx2.color_write_enable, 1);

    // GX2SetAlphaTest(alphaTest=TRUE, func=GREATER(4), ref=0.5) -- real
    // shadow-state fix under test: this must NOT reset the logicOp
    // GX2SetColorControl just set above (that was the actual bug this
    // shared shadow-state refactor fixed).
    ctx->r[3] = 1; ctx->r[4] = 4; ctx->f[1] = 0.5;
    ppc_import_gx2_GX2SetAlphaTest(ctx);
    checkBool("GX2SetAlphaTest.alphaCompareOp", (int)g_arkchemy_gx2.color_state.alphaCompareOp, (int)DkCompareOp_Greater);
    checkBool("GX2SetAlphaTest preserves prior logicOp", (int)g_arkchemy_gx2.color_state.logicOp, (int)DkLogicOp_Set);

    // GX2SetTargetChannelMasks(mask0=RGBA(15), mask1..7=R(1)) -- real
    // precedence-fix under test: channel_masks is now a source of truth
    // independent of GX2SetColorControl's color_write_enable, combined
    // at bind time by arkchemy_gx2_rebind_color_write_state instead of
    // either call overwriting the other's setting outright.
    ctx->r[3] = 15; ctx->r[4] = 1; ctx->r[5] = 1; ctx->r[6] = 1;
    ctx->r[7] = 1; ctx->r[8] = 1; ctx->r[9] = 1; ctx->r[10] = 1;
    ppc_import_gx2_GX2SetTargetChannelMasks(ctx);
    checkBool("GX2SetTargetChannelMasks[0]", (int)(g_arkchemy_gx2.channel_masks & 0xF), 15);
    checkBool("GX2SetTargetChannelMasks[1]", (int)((g_arkchemy_gx2.channel_masks >> 4) & 0xF), 1);

    // Real regression test for the precedence bug the shadow-state
    // fix addressed: calling GX2SetColorControl again afterward (e.g.
    // just to toggle blend for an unrelated reason, a real, plausible
    // per-draw pattern) must NOT reset the narrower per-channel mask
    // GX2SetTargetChannelMasks just set on target 1.
    ctx->r[3] = 0xF0; ctx->r[4] = 0x01; ctx->r[5] = 0; ctx->r[6] = 1;
    ppc_import_gx2_GX2SetColorControl(ctx);
    checkBool("GX2SetColorControl preserves target 1's channel mask", (int)((g_arkchemy_gx2.channel_masks >> 4) & 0xF), 1);

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
    checkBool("GX2SetPolygonControl.cullMode", (int)g_arkchemy_gx2.rasterizer_state.cullMode, (int)DkFace_Front);
    checkBool("GX2SetPolygonControl.polygonModeFront", (int)g_arkchemy_gx2.rasterizer_state.polygonModeFront, (int)DkPolygonMode_Fill);

    // GX2SetRasterizerClipControl(rasterizer=1, zclipEnable=0) -- real
    // shadow-state fix under test: must not reset the cullMode
    // GX2SetPolygonControl just set above. zclipEnable=0 ->
    // depthClampEnable=1 (the documented inverse mapping).
    ctx->r[3] = 1; ctx->r[4] = 0;
    ppc_import_gx2_GX2SetRasterizerClipControl(ctx);
    checkBool("GX2SetRasterizerClipControl.depthClampEnable", (int)g_arkchemy_gx2.rasterizer_state.depthClampEnable, 1);
    checkBool("GX2SetRasterizerClipControl preserves prior cullMode", (int)g_arkchemy_gx2.rasterizer_state.cullMode, (int)DkFace_Front);

    // GX2SetAlphaToMask(alphaToMask=1, mode=DITHER_90(2)) -- any
    // nonzero mode maps to dither=1 per the documented collapse.
    ctx->r[3] = 1; ctx->r[4] = 2;
    ppc_import_gx2_GX2SetAlphaToMask(ctx);
    checkBool("GX2SetAlphaToMask.alphaToCoverageEnable", (int)g_arkchemy_gx2.multisample_state.alphaToCoverageEnable, 1);
    checkBool("GX2SetAlphaToMask.alphaToCoverageDither", (int)g_arkchemy_gx2.multisample_state.alphaToCoverageDither, 1);

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
    checkBool("GX2SetDepthStencilControl.depthCompareOp", (int)g_arkchemy_gx2.depth_stencil_state.depthCompareOp, (int)DkCompareOp_Less);
    checkBool("GX2SetDepthStencilControl.stencilTestEnable", (int)g_arkchemy_gx2.depth_stencil_state.stencilTestEnable, 1);

    // GX2SetDepthOnlyControl(depthTest=0, depthWrite=0,
    // depthCompare=ALWAYS(7)) -- real bugfix under test: must NOT
    // force-disable the stencilTestEnable GX2SetDepthStencilControl
    // just set above.
    ctx->r[3] = 0; ctx->r[4] = 0; ctx->r[5] = 7;
    ppc_import_gx2_GX2SetDepthOnlyControl(ctx);
    checkBool("GX2SetDepthOnlyControl.depthCompareOp", (int)g_arkchemy_gx2.depth_stencil_state.depthCompareOp, (int)DkCompareOp_Always);
    checkBool("GX2SetDepthOnlyControl preserves prior stencilTestEnable", (int)g_arkchemy_gx2.depth_stencil_state.stencilTestEnable, 1);

    // GX2GetDisplayListWriteStatus() -- always FALSE, honestly (no
    // display-list recording implemented yet).
    ppc_import_gx2_GX2GetDisplayListWriteStatus(ctx);
    checkBool("GX2GetDisplayListWriteStatus", (int)ctx->r[3], 0);

    // GX2Flush()/GX2GetLastSubmittedTimeStamp()/GX2DrawDone(): a real
    // submit must advance submitted_timestamp, and GX2DrawDone must
    // return TRUE and catch retired_timestamp up to it.
    uint64_t before = g_arkchemy_gx2.submitted_timestamp;
    ppc_import_gx2_GX2Flush(ctx);
    checkBool("GX2Flush advances submitted_timestamp", g_arkchemy_gx2.submitted_timestamp > before, 1);
    ppc_import_gx2_GX2GetLastSubmittedTimeStamp(ctx);
    uint64_t last_submitted = ((uint64_t)ctx->r[3] << 32) | (uint64_t)ctx->r[4];
    checkU64("GX2GetLastSubmittedTimeStamp matches shadow state", last_submitted, g_arkchemy_gx2.submitted_timestamp);
    ppc_import_gx2_GX2DrawDone(ctx);
    checkBool("GX2DrawDone returns TRUE", (int)ctx->r[3], 1);
    ppc_import_gx2_GX2GetRetiredTimeStamp(ctx);
    uint64_t last_retired = ((uint64_t)ctx->r[3] << 32) | (uint64_t)ctx->r[4];
    checkU64("GX2GetRetiredTimeStamp caught up after GX2DrawDone", last_retired, g_arkchemy_gx2.submitted_timestamp);

    // GX2WaitTimeStamp(time=retired_timestamp) -- already retired, must
    // return TRUE without needing a real wait.
    uint64_t retired = g_arkchemy_gx2.retired_timestamp;
    ctx->r[3] = (uint32_t)(retired >> 32);
    ctx->r[4] = (uint32_t)retired;
    ppc_import_gx2_GX2WaitTimeStamp(ctx);
    checkBool("GX2WaitTimeStamp(already-retired) returns TRUE", (int)ctx->r[3], 1);

    // GX2WaitForVsync() -- real, safe to call with nothing pending;
    // just confirms it doesn't hang/crash (no return value to check).
    ppc_import_gx2_GX2WaitForVsync(ctx);
    checkpoint("[GX2WaitForVsync] returned -- PASS (no hang/crash)");

    // The functions below are all backend-independent (pure guest-memory
    // writes, no deko3d call, no g_arkchemy_gx2 shadow state involved) --
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

        uint8_t *pool = (uint8_t *)dkMemBlockGetCpuAddr(g_arkchemy_gx2.sampler_descriptor_mem_block);
        uint8_t *pixel_slot = pool + 0 * sizeof(DkSamplerDescriptor);
        uint8_t *vertex_slot = pool + ARKCHEMY_GX2_SAMPLER_VERTEX_BASE * sizeof(DkSamplerDescriptor);
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
        pool = (uint8_t *)dkMemBlockGetCpuAddr(g_arkchemy_gx2.sampler_descriptor_mem_block);
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

        checkBool("GX2SetColorBuffer bound target 0", g_arkchemy_gx2.color_target_bound[0], 1);
        if (g_arkchemy_gx2.color_target_bound[0]) {
            uint8_t *dest = (uint8_t *)dkMemBlockGetCpuAddr(g_arkchemy_gx2.color_target_mem_block[0]);
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
        checkBool("GX2SetColorBuffer re-bind still bound", g_arkchemy_gx2.color_target_bound[0], 1);
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

        checkBool("GX2SetDepthBuffer bound", g_arkchemy_gx2.depth_target_bound, 1);
        if (g_arkchemy_gx2.depth_target_bound) {
            uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(g_arkchemy_gx2.depth_target_staging_mem_block);
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
    // one -- see arkchemy_gx2_set_texture's own comment), plus a real
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

        checkBool("GX2SetPixelTexture bound unit 0", g_arkchemy_gx2.texture_bound[ARKCHEMY_GX2_SAMPLER_PIXEL_BASE + 0], 1);
        if (g_arkchemy_gx2.texture_bound[ARKCHEMY_GX2_SAMPLER_PIXEL_BASE + 0]) {
            uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(g_arkchemy_gx2.texture_staging_mem_block[ARKCHEMY_GX2_SAMPLER_PIXEL_BASE + 0]);
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
        // slot range (ARKCHEMY_GX2_SAMPLER_VERTEX_BASE) -- confirm
        // binding unit 0 here doesn't disturb the pixel-stage binding
        // made just above.
        ctx->r[3] = addr; ctx->r[4] = 0; // unit 0
        ppc_import_gx2_GX2SetVertexTexture(ctx);
        checkBool("GX2SetVertexTexture bound unit 0 (distinct slot)", g_arkchemy_gx2.texture_bound[ARKCHEMY_GX2_SAMPLER_VERTEX_BASE + 0], 1);
        checkBool("GX2SetPixelTexture's own slot still bound after GX2SetVertexTexture", g_arkchemy_gx2.texture_bound[ARKCHEMY_GX2_SAMPLER_PIXEL_BASE + 0], 1);

        // Real, recorded GPU commands from both calls above (staging
        // copy + image descriptor push + texture bind) need to
        // actually submit and complete without hanging/crashing.
        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);
        checkpoint("[GX2SetPixelTexture/GX2SetVertexTexture] submitted+completed -- PASS (no hang/crash)");
    }

    // GX2CopySurface(src@0xA000, srcLevel=0, srcSlice=0, dst@0xB000,
    // dstLevel=0, dstSlice=0) -- pure guest-memory-to-guest-memory copy,
    // no deko3d involved at all, so this can assert exact byte values
    // (same reasoning as the GX2SetColorBuffer pixel-copy check above).
    {
        uint32_t src_addr = 0xA000, src_pixel_addr = 0xA100;
        uint32_t dst_addr = 0xB000, dst_pixel_addr = 0xB100;
        uint32_t w = 4, h = 4, x, y;

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint8_t v = (uint8_t)(50 + y * w + x);
                uint32_t p = src_pixel_addr + (y * w + x) * 4;
                ppc_store_u8(ctx, p + 0, v);
                ppc_store_u8(ctx, p + 1, (uint8_t)(v + 1));
                ppc_store_u8(ctx, p + 2, (uint8_t)(v + 2));
                ppc_store_u8(ctx, p + 3, (uint8_t)(v + 3));
            }
        }
        // Sentinel dst bytes so an unwanted (mismatched-scope) copy would be caught.
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint32_t p = dst_pixel_addr + (y * w + x) * 4;
                ppc_store_u8(ctx, p + 0, 0xEE); ppc_store_u8(ctx, p + 1, 0xEE);
                ppc_store_u8(ctx, p + 2, 0xEE); ppc_store_u8(ctx, p + 3, 0xEE);
            }
        }

        ppc_store_u32(ctx, src_addr + 0x00, 1); ppc_store_u32(ctx, src_addr + 0x04, w);
        ppc_store_u32(ctx, src_addr + 0x08, h); ppc_store_u32(ctx, src_addr + 0x10, 1);
        ppc_store_u32(ctx, src_addr + 0x14, 0x1a); ppc_store_u32(ctx, src_addr + 0x30, 16);
        ppc_store_u32(ctx, src_addr + 0x3C, w); ppc_store_u32(ctx, src_addr + 0x24, src_pixel_addr);

        ppc_store_u32(ctx, dst_addr + 0x00, 1); ppc_store_u32(ctx, dst_addr + 0x04, w);
        ppc_store_u32(ctx, dst_addr + 0x08, h); ppc_store_u32(ctx, dst_addr + 0x10, 1);
        ppc_store_u32(ctx, dst_addr + 0x14, 0x1a); ppc_store_u32(ctx, dst_addr + 0x30, 16);
        ppc_store_u32(ctx, dst_addr + 0x3C, w); ppc_store_u32(ctx, dst_addr + 0x24, dst_pixel_addr);

        ctx->r[3] = src_addr; ctx->r[4] = 0; ctx->r[5] = 0;
        ctx->r[6] = dst_addr; ctx->r[7] = 0; ctx->r[8] = 0;
        ppc_import_gx2_GX2CopySurface(ctx);

        int pixels_match = 1;
        for (y = 0; y < h && pixels_match; y++) {
            for (x = 0; x < w && pixels_match; x++) {
                uint8_t v = (uint8_t)(50 + y * w + x);
                uint32_t p = dst_pixel_addr + (y * w + x) * 4;
                if (ppc_load_u8(ctx, p + 0) != v || ppc_load_u8(ctx, p + 1) != (uint8_t)(v + 1) ||
                    ppc_load_u8(ctx, p + 2) != (uint8_t)(v + 2) || ppc_load_u8(ctx, p + 3) != (uint8_t)(v + 3)) {
                    pixels_match = 0;
                }
            }
        }
        checkBool("GX2CopySurface copied real pixel data correctly", pixels_match, 1);
    }

    // GX2CalcTVSize/GX2CalcDRCSize -- pure calculation + guest-memory
    // writes, no deko3d involved, so real, exact expected values can be
    // hand-computed and asserted directly (same reasoning as
    // GX2CalcSurfaceSizeAndAlignment's own checks above).
    {
        uint32_t size_addr = 0xC000, unk_addr = 0xC004;

        // WIDE_720P(3), UNORM_R8_G8_B8_A8(0x1a), DOUBLE(2) buffering:
        // 1280*720*4*2 = 7372800.
        ctx->r[3] = 3; ctx->r[4] = 0x1a; ctx->r[5] = 2; ctx->r[6] = size_addr; ctx->r[7] = unk_addr;
        ppc_import_gx2_GX2CalcTVSize(ctx);
        checkBool("GX2CalcTVSize(WIDE_720P, RGBA8, DOUBLE).size", (int)ppc_load_u32(ctx, size_addr), 1280 * 720 * 4 * 2);
        checkBool("GX2CalcTVSize.unkOut", (int)ppc_load_u32(ctx, unk_addr), 0);

        // SINGLE(1) DRC buffering: 864*480*4*1 = 1658880.
        ctx->r[3] = 1; ctx->r[4] = 0x1a; ctx->r[5] = 1; ctx->r[6] = size_addr; ctx->r[7] = unk_addr;
        ppc_import_gx2_GX2CalcDRCSize(ctx);
        checkBool("GX2CalcDRCSize(SINGLE, RGBA8).size", (int)ppc_load_u32(ctx, size_addr), 864 * 480 * 4);
    }

    // GX2CopyColorBufferToScanBuffer(colorBuffer@0xD000, scanTarget=0) --
    // real, recorded dkCmdBufCopyImage into the live swapchain
    // framebuffer. Unlike GX2SetColorBuffer's own pitch-linear
    // destination (real, direct CPU-readable bytes), the actual
    // destination here is a real block-linear swapchain image with no
    // CPU readback path in this project's design (same real limitation
    // GX2SetDepthBuffer's own block-linear image already has) -- what
    // real hardware *can* confirm here: the real
    // dkMemBlockCreate/dkImageInitialize/dkCmdBufCopyImage calls all
    // succeed and actually submit+complete without crashing/hanging,
    // the real, meaningful risk (wrong format/flags/rect bounds would
    // show up here).
    {
        uint32_t addr = 0xD000, pixel_addr = 0xD100;
        uint32_t w = 4, h = 4, x, y;

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint8_t v = (uint8_t)(150 + y * w + x);
                uint32_t p = pixel_addr + (y * w + x) * 4;
                ppc_store_u8(ctx, p + 0, v);
                ppc_store_u8(ctx, p + 1, (uint8_t)(v + 1));
                ppc_store_u8(ctx, p + 2, (uint8_t)(v + 2));
                ppc_store_u8(ctx, p + 3, (uint8_t)(v + 3));
            }
        }

        ppc_store_u32(ctx, addr + 0x00, 1); ppc_store_u32(ctx, addr + 0x04, w);
        ppc_store_u32(ctx, addr + 0x08, h); ppc_store_u32(ctx, addr + 0x10, 1);
        ppc_store_u32(ctx, addr + 0x14, 0x1a); ppc_store_u32(ctx, addr + 0x30, 16);
        ppc_store_u32(ctx, addr + 0x3C, w); ppc_store_u32(ctx, addr + 0x24, pixel_addr);

        ctx->r[3] = addr; ctx->r[4] = 0; // scanTarget 0 (TV)
        ppc_import_gx2_GX2CopyColorBufferToScanBuffer(ctx);

        checkBool("GX2CopyColorBufferToScanBuffer real temp source bound", g_arkchemy_gx2.scan_copy_temp_bound, 1);

        // Real, recorded GPU command from this call needs to actually
        // submit and complete without hanging/crashing.
        ppc_import_gx2_GX2Flush(ctx);
        ppc_import_gx2_GX2DrawDone(ctx);
        checkpoint("[GX2CopyColorBufferToScanBuffer] submitted+completed -- PASS (no hang/crash)");
    }

    checkpoint("=== self-test done: %d passed, %d failed ===", g_pass_count, g_fail_count);
}

// Second self-test phase: the real GX2* functions this project has
// actually implemented but that no hardware run has ever exercised --
// every one of the checks above (and every one in test-results/) covers
// the state-setter family reached from GX2Init/GX2ClearColor/
// GX2SwapScanBuffers; the ~30 functions below have only ever been
// verified by reading the code. That gap is exactly the kind of thing
// this project found real bugs in before (the rodata-table addressing
// bug, the border-color self-test bug), so they get the same real
// treatment here rather than being assumed correct.
//
// Two genuinely different kinds of check live in here, and the
// difference is worth stating plainly rather than blurring:
//
//  * Functions whose whole real effect is a value returned in r3 or a
//    write into *guest* memory (GX2GetSurfaceFormatBits,
//    GX2SetClearDepth, the GX2InitSampler* family, ...) touch no deko3d
//    state at all, so their real, exact expected values can be asserted
//    outright -- same reasoning as GX2CalcSurfaceSizeAndAlignment's own
//    checks in the phase above.
//
//  * Functions that only record into the real deko3d command buffer
//    (GX2SetViewport, GX2SetScissor, GX2SetBlendControl, ...) keep no
//    shadow state this file can read back, and deko3d exposes no way to
//    query recorded state -- so an honest check here is *not* "the GPU
//    is now in state X", it's "these real commands, with real
//    game-plausible arguments, actually record, submit and complete on
//    real hardware without faulting or hanging". That is a genuinely
//    weaker claim than the assertions above and is deliberately not
//    dressed up as anything more; it is also the exact failure mode
//    that matters for these calls (a bad enum reaching a lookup table,
//    an out-of-range target index, a malformed viewport), and it is
//    unreachable without running them on a real console.
static void run_untested_surface_selftest(PpcContext *ctx) {
    checkpoint("=== GX2 previously-unexercised surface self-test ===");

    // ---- Fixed-answer queries. Real hardware facts sourced from Cemu's
    // own real HLE implementation (see each function's own comment in
    // cafeos_gx2.h), not guessed -- so an exact compare is right here.
    ppc_import_gx2_GX2TempGetGPUVersion(ctx);
    checkBool("GX2TempGetGPUVersion", (int)ctx->r[3], 2);
    ppc_import_gx2_GX2GetSystemTVScanMode(ctx);
    checkBool("GX2GetSystemTVScanMode", (int)ctx->r[3], 7);
    ppc_import_gx2_GX2GetSystemTVAspectRatio(ctx);
    checkBool("GX2GetSystemTVAspectRatio", (int)ctx->r[3], 1);

    // GX2GetSurfaceFormatBits -- real table-driven bit depth. Three
    // real cases, each covering a distinct branch of the real formula:
    // a plain 32-bit format, the same format carrying GX2's real
    // upper-bit flags (which must be masked off, not folded into the
    // table index), and a real BC-compressed format (whose table entry
    // is a per-block size the function divides by 16 to get real bits
    // per pixel).
    ctx->r[3] = 0x01a; /* UNORM_R8_G8_B8_A8 */
    ppc_import_gx2_GX2GetSurfaceFormatBits(ctx);
    checkBool("GX2GetSurfaceFormatBits(RGBA8)", (int)ctx->r[3], 32);
    ctx->r[3] = 0x41a; /* SRGB_R8_G8_B8_A8 -- same hw format, real flag bits set above bit 5 */
    ppc_import_gx2_GX2GetSurfaceFormatBits(ctx);
    checkBool("GX2GetSurfaceFormatBits(SRGB_RGBA8) masks flag bits", (int)ctx->r[3], 32);
    ctx->r[3] = 0x031; /* UNORM_BC1 -- real 64-bit block / 16 pixels */
    ppc_import_gx2_GX2GetSurfaceFormatBits(ctx);
    checkBool("GX2GetSurfaceFormatBits(BC1) per-block divide", (int)ctx->r[3], 4);

    // ---- Real guest-memory writers, at 0xE000+ to stay clear of every
    // scratch region the first phase already used.

    // GX2SetClearDepth(depthBuffer@0xE000, 0.75) / GX2SetClearStencil
    // (depthBuffer@0xE000, 0x1AB) -- the single-field siblings of
    // GX2SetClearDepthStencil (already covered in the phase above),
    // writing the same real WUT_CHECK_OFFSET-confirmed 0x88/0x8C
    // fields. The stencil value here deliberately overflows 8 bits:
    // real GX2 masks it to 0xFF, so 0x1AB must land as 0xAB, and each
    // call must leave the *other* field alone (they are independent
    // real API calls, unlike the combined one).
    ctx->r[3] = 0xE000; ctx->f[1] = 0.75;
    ppc_import_gx2_GX2SetClearDepth(ctx);
    ctx->r[3] = 0xE000; ctx->r[4] = 0x1AB;
    ppc_import_gx2_GX2SetClearStencil(ctx);
    checkBool("GX2SetClearDepth.depthClear", ppc_load_f32(ctx, 0xE000 + 0x88) == 0.75f, 1);
    checkBool("GX2SetClearStencil masks to 8 bits", (int)ppc_load_u32(ctx, 0xE000 + 0x8C), 0xAB);
    checkBool("GX2SetClearStencil preserves GX2SetClearDepth's field", ppc_load_f32(ctx, 0xE000 + 0x88) == 0.75f, 1);

    // GX2CalcColorBufferAuxInfo(colorBuffer@0xE000, sizeOut@0xE100,
    // alignOut@0xE104) -- real Cemu HLE behavior is the same kind of
    // fixed 0x1000/0x100 answer GX2CalcDepthBufferHiZInfo gives (see
    // its own comment); asserted exactly, same as that one.
    ctx->r[3] = 0xE000; ctx->r[4] = 0xE100; ctx->r[5] = 0xE104;
    ppc_import_gx2_GX2CalcColorBufferAuxInfo(ctx);
    checkBool("GX2CalcColorBufferAuxInfo.size", (int)ppc_load_u32(ctx, 0xE100), 0x1000);
    checkBool("GX2CalcColorBufferAuxInfo.align", (int)ppc_load_u32(ctx, 0xE104), 0x100);

    // GX2CalcFetchShaderSizeEx / GX2EndDisplayList -- real, honest
    // fixed answers this project's own shim documents (a fixed 256-byte
    // fetch-shader allocation; no display-list recording implemented,
    // so a recorded size of 0). Checked here so a future change to
    // either can't silently drift away from what the rest of the shim
    // assumes.
    ctx->r[3] = 4; ctx->r[4] = 0; ctx->r[5] = 0;
    ppc_import_gx2_GX2CalcFetchShaderSizeEx(ctx);
    checkBool("GX2CalcFetchShaderSizeEx", (int)ctx->r[3], 256);
    ctx->r[3] = 0;
    ppc_import_gx2_GX2EndDisplayList(ctx);
    checkBool("GX2EndDisplayList reports 0 bytes recorded", (int)ctx->r[3], 0);

    // ---- The rest of the GX2InitSampler* family (sampler@0xE200), all
    // real bit-packers into the same shared WORD0 the already-verified
    // GX2InitSampler/GX2InitSamplerLOD/GX2InitSamplerDepthCompare write
    // -- so this is both a field-encoding check and the same real
    // shared-state question the phase above kept asking of the
    // GX2Set*Control family: does each one leave the others' fields
    // alone?
    ctx->r[3] = 0xE200; ctx->r[4] = 0; ctx->r[5] = 0; /* GX2InitSampler(WRAP, POINT) -- real baseline */
    ppc_import_gx2_GX2InitSampler(ctx);

    // GX2InitSamplerClamping(sampler, clampX=CLAMP(2), clampY=MIRROR(1),
    // clampZ=CLAMP_BORDER(6)) -- real 3-bit fields at bits 0/3/6.
    ctx->r[3] = 0xE200; ctx->r[4] = 2; ctx->r[5] = 1; ctx->r[6] = 6;
    ppc_import_gx2_GX2InitSamplerClamping(ctx);
    {
        uint32_t w0 = ppc_load_u32(ctx, 0xE200);
        checkBool("GX2InitSamplerClamping.CLAMP_X", (int)(w0 & 0x7), 2);
        checkBool("GX2InitSamplerClamping.CLAMP_Y", (int)((w0 >> 3) & 0x7), 1);
        checkBool("GX2InitSamplerClamping.CLAMP_Z", (int)((w0 >> 6) & 0x7), 6);
    }

    // GX2InitSamplerXYFilter(sampler, filterMag=LINEAR(1),
    // filterMin=LINEAR(1), maxAniso=RATIO_2_1(2)) -- real, non-obvious
    // hardware behavior worth pinning down on real silicon: with
    // anisotropy requested, POINT(0)/LINEAR(1) are remapped to the
    // hardware's real anisotropic filter codes 4/5, not written
    // through unchanged.
    ctx->r[3] = 0xE200; ctx->r[4] = 1; ctx->r[5] = 1; ctx->r[6] = 2;
    ppc_import_gx2_GX2InitSamplerXYFilter(ctx);
    {
        uint32_t w0 = ppc_load_u32(ctx, 0xE200);
        checkBool("GX2InitSamplerXYFilter(aniso).XY_MAG_FILTER remapped", (int)((w0 >> 9) & 0x7), 5);
        checkBool("GX2InitSamplerXYFilter(aniso).XY_MIN_FILTER remapped", (int)((w0 >> 12) & 0x7), 5);
        checkBool("GX2InitSamplerXYFilter(aniso).MAX_ANISO_RATIO", (int)((w0 >> 19) & 0x7), 2);
    }

    // Same call with maxAniso=NONE(0): the remap above must NOT happen
    // -- the filter codes go through exactly as given (LINEAR(1) stays
    // 1, POINT(0) stays 0). The real branch the case above can't reach.
    ctx->r[3] = 0xE200; ctx->r[4] = 1; ctx->r[5] = 0; ctx->r[6] = 0;
    ppc_import_gx2_GX2InitSamplerXYFilter(ctx);
    {
        uint32_t w0 = ppc_load_u32(ctx, 0xE200);
        checkBool("GX2InitSamplerXYFilter(no aniso).XY_MAG_FILTER verbatim", (int)((w0 >> 9) & 0x7), 1);
        checkBool("GX2InitSamplerXYFilter(no aniso).XY_MIN_FILTER verbatim", (int)((w0 >> 12) & 0x7), 0);
        checkBool("GX2InitSamplerXYFilter(no aniso).MAX_ANISO_RATIO", (int)((w0 >> 19) & 0x7), 0);
    }

    // GX2InitSamplerZMFilter(sampler, zFilter=POINT(1), mipFilter=
    // LINEAR(2)) -- real 2-bit fields at bits 15/17, and the real
    // shared-WORD0 question: this must not disturb the clamp fields
    // GX2InitSamplerClamping set above, nor the filter fields
    // GX2InitSamplerXYFilter just set.
    ctx->r[3] = 0xE200; ctx->r[4] = 1; ctx->r[5] = 2;
    ppc_import_gx2_GX2InitSamplerZMFilter(ctx);
    {
        uint32_t w0 = ppc_load_u32(ctx, 0xE200);
        checkBool("GX2InitSamplerZMFilter.Z_FILTER", (int)((w0 >> 15) & 0x3), 1);
        checkBool("GX2InitSamplerZMFilter.MIP_FILTER", (int)((w0 >> 17) & 0x3), 2);
        checkBool("GX2InitSamplerZMFilter preserves CLAMP_X", (int)(w0 & 0x7), 2);
        checkBool("GX2InitSamplerZMFilter preserves XY_MAG_FILTER", (int)((w0 >> 9) & 0x7), 1);
    }

    // ---- GX2SetEventCallback: real registration state (not a deko3d
    // call and not a guest-memory write -- it stores into this shim's
    // own g_arkchemy_gx2 event tables), including the real GX2 contract
    // that registering a callback returns whatever was registered
    // before it. Checked without assuming the slot starts empty:
    // register once, then register again and require the second call to
    // hand back exactly what the first one installed.
    {
        ctx->r[3] = 0; ctx->r[4] = 0x11112222; ctx->r[5] = 0xAAAA0001; /* type=GX2_EVENT_TYPE_VSYNC(0) */
        ppc_import_gx2_GX2SetEventCallback(ctx);
        ctx->r[3] = 0; ctx->r[4] = 0x33334444; ctx->r[5] = 0xAAAA0002;
        ppc_import_gx2_GX2SetEventCallback(ctx);
        checkBool("GX2SetEventCallback returns the previously-registered callback",
                  (int)(ctx->r[3] == 0x11112222), 1);
        checkBool("GX2SetEventCallback stored the new callback",
                  (int)(g_arkchemy_gx2.event_callback_func[0] == 0x33334444), 1);
        checkBool("GX2SetEventCallback stored the new user data",
                  (int)(g_arkchemy_gx2.event_callback_userdata[0] == 0xAAAA0002), 1);

        // Real out-of-range event type (>= ARKCHEMY_GX2_NUM_EVENT_TYPES):
        // must report no previous callback and, more importantly, must
        // not write past the real, fixed-size table -- checked by
        // confirming slot 0's registration above is still intact.
        ctx->r[3] = ARKCHEMY_GX2_NUM_EVENT_TYPES + 3; ctx->r[4] = 0x55556666; ctx->r[5] = 0;
        ppc_import_gx2_GX2SetEventCallback(ctx);
        checkBool("GX2SetEventCallback(out-of-range type) returns 0", (int)ctx->r[3], 0);
        checkBool("GX2SetEventCallback(out-of-range type) left the real table untouched",
                  (int)(g_arkchemy_gx2.event_callback_func[0] == 0x33334444), 1);
    }

    // ---- GX2SetCullOnlyControl: the one deko3d-backed function in this
    // phase that *does* keep readable shadow state (it shares
    // g_arkchemy_gx2.rasterizer_state with GX2SetPolygonControl, exactly
    // like GX2SetRasterizerClipControl does), so it gets a real
    // assertion rather than the weaker submit-check the rest get.
    // frontFace=CCW(0), cullFront=0, cullBack=1 -> DkFace_Back, and the
    // polygon mode GX2SetPolygonControl set in the phase above must
    // survive it.
    ctx->r[3] = 0; ctx->r[4] = 0; ctx->r[5] = 1;
    ppc_import_gx2_GX2SetCullOnlyControl(ctx);
    checkBool("GX2SetCullOnlyControl.cullMode", (int)g_arkchemy_gx2.rasterizer_state.cullMode, (int)DkFace_Back);
    checkBool("GX2SetCullOnlyControl preserves prior polygonModeFront",
              (int)g_arkchemy_gx2.rasterizer_state.polygonModeFront, (int)DkPolygonMode_Fill);

    // ---- Command-buffer-only setters. Real, game-plausible arguments
    // (a full 1280x720 viewport/scissor matching this project's own real
    // framebuffer size, ordinary alpha blending, a standard 0xFFFF
    // primitive-restart index), recorded into the real command buffer
    // and then actually submitted below. No shadow state exists to
    // assert -- see this function's own header comment on why that makes
    // this a weaker but still real check.
    ctx->f[1] = 0.0; ctx->f[2] = 0.0; ctx->f[3] = 1280.0; ctx->f[4] = 720.0; ctx->f[5] = 0.0; ctx->f[6] = 1.0;
    ppc_import_gx2_GX2SetViewport(ctx);
    ctx->r[3] = 0; ctx->r[4] = 0; ctx->r[5] = 1280; ctx->r[6] = 720;
    ppc_import_gx2_GX2SetScissor(ctx);
    ctx->f[1] = 2.0;
    ppc_import_gx2_GX2SetLineWidth(ctx);
    ctx->f[1] = 4.0; ctx->f[2] = 4.0;
    ppc_import_gx2_GX2SetPointSize(ctx);
    /* GX2SetPolygonOffset(frontOffset, frontScale, backOffset, backScale, clamp) -- all five real floats in f1-f5 */
    ctx->f[1] = 1.0; ctx->f[2] = 2.0; ctx->f[3] = 3.0; ctx->f[4] = 4.0; ctx->f[5] = 0.5;
    ppc_import_gx2_GX2SetPolygonOffset(ctx);
    ctx->f[1] = 0.25; ctx->f[2] = 0.5; ctx->f[3] = 0.75; ctx->f[4] = 1.0;
    ppc_import_gx2_GX2SetBlendConstantColor(ctx);
    /* GX2SetStencilMask(frontMask, frontWriteMask, frontRef, backMask, backWriteMask, backRef) */
    ctx->r[3] = 0xFF; ctx->r[4] = 0xFF; ctx->r[5] = 1;
    ctx->r[6] = 0x0F; ctx->r[7] = 0x0F; ctx->r[8] = 2;
    ppc_import_gx2_GX2SetStencilMask(ctx);
    ctx->r[3] = 0xFFFF;
    ppc_import_gx2_GX2SetPrimitiveRestartIndex(ctx);

    // GX2SetBlendControl twice, deliberately at both ends of the real
    // GX2BlendMode/GX2BlendCombineMode enum ranges: ordinary alpha
    // blending first, then the highest real values either enum defines
    // (INV_CONSTANT_ALPHA(20), REV_SUB(4)) on the highest real render
    // target index (7). Those maxima are exactly where an off-by-one in
    // this shim's own real lookup tables would read past the end of a
    // 21- or 5-entry array, so they are worth submitting for real
    // rather than only reasoning about.
    ctx->r[3] = 0; /* target 0 */
    ctx->r[4] = 5; ctx->r[5] = 6; ctx->r[6] = 0;  /* SRC_ALPHA, INV_SRC_ALPHA, ADD */
    ctx->r[7] = 1;                                 /* useAlphaBlend */
    ctx->r[8] = 1; ctx->r[9] = 0; ctx->r[10] = 0;  /* ONE, ZERO, ADD */
    ppc_import_gx2_GX2SetBlendControl(ctx);
    ctx->r[3] = 7; /* highest real render target */
    ctx->r[4] = 20; ctx->r[5] = 20; ctx->r[6] = 4; /* INV_CONSTANT_ALPHA x2, REV_SUB -- real enum maxima */
    ctx->r[7] = 0;                                 /* no separate alpha blend -- color factors reused */
    ctx->r[8] = 0; ctx->r[9] = 0; ctx->r[10] = 0;
    ppc_import_gx2_GX2SetBlendControl(ctx);

    // Real, documented no-ops in this shim (shaders, attribute buffers,
    // display-list recording, context state, draws). Calling them proves
    // only that they link and return -- deliberately NOT counted as
    // passes, since there is no real behavior behind them to pass; they
    // are here so that when any of them does gain a real implementation,
    // it is already being called on hardware from this test.
    ctx->r[3] = 0; ctx->r[4] = 0; ctx->r[5] = 0; ctx->r[6] = 0;
    ppc_import_gx2_GX2SetContextState(ctx);
    ppc_import_gx2_GX2SetShaderModeEx(ctx);
    ppc_import_gx2_GX2SetAttribBuffer(ctx);
    ppc_import_gx2_GX2SetFetchShader(ctx);
    ppc_import_gx2_GX2SetVertexShader(ctx);
    ppc_import_gx2_GX2SetPixelShader(ctx);
    ppc_import_gx2_GX2SetSwapInterval(ctx);
    ppc_import_gx2_GX2Invalidate(ctx);
    ppc_import_gx2_GX2DrawEx(ctx);
    ppc_import_gx2_GX2DrawIndexedEx(ctx);
    checkpoint("[documented no-ops: shaders/attrib buffers/draws/context state] called -- linked and returned (no behavior to check)");

    // Everything recorded above has to actually reach the GPU and
    // complete. This is the real check for the whole command-buffer
    // group -- a malformed viewport, an out-of-range blend target or a
    // bad enum reaching a lookup table shows up here, on real hardware,
    // as a fault or a hang, and nowhere else.
    uint64_t before = g_arkchemy_gx2.submitted_timestamp;
    ppc_import_gx2_GX2Flush(ctx);
    ppc_import_gx2_GX2DrawDone(ctx);
    checkBool("previously-unexercised command-buffer state submitted and completed",
              g_arkchemy_gx2.submitted_timestamp > before, 1);
    checkpoint("[viewport/scissor/line/point/depth-bias/blend/stencil/restart] submitted+completed -- PASS (no hang/crash)");

    // Leave the real rasterizer/scissor state where the frame loop below
    // expects it: a full-framebuffer scissor is what was set above, so
    // the loop's own GX2ClearColor still covers the whole screen and the
    // green/red pass indicator stays meaningful.
    checkpoint("=== previously-unexercised surface self-test done: %d passed, %d failed (cumulative) ===",
               g_pass_count, g_fail_count);
}

#define GX2TEST_SWAP_COUNT_ADDR 0x2000u
#define GX2TEST_FLIP_COUNT_ADDR 0x2004u

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Jouster", 0777);
    g_log = fopen("sdmc:/switch/Jouster/gx2-test-results.log", "w");

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
    run_untested_surface_selftest(&ctx);

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
        g_current_frame = frame; /* real, current-frame tracking for __libnx_exception_handler above -- see its own comment */

        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // Real, deliberate brightness pulse layered on top of the real
        // pass/fail hue (green/red) below -- doesn't change *what* the
        // color means (still solid green = pass, solid red = fail, same
        // as before), but now visibly animates frame to frame, so it's
        // obvious at a glance that this is a real, live, still-running
        // loop and not a frozen/hung single frame -- real feedback this
        // .nro had no way to give before, short of pulling the log.
        float pulse = 0.55f + 0.45f * fabsf(sinf((float)frame * 0.10f));

        // void GX2ClearColor(GX2ColorBuffer *colorBuffer, float red,
        // float green, float blue, float alpha) -- real PPC ABI: r3 is
        // the (currently-ignored, see the shim's own comment)
        // colorBuffer pointer, the 4 floats go in f1-f4.
        ctx.r[3] = 0;
        if (selftest_ok) {
            ctx.f[1] = 0.10f * pulse; ctx.f[2] = 0.70f * pulse; ctx.f[3] = 0.20f * pulse; /* green -- all checks passed */
        } else {
            ctx.f[1] = 0.70f * pulse; ctx.f[2] = 0.10f * pulse; ctx.f[3] = 0.10f * pulse; /* red -- at least one check failed, check the log */
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
            checkpoint("first real frame swapped -- screen should now be %s (pulsing -- if it's static, something's wrong)", selftest_ok ? "green (all self-test checks passed)" : "red (a self-test check failed -- see log above)");
        }

        // Real, periodic progress checkpoint -- every 15 frames (~0.25s
        // at a real 60fps present rate), so a real crash mid-loop (see
        // __libnx_exception_handler above for the *hard*-crash case)
        // still leaves a clear trail of exactly how far this got even
        // in a softer failure mode (a real hang that never actually
        // faults, which the exception handler above can't catch at
        // all) -- the log's last periodic line plus its own timestamp
        // gap tells you where and roughly when it stopped.
        if (frame % 15 == 0) {
            checkpoint("frame %d/%d (swap_count=%u flip_count=%u)", frame, GX2TEST_AUTO_EXIT_FRAMES,
                       g_arkchemy_gx2.swap_count, g_arkchemy_gx2.flip_count);
        }

        frame++;
    }

    checkpoint("exiting after %d frames, %d self-test checks passed, %d failed", frame, g_pass_count, g_fail_count);
    ppc_import_gx2_GX2Shutdown(&ctx);
    if (g_log) { fclose(g_log); g_log = NULL; }

    // Chain-loading straight into haze (envSetNextLoad) was tried and
    // reverted: haze crashes with a real null-pointer data abort inside
    // its own code when launched this way instead of directly from
    // hbmenu (confirmed via a real Atmosphère crash report, PC inside
    // haze's own module, not this .nro's) -- and the crash itself was
    // observed to knock out the SD card's MTP/USB file-transfer session
    // on the host side, needing a manual reconnect, the opposite of the
    // "quicker to test" goal this was meant to serve. Plain exit back to
    // hbmenu instead.
    return 0;
}
