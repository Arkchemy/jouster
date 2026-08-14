// Milestone 2, expanded: a genuine libnx homebrew app that runs several of
// Bramble's recompiler test programs (see switch/native/regenerate.sh) on
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
    ctx.r[1] = sizeof(ctx.mem) - 256;
    t1_arithmetic_compute(&ctx);
    checkResult("t1_arithmetic", (int32_t)ctx.r[3], 260);
}

static void runFloating(void) {
    checkpoint("running t2_floating (single-precision FP + rodata constants)...");
    static PpcContext ctx;
    ctx.r[1] = sizeof(ctx.mem) - 256;
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
    ctx.r[1] = sizeof(ctx.mem) - 256;
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
    ctx.r[1] = sizeof(ctx.mem) - 256;
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
    ctx.r[1] = sizeof(ctx.mem) - 256;
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
    mkdir("sdmc:/switch/Bramble", 0777);
    g_log = fopen("sdmc:/switch/Bramble/test-results.log", "w");

    printf("Bramble -- on-hardware recompiler test suite\n\n");
    checkpoint("== Bramble on-hardware test suite starting ==");
    if (!g_log) {
        checkpoint("(warning: could not open SD card log file -- continuing without it)");
    }

    runArithmetic();
    runFloating();
    runLoop();
    runRodataTable();
    runFnptr();

    checkpoint("");
    checkpoint("== done: %d passed, %d failed ==", g_pass_count, g_fail_count);
    printf("\n%s\n\n", g_fail_count == 0 ? "ALL MATCH" : "SOME MISMATCHED");
    if (g_log) {
        printf("Log written to sdmc:/switch/Bramble/test-results.log\n");
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
