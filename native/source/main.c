// Real Milestone 2: a genuine libnx homebrew app that calls
// Bramble's recompiler output (generated.c, produced from
// testdata/arithmetic.c by `recomp`) and displays the result on screen.
//
// This is the actual end-to-end proof the project needs: not just "does a
// minimal NRO boot" (switch/src/start.s, the libnx-free version built from
// the sandbox), but "does recompiled PowerPC code, running as real Switch
// homebrew, produce the correct result" -- ground truth is 260 (see
// tools/verify.sh, which checks the same generated.c against a native
// build of the original C on the host and under QEMU-ARM64).
#include <stdio.h>
#include <switch.h>

#include "ppc_runtime.h"

void ppc_compute(PpcContext *ctx);

int main(int argc, char *argv[]) {
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    static PpcContext ctx; // zero-initialized by BSS
    ctx.r[1] = sizeof(ctx.mem) - 256; // stack pointer, headroom for stwu
    ppc_compute(&ctx);

    printf("Bramble -- Milestone 2\n\n");
    printf("Recompiled PowerPC result: %d\n", (int32_t)ctx.r[3]);
    printf("Expected (ground truth):   260\n\n");
    printf("%s\n\n", (int32_t)ctx.r[3] == 260 ? "MATCH" : "MISMATCH");
    printf("Press + to exit.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
