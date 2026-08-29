#!/usr/bin/env python3
"""Route the game's Bink calls to a native decoder instead of the recompiled one.

Bink is middleware, not game code. conquertron already replaces coreinit, GX2
and snd_core with native implementations rather than recompiling Nintendo's
libraries -- Bink belongs in exactly that category, and the devkitPro ffmpeg
already carries ff_bink_decoder, ff_binkaudio_dct_decoder and ff_bink_demuxer.

Fixing the recompiled decoder's bitstream truncation instruction by
instruction is work the recomp model says we should not have to do.

This renames the recompiled definitions to ppc_Bink*__recompiled and leaves
the call sites untouched, so the shim in source/bink_ffmpeg.c takes the
original names and every call in the game routes to it. Nothing is deleted:
the originals stay linked and callable, so the shim can delegate anything it
does not implement, and the two can be compared frame for frame.

Idempotent -- re-running after a regeneration is a no-op if already applied.
"""
import re, sys, glob, os

# Only the entry points the game actually calls. Verified by counting call
# sites in the generated tree rather than guessed from the header.
API = [
    "BinkOpen", "BinkClose", "BinkDoFrame", "BinkNextFrame", "BinkWait",
    "BinkGetFrameBuffersInfo", "BinkRegisterFrameBuffers", "BinkGoto",
    "BinkPause", "BinkShouldSkip", "BinkSetVolume", "BinkSetVideoOnOff",
    "BinkSetSoundOnOff", "BinkSetSoundTrack", "BinkSetSpeakerVolumes",
    "BinkSetWillLoop", "BinkSetIOSize", "BinkSetFileOffset", "BinkSetError",
    "BinkSetSoundSystem", "BinkGetTrackID", "BinkSetWiiUFileClient",
    "BinkSetSimulate",
]

def main():
    apply = "--apply" in sys.argv
    args = [a for a in sys.argv[1:] if a != "--apply"]
    root = args[0] if args else "source"
    renamed = 0
    for path in sorted(glob.glob(os.path.join(root, "generated_*.c"))):
        src = open(path, encoding="utf-8").read()
        out = src
        for fn in API:
            # the DEFINITION only -- call sites keep the original name so they
            # bind to the shim
            pat = re.compile(r"^void ppc_%s\(PpcContext \*ctx\) \{" % re.escape(fn), re.M)
            if pat.search(out):
                out = pat.sub("void ppc_%s__recompiled(PpcContext *ctx) {" % fn, out)
                renamed += 1
        if out != src and apply:
            open(path, "w").write(out)
    print(("renamed " if apply else "would rename ") + str(renamed) + " recompiled Bink definitions")

main()
