// Arkchemy's first real, full-game smoke test: calls the actual,
// completely recompiled Skylanders: Spyro's Adventure entry point
// (ppc_arkchemy_game_entry, see recomp's own --entry-alias) on a real,
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
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "ppc_runtime.h"
#include "cafeos_coreinit_fs.h"
#include "cafeos_coreinit_mem.h"
#include "cafeos_vpad.h"

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
// persistent state (g_arkchemy_gx2 and friends) was converted from
// `static` (silently, incorrectly private-per-file) to real `extern`
// linkage, with the one, real, shared definition of each now living in
// recomp/include/cafeos_state.c (compiled and linked into this project
// once, alongside every one of those 213 files -- see that file's own
// comment for the full real reasoning). This file itself doesn't call
// into any cafeos_*.h shim directly (its own status display uses
// libnx's console instead, see checkpoint()'s and main()'s own
// comments) -- it calls into the real, complete recompiled game via
// ppc_arkchemy_game_entry, forward-declared where it's used, same as
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
// Real, deliberate choice made 2026-08-20 alongside a more stylised
// console (see main()'s own banner): each real category of checkpoint
// line (a memory event, a file lookup, a debug watch hit, a phase's
// own periodic status line, ...) gets its own real color on screen, so
// the kind of line scrolling past is visible at a glance without
// reading every word. The *file* copy stays plain text, deliberately
// -- these ANSI escape codes are for a real terminal-like console, not
// something a plain-text log viewer should have to strip back out.
static const char *checkpoint_color(const char *msg) {
    if (strncmp(msg, "[MEM EVENT", 10) == 0) return "\x1b[33m";           /* gold */
    if (strncmp(msg, "[FSOpenFile]", 12) == 0) return "\x1b[36m";        /* cyan */
    if (strncmp(msg, "[DEBUG WATCH]", 13) == 0) return "\x1b[35m";       /* magenta */
    if (strncmp(msg, "[ppc_unhandled_stub]", 21) == 0) return "\x1b[31m"; /* red */
    if (strncmp(msg, "[game thread]", 13) == 0) return "\x1b[34m";       /* blue */
    if (strncmp(msg, "main frame", 10) == 0) return "\x1b[90m";          /* dim gray */
    return "\x1b[37m";                                                  /* plain white */
}

static Mutex g_console_mutex;
static void checkpoint(const char *fmt, ...) {
    // Was 512 -- real, confirmed truncation found 2026-08-20: the main
    // periodic status line has grown one field at a time all session
    // (mem counters, four watch slots, two loopwatch entries) until it
    // silently overran 512 bytes, vsnprintf truncating it mid-field
    // right before this exact watchpoint's own distinct/last values --
    // which looked exactly like a real "only 6 hits" finding until this
    // was checked. 4096 is comfortably past anything this one line is
    // likely to grow to next.
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // "\r\x1b[K" first: cleanly overwrites whatever the continuously-
    // animated status spinner (see main()'s own loop) left on the
    // current row, same real reasoning as a normal terminal's own
    // "clear line before printing a fresh one" convention -- without
    // it, a checkpoint line landing mid-spin would visibly concatenate
    // onto the spinner's leftover text instead of starting clean.
    mutexLock(&g_console_mutex);
    printf("\r\x1b[K%s%s\x1b[0m\n", checkpoint_color(buf), buf);
    mutexUnlock(&g_console_mutex);

    if (!g_log) return;
    fprintf(g_log, "%s\n", buf);
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
static void fs_open_log_sink(const char *guest_path, const char *real_path, const char *mode, int found,
                              uint32_t handle, uint32_t handles_in_use) {
    checkpoint("[FSOpenFile] %s -> %s mode=\"%s\" (%s) handle=%u in_use=%u/%d", guest_path, real_path, mode,
               found ? "found" : "NOT FOUND", handle, handles_in_use, ARKCHEMY_FS_MAX_HANDLES);
}

/* Real, ad hoc debug watchpoint sink -- see ppc_runtime.h's own comment
 * on ppc_debug_watch(). Currently hand-inserted (see regenerate.sh's own
 * note that generated_*.c is gitignored and safe to hand-edit for one
 * debugging session) at Core::igStringPoolContainer::mallocString's own
 * internal retry-loop top (real address 0x21a4f9c, right after the
 * `L_21a4f9c:` label), reporting r25 -- the pool-list cursor the loop
 * re-reads every single iteration. w2/w3's own hits=1 already proved
 * mallocString is stuck inside one single, never-returning call (not
 * being retried via fresh `bl`s), spinning via its own internal `goto`;
 * this is the only way to see what that cursor is actually doing across
 * the billion-plus iterations a plain function-entry watch can't see at
 * all. NOT logged on every hit -- at this call frequency that would be
 * a pure firehose -- just tracked as a running last-value/distinct-value
 * count, printed periodically alongside everything else. */
/* Generic small tag->slot table, one entry per hand-inserted
 * ppc_debug_watch() call site currently live in generated_0161.c's
 * mallocString:
 *   0x21a4f9c -- r25, the pool-list cursor, at the loop top
 *   0x21a4f9d -- that cursor's own field-0x18 value (the loop's branch key)
 *   0x21a4fc4 -- "next pool" pointer read from the current cursor
 *   0x21a4ff0 -- raw return value of malloc(28) for a new pool struct
 *   0x21a5024 -- raw return value of malloc() for that new pool's own buffer
 * "changed" counts transitions from the immediately preceding hit, not
 * true distinct-value cardinality -- real, confirmed lesson from this
 * exact investigation: r25's changed count climbed in lockstep with its
 * hit count, which looked like "hundreds of millions of unique pool
 * pointers" until cross-checking every periodic sample's own "last="
 * value showed only three ever appeared (0x0, 0xf, and the initial
 * 0xffffffff sentinel) -- it was oscillating between two fixed values
 * every single call, not visiting new memory. */
/* New lead, 2026-08-21: traced mallocString's own "this" (the
 * igStringPoolContainer* read from igStringPool+0x14) to its one real
 * setter, Core::igStringPool::activate() (0x21aa608) -- confirmed
 * called for real, directly and unconditionally, from
 * Core::igArkCore::initBootstrap (which real hardware confirms DOES
 * fire, hits=1@~21790). activate()'s own real body only writes the new
 * container pointer into +0x14 (at 0x21aa68c) if a preceding
 * malloc(28) for that container (real call at 0x21aa644, result
 * checked at 0x21aa648) succeeds -- if that malloc returns NULL, real
 * code branches straight past the write, leaving +0x14 permanently
 * NULL. This watch tags that exact check (0x21aa648, tracking r31, the
 * malloc's real return value) to settle on real hardware whether this
 * specific small allocation is what's actually failing, independent of
 * the much larger MEMAllocFromExpHeapEx failures already seen
 * elsewhere in this same run (those are for much bigger 131072-byte
 * requests -- a different size class, not proof this one also fails). */
typedef struct { uint32_t tag; const char *label; uint32_t last_value; uint64_t hit_count; uint32_t changed_count; } ArkchemyDebugWatchSlot;
static ArkchemyDebugWatchSlot g_debug_watch_slots[] = {
    {0x21aa648u, "sp_container_alloc", 0xFFFFFFFFu, 0, 0},
    {0xFFFFFFFEu, "dispatch_userInstantiate", 0xFFFFFFFFu, 0, 0},
    {0x215bf24u, "singleton_needs_ctx_flag", 0xFFFFFFFFu, 0, 0},
    /* New lead, 2026-08-21: initBootstrap makes a real virtual call
     * through the freshly-constructed igMemoryContext's own vtable
     * (offset 0x34) before calling bootstrapInitialize/activate() --
     * confirmed via dispatch_userInstantiate (hits=0 every run) that
     * this isn't userInstantiate, but never checked what it *is*. Discord
     * research turned up general confirmation that igArkCore-family
     * objects are reached through a real singleton pointer set up very
     * early, which this virtual call is a real candidate for. */
    {0x21472a8u, "vtable_dispatch_target", 0xFFFFFFFFu, 0, 0},
    /* New lead, 2026-08-21 (cont.): vtable_dispatch_target above came
     * back 0x0 -- traced this to igMemoryContext's own real constructor
     * (0x2178438), which has a REAL bail-out path (L_21785a0) that
     * returns "this" unchanged (still NULL, since initBootstrap passes
     * r3=0) if its own object allocation fails. The constructor picks
     * between two real allocators based on a flag byte at .data+5148:
     * igObject's own pool-based operator new, or a raw
     * MEMAllocFromExpHeapEx against a specific "bootstrap heap" handle
     * read from .bss+306320. These four watches nail down exactly which
     * path runs and whether it succeeds -- confirmed real static rodata
     * shows the correct real vtable (containing a real, valid pointer to
     * userInstantiate at +0x34) exists at the address this constructor
     * *should* be writing into the object, so a construction failure
     * here would fully explain everything traced so far. */
    {0x2178460u, "ctor_alloc_path_flag", 0xFFFFFFFFu, 0, 0},
    {0x2178470u, "ctor_igobject_alloc_result", 0xFFFFFFFFu, 0, 0},
    {0x2178484u, "ctor_bootstrap_heap_handle", 0xFFFFFFFFu, 0, 0},
    {0x2178490u, "ctor_bootstrap_heap_alloc_result", 0xFFFFFFFFu, 0, 0},
    {0x2164260u, "setCount_old_count", 0xFFFFFFFFu, 0, 0},
    {0x2164288u, "setCount_loop_iters", 0xFFFFFFFFu, 0, 0},
    /* Answers a real, specific question: does this loop actually run to
     * completion (hits approaches the real initial 0x1fff0 iteration
     * count, last value ends near 0) before the "no forward progress"
     * detector gives up, or does it genuinely never advance? The
     * detector only tracks *function calls*, and this loop's own
     * unrolled body only calls decrementRefCount when a checked list
     * slot is non-null -- if every slot here is null (plausible if this
     * whole list was never populated, matching the real corrupted-
     * looking old-count value), the entire loop could run without a
     * single new function call, making it invisible to that detector
     * even while genuinely progressing (or genuinely stuck). */
    {0x21642a0u, "setCount_loop_backedge", 0xFFFFFFFFu, 0, 0},
    {0x2164268u, "setCount_newcount_at_decision", 0xFFFFFFFFu, 0, 0},
    {0x216436cu, "setCount_remainder_reached", 0xFFFFFFFFu, 0, 0},
    {0x21643bcu, "setCount_zerofill_iters", 0xFFFFFFFFu, 0, 0},
    {0x21643d0u, "setCount_zerofill_backedge", 0xFFFFFFFFu, 0, 0},
    {0x2164251u, "setCount_real_caller_lr", 0xFFFFFFFFu, 0, 0},
    /* New lead, 2026-08-21: traced setCount's real caller
     * (Core::igMemoryPoolFrameManager::initDefault) and confirmed it
     * asks for a completely sane newCount=128 -- the caller isn't the
     * bug. It calls setCount on *(frame+8), a separate pointer field
     * inside the real igMemoryPoolFrame object, not part of that
     * object's own calloc'd (and confirmed-real-memset'd, so correctly
     * zeroed) memory. This watches that pointer field directly: is it
     * still NULL (nothing ever set it, and Arkchemy's flat-memory model
     * silently reads garbage from address 8 instead of faulting like
     * real hardware would), or a real, plausible pointer (meaning
     * whatever it points to has its own, separate, incomplete
     * construction)? */
    {0x217ea4cu, "frame_plus8_pointer", 0xFFFFFFFFu, 0, 0},
    {0x217ea30u, "frame_real_address", 0xFFFFFFFFu, 0, 0},
    /* New lead, 2026-08-21 (cont.): ruled out tfbGame::configureMemoryFrame
     * (real vaddr 0x2004edc) as a missing dependency -- confirmed via
     * static disassembly of ppc_main's own real call order that
     * Core::igRefAlchemy (which leads to initDefault) runs BEFORE
     * configureMemoryFrame does, on real hardware too, so initDefault
     * can't legitimately depend on it. Back to checking
     * Core::igMetaObject::constructInstance's own two real
     * callocUntracked call sites directly -- this is the real
     * allocation that ultimately backs igMemoryPoolFrame::
     * instantiateFromPool, confirmed returning NULL via
     * frame_real_address above. */
    {0x215bdb3u, "constructInstance_pool", 0xFFFFFFFFu, 0, 0},
    {0x215bdb4u, "constructInstance_calloc1_args", 0xFFFFFFFFu, 0, 0},
    {0x215bdb5u, "constructInstance_calloc1_result", 0xFFFFFFFFu, 0, 0},
    {0x215bdd4u, "constructInstance_calloc2_args", 0xFFFFFFFFu, 0, 0},
    {0x215bdd5u, "constructInstance_calloc2_result", 0xFFFFFFFFu, 0, 0},
    /* New lead, 2026-08-21 (cont.): confirmed reallocCommon's NULL
     * result is real, faithful behavior (realloc(NULL,0) -> NULL by
     * real compiled design, not a Arkchemy bug) -- the real question is
     * why the requested size was 0 in the first place. That comes from
     * the class's own metaobject (real reflection data, offset+0x32 =
     * "instance size"). igMemoryPoolFrame is known to have real fields
     * (frame+8 was read as a pointer field earlier), so a real size of
     * 0 here would be wrong -- these two watches check the metaobject
     * pointer and its own reported size directly. */
    {0x215bd97u, "constructInstance_metaobject_ptr", 0xFFFFFFFFu, 0, 0},
    {0x215bd98u, "constructInstance_metaobject_size", 0xFFFFFFFFu, 0, 0},
    {0x21a4f9cu, "r25@top", 0xFFFFFFFFu, 0, 0},
    {0x21a4f9du, "field18", 0xFFFFFFFFu, 0, 0},
    {0x21a4fc4u, "nextptr", 0xFFFFFFFFu, 0, 0},
    {0x21a4ff0u, "structmalloc", 0xFFFFFFFFu, 0, 0},
    {0x21a5024u, "bufmalloc", 0xFFFFFFFFu, 0, 0},
};
#define ARKCHEMY_DEBUG_WATCH_SLOT_COUNT (sizeof(g_debug_watch_slots) / sizeof(g_debug_watch_slots[0]))
static void debug_watch_sink(uint32_t pc, uint32_t value) {
    for (size_t i = 0; i < ARKCHEMY_DEBUG_WATCH_SLOT_COUNT; i++) {
        if (g_debug_watch_slots[i].tag != pc) continue;
        g_debug_watch_slots[i].hit_count++;
        if (value != g_debug_watch_slots[i].last_value) {
            g_debug_watch_slots[i].changed_count++;
            g_debug_watch_slots[i].last_value = value;
        }
        return;
    }
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
    //
    // Split into a per-"what"-category budget as of 2026-08-21: doubling
    // MEM1's real size didn't fix the boot spin (it just failed again at
    // the same real fill ratio), which pointed at the two new real event
    // types cafeos_coreinit_mem.h now also logs -- large (64KB+) single
    // allocations, and MEMGetAllocatableSizeForExpHeapEx queries -- since
    // those answer *who* is consuming the heap, not just *that* it ran
    // out. Those events happen earlier, during the real fill-up, so a
    // single shared counter risked the (far more frequent) OOM-failure
    // spam using up the whole budget before any of them got logged.
    static int fail_count = 0;
    static int large_count = 0;
    static int query_count = 0;
    static int other_count = 0;
    int *counter = &other_count;
    if (strstr(what, "out of space") != NULL) counter = &fail_count;
    else if (strstr(what, "large alloc") != NULL) counter = &large_count;
    else if (strstr(what, "query") != NULL) counter = &query_count;
    if (*counter >= 40) return;
    (*counter)++;
    // g_ppc_current_pc/g_ppc_fn_call_count are updated at the entry of
    // every real recompiled function (see ppc_runtime.h's own comment) --
    // reading them right here, inside this shim call itself, captures
    // exactly which real function *called into* this event, for free, no
    // extra plumbing needed.
    checkpoint("[MEM EVENT #%d] %s requested=%u heap_base=0x%x heap_size=%u heap_used=%u -- called from last_pc=0x%x calls=%llu",
               *counter, what, requested, heap_base, heap_size, heap_used,
               g_ppc_current_pc, (unsigned long long)g_ppc_fn_call_count);
    if (*counter == 40) checkpoint("[MEM EVENT] further '%s'-type events suppressed", what);
}

alignas(16) static u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    FILE *f = fopen("sdmc:/switch/Jouster/game-exception-dump.log", "w");
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

// void ppc_arkchemy_game_entry(PpcContext *ctx) -- the real, complete,
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
    // ppc_arkchemy_game_entry below it -- untested real code that might
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
    // Real hang, first actually caught 2026-08-20 once real Switch
    // controller input started reaching the game (see cafeos_vpad.h):
    // Core::igStringPool::remove (0x21a55cc) entered with r3 (`this`,
    // the igStringPool singleton) equal to 0x0 -- a NULL pointer, not
    // the bucket-walk/hash-mismatch theory this watch was originally
    // added to test (that theory's now moot; the real problem is one
    // level up). Static analysis of the real disassembly traced this
    // to two specific real functions: `Core::igStringPool::getDefault`
    // (0x21aa838, an accessor that just returns a real static pointer
    // at &.data+5460) and `Core::igStringPool::bootstrapInitialize`
    // (0x21aa564, the real constructor that's supposed to *write* that
    // same pointer) -- both are real, unconditionally reached from
    // `Core::igArkCore::initBootstrap` (0x214726c), in the correct
    // order (bootstrapInitialize before getDefault) in the disassembly.
    // If that's genuinely true on a real run, the pointer should never
    // be NULL by the time anything calls getDefault() -- so this
    // widens the single watch to 4 slots (see ppc_runtime.h's own
    // ARKCHEMY_WATCH_SLOTS comment) to catch all four real call sites in
    // one real run and settle whether initBootstrap/bootstrapInitialize
    // ever actually ran before the NULL reached remove(), or something
    // else entirely is overwriting that pointer back to NULL later.
    g_ppc_watch[0].pc = 0x214726cu; /* igArkCore::initBootstrap entry */
    // Slot 1 repurposed 2026-08-21: bootstrapInitialize had already told
    // its story (hits=1@21795, r3=1, stable every run since). Traced the
    // NULL "current memory context" global back to its real setter,
    // Core::igMemoryContext::userInstantiate(bool) at 0x217b820 -- its
    // only reference anywhere in the whole ~19,600-function binary
    // besides its own definition is inside the indirect-call dispatch
    // table, meaning it is only ever reachable through a vtable slot,
    // not a direct bl a static grep can follow. Checked several plausible
    // real callers (the igMemoryContext ctor, systemActivate, the start
    // of igArkCore::init) by hand; none call it directly. Watching its
    // own real entry point settles empirically, on real hardware, both
    // whether it ever fires at all before the stall sets in, and -- if it
    // does -- whether that happens before or after mallocString's own
    // single real call (hits=1@21872 every run so far).
    // Slot 1 repurposed 2026-08-21: userInstantiate had told its own real
    // story (hits=0 across a whole real 45-second run, and a new
    // dispatch_userInstantiate watch confirmed ppc_dispatch is never even
    // asked for its real address -- so the bug is upstream of the vtable
    // dispatch itself). Traced the real generic consumer of the same
    // "current memory context" global instead:
    // Core::igSingleton::createSingletonInstance (0x215bf04) reads it
    // conditionally, only when the class's own memory pool has a real
    // flag byte set (checked via getPool() then a byte load at its own
    // pool object's offset 0) -- most singletons apparently build via a
    // default/global pool that never touches this global at all. Watching
    // this function's own real entry settles, on real hardware, whether
    // it's even reached near the real stall window, and with what
    // meta-object (r4, the real argument here).
    // Slot 1 repurposed again 2026-08-21: the boot-time igStringPool spin
    // is fixed (real root cause: a missing "bootstrap heap" -- see
    // cafeos_coreinit_mem.h's own ARKCHEMY_BOOTSTRAP_HEAP_HANDLE_ADDR
    // comment). Real hardware now gets much further, into a NEW stall
    // inside Core::igObjectList::setCount (0x2164250), which shrinks a
    // list by decrementRefCount-ing (oldCount-newCount)>>3 groups of 8
    // real elements. If the real "old count" it reads back from the
    // list object's own +8 field is corrupted/huge, that loop could run
    // an enormous (not truly infinite, but effectively so) number of
    // real iterations -- this watch captures the real entry args
    // (r3=this, r4=newCount) to cross-check against the two new
    // ppc_debug_watch calls hand-inserted directly in the function body
    // (real old count at 0x2164260, real computed loop count at
    // 0x2164288).
    g_ppc_watch[1].pc = 0x2164250u; /* igObjectList::setCount entry -- r3=this r4=newCount */
    // Slots 2/3 repurposed 2026-08-20: getDefault/remove had gone stable
    // and uninformative (same 3/0 hit counts every single run since the
    // widened watch went in), while a real, newly-found billion-call
    // stall turned out to be Core::igStringPoolContainer::mallocString
    // looping forever because Core::igStringPoolContainer::reserveMemory
    // (its own internal per-pool free-list search, layered on top of and
    // separate from the ExpHeap allocator this runtime already fixed a
    // real leak in) never succeeds. Manually re-deriving that internal
    // allocator's byte-level correctness from static disassembly alone
    // proved slow and error-prone (same real lesson this project's own
    // memory already has on Cemu ground-truth-over-guessing) -- watching
    // both real call sites' own real arguments directly answers the
    // actual open question (are requested sizes growing without bound,
    // or is this a pure logic bug against a fixed size) with real
    // hardware data instead of more speculation.
    // Slot 2 repurposed again 2026-08-20: reserveMemory had already told
    // us everything it could (hits=1, never called again -- the stall is
    // inside mallocString's own internal loop, not a retry-via-call
    // pattern). The loopwatch data traced the actual failure to
    // Core::igMemoryPool::malloc's real return value being NULL on every
    // one of 260M+ real attempts, without ever reaching this runtime's
    // own ExpHeap shim -- meaning the failure is inside
    // Core::igObject::getMemoryPool -> Core::igMemoryContext::
    // getMemoryPoolByIndex, resolving a *global* "current memory
    // context" pointer, not anything tied to the specific object being
    // allocated for. Watching this function's own real entry args
    // directly (called billions of times, unlike reserveMemory) answers
    // the obvious next question for free: is that global context NULL/
    // uninitialized, or a real pointer with some other problem.
    g_ppc_watch[2].pc = 0x217b058u; /* igMemoryContext::getMemoryPoolByIndex entry -- r3=context r4=index */
    g_ppc_watch[3].pc = 0x21a4f68u; /* igStringPoolContainer::mallocString entry -- r3=container "this" r4=requested string length */
    checkpoint("[game thread] calling ppc_init_globals...");
    ppc_init_globals(&g_ctx);
    g_globals_init_done = true;
    checkpoint("[game thread] ppc_init_globals done");
    checkpoint("[game thread] calling ppc_run_static_initializers (114 real C++ static initializers)...");
    ppc_run_static_initializers(&g_ctx);
    g_static_init_done = true;
    checkpoint("[game thread] ppc_run_static_initializers done");
    // Real, targeted fix added 2026-08-21 after a full real hardware trace
    // (see cafeos_coreinit_mem.h's own ARKCHEMY_BOOTSTRAP_HEAP_HANDLE_ADDR
    // comment for the complete explanation) found the true root cause of
    // the boot-time igStringPool spin: a real dedicated "bootstrap heap"
    // that Core::igMemoryContext's own constructor needs to allocate
    // itself is never created anywhere in this project's currently
    // recompiled output, so that constructor silently fails and returns
    // an unconstructed object, cascading into every symptom traced this
    // session. Real, confirmed-necessary ordering fix: this must run
    // *after* ppc_run_static_initializers, not before -- an earlier
    // attempt placed it before and the handle still read back as 0x0 at
    // the real constructor's own read site, meaning one of the real 114
    // static initializers legitimately zero-constructs the global struct
    // this handle field lives in, clobbering an earlier write. Running
    // last, right before the real game entry point, avoids that.
    checkpoint("[game thread] calling arkchemy_mem_bootstrap_heap_init...");
    arkchemy_mem_bootstrap_heap_init(&g_ctx);
    checkpoint("[game thread] arkchemy_mem_bootstrap_heap_init done");

    // Real, bounded diagnostic added 2026-08-20 per direct owner request
    // to "build something to test" alongside the igStringPool hang hunt
    // -- a first, honest test of whether the recompiled Bink video
    // decoder (the real RAD Bink SDK, statically compiled into this
    // game's own binary -- see the Project Log's own entry on this)
    // actually works at all, before investing in the much larger real
    // work a full display/audio pipeline would need. Real findings that
    // make this test possible without guessing: BinkOpen's own real
    // file I/O (`radopen`/`radclose` in the disassembly) goes through
    // the exact same real FSOpenFile/FSCloseFile Cafe OS calls this
    // project's own cafeos_coreinit_fs.h shim already implements and
    // has real hardware confirmation for -- no separate "WiiU file
    // client" callback struct to reverse-engineer, since this shim's
    // own FSOpenFile ignores the FSClient* argument entirely (confirmed
    // by reading its own source) and just resolves the path directly.
    // Runs here (after static initializers, before the real, currently-
    // hanging ppc_arkchemy_game_entry) so this test's own result is
    // independent of that separate, still-open bug -- global/static
    // state Bink itself might rely on is already real and initialized
    // by this point, same as real hardware's own boot order.
    {
        checkpoint("[game thread] Bink decoder test: calling BinkOpen on a real movie file...");
        void ppc_BinkOpen(PpcContext *ctx);
        void ppc_BinkClose(PpcContext *ctx);
        void ppc_BinkDoFrame(PpcContext *ctx);
        void ppc_BinkNextFrame(PpcContext *ctx);
        const char *test_path = "movies/bash.mov";
        uint32_t str_addr = g_ctx.r[1] - 256; /* real, safe scratch area well below the current real stack top -- nothing else has run since ppc_init_globals set r[1], so this is unused real guest memory */
        for (size_t i = 0; i <= strlen(test_path); i++) {
            ppc_store_u8(&g_ctx, str_addr + (uint32_t)i, (uint8_t)test_path[i]);
        }
        g_ctx.r[3] = str_addr;
        g_ctx.r[4] = 0; /* real BinkOpen flags -- 0 is the real documented default */
        ppc_BinkOpen(&g_ctx);
        uint32_t hbink = g_ctx.r[3];
        if (hbink == 0) {
            checkpoint("[game thread] Bink decoder test: BinkOpen(\"%s\") returned NULL -- either the file wasn't found on the SD card at content/%s, or a real decode/open failure. Real FSOpenFile log line above (if any) shows which.",
                       test_path, test_path);
        } else {
            checkpoint("[game thread] Bink decoder test: BinkOpen succeeded, real HBINK handle=0x%x -- trying BinkDoFrame...", hbink);
            g_ctx.r[3] = hbink;
            ppc_BinkDoFrame(&g_ctx);
            checkpoint("[game thread] Bink decoder test: BinkDoFrame returned r3=0x%x", g_ctx.r[3]);
            g_ctx.r[3] = hbink;
            ppc_BinkNextFrame(&g_ctx);
            checkpoint("[game thread] Bink decoder test: BinkNextFrame call completed (no crash)");
            g_ctx.r[3] = hbink;
            ppc_BinkClose(&g_ctx);
            checkpoint("[game thread] Bink decoder test: BinkClose completed -- real decoder round-trip successful");
        }
    }

    checkpoint("[game thread] calling ppc_arkchemy_game_entry...");
    void ppc_arkchemy_game_entry(PpcContext *ctx);
    ppc_arkchemy_game_entry(&g_ctx);
    checkpoint("[game thread] ppc_arkchemy_game_entry returned");
    g_game_thread_done = true;
}

#define GAME_THREAD_STACK_SIZE (4 * 1024 * 1024)
// Used when sdmc:/switch/Jouster/test-seconds.txt is missing/invalid --
// see main()'s own comment on that file for why short is the safer
// default. 45s is enough for every watch/loopwatch counter this project
// has needed so far to show real, stable data (all of them settle within
// the first couple of real seconds once the game thread gets going).
#define GAME_TEST_DEFAULT_SECONDS 45

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Jouster", 0777);
    g_log = fopen("sdmc:/switch/Jouster/game-results.log", "w");
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
    // arkchemy_gx2_create_framebuffers) -- not CPU-writable, so real text
    // can't be blitted into them without a full shader/vertex pipeline
    // this project doesn't have yet. libnx's own console is a complete,
    // separate, CPU-side text renderer that needs none of that, so this
    // file now uses it instead and no longer touches GX2Init/deko3d at
    // all -- one less thing sharing nwindowGetDefault() with whatever
    // the recompiled game's own (currently still invisible, since
    // shader translation doesn't exist yet) GX2 calls do later.
    // Bigger, more detailed banner as of 2026-08-20 per direct owner
    // request -- a hand-built 5x7 dot-matrix wordmark (each letter's own
    // bit pattern above, rendered with '#') instead of a smaller
    // stylized-font version. Deliberately plain ASCII, not the Unicode
    // block-drawing glyphs (U+2588 etc) an earlier draft of this used --
    // libnx's default console font is a fixed 256-glyph single-byte
    // table (see libnx's own ConsoleFont/console.h), not a real Unicode
    // font, so a multi-byte UTF-8 sequence would render as several
    // wrong/garbled glyphs (one per raw byte) instead of one real block
    // character. Plain ASCII is guaranteed correct on any font that
    // table could plausibly be. Still just 7 text rows tall so it
    // doesn't eat the whole screen.
    //
    // Rebranded 2026-08-21: this project (formerly "Bramble") is now
    // "Arkchemy" -- see CODENAMES.md and the project's own rename memo
    // for the full story. This specific repo/binary is "Jouster" (the
    // Switch runtime piece), named as its own subtitle below the main
    // wordmark, same as the other Arkchemy repos (conquertron, blaster).
    mutexInit(&g_console_mutex);
    consoleInit(NULL);
    printf("\x1b[32m  =~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~\x1b[0m\n\n");
    printf("\x1b[1;32m");
    printf("   ###  ####  #   #  #### #   # ##### #   # #   #\n");
    printf("  #   # #   # #  #  #     #   # #     ## ## #   #\n");
    printf("  #   # #   # # #   #     #   # #     # # #  # # \n");
    printf("  ##### ####  ##    #     ##### ####  # # #   #  \n");
    printf("  #   # # #   # #   #     #   # #     #   #   #  \n");
    printf("  #   # #  #  #  #  #     #   # #     #   #   #  \n");
    printf("  #   # #   # #   #  #### #   # ##### #   #   #  \n");
    printf("\x1b[0m");
    printf("\x1b[32m  =~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~\x1b[0m\n");
    printf("\x1b[2m  Jouster -- static recompilation engine -- Skylanders: Spyro's Adventure\x1b[0m\n\n");
    consoleUpdate(NULL);

    checkpoint("Arkchemy (Jouster) game smoke test starting");

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

    // Real cap on how long this smoke test runs before exiting on its
    // own -- this is a first real run of the actual, complete game
    // entry point; there's no way to know in advance whether it
    // finishes, loops forever (the real, expected shape of a real game
    // main loop), or hangs on something this runtime doesn't support
    // yet. Still exits early if held/pressed sooner.
    //
    // Runtime-configurable as of 2026-08-21, per direct owner request --
    // most real debugging sessions this project runs are short, targeted
    // checks (does this specific counter move in the first few seconds),
    // not genuine ten-minute soaks, and STALL_TIMEOUT_FRAMES below can't
    // help there since a real infinite spin loop (this runtime's most
    // common real failure shape all session) keeps g_ppc_fn_call_count
    // climbing forever, never triggering it. Reading a plain integer
    // (seconds) from a real SD-card text file means the owner can change
    // how long the NEXT run lasts without a rebuild -- just edit the
    // file. Missing/invalid file falls back to a short default, since
    // "make it hang for ten minutes by accident" is a worse default than
    // "have to explicitly ask for a long run".
    int test_seconds = GAME_TEST_DEFAULT_SECONDS;
    {
        FILE *cfg = fopen("sdmc:/switch/Jouster/test-seconds.txt", "r");
        if (cfg) {
            int parsed = 0;
            if (fscanf(cfg, "%d", &parsed) == 1 && parsed > 0) test_seconds = parsed;
            fclose(cfg);
        }
    }
    const int GAME_TEST_AUTO_EXIT_FRAMES = test_seconds * 60;
    checkpoint("test duration: %d second(s) (%d frames) -- edit sdmc:/switch/Jouster/test-seconds.txt to change",
               test_seconds, GAME_TEST_AUTO_EXIT_FRAMES);

    // Real, added 2026-08-20 per direct owner request: every real hang
    // found at the time showed the exact same real signature --
    // g_ppc_fn_call_count (updated on every real recompiled function's
    // entry, see ppc_runtime.h) frozen completely solid, not just slow,
    // for the entire rest of a real run. Waiting out the full 10-minute
    // cap to *confirm* that on every single real test run wastes real
    // owner time for no real diagnostic benefit once it's been frozen
    // for a few real seconds.
    //
    // Real, confirmed false positive found 2026-08-21: this assumption
    // doesn't hold for every real stretch of code, only for genuine
    // hangs. Core::igObjectList::setCount's own real cleanup loop
    // processes a large (real, if implausible-looking -- a separate,
    // still-open question) element count entirely via null-slot checks,
    // never once calling Core::igObject::decrementRefCount because every
    // slot was null -- real hand-inserted instrumentation confirmed the
    // loop's own back-edge hit count landed exactly on the real expected
    // total (131056) and had already stopped changing between two
    // consecutive status lines a full second apart, meaning it
    // genuinely finished and returned, not stuck -- while
    // g_ppc_fn_call_count stayed completely frozen the entire time
    // regardless, since nothing in that stretch calls a newly-entered
    // function at all. The old 3-second threshold was too tight for
    // real, legitimate call-free stretches like this one and killed a
    // run that was still making real progress. Widened with real
    // headroom under the 45-second default full-run cap above -- still
    // short enough to catch a genuinely infinite spin well before that
    // cap, per the same real reasoning as before, just no longer this
    // aggressive about it.
    //
    // Made proportional to the configured test duration as of 2026-08-21,
    // instead of a second independent hardcoded constant: a real,
    // legitimate call-free stretch (like the one above) doesn't have a
    // knowable fixed real duration -- widening this value alone would
    // just mean picking a new arbitrary number to eventually widen
    // again. Tying it to test_seconds means bumping the existing
    // test-seconds.txt override (already real, already documented right
    // above, no rebuild needed) gives a real, longer window to any run
    // that turns out to need one, with a fixed 5-second margin so the
    // stall message still fires with a moment to spare before the hard
    // GAME_TEST_AUTO_EXIT_FRAMES cutoff, rather than the two limits
    // racing each other.
    const int STALL_TIMEOUT_FRAMES = GAME_TEST_AUTO_EXIT_FRAMES > 300 ? GAME_TEST_AUTO_EXIT_FRAMES - 300 : GAME_TEST_AUTO_EXIT_FRAMES;
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
        // Real Switch controller input now wired into the recompiled
        // game's own VPADRead calls (see cafeos_vpad.h's own comment) --
        // added 2026-08-20 after the owner asked how they'd know when to
        // press A: they wouldn't have, since VPADRead previously always
        // reported "no samples" regardless of real input, same as the
        // Portal of Power's own nsyshid detection still honestly does
        // (no real USB HID backend exists yet for that one). This call
        // keeps the shim's own real button/stick state current every
        // frame, same rate as this file's own padUpdate() above.
        arkchemy_vpad_update(&pad);

        // Auto-press A, simulating the owner's own manual "press A
        // repeatedly, semi-randomly" testing session that originally
        // triggered the real igStringPool::remove NULL-this crash --
        // unattended smoke-test runs otherwise never advance past the
        // "press A to start" screen at all (confirmed 2026-08-20: three-
        // plus clean 10-minute runs in a row with w3(remove) hits=0 and
        // last_pc just cycling between a handful of addresses billions
        // of times, a tight idle spin, not real forward progress), so
        // there was no real chance of ever reproducing it without this.
        // ORs onto whatever the real controller already reports (does
        // not clobber genuine manual input) -- 6 held frames (~0.1s at
        // 60fps) out of every 180 (~3s) is long enough for VPADRead's
        // trigger/release edge detection above to see a clean
        // press-then-release, not just a single-frame blip a real human
        // thumb could never actually produce. Gated on static_init_done
        // so it can't interfere with anything during the earlier real
        // init phases, which never read VPAD anyway.
        if (g_static_init_done && (frame % 180) < 6) {
            g_arkchemy_vpad.held |= 0x8000u; /* VPAD_BUTTON_A */
        }

        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // Real, independent "still alive" indicator, replacing the old
        // pulsing GX2 color (see this file's top-of-main comment for
        // why a color alone wasn't enough). Two parts: a one-time,
        // checkpoint()-logged line on every real phase *transition*
        // (permanent in scrollback and the SD-card log, same as any
        // other real event), plus a continuously-animated spinner line
        // below that, updated every single frame via a bare "\r" (no
        // newline -- overwrites in place rather than scrolling, so a
        // real 60fps update rate stays readable instead of flooding the
        // console) showing the spinner glyph, current phase, frame
        // count, and live call count all moving in real time -- a much
        // more direct "still alive, not stuck" signal than a pulsing
        // color ever was.
        // Boot phases named after Spyro's Adventure's own roster as of
        // 2026-08-21, one per element matching the phase's existing real
        // ANSI color, per direct owner request: Trigger Happy (Tech,
        // amber/yellow), Spyro (Magic, purple -- fitting, since he's this
        // game's own protagonist), Gill Grunt (Water, blue), Stealth Elf
        // (Life, green). Same real phase boundaries as before, just
        // named after the roster instead of bare color words.
        static bool globals_announced = false, static_announced = false, done_announced = false;
        if (g_game_thread_done && !done_announced) {
            done_announced = true;
            checkpoint("\x1b[32m== phase: STEALTH ELF (game entry returned) ==\x1b[0m");
        } else if (g_static_init_done && !static_announced) {
            static_announced = true;
            checkpoint("\x1b[34m== phase: GILL GRUNT (running real game entry point) ==\x1b[0m");
        } else if (g_globals_init_done && !globals_announced) {
            globals_announced = true;
            checkpoint("\x1b[35m== phase: SPYRO (114 real static initializers) ==\x1b[0m");
        }

        // Plain ASCII spinner ('|/-\'), not a Unicode braille-dot one --
        // same real font-table reasoning as the banner above.
        static const char spinner_frames[] = { '|', '/', '-', '\\' };
        const char *phase_color = g_game_thread_done ? "\x1b[32m" : g_static_init_done ? "\x1b[34m"
                                 : g_globals_init_done ? "\x1b[35m" : "\x1b[33m";
        const char *phase_name = g_game_thread_done ? "STEALTH ELF" : g_static_init_done ? "GILL GRUNT"
                                : g_globals_init_done ? "SPYRO" : "TRIGGER HAPPY";
        mutexLock(&g_console_mutex);
        printf("\r\x1b[K%s%c\x1b[0m  phase=%-13s frame=%d/%d  calls=%llu",
               phase_color, spinner_frames[(frame / 4) % 4], phase_name, frame, GAME_TEST_AUTO_EXIT_FRAMES,
               (unsigned long long)g_ppc_fn_call_count);
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
            // Was 1024 -- real, confirmed truncation found 2026-08-21,
            // same class of bug as checkpoint()'s own buffer fix above:
            // this session alone added a dozen new debug watches while
            // chasing the setCount investigation, and the very last one
            // registered (setCount_real_caller_lr) silently never
            // appeared in the SD-card log at all, the line just cutting
            // off mid-entry instead. 3072 leaves real headroom below
            // checkpoint()'s own 4096-byte buffer for the "main frame
            // ..." prefix this gets appended to.
            char loopwatch_buf[3072];
            size_t loopwatch_len = 0;
            for (size_t i = 0; i < ARKCHEMY_DEBUG_WATCH_SLOT_COUNT && loopwatch_len < sizeof(loopwatch_buf); i++) {
                int n = snprintf(loopwatch_buf + loopwatch_len, sizeof(loopwatch_buf) - loopwatch_len,
                                  " -- loopwatch(%s) hits=%llu changed=%u last=0x%x", g_debug_watch_slots[i].label,
                                  (unsigned long long)g_debug_watch_slots[i].hit_count, g_debug_watch_slots[i].changed_count,
                                  g_debug_watch_slots[i].last_value);
                if (n > 0) loopwatch_len += (size_t)n;
            }
            checkpoint("main frame %d/%d -- globals_init=%d static_init=%d game_started=%d game_done=%d -- sti_idx=%u last_pc=0x%x caller_lr=0x%x calls=%llu -- r3=0x%x r4=0x%x r5=0x%x r6=0x%x"
                       " -- mem: fail=%llu free=%llu reuse=%llu"
                       " -- w0(initBootstrap) hits=%u@%llu r3=0x%x r4=0x%x"
                       " -- w1(createSingletonInstance) hits=%u@%llu metaObject=0x%x"
                       " -- w2(getMemoryPoolByIndex) hits=%u@%llu context=0x%x index=0x%x"
                       " -- w3(mallocString) hits=%u@%llu this=0x%x reqlen=0x%x"
                       "%s",
                       frame, GAME_TEST_AUTO_EXIT_FRAMES, g_globals_init_done, g_static_init_done,
                       g_game_thread_started, g_game_thread_done,
                       g_ppc_static_init_index, g_ppc_current_pc, g_ppc_last_caller_lr, (unsigned long long)g_ppc_fn_call_count,
                       g_ctx.r[3], g_ctx.r[4], g_ctx.r[5], g_ctx.r[6],
                       (unsigned long long)g_arkchemy_mem_alloc_fail_total, (unsigned long long)g_arkchemy_mem_free_total,
                       (unsigned long long)g_arkchemy_mem_reuse_total,
                       g_ppc_watch[0].hit_count, (unsigned long long)g_ppc_watch[0].last_hit_call_count, g_ppc_watch[0].r3, g_ppc_watch[0].r4,
                       g_ppc_watch[1].hit_count, (unsigned long long)g_ppc_watch[1].last_hit_call_count, g_ppc_watch[1].r3,
                       g_ppc_watch[2].hit_count, (unsigned long long)g_ppc_watch[2].last_hit_call_count, g_ppc_watch[2].r3, g_ppc_watch[2].r4,
                       g_ppc_watch[3].hit_count, (unsigned long long)g_ppc_watch[3].last_hit_call_count, g_ppc_watch[3].r3, g_ppc_watch[3].r4,
                       loopwatch_buf);
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
