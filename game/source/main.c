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

/* On-screen console output is switched off the moment the recompiled game
 * entry is called (2026-08-29). The log file keeps everything.
 *
 * Why: three runs in a row died with libnx 2345-0020 (BadGfxQueueBuffer)
 * inside framebufferEnd, reached from the MAIN thread's consoleUpdate, under
 * a second after the game thread entered real engine code. The game itself
 * is never on that stack.
 *
 * Two explanations were checked and refuted by measurement rather than
 * argument:
 *   - guest memory corrupting the host: impossible, every guest access is
 *     masked with & (PPC_MEM_SIZE - 1);
 *   - host memory starvation: the run of the same day survived 81 seconds of
 *     continuous console updates on the identical "used=3185MB total=3189MB"
 *     3MB of headroom, and capping the libnx heap moved that number not at
 *     all (hbloader commits it before this NRO runs).
 *
 * What is left is that something the game thread does once it reaches real
 * engine code takes the console's framebuffer down with it. Removing the
 * console removes the victim: if the run then survives, that is confirmed
 * and we finally get to watch the boot; if it still dies, the fault is in
 * the game thread and the crash report will point straight at it instead of
 * at libnx's graphics path. Either result is worth more than the screen. */
static volatile bool g_console_enabled = true;
/* Real gap found in an audit, 2026-08-24: this function had no printf
 * format attribute, so GCC did no format/argument checking on it at all
 * -- on the single most important diagnostic function in the project,
 * whose main call site currently passes 72 arguments. A mismatch there
 * is undefined behaviour that prints plausible-looking garbage rather
 * than failing loudly, which is the worst possible failure mode for
 * something every hardware investigation depends on for evidence.
 * Declaring the attribute makes the compiler verify every call site. */
__attribute__((format(printf, 1, 2)))
static void checkpoint(const char *fmt, ...) {
    // Was 512 -- real, confirmed truncation found 2026-08-20: the main
    // periodic status line has grown one field at a time all session
    // (mem counters, four watch slots, two loopwatch entries) until it
    // silently overran 512 bytes, vsnprintf truncating it mid-field
    // right before this exact watchpoint's own distinct/last values --
    // which looked exactly like a real "only 6 hits" finding until this
    // was checked.
    //
    // The exact same bug came back, found in an audit 2026-08-24: "4096
    // is comfortably past anything this one line is likely to grow to"
    // stopped being true once the line picked up the dump/dispatch/
    // vtable fields. Real measurement, not theory -- 116 lines per run
    // were landing at exactly 4095 characters and ending mid-field
    // ("... -- loopwatch" with no name and no values), so every
    // loopwatch slot past that point had been silently invisible.
    // Worst case for the current line is ~4.2KB (loopwatch_buf 3072 +
    // vtable_dump_buf 256 + ~900 of fixed fields), so 8192 leaves real
    // headroom. It was ~6.3KB before the dead dispatch-log field was
    // removed; keep this figure in step with the format string, since a
    // stale estimate here is what allowed the 4096 overflow to recur.
    //
    // Raising the number alone would just set up a third occurrence, so
    // truncation is now *detected and reported* rather than silent:
    // vsnprintf returns the length it would have written, and anything
    // >= the buffer size means output was lost. That turns a silently
    // wrong diagnostic -- the worst kind, since it reads as real data --
    // into an obvious marker in the log.
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (needed >= (int)sizeof(buf)) {
        static const char kMark[] = " [!!TRUNCATED!!]";
        /* Overwrite the tail rather than append: the buffer is already
         * full, and the marker matters more than the last few bytes of
         * a field that was being cut off anyway. */
        memcpy(buf + sizeof(buf) - sizeof(kMark), kMark, sizeof(kMark));
    }

    // "\r\x1b[K" first: cleanly overwrites whatever the continuously-
    // animated status spinner (see main()'s own loop) left on the
    // current row, same real reasoning as a normal terminal's own
    // "clear line before printing a fresh one" convention -- without
    // it, a checkpoint line landing mid-spin would visibly concatenate
    // onto the spinner's leftover text instead of starting clean.
    if (g_console_enabled) {
        mutexLock(&g_console_mutex);
        printf("\r\x1b[K%s%s\x1b[0m\n", checkpoint_color(buf), buf);
        mutexUnlock(&g_console_mutex);
    }

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
typedef struct { uint32_t tag; const char *label; uint32_t last_value; uint64_t hit_count; uint32_t changed_count; uint64_t last_hit_call_count; } ArkchemyDebugWatchSlot;
static ArkchemyDebugWatchSlot g_debug_watch_slots[] = {
    /* The store-watch's own synthetic tags. ppc_store_u32 and
     * ppc_store_u8 in ppc_runtime.h raise these whenever a store hits
     * g_ppc_watch_store_addr, but debug_watch_sink only records events
     * whose tag matches a slot -- so without these four rows, arming
     * that watch records precisely nothing and the run comes back with
     * no evidence either way. That is exactly what happened on the
     * 2026-08-28 run: the watch was armed on the registry global and the
     * log had no field for it at all.
     *
     * 0xf0000001 = the value stored, 0xf0000002 = which recompiled
     * function stored it. The two byte-store tags cover a partial write
     * into the same word, which is the case the 2026-08-20 fix in
     * ppc_store_u8 was added for.
     *
     * hit_count staying 0 across a full run is the interesting answer
     * here, not a broken probe: it means nothing writes the watched
     * address at all. */
    {0xf0000001u, "storewatch_value", 0xFFFFFFFFu, 0, 0},
    {0xf0000002u, "storewatch_writer_pc", 0xFFFFFFFFu, 0, 0},
    {0xf0000003u, "storewatch_bytevalue", 0xFFFFFFFFu, 0, 0},
    {0xf0000004u, "storewatch_bytewriter_pc", 0xFFFFFFFFu, 0, 0},

    /* Positive control for the four slots above. Armed on an address a
     * literal store in init_globals writes unconditionally, so these
     * MUST show hits>0 on any run that gets past globals init. If they
     * do and the suspect slots stay at 0, the suspect really is never
     * written. If these are 0 too, the instrument is broken and the
     * whole result is void -- which is the failure mode that already
     * cost one run. */
    {0xf0000005u, "control_value", 0xFFFFFFFFu, 0, 0},
    {0xf0000006u, "control_writer_pc", 0xFFFFFFFFu, 0, 0},
    {0xf0000007u, "control_bytevalue", 0xFFFFFFFFu, 0, 0},
    {0xf0000008u, "control_bytewriter_pc", 0xFFFFFFFFu, 0, 0},

    /* Load-side watches, 2026-08-28. The store side has now measured
     * that the registry global is never written; this asks whether the
     * meta-object table entry holding arkRegisterMetaValidate's address
     * is ever READ, which separates two very different faults:
     *
     *   never read -> the walk that consumes igRegistry's metadata never
     *                 starts, and the bug is engine ordering.
     *   read, but the function still never runs -> the indirect dispatch
     *                 fails to resolve, and the bug is ours.
     *
     * The control is the registry global itself: setCapacity demonstrably
     * reads it (that read is what returns NULL and starts the whole
     * failure chain), so these slots MUST fire. Their firing alongside a
     * silent table-entry slot is also the cleanest possible statement of
     * the finding: read constantly, written never. */
    {0xf0000009u, "tableentry_loadvalue", 0xFFFFFFFFu, 0, 0},
    {0xf000000au, "tableentry_reader_pc", 0xFFFFFFFFu, 0, 0},
    {0xf000000bu, "regglobal_loadvalue", 0xFFFFFFFFu, 0, 0},
    {0xf000000cu, "regglobal_reader_pc", 0xFFFFFFFFu, 0, 0},

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
    /* New lead, 2026-08-22: statically traced the real caller chain all
     * the way from Core::igMemoryPoolFrame::instantiateFromPool (which
     * constructInstance_metaobject_ptr above already showed reads a
     * possibly-NULL "this") back through the shared lazy-init helper
     * every reflectable Core:: class goes through --
     * Core::__internalObjectBase::getClassMetaSafeInternal (real
     * 0x215b914): reads _Meta__Q2_4Core17igMemoryPoolFrame, and if
     * NULL/incomplete, calls beginArkRegister__Q2_4Core9igArkCoreFv
     * (real 0x2154d74 -- a simple flag-guard reading offset+0x16 on the
     * igArkCore singleton, confirmed statically set to 1 unconditionally
     * by igArkCore's own real ctor at 0x214702c on the success path, so
     * *probably* not the blocker but not hardware-confirmed), then --
     * only if that guard passed -- calls the class's own
     * arkRegisterInitialize__Q2_4Core17igMemoryPoolFrameSFv (real
     * 0x21b507c) through a function pointer, which should eventually
     * write a real pointer into _Meta__Q2_4Core17igMemoryPoolFrame
     * itself. This watch sits at the exact bisection point: right where
     * instantiateFromPool re-reads that same _Meta global fresh,
     * immediately after getClassMetaSafeInternal returns, still before
     * handing it to createInstance/constructInstance. If this shows
     * non-NULL, the bug is in the createInstance->constructInstance
     * handoff, not the registration lazy-init; if this is still NULL,
     * the bug is inside getClassMetaSafeInternal/beginArkRegister/
     * arkRegisterInternal itself -- cuts the remaining search space
     * in half either way. */
    {0x21c643cu, "instantiateFromPool_meta_after_lazyinit", 0xFFFFFFFFu, 0, 0},
    /* Real hardware run, 2026-08-22: instantiateFromPool_meta_after_
     * lazyinit above came back 0x0 -- confirmed the bug is upstream of
     * the createInstance/constructInstance handoff, inside the
     * registration chain itself. Cross-checked the real Discord
     * community's own RE notes on this exact mechanism (channel
     * "alchemy", 2024-03-20 and 2024-10-19 threads): arkRegisterInternal
     * calls Core::igArkRegister(&_Meta, parentArkRegisterInternal, ...,
     * name, size, ..., arkRegisterInitializeFn) -- igArkRegister is
     * confirmed (by reading its own real recompiled body, real address
     * 0x215efe0) to write unconditionally into *_Meta, even if the
     * igMetaObject instance it just tried to allocate
     * (igMetaObject::instantiateFromPool, real 0x21c1f8c) came back
     * NULL. Two remaining explanations, indistinguishable from the
     * evidence so far: (a) beginArkRegister's own flag guard (offset
     * +0x16 on the igArkCore singleton) is blocking arkRegisterInternal
     * from ever running for this class, or (b) it runs but the
     * allocation inside igMetaObject::instantiateFromPool itself fails.
     * This watch is a per-class-unique entry marker for
     * arkRegisterInternal__Q2_4Core17igMemoryPoolFrameSFv itself (unlike
     * getClassMetaSafeInternal, which is shared by every Core:: class)
     * -- hits=0 next run means (a), hits=1 means (b). */
    {0x21b5138u, "arkRegisterInternal_igMemoryPoolFrame_entry", 0xFFFFFFFFu, 0, 0},
    {0x2154d74u, "beginArkRegister_flag", 0xFFFFFFFFu, 0, 0},
    {0x2154d75u, "beginArkRegister_this_arg", 0xFFFFFFFFu, 0, 0},
    /* 2026-08-22 (retraction + correction): the real hardware run showed
     * arkRegisterInternal_igMemoryPoolFrame_entry hits=0 and
     * beginArkRegister_flag stuck at 0 for all 11 real hits -- traced
     * this back to Core::igRefAlchemy (real 0x21486a0), which guards a
     * one-time construction of the ArkCore singleton (ctor, initBootstrap,
     * init, and critically a real global-pointer write into
     * ArkCore__4Core, the exact global beginArkRegister reads) behind a
     * GHS "magic statics" reference-count guard. First suspected the
     * write itself was silently going to the wrong address due to a
     * codegen bug in how recomp folds a lis+addi relocation pair (real
     * addr 0x214871c) -- built a standalone tool linking recomp's own
     * elf_loader.cpp/disassembler.cpp against the real RPX to check this
     * directly rather than guess, and confirmed BOTH the lis and addi
     * carry real, matching HA/LO relocations (section=.data, addend=5148)
     * -- the no-op fold is correct, not a bug. Retracting that theory.
     * The real remaining question is runtime, not static: does
     * igRefAlchemy's guard genuinely see "first call" (and therefore
     * actually reach and execute the global write) on real hardware, or
     * is something upstream making it skip straight past construction.
     * These two watches settle it directly. */
    {0x214872cu, "igRefAlchemy_guard_before_increment", 0xFFFFFFFFu, 0, 0},
    {0x214875cu, "ArkCore_global_write_value", 0xFFFFFFFFu, 0, 0},
    /* 2026-08-22 (night run): the two watches above came back clean --
     * igRefAlchemy_guard_before_increment=0x0 (real first call, took the
     * construction path) and ArkCore_global_write_value=0x810004 (a
     * real, valid pointer, matching w0(initBootstrap)'s own r3 exactly).
     * So ArkCore is genuinely constructed and its global pointer is
     * genuinely set -- yet beginArkRegister_flag still reads 0 on every
     * one of 11 real hits. That narrows it to one specific remaining
     * thing: the constructed object's own offset+0x16 byte. Watches the
     * igArkCore ctor's own real `stb r12, 0x16(r3)` write directly --
     * does it land on the same 0x810004, or somehow on a different
     * address (two distinct objects, not one)? */
    {0x214702cu, "igArkCore_ctor_flag_write_address", 0xFFFFFFFFu, 0, 0},
    /* Real hardware run, 2026-08-22 (fresh Jouster.nro on real hardware):
     * igArkCore_ctor_flag_write_address=0x810004 -- the SAME address as
     * ArkCore_global_write_value and w0(initBootstrap)'s r3. Retracts
     * the "two distinct objects" theory above: the ctor's offset+0x16
     * write and beginArkRegister's read are unambiguously the same
     * object. Checked the "wrong value" theory directly against the
     * generated code (generated_0155.c, ~line 23784) rather than leave
     * it open: `2147024: li r12, 1` loads r12 as a compile-time
     * constant immediately before `214702c: stb r12, 0x16(r3)`, no
     * branch in between -- whenever this instruction executes, it
     * provably stores exactly 1, no runtime ambiguity possible. That
     * closes the value question outright and leaves exactly one open
     * explanation: ordering -- all 11 of beginArkRegister_flag's real
     * hits happen *before* the ctor's flag-write instruction ever
     * executes, legitimately reading a not-yet-initialized byte every
     * time. This loopwatch table couldn't settle that because none of
     * its slots carried a call-count timestamp (unlike the dedicated
     * w0-w3 slots' own `last_hit_call_count`, see ArkchemyWatchSlot in
     * ppc_runtime.h) -- added the same field to every generic loopwatch
     * slot so the next real run can directly compare
     * ArkCore_global_write_value's, igArkCore_ctor_flag_write_address's,
     * and beginArkRegister_flag's own call-count timestamps and settle
     * ordering outright.
     *
     * Real hardware run, 2026-08-22 (second run, with the timestamps):
     * ordering is settled, and it's NOT ordering. ArkCore_global_write_
     * value and igArkCore_ctor_flag_write_address both hit at call 21104
     * (same real ctor invocation, as expected); beginArkRegister_flag
     * hit 11 times with its LAST hit at call 21243 -- 139 calls *after*
     * the confirmed-correct write -- and still read 0 every single time,
     * including that last one. A read happening well after a confirmed
     * write to the exact same address cannot legitimately see the old
     * value; this is the distinct-object case, just one level upstream
     * of where "two distinct objects" was already ruled out (that ruled
     * out the ctor writing to the wrong place; this is beginArkRegister
     * reading from the wrong place). `beginArkRegister`'s real address
     * IS 0x2154d74 (its own function entry, confirmed via the
     * ARKCHEMY_WATCH_SLOTS per-function-entry stamp immediately above
     * this PC in generated_0156.c) and per PPC calling convention r3 at
     * entry is simply its "this" -- the igArkCore* the *caller* passed
     * in. Added a second hand-inserted watch right next to the existing
     * one (`beginArkRegister_this_arg`, synthetic tag 0x2154d75,
     * generated_0156.c) to capture that r3 directly: does the caller
     * pass 0x810004 (in which case the bug is inside beginArkRegister's
     * own dereference, weirder still) or something else entirely (in
     * which case the bug is in whatever computes/caches the pointer
     * being passed to it, e.g. a stale copy taken before ArkCore_global_
     * write_value's own call-21104 write, or a completely different
     * global read by mistake). */
    /* 2026-08-22: tried a standalone loopwatch entry here first
     * (reallocCommon_caller_lr, synthetic tag 0x216f89d) to capture
     * reallocCommon's caller -- confirmed present and unique in the
     * built .elf, yet never once fired across a full 120s hardware run,
     * not even frame 0. Root cause not found; abandoned rather than
     * keep chasing an instrumentation bug. Got the same data instead by
     * repurposing w3's own unused r6 capture (generated_0158.c) to hold
     * ctx->lr -- reusing the already-proven w0-w3 mechanism. */
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
        g_debug_watch_slots[i].last_hit_call_count = g_ppc_fn_call_count;
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
/* Named diagnostic probe, for tracing engine faults from inside
 * recompiled code.
 *
 * Written 2026-08-28 after decoding the 2026-08-27 game log. The eight
 * probes that produced that log (NULL TABLE, NULL REGISTRY ENTRY,
 * BADBUF, FOREIGN FREE, INTEGRITY, ...) each carried four values, and
 * they were smuggled through mem_alloc_fail_log_sink's own parameters --
 * so the log recorded them under the names `requested`, `heap_base`,
 * `heap_size` and `heap_used`, which is not what any of them were. The
 * label carried the real names as free text ("table / index / byteoff /
 * caller_lr") and a reader had to line the two up by position. That cost
 * a decoding step on every read and one confirmation experiment to be
 * sure the order was even right.
 *
 * This prints the names with the values instead. Same budget-free,
 * flush-after-every-line path as checkpoint(), because a probe that is
 * lost when the app dies is worth nothing.
 *
 * WHY THIS IS HERE AND THE PROBES ARE NOT: the probes themselves live at
 * specific points *inside* generated_*.c, which regenerate.sh overwrites
 * wholesale from the recompiler. They were not lost by accident -- any
 * edit to generated code is destroyed by the next regeneration, by
 * design. Anything meant to survive has to live either here, in the
 * committed harness, or in a patch that is re-applied after regenerating
 * (see docs/game-probe-reconstruction.md for the eight hook sites and
 * the values each one should report). */
/* Not static: probe patches call this from generated_*.c (see
   game/probes/). Declared in ppc_runtime.h-adjacent scope by the patch
   itself would be fragile, so it is a plain external symbol. */
void arkchemy_probe4(const char *label,
                            const char *n0, uint32_t v0,
                            const char *n1, uint32_t v1,
                            const char *n2, uint32_t v2,
                            const char *n3, uint32_t v3) {
    checkpoint("[PROBE %s] %s=0x%x %s=0x%x %s=0x%x %s=0x%x"
               " -- at last_pc=0x%x caller_lr=0x%x sti_idx=%u calls=%llu",
               label, n0, v0, n1, v1, n2, v2, n3, v3,
               g_ppc_current_pc, g_ppc_last_caller_lr, g_ppc_static_init_index,
               (unsigned long long)g_ppc_fn_call_count);
}

/* Kept reachable so the compiler cannot drop it while no generated file
 * is calling it yet -- the probes are re-applied per regeneration. */
void arkchemy_probe4_keepalive(void);
void arkchemy_probe4_keepalive(void) {
    arkchemy_probe4("keepalive", "a", 0u, "b", 0u, "c", 0u, "d", 0u);
}

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
    //
    // Real gap found and fixed 2026-08-24: the category budget used to be
    // shared across *every* heap. MEM1's own real exhaustion (calls
    // ~2902-3096, an already-diagnosed, separate issue) burned through
    // the entire 40-event "out of space" budget within the first few
    // seconds of any run -- so once the real bug this session was
    // actually chasing (the bootstrap heap being undersized for a real
    // igCafeSystemMemoryPool) got far enough to also start failing
    // "out of space", there was zero budget left to log it: a real
    // hardware round after widening that heap 64KB->3MB produced total
    // silence for 40+ real minutes with no crash and no exception dump,
    // consistent with a *different* pool now quietly repeating the same
    // exhaustion pattern against the same shared heap with its own
    // logging invisible the whole time. Keying the budget by
    // (category, heap_base) instead of category alone means a new heap
    // hitting the same failure mode always gets its own fresh budget,
    // regardless of what any other heap already used up.
    typedef struct { uint32_t heap_base; int fail_count, large_count, query_count, other_count; } ArkchemyMemLogBudget;
    static ArkchemyMemLogBudget budgets[ARKCHEMY_MEM_MAX_HEAPS + 4];
    static int budget_count = 0;
    ArkchemyMemLogBudget *b = NULL;
    int i;
    for (i = 0; i < budget_count; i++) {
        if (budgets[i].heap_base == heap_base) { b = &budgets[i]; break; }
    }
    if (b == NULL) {
        if (budget_count < (int)(sizeof(budgets) / sizeof(budgets[0]))) {
            b = &budgets[budget_count++];
            b->heap_base = heap_base;
            b->fail_count = b->large_count = b->query_count = b->other_count = 0;
        } else {
            b = &budgets[0]; /* real, documented fallback if genuinely more distinct heaps than slots turn up -- shares budget 0 rather than crashing */
        }
    }
    int *counter = &b->other_count;
    if (strstr(what, "out of space") != NULL) counter = &b->fail_count;
    else if (strstr(what, "large alloc") != NULL) counter = &b->large_count;
    else if (strstr(what, "query") != NULL) counter = &b->query_count;
    if (*counter >= 40) return;
    (*counter)++;
    // g_ppc_current_pc/g_ppc_fn_call_count are updated at the entry of
    // every real recompiled function (see ppc_runtime.h's own comment) --
    // reading them right here, inside this shim call itself, captures
    // exactly which real function *called into* this event, for free, no
    // extra plumbing needed.
    /* sti_idx added 2026-08-24: sbrk consumes every byte of MEM2 it is
     * given, at every size from 32MB to 928MB, so the question is no
     * longer "how big" but "who". ppc_run_static_initializers sets this
     * index before each of the 114 real static initializers, so pairing
     * it with each allocation event says which initializer is doing the
     * allocating -- for free, since the global is already maintained. */
    checkpoint("[MEM EVENT #%d] %s requested=%u heap_base=0x%x heap_size=%u heap_used=%u -- called from last_pc=0x%x caller_lr=0x%x sti_idx=%u calls=%llu",
               *counter, what, requested, heap_base, heap_size, heap_used,
               g_ppc_current_pc, g_ppc_last_caller_lr, g_ppc_static_init_index,
               (unsigned long long)g_ppc_fn_call_count);
    if (*counter == 40) checkpoint("[MEM EVENT] further '%s'-type events suppressed", what);
}

/* Was 0x1000 (4KB). Real, observed failure found in an audit 2026-08-24:
 * a run produced a game-exception-dump.log that existed but was ZERO
 * bytes, and no "UNHANDLED EXCEPTION" line in the results log either --
 * so the handler got as far as fopen() (which created the file) and then
 * died partway through writing, before its own fclose().
 *
 * That reasoning was WRONG and the "fix" was reverted on 2026-08-24.
 * Sequential fprintf() calls do not accumulate stack -- each returns and
 * reclaims before the next, so 45 of them peak no higher than one. The
 * empty dump is therefore not a stack overflow; the likelier cause is
 * the faulting thread holding newlib's stdio/malloc lock, so the
 * handler's own fopen/fprintf deadlocks or fails.
 *
 * A memory-limit explanation was also floated for a launch failure seen
 * the same day and is likewise NOT established: this .nro needs ~158MB
 * .text plus ~256MB .bss, which would be tight in applet mode (~448MB)
 * but is comfortable in application mode, and the owner confirmed they
 * launch via a Sphaira forwarder from the HOME menu, i.e. application
 * mode. Left at 4KB because that is the configuration with known-good
 * launches, not because the 32KB version was proven guilty.
 *
 * The consequence is worse than losing one file: it means every real
 * crash this project has hit was silently unreportable, which is
 * indistinguishable from a hang when reading the logs afterwards --
 * and this session already lost time to exactly that ambiguity.
 * 32KB is generous next to the handler's real needs and costs nothing
 * but BSS. */
alignas(16) static u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    /* Record the bare fact of the fault, and the few registers that
     * matter most, into the main log FIRST -- before attempting the
     * much larger dump below. Ordering matters: the dump previously
     * failed partway through and left a zero-byte file with nothing in
     * the results log either, so a real crash was indistinguishable
     * from a hang. Getting the essentials down and flushed up front
     * means even a completely failed dump still leaves evidence that a
     * fault happened and roughly where. */
    if (g_log) {
        int gi;
        fprintf(g_log, "*** FAULT: pc=0x%lx lr=0x%lx far=0x%lx esr=0x%x error_desc=0x%x (frame %d) ***\n",
                (unsigned long)ctx->pc.x, (unsigned long)ctx->lr.x, (unsigned long)ctx->far.x,
                ctx->esr, ctx->error_desc, g_current_frame);
        /* Full register set goes here, into the ALREADY-OPEN log, rather
         * than into the separate dump file below. Confirmed on hardware
         * 2026-08-24: this early fprintf lands every time, but the
         * fopen() below never produces a file -- almost certainly the
         * faulting thread holding newlib's stdio/malloc lock, so opening
         * a new stream inside the handler cannot complete. Writing to a
         * stream that is already open avoids that entirely, so the
         * registers actually survive the crash. */
        for (gi = 0; gi < 29; gi++) {
            fprintf(g_log, "  X%d=0x%lx\n", gi, (unsigned long)ctx->cpu_gprs[gi].x);
        }
        fprintf(g_log, "  fp=0x%lx sp=0x%lx\n",
                (unsigned long)ctx->fp.x, (unsigned long)ctx->sp.x);
        fflush(g_log);
    }
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

/* Read a guest string into a host buffer, bounded and sanitised.
 * Added 2026-08-24 to recover the engine's own error text: the game
 * calls Core::igReportHandler::reportVaList to describe some failure,
 * and igStringBuf::append then hangs scanning that message for a NUL
 * that never arrives -- so the report is never emitted and we have
 * never seen what the engine was actually complaining about. Reading it
 * here, with a hard length cap and no reliance on a terminator, shows
 * the message even though the game's own strlen cannot finish it.
 * Non-printable bytes are escaped so a garbage pointer is obvious
 * rather than corrupting the log line. */
static void guest_str(uint32_t addr, char *out, size_t out_size) {
    size_t n = 0;
    if (addr == 0) { snprintf(out, out_size, "<null>"); return; }
    while (n + 5 < out_size) {
        uint8_t c = ppc_load_u8(&g_ctx, addr + (uint32_t)n);
        if (c == 0) break;
        if (c >= 0x20 && c < 0x7f) out[n] = (char)c;
        else { out[n] = '.'; }
        n++;
        if (n >= 96) break;   /* cap: this may be an unterminated string */
    }
    out[n] = '\0';
    if (n == 0) snprintf(out, out_size, "<empty>");
}

/* No __libnx_initheap override here, deliberately. One was tried on
 * 2026-08-29 to leave the graphics driver room to allocate, on the theory
 * that libnx's default heap was starving it. It was removed the same day:
 * "used" stayed at exactly 3185MB with the override in place, because
 * hbloader commits that memory before this NRO ever runs, and a fixed inner
 * heap only adds its own size to .bss inside that same committed region --
 * spending 128MB to fix nothing. The starvation theory itself was refuted
 * separately: an earlier run survived 81 seconds of continuous console
 * updates on the identical 3MB of headroom. */

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
    // Slot 0 repurposed, 2026-08-24: initBootstrap has been a stable,
    // unchanging control reading (hits=1@21105, same every run) for
    // this entire investigation -- confirmed, not informative anymore.
    // Traced mallocString's real body directly (generated_0161.c,
    // 0x21a4f68): it calls getMemoryPool() on r3 = *(container+0) --
    // NOT on the memory context object itself -- and getMemoryPool()
    // computes the pool index as (*(that_object+4)) >> 22, then looks
    // it up against the real memory-context global. If "container"
    // (mallocString's own real "this", r3 at its entry) or the object
    // at its own +0 field was never fully constructed, index=0 could
    // be arising from reading zeroed/uninitialized memory, not a real,
    // deliberate "give me pool 0" request -- reframes the whole open
    // question from cont. 34. Watching mallocString's own entry
    // directly to get the real container address, needed before its
    // own +0/+4 fields can be read.
    // Slot 0 repurposed again, 2026-08-24: mallocString's container told
    // its story (hits=1, container=0x0 -- confirmed NULL, matching
    // igStringPool::activate()'s own bail-out path when its own
    // malloc(28) fails, real code traced directly, generated_0161.c
    // 0x21aa608). Traced igMemoryContext's own real constructor
    // (0x2178438, generated_0158.c) directly: it explicitly zeroes all
    // 4 pool-table slots (context+8/0xc/0x10/0x14, matching
    // getMemoryPoolByIndex's real hardcoded index->offset switch,
    // 0x217b074) as routine smart-pointer member init -- they start
    // NULL, so something else, running after construction but before
    // userInstantiate publishes the context, must populate them (3 of
    // 4 do end up with real pool objects; only index 0 stays
    // capacity-0). Real candidate found: Core::igMemoryContext::
    // setMemoryPools(igMemoryFrameConfigList*, igMemoryPoolList*)
    // (0x217b174) -- iterates a real, data-driven list of pool config
    // entries. Rather than keep hand-tracing this loop's many branches
    // statically, watching its own real entry directly: is it even
    // reached, and with what config-list arguments.
    // Slot 0 corrected, 2026-08-24 (same session): setMemoryPools never
    // fired (hits=0) -- wrong function, a guess that turned out unused.
    // Read igArkCore::initBootstrap's OWN real body directly instead of
    // guessing again (generated_0155.c, ~0x2147290): right after
    // constructing the context (0x2178438, already traced), it does a
    // vtable dispatch through the new context's own vtable slot+0x34
    // (the same "vtable_dispatch_target" this project found NULL back
    // in its very first session -- ppc_dispatch(ctx, 0) is a real no-op
    // in that case, not a hard failure) and then, UNCONDITIONALLY, right
    // after, calls Core::igMemoryContext::bootstrapInitialize()
    // (0x217a5ac) -- the real function whose body directly calls
    // initializePool (offsets 0xc and 0x10) and initializeStringPool
    // (offset 0x14), guarded by an early conditional skip based on a
    // separate igCafeSystemMemoryPool vtable dispatch's own return
    // value. Watching bootstrapInitialize's own entry directly this
    // time -- confirms whether it's reached at all before drilling into
    // which specific initializePool call fails.
    // Slot 0 advanced, 2026-08-24 (same session): bootstrapInitialize
    // confirmed reached (hits=1@21113, context=0x8100f0, matching
    // userInstantiate's own "this"). Narrows the question to one bit:
    // does the early conditional skip right after its own
    // igCafeSystemMemoryPool vtable dispatch (`cmpwi r3,1; beq
    // 0x217a67c`, generated_0158.c ~26639-26642) bypass all three
    // initializePool/initializeStringPool calls entirely? Rather than
    // hand-patch a mid-function watch at that exact branch (unreliable
    // this session, see cont. 26/27), watching initializePool's own
    // real function entry directly instead: hits=0 means the skip was
    // taken (never even attempted); hits>=1 means it ran and something
    // inside it is what actually fails.
    // Slot 0 advanced again, 2026-08-24 (same night, post-ADDIC-fix
    // confirmation): initializePool confirmed called with real args
    // (id=0, size=0x100000). With the vtable fix in, the dispatch log
    // now shows Core::igMemoryPool::allocatePoolMemory (real vtable
    // slot 39, offset 0x9c -- confirmed against the real RPX via
    // verify_vtable.cpp) genuinely gets called on this pool with the
    // real 1MB size, followed later by a real, direct (non-virtual)
    // call to igHeapMemoryPool::activate(). activate()'s own body
    // reads this+0x10/this+0x14 (a buffer pointer + size) and passes
    // them to a real tlsf_create() call -- both still read back as 0
    // in pool_dump even after all of this runs, so activate()'s own
    // tlsf_create() call must be failing. allocatePoolMemory is the
    // one real, plausible place that should have written those two
    // fields before activate() ever reads them -- watching its own
    // entry directly to see its real arguments and (via last_hit_
    // call_count) confirm it actually runs before activate() does.
    /* Retargeted 2026-08-24. allocatePoolMemory's question is answered
     * -- pools activate correctly now. The live question is why
     * reallocCommon is entered 35 MILLION times with pool=0x0: every
     * pool lookup goes through Core::igObject::getMemoryPool, which
     * reads the "current memory context" pointer at guest 13528
     * (.data+5336) and tail-calls getMemoryPoolByIndex on it. A static
     * scan of all 217 generated files shows exactly two writers of that
     * global: igMemoryContext::userInstantiate sets it, and
     * igMemoryContext::userRelease clears it. w1 already watches
     * userInstantiate, so watching userRelease here distinguishes the
     * three possibilities directly -- never set, set then cleared, or
     * set but not visible. */
    /* Retargeted again 2026-08-24: userRelease hits=0, so the context is
     * not being cleared -- userInstantiate (w1) simply never runs at all,
     * and the context global stays 0 for the whole run. That is a
     * REGRESSION: earlier logs this session showed
     * w1(userInstantiate) hits=1@21112 this=0x8100f0. So the boot
     * sequence used to reach it and no longer does. Watching
     * igArkCore::initBootstrap, the function that drives the whole
     * memory-context bring-up, says whether boot still gets that far. */
    /* Retargeted 2026-08-24 (late). initBootstrap has answered its
     * question -- the boot chain is repaired and userInstantiate now
     * runs. The live blocker is Core::igStringBuf::append: 113 of 120
     * samples land there while total calls collapsed to ~78k, because
     * it is spinning in append's strlen loop (lbzu / extsb. / bne, no
     * calls inside) scanning for a NUL that never arrives. Loads are
     * address-masked, so it wraps forever instead of faulting.
     *
     * That means append was handed a bad const char*. r4 is that
     * pointer and r3 is the igStringBuf, so watching the entry captures
     * both -- if r4 is 0, or points outside anything sane, that says
     * whether this is a failed allocation being passed as a string or a
     * genuinely corrupt pointer. */
    /* Write-watch on the registry-table global, armed 2026-08-28.
     *
     * The 2026-08-27 run reported "NULL REGISTRY ENTRY: table=0x0
     * index=0" from Core::igDataList::setCapacity. Reading the freshly
     * regenerated C for that function shows where both halves come
     * from:
     *
     *     215db80: lis  r12, 0x1013      -> folded to 435928 (&.bss+321000)
     *     215db84: lwz  r12, 0x45e8(r12) -> r12 = *(that global)
     *     215db88: lha  r6, 0xc(r12)     -> index  = *(int16*)(r12+0xC)
     *     215db8c: lwz  r0,  0x14(r3)    -> table  = *(r3+0x14)
     *     215db94: lwzx r27, r10, r0     -> entry  = table[index]
     *
     * Synthetic address 435928 occurs exactly ONCE in all ~8.7M lines of
     * generated C -- this read. No statically-resolved store writes it,
     * and it lives in .bss, so it is zero unless something writes it
     * through a computed address at runtime. That is what this watch
     * settles: if nothing ever fires, nothing writes it at all, and the
     * fault is a registration step that never runs rather than one that
     * runs too late.
     *
     * Structurally identical to the igStringPool::getDefault /
     * bootstrapInitialize bug described further up -- a global that a
     * bootstrap function is supposed to write, read while still NULL. */
    g_ppc_watch_store_addr = 435928u;

    /* Control address: generated_0071.c's init_globals does
     * ppc_store_u8(ctx, 4359280u, 200) unconditionally. If the control
     * slots above come back empty, the measurement is void. */
    g_ppc_watch_store_addr2 = 4359280u;

    /* 0x119f08 -- the meta-object table slot holding
     * arkRegisterMetaValidate's address, found by scanning init_globals'
     * literal byte stores. Control on the registry global, which is read
     * every time setCapacity runs. */
    g_ppc_watch_load_addr  = 1154824u;
    g_ppc_watch_load_addr2 = 435928u;

    /* Slots 4-6, armed 2026-08-28: does registration run at all?
     *
     * The store-watch has now measured, with a working positive control
     * in the same run, that nothing writes the registry global at
     * synthetic 435928 during a full 7200-frame run. The address occurs
     * exactly once in ~8.7M lines of generated C -- the read in
     * igDataList::setCapacity -- so the question is no longer "who wrote
     * it wrong" but "what was supposed to write it, and did that ever
     * run".
     *
     * Core::igRegistry's three registration entry points are the obvious
     * candidates. hits=0 on all three means registration never happens,
     * and the NULL table is a consequence rather than a cause. hits>0
     * means it runs and does not write this global, which points
     * somewhere else entirely -- either answer narrows the search a lot,
     * which is the point of asking. */
    /* Answered 2026-08-28: all three came back hits=0 in a run where four
     * other entry watches fired, so registration never runs. The
     * load-watch then showed igRegistry's meta-object table entry is
     * never even read, which makes this engine ordering rather than a
     * dispatch-resolution fault. Slots re-pointed at the machinery that
     * is supposed to drive registration, to find where the sequence
     * stops:
     *
     *   beginArkRegister / endArkRegister bracket the phase.
     *   callClassRegistrationFunctions is the walker that should reach
     *   every class's own arkRegister* function -- there is one per
     *   class across the whole binary, not just igRegistry's.
     *   addObjectMeta is what registers each meta-object.
     *
     * begin=0 means the registration phase never starts at all.
     * begin>0 with call=0 means it starts and the walk never happens.
     * call>0 with addObjectMeta=0 means the walk runs and registers
     * nothing, which would point at an empty or unbuilt meta list. */
    /* The boot ladder, 2026-08-28. Community reverse-engineering of
     * Alchemy (NefariousTechSupport and others, Skylanders RE Discord)
     * describes the registration architecture: every class has an
     * arkRegisterInternal that calls Core::igArkRegister with its name,
     * metaobject, parent's arkRegisterInternal, size, vtable and a
     * pointer to its arkRegisterInitialize. Reading the regenerated C
     * gives the chain that is supposed to reach those:
     *
     *   arkchemy_game_entry -> main -> Core::igRefAlchemy
     *     -> Core::igArkCore::__ct -> beginArkRegister
     *       -> callClassRegistrationFunctions -> per-class registration
     *
     * Measured already: registration never runs, and igRegistry's
     * meta-object descriptor is never even read. This run finds which
     * rung the sequence stops on. The first zero going down the ladder
     * is the answer. */
    /* 2026-08-28, second correction. The boot ladder came back
     * main=1, igRefAlchemy=1, igArkCore::ctor=1, beginArkRegister=23.
     * Registration is NOT missing -- it runs twenty-three times. The
     * earlier reading of "registration never runs" was wrong: what was
     * measured was igRegistry's own three arkRegister* methods never
     * being entered, and that was generalised further than the evidence
     * allowed.
     *
     * So the question narrows again: the phase runs, and the classes we
     * need are not reached by it. This set follows the walk itself.
     *
     *   call=0            -> begin runs but the walk never does.
     *   call>0, add=0     -> the walk runs and registers nothing.
     *   add>0, end=0      -> registration is entered and never completes,
     *                        i.e. it dies partway through the class list.
     *   igRegistry's own arkRegisterInternal is the specific class whose
     *   absence starts the failure chain, so it is worth a slot of its
     *   own to see whether the walk simply never gets that far. */
    /* 2026-08-28, third round. The walk answered:
     *
     *   beginArkRegister  23    endArkRegister 23   -> brackets balance
     *   addObjectMeta     36                        -> 36 classes register
     *   callClassRegistrationFunctions 0            -> the bulk walk never runs
     *
     * So registration works and is simply tiny. Tracing the two paths in
     * the generated C explains the split: addObjectMeta's caller is
     * igMetaObject::appendToArkCore, which is how a class registers
     * itself from static init -- that is the 36. The bulk path is
     * igIGBFile::processMetaObjectList -> callClassRegistrationFunctions,
     * reached from igIGBFile::readFile. In other words the rest of the
     * class list is supposed to arrive by READING A METADATA FILE, and
     * this engine has never opened a file of its own in any run.
     *
     * So the question is now about file loading, not registration:
     *
     *   readFile=0  -> nothing ever tries to read the metadata file, and
     *                  the fault is upstream in whatever should ask for it.
     *   readFile>0, processMetaObjectList=0 -> the read is attempted and
     *                  fails or returns early, which points at the FS shim.
     *
     * appendToArkCore is kept as an in-run control: it should read 36. */
    /* 2026-08-28, fourth round. Nothing tries to read the metadata file:
     * readFile=0, processMetaObjectList=0, with appendToArkCore=36 as a
     * working control in the same run. So the fault is upstream of the
     * file read, not in it.
     *
     * Tracing the failing lookup upward names the demander:
     * Core::igHandlePool::setCapacity (0x2165878) is what calls the
     * igDataList::setCapacity that reads a NULL metaobject. So the handle
     * pool grows, needs the metaobject for its element type, and that
     * type is not among the 36 that self-register -- it was supposed to
     * arrive in the metadata file that never loads. permanent/bootstrap.bld
     * (198KB) in the dump is the obvious candidate for that file.
     *
     * This set asks when the handle pool is first used, and whether any
     * loading machinery starts at all:
     *
     *   handlePool>0 with igz=0 -> the pool is used before anything has
     *       loaded, and the ordering is the bug.
     *   igz>0 -> loading does start, and it is failing rather than being
     *       skipped, which points at the filesystem shim. */
    /* 2026-08-28, fifth round -- and the probe changed the question
     * completely.
     *
     * REGISTRY_LOOKUP fired five times. Three succeeded with real tables
     * and real entries. The fatal one reported
     *
     *   table=0x0 byteoff=0x0 datalist=0x0 entry=0x0
     *
     * datalist is r3, the igDataList `this`. It is NULL. The table then
     * reads as *(0 + 0x14) = 0, so the registry was never the problem --
     * setCapacity was called on a null object.
     *
     * igHandlePool::setCapacity makes two calls. The first passes the
     * pool itself; the second does `lwz r3, 0x18(r31)` and passes
     * *(pool + 0x18). The failing call's caller_lr (0x21658c8) is the
     * return address of that second one, so *(pool + 0x18) is NULL.
     *
     * Nothing in igHandlePool writes +0x18, and the class has no
     * recovered constructor -- its fields are built by the reflection
     * system from the metafields its own arkRegisterInitialize defines.
     * So the question is whether igHandlePool is itself registered:
     *
     *   internal=0 -> igHandlePool is not among the 36, so instantiating
     *       one leaves its members unconstructed, and +0x18 stays NULL.
     *       That is the whole bug, and it is about which classes
     *       self-register rather than about the file that never loads.
     *   internal>0 -> it is registered and something else leaves the
     *       member null, which points at instantiation instead. */
    /* 2026-08-28, seventh round. igHandlePool IS registered
     * (arkRegisterInternal=1, arkRegisterInitialize=1), and the probe
     * shows the failing object is genuinely constructed:
     *
     *   pool=0x300565c vtable=0x11f0b0
     *   _handleList@0x18=0x0  _freeHandle@0x1c=0xffff
     *
     * A real vtable, and the scalar field holding a sensible 0xffff
     * sentinel. So construction ran. What did not happen is the
     * instantiation of the one OBJECT-typed field: _handleList is an
     * igDataList that something has to allocate and attach.
     *
     * That is the metafield instFuncs step. igMetaObject has
     * instantiateAndAppendFields (0x21616d0), and each metafield type has
     * its own instantiateFromPool. This asks whether either ever runs:
     *
     *   instantiateAndAppendFields=0 -> the metaobject never gets its
     *       field list built, so nothing can instantiate _handleList.
     *   >0 with instantiateFromPool=0 -> fields are appended but never
     *       instantiated for this object. */
    /* 2026-08-28, ninth round. The append call is correct:
     *
     *   [PROBE HP_APPENDFIELDS] meta=0x300552c meta_0xC=0x3
     *                           arg_r4=0x115248 arg_r5=0x0
     *   21b1b84: li r5, 0    start index
     *   21b1b88: li r6, 2    count -- both fields
     *
     * instantiateAndAppendFields is handed a real metaobject, the real
     * instFuncs array, and a count of 2. Nothing is wrong with it.
     *
     * And _freeHandle=0xffff is now explained: its metafield is an
     * igIntMetaField with setDefault, so 0xffff is a DEFAULT applied at
     * construction. _handleList is an igObjectRefMetaField, whose default
     * is legitimately null -- something else has to populate it.
     *
     * So a null _handleList may not be a fault at all. The fault may be
     * that setCapacity is called before whatever assigns it. These are
     * its four possible callers; the one that fires identifies the
     * context, and therefore what was supposed to have run first. */
    /* 2026-08-28, after Cemu gave us the real boot order. The retail
     * title opens /vol/content/alchemy.xml first, then bootstrap.bld.
     * Ours opens no file at all, and every step around the failing
     * pointer has been measured as working -- so the fault is upstream,
     * in never reaching the engine's own file loading.
     *
     * Tracing the config path backwards through the recompiled C finds
     * the step: the alchemy.xml string is stored into a .data global by
     * __sti___22_tfbCafeApplication_cpp (which does run), and exactly one
     * function reads that global -- Core::igArkCore::init (0x2147e98).
     *
     * init() is called directly by Core::igRefAlchemy, which we have
     * already measured running. It registers the three object loaders
     * (igIGB/igIGX/igIGZObjectLoader) and calls igDirectory::loadRef.
     * That is both the loaders whose absence we traced AND the start of
     * file loading.
     *
     *   init=0     -> igRefAlchemy runs but does not reach init, and the
     *                 gap is inside igRefAlchemy: small and specific.
     *   init>0, loadRef=0 -> init runs and stops before loading.
     *   loadRef>0  -> loading is attempted and fails, which points at the
     *                 filesystem shim rather than at ordering. */
    /* 2026-08-28, and this pins it to four consecutive calls.
     * igArkCore::init has hits=0 while appendToArkCore=36 controls the
     * run, so init genuinely never executes. Reading igRefAlchemy shows
     * why that is such a tight result -- init is the last of four direct
     * calls in a row:
     *
     *   2148750: bl igMemoryContext::systemActivate
     *   2148758: bl igArkCore::igArkCore       measured, hits=1
     *   2148760: bl igArkCore::initBootstrap
     *   2148768: bl igArkCore::init            hits=0
     *
     * The constructor runs and init does not, so execution stops in
     * initBootstrap -- which is exactly where this project's own
     * 2026-08-24 notes had the engine stuck, before the investigation
     * moved on to the handle pool. It also means the handle-pool null
     * chased all evening is downstream scenery: systemActivate is called
     * BEFORE the constructor, so that failure happens first and the
     * engine keeps going regardless.
     *
     *   initBootstrap=0 -> it is never entered, and the gap is between
     *       two adjacent instructions.
     *   initBootstrap=1 with init=0 -> it is entered and never returns,
     *       and everything downstream follows from that one call. */
    g_ppc_watch[4].pc = 0x214726cu; /* Core::igArkCore::initBootstrap */
    g_ppc_watch[5].pc = 0x217bc24u; /* Core::igMemoryContext::systemActivate */
    g_ppc_watch[6].pc = 0x2147e98u; /* Core::igArkCore::init (expect 0) */
    g_ppc_watch[7].pc = 0x21608ecu; /* appendToArkCore (control, expect 36) */

    g_ppc_watch[0].pc = 0x21a6b5cu; /* igStringBuf::append(const char*) -- r3=this r4=str */
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
    // Slot 1 repurposed AGAIN, 2026-08-22: the reallocCommon investigation
    // this session (see main.c's watch-table comments and the LWZU fix in
    // codegen.cpp) found a real, live, hardware-confirmed infinite retry
    // loop -- reallocCommon called 60M+ times with byte-for-byte identical
    // args (pool=0x810184, ptr=0x0, size=0x1c) -- that matches, exactly,
    // the mechanism this OLDER investigation (right above) already
    // root-caused before this session started: Core::igObject::
    // getMemoryPool() unconditionally reads the "current memory context"
    // global, which stays NULL because Core::igMemoryContext::
    // userInstantiate never runs, so every pool returned is degenerate
    // and every allocation against it fails forever, independent of real
    // heap size (already ruled out -- MEM1 was doubled twice with no
    // effect). That investigation confirmed hits=0 on userInstantiate's
    // own entry via a dedicated watch -- but a full 217-file regen this
    // session (part of landing the LWZU fix) silently wiped every
    // hand-inserted ppc_debug_watch() call project-wide (confirmed: only
    // 1 remains anywhere in switch/game/source/generated_*.c, my own,
    // itself mysteriously non-functional), including setCount's watches
    // above and the original userInstantiate one. Re-pointing this slot
    // directly at userInstantiate's own real entry to reconfirm with
    // fresh, live, post-regen/post-fix data whether it still never runs.
    g_ppc_watch[1].pc = 0x217b820u; /* igMemoryContext::userInstantiate entry -- r3=this r4=bool arg */
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
    /* Retargeted 2026-08-24: getMemoryPoolByIndex has told us all it can
     * (71,343,306 hits, context=0x0 every time). Watching
     * igMemoryContext's own constructor instead pins the regression to a
     * specific step: with w0 on initBootstrap and w1 on userInstantiate,
     * the three together say whether boot reaches initBootstrap, whether
     * the context object is ever constructed, and whether it is ever
     * published to the global. */
    /* Retargeted 2026-08-24 (final). The constructor question is
     * answered -- the boot chain is repaired. Far more interesting: the
     * caller_lr recorded while igStringBuf::append spins is 0x218a62c,
     * inside Core::igReportHandler::reportVaList. So the engine hit some
     * error, called its own reporter to describe it, and the reporter
     * hung formatting the message. Whatever that report says is very
     * likely the explanation for everything downstream, and we have
     * never seen it because the reporter never finishes.
     *
     * r3 is the ReportType and r4 the format string, so watching the
     * entry captures the message the engine was trying to emit. */
    g_ppc_watch[2].pc = 0x218a548u; /* igReportHandler::reportVaList -- r3=type r4=fmt */
    /* Repurposed 2026-08-22: mallocString never fired in any hardware run
     * across the whole igMemoryPoolFrame investigation (hits=0 always) --
     * dead slot. Post-LWZU-fix, the game now runs a sustained real loop
     * (~2M calls/60 frames, steady across two independent hardware runs)
     * cycling through Core::igMemoryPool::reallocCommon/malloc,
     * Core::igMemoryContext::getMemoryPoolByIndex, and
     * Core::igObject::getMemoryPool -- worth knowing whether this is
     * healthy bulk content-loading allocation or a stuck retry loop.
     * Discord export (alchemy channel, 2025-12-29, bonesinmysoup/
     * nefarioustechsupport) documents a real, previously-seen SSA bug
     * where reallocCommon fails and calls
     * Core::igDefaultMemoryFailureCallback when a memory pool's
     * configured size doesn't match what's actually being requested
     * (their example: pool index mismatch, requesting ~0.3MB against an
     * undersized pool) -- real precedent for exactly this failure shape
     * on this exact game. Retargeting w3 at reallocCommon's own real
     * entry (0x216f89c) to capture its actual arguments per real PPC
     * signature Core::igMemoryPool::reallocCommon(void* ptr, uint size,
     * bool, bool): r3=pool "this", r4=ptr being reallocated, r5=new
     * size. */
    g_ppc_watch[3].pc = 0x216f89cu;
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
    checkpoint("[game thread] arkchemy_mem_bootstrap_heap_init done -- handle written=0x%x (readback=0x%x)",
               (unsigned)ARKCHEMY_BOOTSTRAP_HEAP_BASE,
               ppc_load_u32(&g_ctx, ARKCHEMY_BOOTSTRAP_HEAP_HANDLE_ADDR));

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

    /* The game entry point is the real main(argc, argv): r3 is argc and r4
     * is argv, and tfbCore::tfbApplication::setAppCommandLine scans argv[0
     * .. argc) comparing each entry against its known switches.
     *
     * Nothing set them before 2026-08-29, so argc was whatever happened to
     * be left in r3 -- after the Bink test above, the HBINK handle
     * (0xb400040 = 188,743,744). The game then walked a 188-million-entry
     * argv, calling igStringHelper::comparei on garbage pointers, and burned
     * ~80 seconds of a 120-second test doing it. It was not a hang: the loop
     * is bounded by argc and did run to completion, which is why the first
     * MEMAllocFromExpHeapEx only appears at call 189,611,248.
     *
     * This was invisible until the record-form CR0 fix landed the same day.
     * Before it, the `bge` guarding that loop tested a stale flag and
     * skipped the scan entirely, so the wrong argc never cost anything.
     *
     * argc = 0 is the honest value here -- this harness genuinely has no
     * command line to pass -- and it makes setAppCommandLine skip the scan
     * on its own bounds check rather than by accident. */
    checkpoint("[game thread] on-screen console going quiet now -- everything "
               "from here is in this log file only (see g_console_enabled)");
    g_console_enabled = false;

    g_ctx.r[3] = 0; /* argc */
    g_ctx.r[4] = 0; /* argv */

    checkpoint("[game thread] calling ppc_arkchemy_game_entry (argc=0, argv=NULL)...");
    void ppc_arkchemy_game_entry(PpcContext *ctx);
    ppc_arkchemy_game_entry(&g_ctx);
    checkpoint("[game thread] ppc_arkchemy_game_entry returned");
    g_game_thread_done = true;
}

/* Was 4MB. Real fault captured on hardware 2026-08-24, the first crash
 * this project has ever managed to report (the handler could not write
 * a dump before today, so every one of these looked like a hang):
 *
 *   FAULT pc=0xaf904e4cc lr=0xaf90522ec far=0x79563b000
 *         esr=0x92000145 error_desc=0x101
 *
 * esr decodes as EC=0x24 data abort, WnR=1 (a write), DFSC=0x05
 * (level-1 translation fault) -- a write to an unmapped page. It cannot
 * be a guest store: ppc_store_u32 masks every address into the 256MB
 * mem[] array and physically cannot leave it. A write to an unmapped
 * host page, faulting deep inside recursive metaobject code
 * (igMetaObject::inheritFrom -> instantiateAndAppendFields), is the
 * signature of running off the end of this thread's stack into its
 * guard page.
 *
 * The engine's reflection setup recurses through inheritance chains, so
 * 4MB is a guess that happened to hold until the memory-pool fixes let
 * that code run for real. Raised to 32MB: the owner launches via a
 * Sphaira forwarder from the HOME menu, i.e. application mode with
 * gigabytes available, so this is cheap. If a fault still lands just
 * past a 32MB boundary the hypothesis is wrong and the real cause is a
 * bad pointer rather than depth -- the stack bounds logged at startup
 * make that check possible. */
#define GAME_THREAD_STACK_SIZE (32 * 1024 * 1024)
// Used when sdmc:/switch/Jouster/test-seconds.txt is missing/invalid --
// see main()'s own comment on that file for why short is the safer
// default. 45s was enough for every watch/loopwatch counter this project
// needed up through the igMemoryPoolFrame hang (all of them settled
// within the first couple of real seconds once the game thread got
// going). Bumped to 120s, 2026-08-22: post-LWZU-fix, the game now runs
// a sustained, real reallocCommon/malloc/getMemoryPool(ByIndex) loop the
// entire 45s window without ever visibly finishing or transitioning to
// anything else -- worth knowing whether that's bounded (a real bulk
// content-load that eventually completes) or genuinely unbounded, which
// 45s wasn't long enough to distinguish.
#define GAME_TEST_DEFAULT_SECONDS 120

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
    // Rebranded 2026-08-21: this project (formerly "Arkchemy") is now
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

    /* Build stamp, added 2026-08-24. This run is deterministic, so a
     * re-run of an older .nro produces a byte-identical log -- which
     * made "the new build crashed before logging" and "the old build
     * was run again" impossible to tell apart from the log alone, and
     * that ambiguity has already caused at least one wrong diagnosis.
     * Stamping the compile time makes every log self-identifying. */
    checkpoint("Arkchemy (Jouster) game smoke test starting -- build " __DATE__ " " __TIME__);

    /* Report the process's real memory limits, added 2026-08-24. This
     * build needs ~1GB of BSS plus ~158MB of .text, which is far past
     * what earlier builds asked for, and whether that fits depends on
     * the launcher's address-space setting (Sphaira exposes 32/36/39-bit)
     * rather than on anything in this repo. Guessing at Switch internals
     * from memory is exactly how this session already produced one wrong
     * memory-limit explanation, so the process is asked directly instead:
     * if a launch fails or allocation is tight, these numbers say whether
     * the address space is genuinely too small. */
    {
        u64 total = 0, used = 0, heap_sz = 0, aslr_sz = 0;
        svcGetInfo(&total,   InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used,    InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&heap_sz, InfoType_HeapRegionSize,  CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&aslr_sz, InfoType_AslrRegionSize,  CUR_PROCESS_HANDLE, 0);
        checkpoint("process memory: total=%lluMB used=%lluMB heap_region=%lluMB aslr_region=%lluMB (guest mem[]=%uMB)",
                   (unsigned long long)(total / (1024*1024)), (unsigned long long)(used / (1024*1024)),
                   (unsigned long long)(heap_sz / (1024*1024)), (unsigned long long)(aslr_sz / (1024*1024)),
                   (unsigned)(PPC_MEM_SIZE / (1024*1024)));
    }

    Thread game_thread;
    /* Priority was 0x2C -- identical to the main thread's own default,
     * on the same core (-2). Real, measured consequence found in an
     * audit 2026-08-24: when the recompiled game enters a tight loop
     * that makes no function calls and no syscalls (for example the
     * pointer-walk loops in igArkCore::initBootstrap), it never yields,
     * and an equal-priority main thread gets starved -- real runs
     * managed roughly 180 frames of a 7200-frame/120-second test, about
     * 1.5fps instead of 60, so the periodic status line simply stopped
     * appearing.
     *
     * That is a genuinely dangerous failure mode for a diagnostic
     * harness, because "the log stopped" then looks exactly like "the
     * game crashed or hung", and this session lost real time to that
     * ambiguity before it was understood. The thread being observed
     * must never be able to starve the thread doing the observing.
     *
     * 0x30 is numerically lower priority than the main thread on
     * Horizon (0x00 highest, 0x3F lowest), so the harness always
     * preempts the game and keeps logging. This changes scheduling
     * only -- no recompiled game logic is affected. */
    Result rc = threadCreate(&game_thread, game_thread_func, NULL, NULL, GAME_THREAD_STACK_SIZE, 0x30, -2);
    /* Log the real stack region so a future fault address can be checked
     * against it directly, instead of inferring stack overflow from the
     * shape of the fault. */
    if (R_SUCCEEDED(rc)) {
        uintptr_t sp_lo = (uintptr_t)game_thread.stack_mirror;
        checkpoint("game thread stack: base=0x%lx size=0x%x top=0x%lx",
                   (unsigned long)sp_lo, (unsigned)GAME_THREAD_STACK_SIZE,
                   (unsigned long)(sp_lo + GAME_THREAD_STACK_SIZE));
    }
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
        if (g_console_enabled) {
        mutexLock(&g_console_mutex);
        printf("\r\x1b[K%s%c\x1b[0m  phase=%-13s frame=%d/%d  calls=%llu",
               phase_color, spinner_frames[(frame / 4) % 4], phase_name, frame, GAME_TEST_AUTO_EXIT_FRAMES,
               (unsigned long long)g_ppc_fn_call_count);
        consoleUpdate(NULL);
        mutexUnlock(&g_console_mutex);
        }

        if (frame % 300 == 0) {
            /* Host-side headroom over time. If the graphics abort really is
             * memory starvation, this shrinks toward zero before it fires;
             * if it stays flat, the cause is elsewhere and this line says so
             * in one number instead of another round of theorising. */
            u64 t = 0, u = 0;
            svcGetInfo(&t, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
            svcGetInfo(&u, InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
            checkpoint("host mem @frame %d: used=%lluMB total=%lluMB free=%lluMB",
                       frame, (unsigned long long)(u / (1024*1024)),
                       (unsigned long long)(t / (1024*1024)),
                       (unsigned long long)((t - u) / (1024*1024)));
        }

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
                                  " -- loopwatch(%s) hits=%llu@%llu changed=%u last=0x%x", g_debug_watch_slots[i].label,
                                  (unsigned long long)g_debug_watch_slots[i].hit_count, (unsigned long long)g_debug_watch_slots[i].last_hit_call_count,
                                  g_debug_watch_slots[i].changed_count, g_debug_watch_slots[i].last_value);
                if (n > 0) loopwatch_len += (size_t)n;
            }
            // Direct memory dump of the pool object reallocCommon keeps
            // being called against, 2026-08-23: w3's pool=0x810184 is a
            // fixed constant across every hardware run so far (not a
            // moving/growing allocation), so it's readable straight out
            // of g_ctx.mem without needing any per-call hand-inserted
            // watch (which this session already found are wiped by a
            // full regen and unreliable to add fresh -- see prior
            // commits). Dumping the first 8 words blind (no known real
            // igMemoryPool struct layout to name these by field yet) to
            // see whether this looks like a real, populated pool object
            // or genuinely all-zero/degenerate.
            uint32_t pool_dump[8];
            for (int pd = 0; pd < 8; pd++) pool_dump[pd] = ppc_load_u32(&g_ctx, ARKCHEMY_BOOTSTRAP_HEAP_BASE + 0x184u + (uint32_t)(pd * 4));
            // 2026-08-23: userInstantiate's own real body (generated_0158.c,
            // 0x217b820) turned out to be exactly two real instructions --
            // store "this" into .data+5336 (the "current memory context"
            // global) and return. It does NO pool/buffer setup at all --
            // was never its job. So whatever's actually zeroed in pool_dump
            // above must trace back further, to whatever constructed the
            // *context* object (0x8100f0, confirmed w1's real "this") in
            // the first place, before userInstantiate ever published it.
            // Same direct-read technique, same fixed constant address
            // across every run so far.
            uint32_t ctx_dump[8];
            for (int cd = 0; cd < 8; cd++) ctx_dump[cd] = ppc_load_u32(&g_ctx, ARKCHEMY_BOOTSTRAP_HEAP_BASE + 0xf0u + (uint32_t)(cd * 4));
            // 2026-08-24: initializePool (context offsets 0xc/0x10) turns
            // out to allocate a small housekeeping object via the real
            // MEMAllocFromExpHeapEx (this project's own already-fixed
            // Cafe OS shim -- succeeds, matching the valid vtable+refcount
            // headers already found) and store its pointer into the
            // context's table slot immediately, BEFORE two vtable-
            // dispatched "create"/"initialize" calls that are presumably
            // where real capacity actually gets reserved. Logging the
            // real dispatch targets seen (the g_ppc_dispatch_log
            // scaffolding this used has since been removed -- it was fed
            // by hand-patches that the full regen wiped) to see whether
            // those two specific calls resolve to real functions or to
            // 0/an unhandled address -- matches the same "vtable slot never populated"
            // shape already found once this session (igMemoryContext's
            // own vtable+0x34 dispatch).
            // 2026-08-23 (cont.): ctx_dump[2..5] = 0x810128, 0x810184,
            // 0x8101e8, 0x81024c -- four real, sequential, non-null
            // pointers, evenly spaced (~0x60-0x64 apart) -- looks exactly
            // like a real pool table, and 0x810184 (the one reallocCommon
            // keeps failing against) is genuinely IN it, not orphaned.
            // Dumping the other three pools' own first 4 words each,
            // alongside 0x810184's already-known all-zero-past-header
            // state, to see whether this is one specific broken slot or
            // every pool in this table is equally uninitialized.
            uint32_t other_pools[3] = {ARKCHEMY_BOOTSTRAP_HEAP_BASE + 0x128u, ARKCHEMY_BOOTSTRAP_HEAP_BASE + 0x1e8u, ARKCHEMY_BOOTSTRAP_HEAP_BASE + 0x24cu};
            uint32_t other_pool_dump[3][4];
            for (int op = 0; op < 3; op++) {
                for (int w = 0; w < 4; w++) other_pool_dump[op][w] = ppc_load_u32(&g_ctx, other_pools[op] + (uint32_t)(w * 4));
            }
            // 2026-08-23 (cont. 2): decoded word[3] as a real pool ID, not
            // table position -- 0x810128=ID1 (word[2]=0x1000000, a real
            // 16MB capacity), 0x810184=ID0 (capacity 0, the one
            // getMemoryPoolByIndex(index=0) resolves to and reallocCommon
            // keeps failing against), 0x8101e8=ID2 (capacity 0), 0x81024c=
            // ID3 (capacity 0). IDs 0 and 2 SHARE a vtable (0x10f738),
            // distinct from ID1's (0x10950c) and ID3's (0x10b434) -- looks
            // like a real "lazy/unbacked pool" class, separate from a
            // "real, backed pool" class, by design. Dumping that shared
            // vtable's own first 6 real function-pointer entries to see
            // what methods it actually has (e.g. some "back me now" /
            // lazy-init method reallocCommon might be supposed to trigger
            // but doesn't).
            // 2026-08-24: the hardcoded 0x10f738 read above is now STALE --
            // real hardware confirmed the ADDIC relocation-fold fix
            // (codegen.cpp) changed pool_dump[0] (this pool's own real
            // vtable pointer) to 0x10b55c, matching the correctly-computed
            // synthetic address verified against the real RPX
            // (recomp/src/verify_vtable.cpp). Following the pool's own
            // CURRENT vtable pointer dynamically instead of a fixed
            // address, and widening to 18 words to cover the real vtable's
            // full extent (ground truth showed real entries at odd
            // word-offsets up to 17, consistent with a real 8-byte-per-
            // slot GHS vtable layout -- alternating real-pointer/zero).
            uint32_t vtable_dump[18];
            for (int vd = 0; vd < 18; vd++) vtable_dump[vd] = ppc_load_u32(&g_ctx, pool_dump[0] + (uint32_t)(vd * 4));
            char vtable_dump_buf[256];
            size_t vtable_dump_len = 0;
            for (int vd = 0; vd < 18 && vtable_dump_len < sizeof(vtable_dump_buf); vd++) {
                int n = snprintf(vtable_dump_buf + vtable_dump_len, sizeof(vtable_dump_buf) - vtable_dump_len,
                                  "%s0x%x", vd == 0 ? "" : ",", vtable_dump[vd]);
                if (n > 0) vtable_dump_len += (size_t)n;
            }
            /* Recover the engine's own report text and whatever
             * igStringBuf::append is stuck scanning -- see guest_str. */
            char report_fmt_str[112];
            char append_str_str[112];
            guest_str(g_ppc_watch[2].r4, report_fmt_str, sizeof(report_fmt_str));
            guest_str(g_ppc_watch[0].r4, append_str_str, sizeof(append_str_str));
            checkpoint("main frame %d/%d -- globals_init=%d static_init=%d game_started=%d game_done=%d -- sti_idx=%u last_pc=0x%x caller_lr=0x%x calls=%llu -- r3=0x%x r4=0x%x r5=0x%x r6=0x%x"
                       " -- mem: fail=%llu free=%llu reuse=%llu"
                       " -- w4(igArkCore::initBootstrap) hits=%u"
                       " w5(igMemoryContext::systemActivate) hits=%u"
                       " w6(igArkCore::init) hits=%u"
                       " w7(appendToArkCore) hits=%u"
                       " -- w0(igStringBufAppend) hits=%u@%llu this=0x%x str=0x%x r5=0x%x r6=0x%x"
                       " -- w1(userInstantiate) hits=%u@%llu this=0x%x boolArg=0x%x"
                       " -- w2(reportVaList) hits=%u@%llu type=0x%x fmt=0x%x"
                       " -- w3(reallocCommon) hits=%u@%llu pool=0x%x ptr=0x%x size=0x%x caller_lr=0x%x"
                       " -- pool_dump[0..7]=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x"
                       " -- ctx_dump[0..7]=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x"
                       " -- pool0x810128[0..3]=0x%x,0x%x,0x%x,0x%x"
                       " -- pool0x8101e8[0..3]=0x%x,0x%x,0x%x,0x%x"
                       " -- pool0x81024c[0..3]=0x%x,0x%x,0x%x,0x%x"
                       " -- pool_vtable[0..17]=%s"
                       " -- cur_mem_ctx(.data+5336)=0x%x boot_heap_handle=0x%x"
                       " -- report_fmt=\"%s\" append_str=\"%s\""
                       "%s",
                       frame, GAME_TEST_AUTO_EXIT_FRAMES, g_globals_init_done, g_static_init_done,
                       g_game_thread_started, g_game_thread_done,
                       g_ppc_static_init_index, g_ppc_current_pc, g_ppc_last_caller_lr, (unsigned long long)g_ppc_fn_call_count,
                       g_ctx.r[3], g_ctx.r[4], g_ctx.r[5], g_ctx.r[6],
                       (unsigned long long)g_arkchemy_mem_alloc_fail_total, (unsigned long long)g_arkchemy_mem_free_total,
                       (unsigned long long)g_arkchemy_mem_reuse_total,
                       g_ppc_watch[4].hit_count, g_ppc_watch[5].hit_count, g_ppc_watch[6].hit_count, g_ppc_watch[7].hit_count,
                       g_ppc_watch[0].hit_count, (unsigned long long)g_ppc_watch[0].last_hit_call_count, g_ppc_watch[0].r3, g_ppc_watch[0].r4, g_ppc_watch[0].r5, g_ppc_watch[0].r6,
                       g_ppc_watch[1].hit_count, (unsigned long long)g_ppc_watch[1].last_hit_call_count, g_ppc_watch[1].r3, g_ppc_watch[1].r4,
                       g_ppc_watch[2].hit_count, (unsigned long long)g_ppc_watch[2].last_hit_call_count, g_ppc_watch[2].r3, g_ppc_watch[2].r4,
                       g_ppc_watch[3].hit_count, (unsigned long long)g_ppc_watch[3].last_hit_call_count, g_ppc_watch[3].r3, g_ppc_watch[3].r4, g_ppc_watch[3].r5, g_ppc_watch[3].r6,
                       pool_dump[0], pool_dump[1], pool_dump[2], pool_dump[3], pool_dump[4], pool_dump[5], pool_dump[6], pool_dump[7],
                       ctx_dump[0], ctx_dump[1], ctx_dump[2], ctx_dump[3], ctx_dump[4], ctx_dump[5], ctx_dump[6], ctx_dump[7],
                       other_pool_dump[0][0], other_pool_dump[0][1], other_pool_dump[0][2], other_pool_dump[0][3],
                       other_pool_dump[1][0], other_pool_dump[1][1], other_pool_dump[1][2], other_pool_dump[1][3],
                       other_pool_dump[2][0], other_pool_dump[2][1], other_pool_dump[2][2], other_pool_dump[2][3],
                       vtable_dump_buf,
                       /* The "current memory context" pointer that
                        * Core::igObject::getMemoryPool reads before every
                        * single pool lookup (confirmed by reading its
                        * generated body: it loads guest 13528 = .data+5336
                        * then tail-calls getMemoryPoolByIndex). w3 shows
                        * reallocCommon being entered 35 MILLION times with
                        * pool=0x0, so the suspicion is this global is
                        * still null and every lookup degenerates. Dumping
                        * it directly settles that. */
                       ppc_load_u32(&g_ctx, 13528u),
                       /* The bootstrap-heap handle our own boot shim writes
                        * before the game runs. Confirmed written, yet read
                        * back as 0 by igMemoryContext's constructor at call
                        * ~36,700 -- so it is being cleared during boot, and
                        * that null handle is why the constructor cannot
                        * allocate, returns NULL, and userInstantiate is
                        * never dispatched. Sampling it each frame shows
                        * when it goes. */
                       ppc_load_u32(&g_ctx, ARKCHEMY_BOOTSTRAP_HEAP_HANDLE_ADDR),
                       report_fmt_str, append_str_str,
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
