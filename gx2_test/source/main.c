// Real, minimal, dedicated test for the gx2 shim's first actual
// rendering path (cafeos_gx2.h's real deko3d-backed GX2Init/
// GX2ClearColor/GX2SwapScanBuffers) -- kept as its own separate .nro,
// not merged into switch/native/'s existing text-console-based
// recompiler test suite, since mixing a real deko3d swapchain into the
// same native window libnx's own consoleInit() already claims for text
// output is a real integration risk this hasn't been checked yet, and
// this project doesn't want to put the already-hardware-verified main
// suite at risk to find out.
//
// This calls the real ppc_import_gx2_* shim functions through the same
// PpcContext-based calling convention real recompiled code would use
// (not a shortcut/bypass) -- if this works, the real Switch screen
// clears to a solid color and updates every frame until + is pressed,
// real, visible, on-hardware confirmation that the GX2->deko3d bridge
// this session built actually produces pixels on a real console, not
// just that it compiles and links.
#include <switch.h>

#include "ppc_runtime.h"
#include "cafeos_gx2.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    static PpcContext ctx;
    static PpcSharedMemory shared;
    ctx.shared = &shared;

    // void GX2Init(uint32_t *attributes) -- real args ignored by this
    // shim's current implementation (see its own comment), r3 left 0.
    ppc_import_gx2_GX2Init(&ctx);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // void GX2ClearColor(GX2ColorBuffer *colorBuffer, float red,
        // float green, float blue, float alpha) -- real PPC ABI: r3 is
        // the (currently-ignored, see the shim's own comment)
        // colorBuffer pointer, the 4 floats go in f1-f4. A distinctive
        // blue -- not black/white -- so success is unambiguous on a
        // real screen.
        ctx.r[3] = 0;
        ctx.f[1] = 0.10;
        ctx.f[2] = 0.40;
        ctx.f[3] = 0.70;
        ctx.f[4] = 1.0;
        ppc_import_gx2_GX2ClearColor(&ctx);

        // void GX2SwapScanBuffers(void) -- presents the frame.
        ppc_import_gx2_GX2SwapScanBuffers(&ctx);
    }

    ppc_import_gx2_GX2Shutdown(&ctx);
    return 0;
}
