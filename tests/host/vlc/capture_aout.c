/*
 * capture_aout: the native test's audio output. Like upstream's file output
 * it takes S16 buffers as the mixer hands them over (no clock of its own),
 * and keeps them for the test: the PCM, the frame count, and each channel's
 * zero crossings, which give a tone's pitch.
 */
#include <stdlib.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/aout.h>

#include "aout_internal.h"
#include "capture.h"

static int  Open(vlc_object_t *);
static void Play(aout_instance_t *);

vlc_module_begin();
    set_description(_("Capture audio output (native test)"));
    set_capability("audio output", 0);
    add_shortcut("capture");
    set_callbacks(Open, NULL);
vlc_module_end();

struct vlc_capture vlc_capture;

void vlc_capture_reset(void)
{
    free(vlc_capture.pcm);
    memset(&vlc_capture, 0, sizeof(vlc_capture));
}

static int Open(vlc_object_t *p_this)
{
    aout_instance_t *p_aout = (aout_instance_t *)p_this;
    audio_sample_format_t *fmt = &p_aout->output.output;

    if (fmt->i_format == VLC_FOURCC('s', 'p', 'd', 'i'))
        return VLC_EGENERIC;
    int channels = aout_FormatNbChannels(fmt) >= 2 ? 2 : 1;
    fmt->i_format = AOUT_FMT_S16_NE;
    fmt->i_physical_channels = channels == 2 ? AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT : AOUT_CHAN_CENTER;
    aout_FormatPrepare(fmt);
    /* aout_glaze's buffer size: about 100 ms, at most 2048 frames. */
    p_aout->output.i_nb_samples = fmt->i_rate / 10 >= 2048 ? 2048 : (fmt->i_rate / 10 + 63) & ~63u;
    p_aout->output.pf_play = Play;
    aout_VolumeSoftInit(p_aout);
    vlc_capture.rate = fmt->i_rate;
    vlc_capture.channels = (unsigned)channels;
    return VLC_SUCCESS;
}

static void Play(aout_instance_t *p_aout)
{
    aout_buffer_t *p_buffer;
    struct vlc_capture *c = &vlc_capture;

    while ((p_buffer = aout_FifoPop(p_aout, &p_aout->output.fifo)) != NULL) {
        const int16_t *s = (const int16_t *)p_buffer->p_buffer;
        size_t n = p_buffer->i_nb_bytes / 2;
        unsigned ch = c->channels ? c->channels : 1;

        for (size_t i = 0; i < n; i++) {
            unsigned k = (unsigned)(i % ch);
            if ((s[i] < 0) != (c->last[k] < 0))
                c->crossings[k]++;
            c->last[k] = s[i];
            int16_t a = s[i] < 0 ? (int16_t)(s[i] == -32768 ? 32767 : -s[i]) : s[i];
            if (a > c->peak)
                c->peak = a;
        }
        c->frames += n / ch;
        if (c->pcm_len + p_buffer->i_nb_bytes > c->pcm_cap) {
            size_t cap = (c->pcm_len + p_buffer->i_nb_bytes) * 2;
            uint8_t *pcm = realloc(c->pcm, cap);
            if (pcm) {
                c->pcm = pcm;
                c->pcm_cap = cap;
            }
        }
        if (c->pcm_len + p_buffer->i_nb_bytes <= c->pcm_cap) {
            memcpy(c->pcm + c->pcm_len, p_buffer->p_buffer, p_buffer->i_nb_bytes);
            c->pcm_len += p_buffer->i_nb_bytes;
        }
        aout_BufferFree(p_buffer);
    }
}
