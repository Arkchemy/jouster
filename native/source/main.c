// Milestone 2, expanded: a genuine libnx homebrew app that runs several of
// Arkchemy's recompiler test programs (see switch/native/regenerate.sh) on
// real Switch hardware, checks each against the same known-correct ground
// truth tools/verify.sh already checks under QEMU-ARM64, and -- per the
// project plan's own "Tooling & Development Approach" section -- writes a
// checkpointed log to the SD card as it goes, not just a final on-screen
// result. QEMU is a good proxy for "does the recompiled code compute the
// right answer," but it's still an emulator; this is the actual target
// hardware, running actual recompiler output, for more than just one
// integer-arithmetic program.
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>

#include "ppc_runtime.h"
#include "skylanders_figure.h"
#include "skylanders_nfc.h"

// t1_arithmetic (testdata/arithmetic.c) -- integer arithmetic/calls,
// stripped-binary heuristic recovery's target in tools/verify.sh (not
// exercised here, just the direct-symtab path).
void t1_arithmetic_compute(PpcContext *ctx);
// t2_floating (testdata/floating.c) -- single-precision FP + rodata
// constant resolution (float literal pool).
void t2_floating_compute(PpcContext *ctx);
void t2_floating_init_globals(PpcContext *ctx);
// t3_loop (testdata/loop_counted.c) -- mtctr/bdnz counted-loop branches.
void t3_loop_sumn(PpcContext *ctx);
void t3_loop_init_globals(PpcContext *ctx);
// t4_rodata (testdata/rodata_table.c) -- the real read-only-data addressing
// bug this session found and fixed: a compiler-generated switch-statement
// lookup table, indexed at runtime, not a single fixed-offset scalar load.
void t4_rodata_classify(PpcContext *ctx);
void t4_rodata_init_globals(PpcContext *ctx);
// t5_fnptr (testdata/fnptr.c) -- mtctr/bctrl indirect calls through a
// function pointer.
void t5_fnptr_compute(PpcContext *ctx);
void t5_fnptr_init_globals(PpcContext *ctx);

// t6_andi_lwzu (testdata/andi_lwzu.c, -O1) -- andi./lwzu, found missing
// while recompiling a real Wii U homebrew .rpx.
void t6_andi_lwzu_compute(PpcContext *ctx);
// t7_cond_return (testdata/cond_return.c, -O1) -- conditional-return
// (blelr and friends), same real-code origin.
void t7_cond_return_guarded(PpcContext *ctx);
// t8_addis_frsp (testdata/addis_frsp.c, -O1) -- addis/frsp, same real-code
// origin; also the only test here whose result is a double.
void t8_addis_frsp_compute(PpcContext *ctx);
void t8_addis_frsp_init_globals(PpcContext *ctx);
// t9_bss_large (testdata/bss_large.c, -O0) -- the real oversized-.bss
// address-assignment bug found in the actual Skylanders binary: .bss
// regions were capped at a 256-byte placeholder, so every global past
// the first 256 bytes silently aliased whatever section came next.
void t9_bss_large_fill_and_check(PpcContext *ctx);
void t9_bss_large_init_globals(PpcContext *ctx);

static FILE *g_log;
static int g_pass_count, g_fail_count;

// Prints to the console AND appends to the SD-card log (flushed after every
// line, not just at exit) -- if the app freezes or crashes partway through,
// whatever ran before that point is still on the SD card afterward, per the
// project plan's "self-instrumented automated testing" approach: narrow
// down where a fault happened from the last-written checkpoint, without
// needing a human to guess blind.
static void checkpoint(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fprintf(g_log, "\n");
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

static void checkResult(const char *name, int32_t got, int32_t want) {
    int pass = (got == want);
    if (pass) g_pass_count++; else g_fail_count++;
    checkpoint("[%s] got=%d want=%d -- %s", name, (int)got, (int)want, pass ? "PASS" : "FAIL");
}

static void runArithmetic(void) {
    checkpoint("running t1_arithmetic (integer arithmetic/calls)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t1_arithmetic_compute(&ctx);
    checkResult("t1_arithmetic", (int32_t)ctx.r[3], 260);
}

static void runFloating(void) {
    checkpoint("running t2_floating (single-precision FP + rodata constants)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t2_floating_init_globals(&ctx);
    t2_floating_compute(&ctx);
    // compute() returns float in f1; ground truth is 20.25 exactly, so an
    // exact compare is safe here (no accumulated rounding to worry about).
    int32_t got = (int32_t)((float)ctx.f[1] * 100.0f + 0.5f);
    checkResult("t2_floating (x100)", got, 2025);
}

static void runLoop(void) {
    checkpoint("running t3_loop (mtctr/bdnz counted-loop branches, -O2)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t3_loop_init_globals(&ctx);
    uint32_t acc_addr = 0x100;
    ppc_store_u32(&ctx, acc_addr, 7);
    ctx.r[3] = 5;
    ctx.r[4] = acc_addr;
    t3_loop_sumn(&ctx);
    checkResult("t3_loop", (int32_t)ctx.r[3], 35);
}

static void runRodataTable(void) {
    checkpoint("running t4_rodata (switch-statement lookup table, -O2)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t4_rodata_init_globals(&ctx);
    static const int32_t expect[7] = {7, 300, -19, 42, 1001, 5, -1};
    int allPass = 1;
    for (int x = 0; x <= 6; x++) {
        ctx.r[3] = (uint32_t)x;
        t4_rodata_classify(&ctx);
        int32_t got = (int32_t)ctx.r[3];
        int pass = (got == expect[x]);
        if (!pass) allPass = 0;
        checkpoint("  classify(%d) got=%d want=%d -- %s", x, (int)got, (int)expect[x], pass ? "ok" : "WRONG");
    }
    if (allPass) g_pass_count++; else g_fail_count++;
    checkpoint("[t4_rodata] -- %s", allPass ? "PASS" : "FAIL");
}

static void runFnptr(void) {
    checkpoint("running t5_fnptr (mtctr/bctrl indirect calls)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t5_fnptr_init_globals(&ctx);

    ctx.r[3] = 10; ctx.r[4] = 20; ctx.r[5] = 0; // which=0 -> add
    t5_fnptr_compute(&ctx);
    int32_t gotAdd = (int32_t)ctx.r[3];

    ctx.r[3] = 10; ctx.r[4] = 20; ctx.r[5] = 1; // which=1 -> mul
    t5_fnptr_compute(&ctx);
    int32_t gotMul = (int32_t)ctx.r[3];

    int pass = (gotAdd == 30) && (gotMul == 200);
    if (pass) g_pass_count++; else g_fail_count++;
    checkpoint("[t5_fnptr] add=%d(want 30) mul=%d(want 200) -- %s", (int)gotAdd, (int)gotMul,
               pass ? "PASS" : "FAIL");
}

// The four tests below cover instructions and a memory-layout bug found
// in real code rather than invented for a test: three came out of
// recompiling a genuine Wii U homebrew .rpx (vgmoose/wiiu-space, open
// source) and one out of the real Skylanders binary's oversized .bss.
// blaster's verify.sh has run all four under QEMU-ARM64; none had ever
// run on the actual console before this build. Ground truth for each is
// the same as verify.sh's: a plain native gcc build of the original
// source, run and compared.

static void runAndiLwzu(void) {
    checkpoint("running t6_andi_lwzu (andi./lwzu, real Wii U code find, -O1)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    // compute(x=0x23, arr=[10,20,30,40,50], n=5): masked = 0x23 & 0x1F = 3,
    // total = 150, so 153.
    uint32_t arr_addr = 0x1000;
    static const int32_t arr[5] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        ppc_store_u32(&ctx, arr_addr + (uint32_t)i * 4, (uint32_t)arr[i]);
    }
    ctx.r[3] = 0x23;
    ctx.r[4] = arr_addr;
    ctx.r[5] = 5;
    t6_andi_lwzu_compute(&ctx);
    checkResult("t6_andi_lwzu", (int32_t)ctx.r[3], 153);
}

static void runCondReturn(void) {
    checkpoint("running t7_cond_return (conditional return, real Wii U code find, -O1)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    // guarded(x) returns 0 for x <= 0 and x*2 otherwise -- both sides of
    // the early-return branch, since that branch is the whole point.
    ctx.r[3] = (uint32_t)-5;
    t7_cond_return_guarded(&ctx);
    int32_t gotNeg = (int32_t)ctx.r[3];
    ctx.r[3] = 7;
    t7_cond_return_guarded(&ctx);
    int32_t gotPos = (int32_t)ctx.r[3];
    int pass = (gotNeg == 0) && (gotPos == 14);
    if (pass) g_pass_count++; else g_fail_count++;
    checkpoint("[t7_cond_return] guarded(-5)=%d(want 0) guarded(7)=%d(want 14) -- %s",
               (int)gotNeg, (int)gotPos, pass ? "PASS" : "FAIL");
}

static void runAddisFrsp(void) {
    checkpoint("running t8_addis_frsp (addis/frsp, real Wii U code find, -O1)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t8_addis_frsp_init_globals(&ctx);
    // compute(a=3.5, b=1.25, base=100) = (float)3.5 + (100 + 0x50000)
    //                                  = 3.5 + 327780 = 327783.5, in f1.
    // b is genuinely unused by the source; it is passed anyway because the
    // real ABI still gives it an FPR slot, which is part of what this
    // test is checking. Scaled by 2 to compare as an exact integer: the
    // value has a single binary fraction digit, so this is exact, not a
    // tolerance.
    ctx.f[1] = 3.5;
    ctx.f[2] = 1.25;
    ctx.r[3] = 100;
    t8_addis_frsp_compute(&ctx);
    checkResult("t8_addis_frsp (x2)", (int32_t)(ctx.f[1] * 2.0), 655567);
}

static void runBssLarge(void) {
    checkpoint("running t9_bss_large (oversized .bss, real Skylanders regression, -O0)...");
    static PpcContext ctx;
    static PpcSharedMemory ctx_shared;
    ctx.shared = &ctx_shared;
    ctx.r[1] = PPC_MEM_SIZE - 256;
    t9_bss_large_init_globals(&ctx);
    // fill_and_check() writes 300 ints into a .bss table, sums them
    // (1..300 = 45150) and adds a .rodata entry that sits immediately
    // after .bss (333) -> 45483. Under the bug this covers, the writes
    // past byte 256 of .bss landed on top of that .rodata entry, so the
    // wrong answer here means addresses are aliasing again.
    t9_bss_large_fill_and_check(&ctx);
    checkResult("t9_bss_large", (int32_t)ctx.r[3], 45483);
}

static int g_figures_found;

static void onFigureFound(const SkylandersDumpEntry *entry, void *user_data) {
    (void)user_data;
    g_figures_found++;
    if (entry->name) {
        checkpoint("  %s: CharacterID=%d VariantID=%d -- %s", entry->path,
                   entry->figure.character_id, entry->figure.variant_id,
                   entry->variant_name ? entry->variant_name : entry->name);
    } else {
        checkpoint("  %s: CharacterID=%d VariantID=%d -- not in this project's table",
                   entry->path, entry->figure.character_id, entry->figure.variant_id);
    }
}

static void runFigureScan(void) {
    checkpoint("scanning sdmc:/switch/Jouster/figures for local figure dumps (Phase 3b)...");
    g_figures_found = 0;
    int n = skylanders_figure_scan_dir("sdmc:/switch/Jouster/figures", onFigureFound, NULL);
    if (n < 0) {
        checkpoint("[figure scan] could not open sdmc:/switch/Jouster/figures");
    } else if (n == 0) {
        checkpoint("[figure scan] no dumps found (drop real figure dumps in that folder to test this)");
    } else {
        checkpoint("[figure scan] found %d dump(s)", n);
    }
}

static void runNfcRead(void) {
    // Phase 3c (Joy-Con NFC, no portal/dumps needed) -- ***UNVERIFIED***,
    // see skylanders_nfc.h's own detailed disclaimer. Waits up to ~5s for
    // a figure; not finding one (no NFC-capable controller connected, or
    // nothing placed on it in time) is a normal, expected outcome here,
    // same as the other real-hardware-optional checks above -- doesn't
    // affect g_pass_count/g_fail_count.
    checkpoint("checking for a Skylanders figure via Joy-Con NFC (Phase 3c, up to ~5s)...");
    if (!skylanders_nfc_init()) {
        checkpoint("[nfc read] no NFC-capable controller found, or NFC service unavailable");
        return;
    }
    SkylandersFigureId id;
    if (skylanders_nfc_read_figure(&id)) {
        const char *name = skylanders_figure_name(id.character_id);
        const char *variant = skylanders_figure_variant_name(id.character_id, id.variant_id);
        checkpoint("[nfc read] CharacterID=%d VariantID=%d -- %s", id.character_id, id.variant_id,
                   variant ? variant : (name ? name : "not in this project's table"));
    } else {
        checkpoint("[nfc read] no figure detected in time (place one on the controller's NFC point to test this)");
    }
    skylanders_nfc_exit();
}

int main(int argc, char *argv[]) {
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // Log to the SD card, not just the screen -- see the checkpoint()
    // comment above. fsdevMountSdmc() is normally already done by libnx's
    // default startup for an hbloader-launched app, but costs nothing to
    // make explicit/defensive here. mkdir failing because the directory
    // already exists is expected and fine; only actual open failure is
    // worth knowing about.
    fsdevMountSdmc();
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Jouster", 0777);
    g_log = fopen("sdmc:/switch/Jouster/test-results.log", "w");

    // Phase 3b (local figure dumps, no portal needed): scan a dedicated
    // SD card folder for real figure dumps, matching the project plan's
    // own convention (a Skylanders/figures folder, browsable subfolders).
    // An empty folder finding 0 dumps is a normal, expected outcome (no
    // dumps placed there yet) -- not a pass/fail test, so it doesn't
    // count toward g_pass_count/g_fail_count, same reasoning as
    // portal_init() reporting "no portal attached" as a normal outcome
    // rather than an error.
    mkdir("sdmc:/switch/Jouster/figures", 0777);

    printf("Arkchemy -- on-hardware recompiler test suite\n\n");
    checkpoint("== Arkchemy on-hardware test suite starting ==");
    if (!g_log) {
        checkpoint("(warning: could not open SD card log file -- continuing without it)");
    }

    runArithmetic();
    runFloating();
    runLoop();
    runRodataTable();
    runFnptr();
    runAndiLwzu();
    runCondReturn();
    runAddisFrsp();
    runBssLarge();
    runFigureScan();
    runNfcRead();

    checkpoint("");
    checkpoint("== done: %d passed, %d failed ==", g_pass_count, g_fail_count);
    printf("\n%s\n\n", g_fail_count == 0 ? "ALL MATCH" : "SOME MISMATCHED");
    if (g_log) {
        printf("Log written to sdmc:/switch/Jouster/test-results.log\n");
    }
    printf("Press + to exit.\n");
    consoleUpdate(NULL);

    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
