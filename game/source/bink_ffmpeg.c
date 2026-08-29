/* Bink, implemented natively with ffmpeg instead of recompiled.
 * ---------------------------------------------------------------------------
 * Bink is middleware, not game code. conquertron already replaces coreinit,
 * GX2 and snd_core with native implementations rather than recompiling
 * Nintendo's libraries; RAD's decoder belongs in exactly that category. The
 * devkitPro ffmpeg already carries ff_bink_decoder, ff_binkaudio_dct_decoder
 * and ff_bink_demuxer, so the decode is a solved problem here.
 *
 * The recompiled decoder does run -- 1,445 to 2,271 guest calls a frame, real
 * moving pixels -- but truncates after 32 to 48 luma rows of 720, and chasing
 * that through a bitstream decoder instruction by instruction is work the
 * recomp model says we should not have to do.
 *
 * tools/shim_bink.py renames the recompiled definitions to
 * ppc_Bink*__recompiled and leaves every call site alone, so these functions
 * take the original names.
 *
 * The renamed originals are still compiled, but the build uses
 * -ffunction-sections with --gc-sections, so they are dropped from the binary
 * unless something calls them. Referencing one brings it back -- which is how
 * a delegation, or a frame-for-frame diff between the two decoders, would be
 * done. Nothing here calls them today, so nothing is paying for them.
 *
 * What the game owns is untouched: it still allocates the frame buffers in
 * guest memory, still registers them, and still uploads and presents them
 * itself. All that changes is who fills the planes.
 * ------------------------------------------------------------------------ */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ppc_runtime.h"
#include "cafeos_coreinit_fs.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

void arkchemy_bink_log(const char *fmt, ...);

/* BINKFRAMEBUFFERS, verified against the game's own configureVideo loop
 * (22992a4..2299300) rather than taken from documentation, which had the
 * header right and the plane order wrong:
 *   +0  TotalFrames   +4 YABufferWidth   +8  YABufferHeight
 *   +12 cRcBWidth     +16 cRcBHeight     +20 FrameNum
 *   +24 Frames[], 48 bytes each, four planes of {Allocate, Buffer, Pitch} */
#define FB_TOTAL 0u
#define FB_YAW   4u
#define FB_YAH   8u
#define FB_CW    12u
#define FB_CH    16u
#define FB_NUM   20u
#define FB_SETS  24u
#define FB_SET_SZ 48u

#define ARK_BINK_MAX 4

typedef struct {
    int              used;
    uint32_t         handle;        /* guest address the game holds as HBINK */
    AVFormatContext *fmt;
    AVCodecContext  *vctx;
    AVPacket        *pkt;
    AVFrame         *frm;
    int              v_idx;
    uint32_t         fbufs;         /* guest BINKFRAMEBUFFERS, once registered */
    uint32_t         frame_num;
    int              eof;
} ArkBink;

static ArkBink g_bink[ARK_BINK_MAX];

/* A small slab of guest memory for the BINK structs we hand back as handles.
 * Deliberately high in the arena, clear of the game's own heaps: MEM2's
 * ExpHeap bump had reached about 12MB in every run measured. */
#define ARK_BINK_SLAB 0x24000000u
static uint32_t g_slab_next = ARK_BINK_SLAB;

static ArkBink *slot_for(uint32_t handle) {
    int i;
    for (i = 0; i < ARK_BINK_MAX; i++)
        if (g_bink[i].used && g_bink[i].handle == handle) return &g_bink[i];
    return NULL;
}
static ArkBink *free_slot(void) {
    int i;
    for (i = 0; i < ARK_BINK_MAX; i++) if (!g_bink[i].used) return &g_bink[i];
    return NULL;
}

/* Pull a NUL-terminated guest string out of the arena. */
static void guest_str(PpcContext *ctx, uint32_t addr, char *out, size_t max) {
    size_t i;
    for (i = 0; i + 1 < max; i++) {
        uint8_t c = ppc_load_u8(ctx, addr + (uint32_t)i);
        if (!c) break;
        out[i] = (char)c;
    }
    out[i] = 0;
}

void ppc_BinkOpen(PpcContext *ctx) {
    char guest_path[256], real_path[512], url[600];
    ArkBink *b;
    const AVCodec *dec;
    int i, rc;

    guest_str(ctx, ctx->r[3], guest_path, sizeof(guest_path));
    /* the FS shim already knows how to turn /vol/content and bare relative
       paths into a real one -- reuse it rather than reinventing the mapping */
    ppc_fs_translate_path(guest_path, real_path, sizeof(real_path));
    snprintf(url, sizeof(url), "file:%s", real_path);

    b = free_slot();
    if (!b) { arkchemy_bink_log("[bink] no free slot for %s", guest_path); ctx->r[3] = 0; return; }
    memset(b, 0, sizeof(*b));
    b->v_idx = -1;

    rc = avformat_open_input(&b->fmt, url, NULL, NULL);
    if (rc < 0) {
        char err[128];
        av_strerror(rc, err, sizeof(err));
        arkchemy_bink_log("[bink] open failed: %s -> %s (%s)", guest_path, url, err);
        ctx->r[3] = 0;
        return;
    }
    avformat_find_stream_info(b->fmt, NULL);
    for (i = 0; i < (int)b->fmt->nb_streams; i++)
        if (b->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { b->v_idx = i; break; }
    if (b->v_idx < 0) { avformat_close_input(&b->fmt); ctx->r[3] = 0; return; }

    dec = avcodec_find_decoder(b->fmt->streams[b->v_idx]->codecpar->codec_id);
    b->vctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(b->vctx, b->fmt->streams[b->v_idx]->codecpar);
    if (!dec || avcodec_open2(b->vctx, dec, NULL) < 0) {
        arkchemy_bink_log("[bink] no usable decoder for %s", guest_path);
        avformat_close_input(&b->fmt);
        ctx->r[3] = 0;
        return;
    }
    b->pkt = av_packet_alloc();
    b->frm = av_frame_alloc();

    /* Hand back a guest BINK struct. Only the public head is filled -- it is
       what the engine reads -- and the rest stays zero. */
    b->handle = g_slab_next;
    g_slab_next += 0x600u;
    ppc_store_u32(ctx, b->handle + 0x0u, (uint32_t)b->vctx->width);
    ppc_store_u32(ctx, b->handle + 0x4u, (uint32_t)b->vctx->height);
    ppc_store_u32(ctx, b->handle + 0x8u, (uint32_t)b->fmt->streams[b->v_idx]->nb_frames);
    ppc_store_u32(ctx, b->handle + 0xcu, 0u);
    b->used = 1;

    arkchemy_bink_log("[bink] %s -> %s  %dx%d via %s", guest_path, real_path,
                      b->vctx->width, b->vctx->height, dec->name);
    ctx->r[3] = b->handle;
}

void ppc_BinkGetFrameBuffersInfo(PpcContext *ctx) {
    ArkBink *b = slot_for(ctx->r[3]);
    uint32_t info = ctx->r[4], f, p;
    uint32_t yaw, yah, cw, ch;
    if (!b || !info) return;

    yaw = (uint32_t)b->vctx->width;
    yah = (uint32_t)b->vctx->height;
    cw  = (yaw + 1u) / 2u;
    ch  = (yah + 1u) / 2u;

    for (f = 0; f < 256u; f += 4u) ppc_store_u32(ctx, info + f, 0);
    ppc_store_u32(ctx, info + FB_TOTAL, 2u);
    ppc_store_u32(ctx, info + FB_YAW, yaw);
    ppc_store_u32(ctx, info + FB_YAH, yah);
    ppc_store_u32(ctx, info + FB_CW, cw);
    ppc_store_u32(ctx, info + FB_CH, ch);
    ppc_store_u32(ctx, info + FB_NUM, 0u);

    /* Ask the caller to allocate Y, cR and cB; no alpha. Allocate lives at
       plane+0, which is the ordering the game's own loop confirms. */
    for (f = 0; f < 2u; f++) {
        uint32_t set = info + FB_SETS + f * FB_SET_SZ;
        for (p = 0; p < 3u; p++) ppc_store_u32(ctx, set + p * 12u + 0u, 1u);
        ppc_store_u32(ctx, set + 3u * 12u + 0u, 0u);
    }
}

void ppc_BinkRegisterFrameBuffers(PpcContext *ctx) {
    ArkBink *b = slot_for(ctx->r[3]);
    if (!b || !ctx->r[4]) return;
    b->fbufs = ctx->r[4];
}

/* Copy one decoded plane into the guest buffer the game gave us, honouring
   the pitch it chose -- it may be wider than the image. */
static void copy_plane(PpcContext *ctx, uint32_t dst, uint32_t pitch,
                       const uint8_t *src, int stride, uint32_t w, uint32_t h) {
    uint32_t y, x;
    if (!dst || !pitch || !src) return;
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)stride;
        uint32_t base = dst + y * pitch;
        for (x = 0; x < w; x++) ppc_store_u8(ctx, base + x, row[x]);
    }
}

void ppc_BinkDoFrame(PpcContext *ctx) {
    ArkBink *b = slot_for(ctx->r[3]);
    int got = 0;
    if (!b) { ctx->r[3] = 1; return; }

    while (!got) {
        int have = (av_read_frame(b->fmt, b->pkt) >= 0);
        if (!have) {
            if (b->eof) break;
            b->eof = 1;
            avcodec_send_packet(b->vctx, NULL);      /* flush: frames sit buffered */
        } else if (b->pkt->stream_index != b->v_idx) {
            av_packet_unref(b->pkt);
            continue;
        } else if (avcodec_send_packet(b->vctx, b->pkt) < 0) {
            av_packet_unref(b->pkt);
            continue;
        }
        if (avcodec_receive_frame(b->vctx, b->frm) >= 0) got = 1;
        if (have) av_packet_unref(b->pkt);
    }
    if (!got) { ctx->r[3] = 1; return; }             /* 1 = skipped, RAD's convention */

    if (b->fbufs) {
        uint32_t cur = ppc_load_u32(ctx, b->fbufs + FB_NUM) & 1u;
        uint32_t set = b->fbufs + FB_SETS + cur * FB_SET_SZ;
        uint32_t yaw = (uint32_t)b->vctx->width, yah = (uint32_t)b->vctx->height;
        uint32_t cw = (yaw + 1u) / 2u, ch = (yah + 1u) / 2u;
        /* Bink's cR is Cr (ffmpeg data[2]) and cB is Cb (data[1]). */
        copy_plane(ctx, ppc_load_u32(ctx, set + 0u * 12u + 4u),
                        ppc_load_u32(ctx, set + 0u * 12u + 8u), b->frm->data[0], b->frm->linesize[0], yaw, yah);
        copy_plane(ctx, ppc_load_u32(ctx, set + 1u * 12u + 4u),
                        ppc_load_u32(ctx, set + 1u * 12u + 8u), b->frm->data[2], b->frm->linesize[2], cw, ch);
        copy_plane(ctx, ppc_load_u32(ctx, set + 2u * 12u + 4u),
                        ppc_load_u32(ctx, set + 2u * 12u + 8u), b->frm->data[1], b->frm->linesize[1], cw, ch);
    }
    ctx->r[3] = 0;
}

void ppc_BinkNextFrame(PpcContext *ctx) {
    ArkBink *b = slot_for(ctx->r[3]);
    if (!b) return;
    b->frame_num++;
    ppc_store_u32(ctx, b->handle + 0xcu, b->frame_num);
    if (b->fbufs) ppc_store_u32(ctx, b->fbufs + FB_NUM, b->frame_num & 1u);
}

void ppc_BinkClose(PpcContext *ctx) {
    ArkBink *b = slot_for(ctx->r[3]);
    if (!b) return;
    if (b->frm) av_frame_free(&b->frm);
    if (b->pkt) av_packet_free(&b->pkt);
    if (b->vctx) avcodec_free_context(&b->vctx);
    if (b->fmt) avformat_close_input(&b->fmt);
    b->used = 0;
}

/* Decode is always ready here: there is no background I/O thread to wait on,
   because ffmpeg reads synchronously. */
void ppc_BinkWait(PpcContext *ctx) { ctx->r[3] = 0; }
void ppc_BinkShouldSkip(PpcContext *ctx) { ctx->r[3] = 0; }

/* Playback controls the engine calls but that do not affect decoding here.
   Accepting them silently is correct; failing them would stall the caller. */
void ppc_BinkPause(PpcContext *ctx) { (void)ctx; }
void ppc_BinkGoto(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetVideoOnOff(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetSoundOnOff(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetSpeakerVolumes(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetWillLoop(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetIOSize(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetFileOffset(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetError(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetSoundSystem(PpcContext *ctx) { ctx->r[3] = 1; }
void ppc_BinkSetWiiUFileClient(PpcContext *ctx) { (void)ctx; }
void ppc_BinkSetSimulate(PpcContext *ctx) { (void)ctx; }
void ppc_BinkGetTrackID(PpcContext *ctx) { ctx->r[3] = 0; }
void ppc_BinkSetSoundTrack(PpcContext *ctx) { ctx->r[3] = 0; }
void ppc_BinkSetVolume(PpcContext *ctx) { (void)ctx; }
