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
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>

#include "ppc_runtime.h"
#include "cafeos_coreinit_fs.h"
#include "cafeos_coreinit_mem.h"

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
// comment for the full real reasoning). This file itself doesn't call
// into any cafeos_*.h shim directly (its own status display uses
// libnx's console instead, see checkpoint()'s and main()'s own
// comments) -- it calls into the real, complete recompiled game via
// ppc_bramble_game_entry, forward-declared where it's used, same as
// any other externally-linked function.

static FILE *g_log;
static volatile int g_current_frame = -1;
static volatile bool g_game_thread_done = false;
static volatile bool g_game_thread_started = false;
// Real, distinct phase flags for the main thread's own pulse color below --
// added 2026-08-20 alongside the fix that made game_thread_func actually
// call ppc_init_globals/ppc_run_static_initializers (114 real C++ static
// initializers, completely untested code paths, running for the first
// time ever): these are exactly the kind of "might genuinely hang" real
// code this whole file's own architecture exists to isolate from the
// main thread, same as the entry point itself -- so they need their own
// visible phase, not just a binary "started/done".
static volatile bool g_globals_init_done = false;
static volatile bool g_static_init_done = false;

// Appends to the SD-card log, flushed after every line -- same reasoning
// as switch/gx2_test's own checkpoint(). Also printed live to the
// on-screen libnx console (see main()'s own consoleInit) -- added
// 2026-08-20 per direct owner request: a pulsing color alone gives no
// way to tell "alive and doing something specific" from "alive but
// stuck," and closing the app to pull the SD-card log is the only way
// to see what actually happened. This makes every real checkpoint --
// phase transitions, FSOpenFile results, mem events, the periodic
// register-watch line -- visible immediately, live, on the real screen.
//
// Real bug found and fixed the same day this was added: checkpoint()
// is called from both this file's own main thread *and* the separate
// game thread (see game_thread_func and the various log-sink
// callbacks above) -- unlike stdio FILE* writes (glibc locks those
// internally, safe to interleave), libnx's console has no such
// built-in locking of its own. Two threads calling printf()/
// consoleUpdate() concurrently raced on the same shared console state
// and hung the very first real run of this build solid before even
// ppc_init_globals finished -- no crash, no exception dump, just a
// silent deadlock, exactly consoleMutex below now prevents.
static Mutex g_console_mutex;
static void checkpoint(const char *fmt, ...) {
    va_list ap;
    mutexLock(&g_console_mutex);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    mutexUnlock(&g_console_mutex);
    if (!g_log) return;
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

/* Real hook into cafeos_coreinit_fs.h's own FSOpenFile logging -- see
 * that header's comment. Every real file the game's actual entry point
 * tries to open, and whether it was actually found on the SD card,
 * lands here -- the real, concrete answer to "what does the game
 * actually do during its first 10 real minutes" that the FS path
 * translation fix alone doesn't surface on its own. */
static void fs_open_log_sink(const char *guest_path, const char *real_path, int found) {
    checkpoint("[FSOpenFile] %s -> %s (%s)", guest_path, real_path, found ? "found" : "NOT FOUND");
}

/* Real, ad hoc debug watchpoint sink -- see ppc_runtime.h's own comment
 * on ppc_debug_watch(). Currently watching a real, specific value: the
 * "entry count" Core::igArchive::loadArchiveTableOfContents reads
 * straight out of its just-read file buffer (real address 0x2169e34,
 * `lwz r0, 0x3c(r31)`) -- confirming whether that buffer actually holds
 * real file data (a small, sane count) or was never filled in (leaving
 * whatever garbage was already in guest memory, likely a huge or
 * otherwise implausible count) is the real, direct way to settle
 * whether the sustained malloc/realloc spin traces back to this. */
static void debug_watch_sink(uint32_t pc, uint32_t value) {
    checkpoint("[DEBUG WATCH] pc=0x%x value=%u (0x%x)", pc, value, value);
}

/* Real hook into cafeos_coreinit_mem.h's own allocation-failure logging
 * -- see that header's own comment. Answers a real, specific question
 * about the malloc/realloc spin loop found via g_ppc_current_pc: is the
 * real game's own memory-pool code genuinely out of real (shim-provided)
 * heap space, or something else entirely. */
static void mem_alloc_fail_log_sink(const char *what, uint32_t requested, uint32_t heap_base, uint32_t heap_size, uint32_t heap_used) {
    // Throttled: if a real allocation failure really is a real retry-
    // loop-on-OOM, it could fire billions of times same as the malloc
    // calls themselves -- logging all of them would flood the SD card
    // instead of answering the question. Real heap-creation events are
    // naturally rare (a handful total) and always logged regardless, so
    // 40 total comfortably covers "every real heap-setup event, plus a
    // healthy number of any real allocation failures" without risking a
    // flood if the latter turns out to still be a spin.
    static int count = 0;
    if (count >= 40) return;
    count++;
    // g_ppc_current_pc/g_ppc_fn_call_count are updated at the entry of
    // every real recompiled function (see ppc_runtime.h's own comment) --
    // reading them right here, inside this shim call itself, captures
    // exactly which real function *called into* this event, for free, no
    // extra plumbing needed.
    checkpoint("[MEM EVENT #%d] %s requested=%u heap_base=0x%x heap_size=%u heap_used=%u -- called from last_pc=0x%x calls=%llu",
               count, what, requested, heap_base, heap_size, heap_used,
               g_ppc_current_pc, (unsigned long long)g_ppc_fn_call_count);
    if (count == 40) checkpoint("[MEM EVENT] further events suppressed");
}

alignas(16) static u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    FILE *f = fopen("sdmc:/switch/Bramble/game-exception-dump.log", "w");
    int i;
    if (!f) return;
    fprintf(f, "real, unhandled hardware exception caught by this .nro's own fallback handler\n");
    fprintf(f, "main-thread frame at time of fault: %d\n", g_current_frame);
    fprintf(f, "game thread started: %d, globals_init: %d, static_init: %d, done: %d\n",
            g_game_thread_started, g_globals_init_done, g_static_init_done, g_game_thread_done);
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
        fprintf(g_log, "*** UNHANDLED EXCEPTION at main frame %d (game thread started=%d globals_init=%d static_init=%d done=%d) -- see game-exception-dump.log ***\n",
                g_current_frame, g_game_thread_started, g_globals_init_done, g_static_init_done, g_game_thread_done);
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

    // Real, severe bug found and fixed 2026-08-20, then found again in a
    // different form the same day: these two calls used to not run at
    // all (see git history), then got added but wrongly placed in
    // main() itself, *before* GX2Init and the thread spawn below --
    // blocking the main thread's own pulse-and-log loop (this file's
    // whole reason for existing, see the top comment) behind up to 114
    // completely untested real C++ constructors, which is exactly the
    // "looks totally frozen, no way to tell if it's alive" failure mode
    // this architecture exists to prevent. Belongs here instead, same as
    // ppc_bramble_game_entry below it -- untested real code that might
    // genuinely hang, isolated from the main thread like everything
    // else in this function.
    void ppc_init_globals(PpcContext *ctx);
    void ppc_run_static_initializers(PpcContext *ctx);
    // Real, ad hoc, one-off watch (see ppc_runtime.h's own comment on
    // g_ppc_watch_store_addr) -- the malloc free-list hang this used to
    // track (0x16078, __mallocInfo's free-list head field) is resolved;
    // the game now reaches its real entry point ("blue" phase). Left at
    // the sentinel (never matches a real store) since the current hang
    // is a register-only spin (see g_ppc_watch_pc below), not a bad
    // store this mechanism would catch.
    //
    // Current real hang: frozen inside Core::igStringPool::remove
    // (0x21a55cc) with g_ppc_fn_call_count completely flat -- that
    // function's own bucket-walk loop (`r11 = *(r11+8)` chasing a
    // linked list, comparing against the target item) has no NULL/
    // end-of-list check in the real shipped code, by design: real GHS
    // source trusts its caller to only ever remove an item that is
    // actually present in the bucket its own recomputed hash names.
    // Static analysis of the real disassembly (both this function and
    // its insertion counterpart, Core::igStringPool::findAndSetString)
    // confirms insertion and removal use the identical hash computation
    // with no rehashing in between, so both *should* agree -- meaning
    // if they don't on a real run, something upstream (memory
    // corruption, a still-missing struct-layout/relocation bug) is the
    // real cause, not this function's own logic. g_ppc_watch_pc dumps
    // r3(this)/r4(item)/r5(bucket index) the instant this function is
    // entered so the next real run can show the actual values instead
    // of guessing further.
    g_ppc_watch_pc = 0x21a55ccu;
    checkpoint("[game thread] calling ppc_init_globals...");
    ppc_init_globals(&g_ctx);
    g_globals_init_done = true;
    checkpoint("[game thread] ppc_init_globals done");
    checkpoint("[game thread] calling ppc_run_static_initializers (114 real C++ static initializers)...");
    ppc_run_static_initializers(&g_ctx);
    g_static_init_done = true;
    checkpoint("[game thread] ppc_run_static_initializers done");

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
    ppc_fs_set_open_log(fs_open_log_sink);
    ppc_mem_set_alloc_fail_log(mem_alloc_fail_log_sink);
    ppc_set_debug_watch(debug_watch_sink);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    g_ctx.shared = &g_shared;
    // Real, severe bug found and fixed here: this was never set before --
    // every other real consumer of a PpcContext that calls into actual
    // recompiled code (tools/gen_harness*.c, switch/native/source/main.c)
    // sets r[1] (the real PowerPC stack pointer) before its first call,
    // since real hardware's own loader would have done this before ever
    // jumping to a real entry point. Left at its zero-initialized BSS
    // default, the real game entry's very first real `stwu` (stack-frame
    // push) instruction computed a huge, wrapped/masked guest address and
    // started corrupting unrelated real memory (globals, heap state)
    // before the real game's own code had done anything meaningful --
    // see ppc_runtime.h's own PPC_MEM_SIZE comment for the matching real
    // guest-address-space bug this was compounding.
    g_ctx.r[1] = PPC_MEM_SIZE - 256;

    // Real, deliberate choice made 2026-08-20 alongside the switch to a
    // live on-screen console (see checkpoint()'s own comment): this
    // file used to call the real GX2Init/GX2ClearColor/GX2SwapScanBuffers
    // shim (deko3d-backed) purely to pulse a solid color as a "still
    // alive" indicator. deko3d's swapchain framebuffers are GPU-tiled,
    // hardware-compressed image memory (DkImageFlags_HwCompression,
    // DkMemBlockFlags_GpuCached -- see cafeos_gx2.h's own
    // bramble_gx2_create_framebuffers) -- not CPU-writable, so real text
    // can't be blitted into them without a full shader/vertex pipeline
    // this project doesn't have yet. libnx's own console is a complete,
    // separate, CPU-side text renderer that needs none of that, so this
    // file now uses it instead and no longer touches GX2Init/deko3d at
    // all -- one less thing sharing nwindowGetDefault() with whatever
    // the recompiled game's own (currently still invisible, since
    // shader translation doesn't exist yet) GX2 calls do later.
    mutexInit(&g_console_mutex);
    consoleInit(NULL);
    printf("\x1b[32m");
    printf("   ___                     _     _\n");
    printf("  | _ ) _ _  __ _  _ __   | |__ | | ___\n");
    printf("  | _ \\| '_|/ _` || '  \\  | '_ \\| |/ -_)\n");
    printf("  |___/|_|  \\__,_||_|_|_| |_.__/|_|\\___|\n");
    printf("\x1b[0m");
    printf("  static recompilation engine -- Skylanders: Spyro's Adventure\n\n");
    consoleUpdate(NULL);

    checkpoint("Bramble game smoke test starting");

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

    // Real, added 2026-08-20 per direct owner request: every real hang
    // found so far shows the exact same real signature -- g_ppc_fn_call_count
    // (updated on every real recompiled function's entry, see
    // ppc_runtime.h) frozen completely solid, not just slow, for the
    // entire rest of a real run. Waiting out the full 10-minute cap to
    // *confirm* that on every single real test run wastes real owner
    // time for no real diagnostic benefit once it's been frozen for a
    // few real seconds. STALL_TIMEOUT_FRAMES is deliberately short --
    // real, if bursty, forward progress should never actually go this
    // long completely flat, since this runtime has no real async I/O
    // yet for a legitimate wait to hide behind.
    #define STALL_TIMEOUT_FRAMES (3 * 60)
    uint64_t last_progress_calls = 0;
    int last_progress_frame = 0;

    int frame = 0;
    while (appletMainLoop() && frame < GAME_TEST_AUTO_EXIT_FRAMES) {
        g_current_frame = frame;

        if (g_ppc_fn_call_count != last_progress_calls) {
            last_progress_calls = g_ppc_fn_call_count;
            last_progress_frame = frame;
        } else if (g_game_thread_started && !g_game_thread_done &&
                   frame - last_progress_frame >= STALL_TIMEOUT_FRAMES) {
            checkpoint("no forward progress (g_ppc_fn_call_count frozen at %llu, last_pc=0x%x) for %d frames "
                       "(~%d sec) -- auto-exiting early instead of waiting the full %d-frame cap",
                       (unsigned long long)g_ppc_fn_call_count, g_ppc_current_pc, frame - last_progress_frame,
                       STALL_TIMEOUT_FRAMES / 60, GAME_TEST_AUTO_EXIT_FRAMES);
            break;
        }

        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // Real, independent "still alive" indicator, replacing the old
        // pulsing GX2 color (see this file's top-of-main comment for
        // why a color alone wasn't enough): a one-word phase tag
        // printed on every real phase *transition* (not every frame --
        // scrolling text every single frame at 60fps would flood the
        // console faster than it's readable), plus consoleUpdate(NULL)
        // every frame so the screen visibly refreshes continuously even
        // between checkpoint lines, rather than looking frozen between
        // them the same way the old pulse color visibly animated.
        static bool globals_announced = false, static_announced = false, done_announced = false;
        mutexLock(&g_console_mutex);
        if (g_game_thread_done && !done_announced) {
            done_announced = true;
            printf("\x1b[32m  == phase: GREEN (game entry returned) ==\x1b[0m\n");
        } else if (g_static_init_done && !static_announced) {
            static_announced = true;
            printf("\x1b[34m  == phase: BLUE (running real game entry point) ==\x1b[0m\n");
        } else if (g_globals_init_done && !globals_announced) {
            globals_announced = true;
            printf("\x1b[35m  == phase: PURPLE (114 real static initializers) ==\x1b[0m\n");
        }
        consoleUpdate(NULL);
        mutexUnlock(&g_console_mutex);

        if (frame % 60 == 0) {
            // Real, best-effort (racy, same as reading g_ppc_current_pc
            // itself) snapshot of the currently-executing function's own
            // real PowerPC integer argument registers -- not correctness-
            // critical, just diagnostic: if the game thread really is
            // making real (if pointless/looping) progress, these values
            // should visibly change between samples; if it's a genuine
            // stuck infinite loop with no real state change, they'll stay
            // identical every time. Also true now for the two new phases
            // above (g_ppc_current_pc updates on every real recompiled
            // function's entry, including inside static initializers and
            // whatever they call), not just inside the entry point.
            checkpoint("main frame %d/%d -- globals_init=%d static_init=%d game_started=%d game_done=%d -- sti_idx=%u last_pc=0x%x caller_lr=0x%x calls=%llu -- r3=0x%x r4=0x%x r5=0x%x r6=0x%x -- watch(igStringPool::remove entry) r3=0x%x r4=0x%x r5=0x%x r6=0x%x",
                       frame, GAME_TEST_AUTO_EXIT_FRAMES, g_globals_init_done, g_static_init_done,
                       g_game_thread_started, g_game_thread_done,
                       g_ppc_static_init_index, g_ppc_current_pc, g_ppc_last_caller_lr, (unsigned long long)g_ppc_fn_call_count,
                       g_ctx.r[3], g_ctx.r[4], g_ctx.r[5], g_ctx.r[6],
                       g_ppc_watch_r3, g_ppc_watch_r4, g_ppc_watch_r5, g_ppc_watch_r6);
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
