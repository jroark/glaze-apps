/*
 * vout_glaze: VLC's video output on the app's libui surface (RGB565, the
 * area below the status bar).
 *
 * Planar YUV (MPEG) asks for RV16 at the size the picture is shown at, fitted
 * to the width or height by its aspect ratio, so i420_rgb converts and scales
 * in one pass and Display only copies rows. RGB pictures (Motion-JPEG from
 * codec_mjpeg) are taken at their own size, as direct buffers the decoder
 * writes into, and scaled by libgfx as they are shown: VLC 0.8 has no RGB
 * scaler without ffmpeg. The bars around the picture are cleared once.
 *
 * Pictures are presented from the vout thread. Until the libui interface,
 * nothing else draws; the interface will take vout_glaze_lock() around its own
 * drawing.
 */
#include <stdlib.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/vout.h>

#include <glaze/thread.h>
#include <glaze/ui.h>

#include "vout_glaze.h"

/* Output pictures: one shown, one being rendered. */
#define OUT_PICTURES 2

static int  Open(vlc_object_t *);
static void Close(vlc_object_t *);
static int  Init(vout_thread_t *);
static void End(vout_thread_t *);
static int  Manage(vout_thread_t *);
static void Display(vout_thread_t *, picture_t *);

vlc_module_begin();
    set_shortname("Glaze");
    set_description(_("Glaze video output"));
    set_capability("video output", 100);
    set_category(CAT_VIDEO);
    set_subcategory(SUBCAT_VIDEO_VOUT);
    add_shortcut("glaze");
    set_callbacks(Open, Close);
vlc_module_end();

struct vout_sys_t {
    gfx_rect area;              /* where the picture is shown */
    vlc_bool_t cleared;         /* the bars have been painted */
    int shown;                  /* pictures displayed, for the log */
    mtime_t first, last;        /* when the first and latest were shown */
};

static struct gz_mutex draw_lock = GZ_MUTEX_INIT;

void vout_glaze_lock(void)
{
    gz_mutex_lock(&draw_lock);
}

void vout_glaze_unlock(void)
{
    gz_mutex_unlock(&draw_lock);
}

static int Open(vlc_object_t *p_this)
{
    vout_thread_t *p_vout = (vout_thread_t *)p_this;

    p_vout->p_sys = calloc(1, sizeof(struct vout_sys_t));
    if (!p_vout->p_sys)
        return VLC_ENOMEM;
    p_vout->pf_init = Init;
    p_vout->pf_end = End;
    p_vout->pf_manage = Manage;
    p_vout->pf_render = NULL;
    p_vout->pf_display = Display;
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *p_this)
{
    vout_thread_t *p_vout = (vout_thread_t *)p_this;

    free(p_vout->p_sys);
}

/* The largest rectangle of the picture's aspect ratio that fits the
 * surface, centred, with even sides (i420_rgb converts pixel pairs). */
static gfx_rect Fit(unsigned aspect)
{
    int w = UI_W, h = UI_H;

    if (aspect) {
        h = (int)((int64_t)UI_W * VOUT_ASPECT_FACTOR / aspect);
        if (h > UI_H) {
            h = UI_H;
            w = (int)((int64_t)UI_H * aspect / VOUT_ASPECT_FACTOR);
        }
    }
    w &= ~1;
    h &= ~1;
    if (w < 2)
        w = 2;
    if (h < 2)
        h = 2;
    return gfx_r((UI_W - w) / 2, (UI_H - h) / 2, w, h);
}

static vlc_bool_t IsYUV(vlc_fourcc_t c)
{
    return c == VLC_FOURCC('I', '4', '2', '0') || c == VLC_FOURCC('I', 'Y', 'U', 'V') ||
           c == VLC_FOURCC('Y', 'V', '1', '2');
}

static int Init(vout_thread_t *p_vout)
{
    struct vout_sys_t *p_sys = p_vout->p_sys;

    p_sys->area = Fit(p_vout->render.i_aspect);
    p_sys->cleared = VLC_FALSE;

    p_vout->output.i_chroma = VLC_FOURCC('R', 'V', '1', '6');
    p_vout->output.i_rmask = 0xf800;
    p_vout->output.i_gmask = 0x07e0;
    p_vout->output.i_bmask = 0x001f;
    if (IsYUV(p_vout->render.i_chroma)) {
        p_vout->output.i_width = (unsigned)p_sys->area.w;
        p_vout->output.i_height = (unsigned)p_sys->area.h;
    } else {
        p_vout->output.i_width = p_vout->render.i_width;
        p_vout->output.i_height = p_vout->render.i_height;
    }
    p_vout->output.i_aspect = p_vout->render.i_aspect;
    p_vout->fmt_out = p_vout->fmt_in;
    p_vout->fmt_out.i_chroma = p_vout->output.i_chroma;
    p_vout->fmt_out.i_width = p_vout->fmt_out.i_visible_width = p_vout->output.i_width;
    p_vout->fmt_out.i_height = p_vout->fmt_out.i_visible_height = p_vout->output.i_height;
    p_vout->fmt_out.i_x_offset = p_vout->fmt_out.i_y_offset = 0;
    p_vout->fmt_out.i_aspect = p_vout->output.i_aspect;
    p_vout->fmt_out.i_sar_num = p_vout->fmt_out.i_sar_den = 0;

    I_OUTPUTPICTURES = 0;
    while (I_OUTPUTPICTURES < OUT_PICTURES) {
        picture_t *p_pic = NULL;
        for (int i = 0; i < VOUT_MAX_PICTURES; i++) {
            if (p_vout->p_picture[i].i_status == FREE_PICTURE) {
                p_pic = &p_vout->p_picture[i];
                break;
            }
        }
        if (!p_pic)
            break;
        vout_AllocatePicture(VLC_OBJECT(p_vout), p_pic, p_vout->output.i_chroma,
                             p_vout->output.i_width, p_vout->output.i_height,
                             p_vout->output.i_aspect);
        if (p_pic->i_planes == 0)
            break;
        p_pic->i_status = DESTROYED_PICTURE;
        p_pic->i_type = DIRECT_PICTURE;
        PP_OUTPUTPICTURE[I_OUTPUTPICTURES] = p_pic;
        I_OUTPUTPICTURES++;
    }
    if (I_OUTPUTPICTURES == 0)
        return VLC_ENOMEM;

    msg_Dbg(p_vout, "%ux%u %4.4s shown at %d,%d %dx%d", p_vout->render.i_width,
            p_vout->render.i_height, (char *)&p_vout->render.i_chroma, p_sys->area.x,
            p_sys->area.y, p_sys->area.w, p_sys->area.h);
    return VLC_SUCCESS;
}

static void End(vout_thread_t *p_vout)
{
    struct vout_sys_t *p_sys = p_vout->p_sys;
    if (p_sys->shown > 1) {
        int ms = (int)((p_sys->last - p_sys->first) / 1000);
        int fps10 = ms ? (p_sys->shown - 1) * 10000 / ms : 0;
        msg_Info(p_vout, "showed %d pictures in %d ms (%d.%d fps)", p_sys->shown, ms,
                 fps10 / 10, fps10 % 10);
    }
    p_sys->shown = 0;
    for (int i = I_OUTPUTPICTURES; i > 0; ) {
        i--;
        free(PP_OUTPUTPICTURE[i]->p_data_orig);
    }
    I_OUTPUTPICTURES = 0;
}

static int Manage(vout_thread_t *p_vout)
{
    (void)p_vout;
    return VLC_SUCCESS;
}

static void Display(vout_thread_t *p_vout, picture_t *p_pic)
{
    struct vout_sys_t *p_sys = p_vout->p_sys;
    gfx_rect a = p_sys->area;
    plane_t *pl = &p_pic->p[0];

    vout_glaze_lock();
    if (!p_sys->cleared) {
        gfx_fill(&ui.surf, gfx_r(0, 0, UI_W, UI_H), 0x000000);
        ui_invalidate_all();
        p_sys->cleared = VLC_TRUE;
    }
    if (pl->i_visible_pitch / 2 == a.w && pl->i_visible_lines == a.h) {
        uint16_t *dst = ui.surf.px + a.y * ui.surf.stride + a.x;
        const uint8_t *src = pl->p_pixels;
        for (int y = 0; y < a.h; y++, dst += ui.surf.stride, src += pl->i_pitch)
            memcpy(dst, src, (size_t)a.w * 2);
    } else {
        gfx_surface s;
        gfx_surface_init(&s, (uint16_t *)pl->p_pixels, pl->i_visible_pitch / 2,
                         pl->i_visible_lines, pl->i_pitch / 2);
        gfx_blit_scaled(&ui.surf, a, &s, gfx_r(0, 0, s.w, s.h), 255);
    }
    ui_invalidate(a);
    ui_present();
    vout_glaze_unlock();
    p_sys->last = mdate();
    if (!p_sys->shown++)
        p_sys->first = p_sys->last;
}
