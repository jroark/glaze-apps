/*
 * codec_mjpeg: Motion-JPEG for VLC on Glaze. VLC 0.8 decoded it only through
 * ffmpeg; this decodes each frame with stb_image (as the Videos app does)
 * into an RV16 picture that vout_glaze scales as it shows it. Frames in the
 * "AVI1" form leave out the Huffman tables; libmedia puts the standard ones
 * back (media_jpeg_add_dht).
 *
 * Every frame is a key frame, so a frame that could not be decoded before
 * it is due is skipped rather than decoded: decoding is most of the cost,
 * and the video output drops a late picture anyway. "Due" allows for the
 * average decode time so far and the output's own margin; on a file too
 * heavy to keep up, the frames that are decoded are then the ones in time.
 * At most MAX_SKIP in a row are skipped, so something is always shown.
 */
#include <stdlib.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/decoder.h>

#include <glaze/media.h>
#include <glaze/thread.h>

/* stb_image allocates the same few large blocks for every frame; libc
 * serves those with a fresh mapping each time, so they are kept here
 * between frames instead (see Pool*). */
static void *PoolMalloc(size_t n);
static void *PoolRealloc(void *p, size_t n);
static void PoolFree(void *p);
#define STBI_MALLOC(n)          PoolMalloc(n)
#define STBI_REALLOC(p, n)      PoolRealloc(p, n)
#define STBI_FREE(p)            PoolFree(p)

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT(x)          ((void)0)
#define STBI_MAX_DIMENSIONS     1024
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wshadow"
#include "../stb/stb_image.h"
#pragma GCC diagnostic pop
/* With STBI_NO_THREAD_LOCALS, stb declares three _thread functions it never
 * defines; that warning comes at the end of the file. */
#pragma GCC diagnostic ignored "-Wunused-function"

#define MAX_SKIP 15
/* Allowance for the video output (its render time) past the decode. */
#define SHOW_MARGIN 20000
/* First guess at the decode time, before one is measured. */
#define DECODE_GUESS 40000

/* Blocks stb_image freed, kept for the next frame's request of the same
 * size. Each block starts with an 8-byte header holding its size. Only one
 * decoder runs at a time, but the lock keeps two from mixing the pool. */
#define POOL_SLOTS 8
#define POOL_HDR 8
static void *pool[POOL_SLOTS];
static struct gz_mutex pool_lock = GZ_MUTEX_INIT;

static void *PoolMalloc(size_t n)
{
    void *b = NULL;

    gz_mutex_lock(&pool_lock);
    for (int i = 0; i < POOL_SLOTS; i++) {
        if (pool[i] && *(size_t *)pool[i] == n) {
            b = pool[i];
            pool[i] = NULL;
            break;
        }
    }
    gz_mutex_unlock(&pool_lock);
    if (!b) {
        b = malloc(n + POOL_HDR);
        if (!b)
            return NULL;
        *(size_t *)b = n;
    }
    return (uint8_t *)b + POOL_HDR;
}

static void PoolFree(void *p)
{
    if (!p)
        return;
    void *b = (uint8_t *)p - POOL_HDR;
    gz_mutex_lock(&pool_lock);
    for (int i = 0; i < POOL_SLOTS; i++) {
        if (!pool[i]) {
            pool[i] = b;
            b = NULL;
            break;
        }
    }
    gz_mutex_unlock(&pool_lock);
    free(b);
}

static void *PoolRealloc(void *p, size_t n)
{
    if (!p)
        return PoolMalloc(n);
    size_t old = *(size_t *)((uint8_t *)p - POOL_HDR);
    if (n <= old)
        return p;
    void *q = PoolMalloc(n);
    if (q) {
        memcpy(q, p, old);
        PoolFree(p);
    }
    return q;
}

static void PoolDrain(void)
{
    gz_mutex_lock(&pool_lock);
    for (int i = 0; i < POOL_SLOTS; i++) {
        free(pool[i]);
        pool[i] = NULL;
    }
    gz_mutex_unlock(&pool_lock);
}

static int  Open(vlc_object_t *);
static void Close(vlc_object_t *);
static picture_t *Decode(decoder_t *, block_t **);

vlc_module_begin();
    set_shortname("MJPEG");
    set_description(_("Motion-JPEG decoder (stb_image)"));
    set_capability("decoder", 100);
    set_category(CAT_INPUT);
    set_subcategory(SUBCAT_INPUT_VCODEC);
    set_callbacks(Open, Close);
vlc_module_end();

struct decoder_sys_t {
    uint8_t *fixed;             /* a frame with the tables put back */
    size_t fixed_size;
    int skipped;                /* late frames skipped in a row */
    int decoded, dropped;       /* for the log at close */
    mtime_t decode_time;
    mtime_t decode_avg;         /* running average, microseconds */
};

static int Open(vlc_object_t *p_this)
{
    decoder_t *p_dec = (decoder_t *)p_this;
    static const vlc_fourcc_t codecs[] = {
        VLC_FOURCC('M', 'J', 'P', 'G'), VLC_FOURCC('m', 'j', 'p', 'g'),
        VLC_FOURCC('J', 'P', 'E', 'G'), VLC_FOURCC('j', 'p', 'e', 'g'),
        VLC_FOURCC('m', 'j', 'p', 'a'), VLC_FOURCC('A', 'V', 'R', 'n'),
        VLC_FOURCC('A', 'V', 'D', 'J'), VLC_FOURCC('L', 'J', 'P', 'G'),
    };
    unsigned i;

    for (i = 0; i < sizeof(codecs) / sizeof(codecs[0]); i++)
        if (p_dec->fmt_in.i_codec == codecs[i])
            break;
    if (i == sizeof(codecs) / sizeof(codecs[0]))
        return VLC_EGENERIC;

    p_dec->p_sys = calloc(1, sizeof(struct decoder_sys_t));
    if (!p_dec->p_sys)
        return VLC_ENOMEM;

    es_format_Init(&p_dec->fmt_out, VIDEO_ES, VLC_FOURCC('R', 'V', '1', '6'));
    p_dec->fmt_out.video = p_dec->fmt_in.video;
    p_dec->fmt_out.video.i_chroma = VLC_FOURCC('R', 'V', '1', '6');
    p_dec->fmt_out.video.i_rmask = 0xf800;
    p_dec->fmt_out.video.i_gmask = 0x07e0;
    p_dec->fmt_out.video.i_bmask = 0x001f;
    p_dec->p_sys->decode_avg = DECODE_GUESS;
    p_dec->pf_decode_video = Decode;
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *p_this)
{
    decoder_t *p_dec = (decoder_t *)p_this;
    struct decoder_sys_t *p_sys = p_dec->p_sys;

    if (p_sys->decoded)
        msg_Info(p_dec, "decoded %d frames, %d ms each; skipped %d late", p_sys->decoded,
                 (int)(p_sys->decode_time / p_sys->decoded / 1000), p_sys->dropped);
    free(p_dec->p_sys->fixed);
    free(p_dec->p_sys);
    PoolDrain();
}

/* Sets the output size from a decoded frame (the first, or a change). */
static void SetSize(decoder_t *p_dec, int w, int h)
{
    video_format_t *v = &p_dec->fmt_out.video;

    if ((int)v->i_width == w && (int)v->i_height == h && v->i_aspect)
        return;
    v->i_width = v->i_visible_width = (unsigned)w;
    v->i_height = v->i_visible_height = (unsigned)h;
    v->i_x_offset = v->i_y_offset = 0;
    v->i_aspect = (unsigned)((int64_t)VOUT_ASPECT_FACTOR * w / h);
}

static picture_t *Decode(decoder_t *p_dec, block_t **pp_block)
{
    struct decoder_sys_t *p_sys = p_dec->p_sys;
    block_t *p_block = pp_block ? *pp_block : NULL;

    if (!p_block)
        return NULL;
    *pp_block = NULL;

    mtime_t date = p_block->i_pts ? p_block->i_pts : p_block->i_dts;
    if ((p_block->i_flags & (BLOCK_FLAG_DISCONTINUITY | BLOCK_FLAG_CORRUPTED)) == BLOCK_FLAG_CORRUPTED ||
        (p_block->i_flags & BLOCK_FLAG_PREROLL) ||
        (date && date < mdate() + p_sys->decode_avg + SHOW_MARGIN && p_sys->skipped < MAX_SKIP)) {
        p_sys->skipped++;
        p_sys->dropped++;
        block_Release(p_block);
        return NULL;
    }
    p_sys->skipped = 0;

    const uint8_t *jpeg = p_block->p_buffer;
    int len = (int)p_block->i_buffer;
    size_t need = (size_t)len + MEDIA_JPEG_DHT_SIZE;
    if (need > p_sys->fixed_size) {
        uint8_t *b = realloc(p_sys->fixed, need);
        if (b) {
            p_sys->fixed = b;
            p_sys->fixed_size = need;
        }
    }
    if (p_sys->fixed_size >= need) {
        int fixed = media_jpeg_add_dht(jpeg, (uint32_t)len, p_sys->fixed);
        if (fixed > 0) {
            jpeg = p_sys->fixed;
            len = fixed;
        }
    }

    int w, h, comp;
    mtime_t start = mdate();
    uint8_t *rgb = stbi_load_from_memory(jpeg, len, &w, &h, &comp, 3);
    if (!rgb) {
        msg_Warn(p_dec, "cannot decode a frame");
        block_Release(p_block);
        return NULL;
    }
    SetSize(p_dec, w, h);

    picture_t *p_pic = p_dec->pf_vout_buffer_new(p_dec);
    if (p_pic) {
        plane_t *pl = &p_pic->p[0];
        const uint8_t *s = rgb;
        int pw = pl->i_visible_pitch / 2 < w ? pl->i_visible_pitch / 2 : w;
        int ph = pl->i_visible_lines < h ? pl->i_visible_lines : h;
        for (int y = 0; y < ph; y++) {
            uint16_t *d = (uint16_t *)(pl->p_pixels + y * pl->i_pitch);
            s = rgb + (size_t)y * w * 3;
            for (int x = 0; x < pw; x++, s += 3)
                d[x] = (uint16_t)((s[0] & 0xF8) << 8 | (s[1] & 0xFC) << 3 | s[2] >> 3);
        }
        p_pic->date = date;
        p_pic->b_progressive = VLC_TRUE;
    }
    stbi_image_free(rgb);
    block_Release(p_block);
    mtime_t took = mdate() - start;
    p_sys->decoded++;
    p_sys->decode_time += took;
    p_sys->decode_avg += (took - p_sys->decode_avg) / 4;
    return p_pic;
}
