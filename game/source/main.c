// Bramble's first real, full-game smoke test: calls the actual,
// completely recompiled Skylanders: Spyro's Adventure entry point
// (ppc_bramble_game_entry, see recomp's own --entry-alias) on a real,
// separate background thread, while this file's own main thread shows
// a real, independent progress indicator -- deliberately *not* relying
// on the recompiled game's own draw calls (see cafeos_gx2.h's "Real
// shader/draw-call pipeline" comment: those are still real, honest
// no-ops, since shader translation doesn't exist yet -- nothing the
// game itself draws would show up on screen regardless).
//
// Why a separate thread at all: this is the real game's actual,
// complete entry point, running for the very first time -- its real
// behavior (does it return quickly after basic init, loop forever like
// a real game main loop, block on a real resource this runtime doesn't
// have, or crash) isn't known yet. Running it inline on the main
// thread would mean a real hang there looks identical to this whole
// app being frozen, exactly what the owner asked this file avoid.
// Running it on its own thread means the main thread's own simple
// pulse-and-log loop keeps going regardless of what the game thread is
// actually doing -- real, visible proof this app itself isn't frozen,
// independent of whether the real game logic is behaving.
//
// Same real diagnostic pattern already proven in switch/gx2_test:
// checkpointed SD-card logging (survives a hang -- whatever was
// written before that point stays on the card), a real libnx
// exception handler as a fallback for an actual unhandled hardware
// exception (writes a full register dump plus which frame the main
// thread was on), and a pulsing screen color instead of a static one.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>

#include "ppc_runtime.h"
#include "cafeos_gx2.h"

// Real, deliberate architecture: the actual, complete recompiled game
// (8.5M+ lines from one real recomp run against tfbGame_cafe.rpx) lives
// in switch/game/source/generated_*.c -- 213 separately-compiled files,
// not included here, so each individual `gcc` invocation's own compile-
// time memory use stays bounded (a real, single 308MB/8.5M-line
// translation unit exhausted a deliberate 5.5GB compile-memory safety
// cap in an earlier real attempt -- see git history around this
// change). Every one of those 213 files, and this one, `#include`s the
// same real cafeos_*.h shim headers -- safe now, unlike an earlier
// real attempt at this same split, because every shim header's own
// persistent state (g_bramble_gx2 and friends) was converted from
// `static` (silently, incorrectly private-per-file) to real `extern`
// linkage, with the one, real, shared definition of each now living in
// recomp/include/cafeos_state.c (compiled and linked into this project
// once, alongside every one of those 213 files -- see that file's own
// comment for the full real reasoning). This file only needs
// cafeos_gx2.h directly, for its own independent progress-indicator
// pulse below -- it calls into the real, complete recompiled game via
// ppc_bramble_game_entry, forward-declared where it's used, same as
// any other externally-linked function.

static FILE *g_log;
static volatile int g_current_frame = -1;
static volatile bool g_game_thread_done = false;
static volatile bool g_game_thread_started = false;

// Appends to the SD-card log, flushed after every line -- same reasoning
// as switch/gx2_test's own checkpoint().
static void checkpoint(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
    fflush(g_log);
}

// Real hook into ppc_runtime.h's ppc_unhandled_stub (see its own
// comment) -- the small number of real instructions this runtime
// doesn't yet model correctly (10 real sites, all pragmatically
// stubbed for this first build -- see the git history for exactly
// which and why) log here instead of silently vanishing, so if one of
// them turns out to matter for what the game actually does early on,
// it's visible in the log, not a mystery.
static void unhandled_log_sink(const char *what) {
    checkpoint("[ppc_unhandled_stub] %s", what);
}

alignas(16) static u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    FILE *f = fopen("sdmc:/switch/Bramble/game-exception-dump.log", "w");
    int i;
    if (!f) return;
    fprintf(f, "real, unhandled hardware exception caught by this .nro's own fallback handler\n");
    fprintf(f, "main-thread frame at time of fault: %d\n", g_current_frame);
    fprintf(f, "game thread started: %d, done: %d\n", g_game_thread_started, g_game_thread_done);
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
    if (g_log) {
        fprintf(g_log, "*** UNHANDLED EXCEPTION at main frame %d (game thread started=%d done=%d) -- see game-exception-dump.log ***\n",
                g_current_frame, g_game_thread_started, g_game_thread_done);
        fflush(g_log);
    }
}

static PpcContext g_ctx;
static PpcSharedMemory g_shared;

// void ppc_bramble_game_entry(PpcContext *ctx) -- the real, complete,
// recompiled game entry point (see recomp's own --entry-alias). Runs
// on its own real thread (see this file's own top comment for why).
static void game_thread_func(void *arg) {
    (void)arg;
    g_game_thread_started = true;
    checkpoint("[game thread] calling ppc_bramble_game_entry...");
    void ppc_bramble_game_entry(PpcContext *ctx);
    ppc_bramble_game_entry(&g_ctx);
    checkpoint("[game thread] ppc_bramble_game_entry returned");
    g_game_thread_done = true;
}

#define GAME_THREAD_STACK_SIZE (4 * 1024 * 1024)

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Bramble", 0777);
    g_log = fopen("sdmc:/switch/Bramble/game-results.log", "w");
    ppc_set_unhandled_log(unhandled_log_sink);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    g_ctx.shared = &g_shared;

    checkpoint("Bramble game smoke test starting");

    // void GX2Init(uint32_t *attributes) -- real init, same call every
    // other Bramble test .nro already makes. Only used here for this
    // file's own real, independent pulse indicator below -- not
    // assuming the recompiled game's own GX2 calls (which it will also
    // make, redundantly but harmlessly, since GX2Init is real,
    // idempotent state setup) do anything visible yet.
    g_ctx.r[3] = 0;
    ppc_import_gx2_GX2Init(&g_ctx);
    checkpoint("GX2Init done");

    Thread game_thread;
    Result rc = threadCreate(&game_thread, game_thread_func, NULL, NULL, GAME_THREAD_STACK_SIZE, 0x2C, -2);
    if (R_FAILED(rc)) {
        checkpoint("threadCreate failed: 0x%x", rc);
    } else {
        rc = threadStart(&game_thread);
        if (R_FAILED(rc)) {
            checkpoint("threadStart failed: 0x%x", rc);
        } else {
            checkpoint("game thread started");
        }
    }

    // Real, fixed cap on how long this smoke test runs before exiting
    // on its own -- this is a first real run of the actual, complete
    // game entry point; there's no way to know in advance whether it
    // finishes, loops forever (the real, expected shape of a real game
    // main loop), or hangs on something this runtime doesn't support
    // yet. 10 real minutes at 60fps -- generous, but bounded, so this
    // doesn't run forever unattended even if the game thread never
    // finishes. + still exits early if held/pressed sooner.
    #define GAME_TEST_AUTO_EXIT_FRAMES (600 * 60)

    int frame = 0;
    while (appletMainLoop() && frame < GAME_TEST_AUTO_EXIT_FRAMES) {
        g_current_frame = frame;

        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // Real, independent "still alive" indicator -- this file's own
        // pulse (see switch/gx2_test's own comment for the same real
        // reasoning), not the recompiled game's. Blue while the game
        // thread is still running, green once it's finished cleanly.
        float pulse = 0.55f + 0.45f * fabsf(sinf((float)frame * 0.10f));
        g_ctx.r[3] = 0;
        if (g_game_thread_done) {
            g_ctx.f[1] = 0.10f * pulse; g_ctx.f[2] = 0.70f * pulse; g_ctx.f[3] = 0.20f * pulse; /* green */
        } else {
            g_ctx.f[1] = 0.10f * pulse; g_ctx.f[2] = 0.30f * pulse; g_ctx.f[3] = 0.80f * pulse; /* blue */
        }
        g_ctx.f[4] = 1.0;
        ppc_import_gx2_GX2ClearColor(&g_ctx);
        ppc_import_gx2_GX2SwapScanBuffers(&g_ctx);

        if (frame % 60 == 0) {
            checkpoint("main frame %d/%d -- game thread started=%d done=%d",
                       frame, GAME_TEST_AUTO_EXIT_FRAMES, g_game_thread_started, g_game_thread_done);
        }

        if (g_game_thread_done) {
            checkpoint("game thread finished at main frame %d -- exiting shortly", frame);
            break;
        }

        frame++;
    }

    checkpoint("exiting after %d main frames -- game thread started=%d done=%d",
               frame, g_game_thread_started, g_game_thread_done);

    // Real, deliberate choice: don't threadWaitForExit/threadClose here
    // if the game thread never finished -- it may be legitimately stuck
    // in a real, long-running (or infinite) loop, and blocking this
    // exit on it would defeat the entire point of this file's own
    // bounded auto-exit above.
    if (g_game_thread_done) {
        threadWaitForExit(&game_thread);
        threadClose(&game_thread);
    }

    if (g_log) { fclose(g_log); g_log = NULL; }

    // Real bug found and fixed here, via a real on-hardware report (an
    // "error occurred"-style abnormal-exit screen, not a crash inside
    // the recompiled game logic itself -- the log above showed a
    // clean, complete run right up to this exact point every time).
    // Real cause: a plain `return 0` here runs libnx's own normal,
    // *orderly* per-thread/service shutdown sequence -- correct when
    // every thread has already stopped, but a real race when the game
    // thread is still actively running recompiled code (reading/
    // writing real shared runtime state, possibly mid real service
    // call) at the exact moment that teardown starts out from under
    // it. Real, deliberate fix: when the game thread never finished on
    // its own, skip the normal C runtime exit path entirely and call
    // the real, raw `svcExitProcess` kernel syscall instead -- this
    // terminates the *entire real process*, every thread included,
    // atomically, at the kernel level, with no per-thread unwind for
    // anything to race against. The normal, orderly `return 0` path is
    // still used whenever the game thread genuinely finished on its
    // own (the common, non-racy case).
    if (!g_game_thread_done) {
        svcExitProcess();
    }
    return 0;
}
