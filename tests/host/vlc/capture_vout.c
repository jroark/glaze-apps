/*
 * capture_vout: the native test's video output. It asks for what the Glaze
 * output asks for, RV16 (565), at the picture's own size, so MPEG goes
 * through i420_rgb and Motion-JPEG arrives as codec_mjpeg's direct RGB, and
 * counts the pictures shown and keeps the first one's middle pixel.
 */
#include <stdlib.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/vout.h>

#include "capture.h"

#define OUT_PICTURES 2

static int  Open(vlc_object_t *);
static void Close(vlc_object_t *);
static int  Init(vout_thread_t *);
static void End(vout_thread_t *);
static void Display(vout_thread_t *, picture_t *);

vlc_module_begin();
    set_description(_("Capture video output (native test)"));
    set_capability("video output", 0);
    add_shortcut("capture");
    set_callbacks(Open, Close);
vlc_module_end();

static int Open(vlc_object_t *p_this)
{
    vout_thread_t *p_vout = (vout_thread_t *)p_this;

    p_vout->pf_init = Init;
    p_vout->pf_end = End;
    p_vout->pf_manage = NULL;
    p_vout->pf_render = NULL;
    p_vout->pf_display = Display;
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *p_this)
{
    (void)p_this;
}

static int Init(vout_thread_t *p_vout)
{
    p_vout->output.i_chroma = VLC_FOURCC('R', 'V', '1', '6');
    p_vout->output.i_rmask = 0xf800;
    p_vout->output.i_gmask = 0x07e0;
    p_vout->output.i_bmask = 0x001f;
    p_vout->output.i_width = p_vout->render.i_width;
    p_vout->output.i_height = p_vout->render.i_height;
    p_vout->output.i_aspect = p_vout->render.i_aspect;
    p_vout->fmt_out = p_vout->fmt_in;
    p_vout->fmt_out.i_chroma = p_vout->output.i_chroma;

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
    vlc_capture.width = p_vout->output.i_width;
    vlc_capture.height = p_vout->output.i_height;
    return I_OUTPUTPICTURES ? VLC_SUCCESS : VLC_ENOMEM;
}

static void End(vout_thread_t *p_vout)
{
    for (int i = I_OUTPUTPICTURES; i > 0; ) {
        i--;
        free(PP_OUTPUTPICTURE[i]->p_data_orig);
    }
    I_OUTPUTPICTURES = 0;
}

static void Display(vout_thread_t *p_vout, picture_t *p_pic)
{
    plane_t *pl = &p_pic->p[0];

    (void)p_vout;
    if (!vlc_capture.pictures++) {
        const uint8_t *row = pl->p_pixels + (pl->i_visible_lines / 2) * pl->i_pitch;
        memcpy(&vlc_capture.first_center, row + (pl->i_visible_pitch / 4) * 2, 2);
    }
}
