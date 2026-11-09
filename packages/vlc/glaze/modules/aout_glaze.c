/*
 * aout_glaze: VLC's audio output on the Glaze audio server's stream mode
 * (glaze/audio.h). The stream takes S16LE at any rate, mono or stereo, and
 * converts to the hardware rate itself, so the output opens at the input's
 * rate and VLC's own resamplers stay out of the way.
 *
 * A thread pulls buffers the way upstream's OSS output does: each is asked
 * for at mdate() + the output's latency, so VLC's A/V sync follows what is
 * actually heard. The latency is what this module has written less what the
 * stream clock says was heard; the server's own queued figure leaves out
 * what it has accepted but not converted yet, which made the estimate jump
 * by a buffer's length and set VLC's resampler hunting. The clock moves in
 * whole DMA pages (23 ms at 44.1 kHz stereo, 93 ms at 22.05 kHz mono),
 * coarser than VLC's 40 ms tolerance, so the heard time is estimated as
 * running on mdate() from the start of output and pulled 1/8 of the way
 * toward the clock at each of its steps: that smooths the steps and the
 * emulator's bursty DMA, and follows the slow drift between the audio and
 * system clocks. An error past 200 ms (a stall) resets the estimate. When there is
 * nothing due yet it waits a little while the server holds enough, and
 * plays silence only when it would otherwise run dry.
 *
 * Volume is applied here, in integer steps: VLC's software volume is a
 * multiplier for the float mixer, which this no-FPU build does not have. If the Music
 * player takes the output over (GZ_EBUSY), buffers are dropped in real
 * time so the input neither stalls nor races; the interface pauses then.
 * The interface also holds the output (aout_glaze.h) while paused or in the
 * background: the thread closes the stream, so the Music player can have
 * it, and opens it again when the hold is released.
 */
#include <stdlib.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/aout.h>

#include "aout_internal.h"
#include "aout_glaze.h"

#include <glaze/audio.h>
#include <glaze/sys.h>

/* 2048 frames is 8 KiB of stereo S16: one AUDIO_BUF_SIZE write. */
#define FRAME_SIZE 2048
/* Wait for VLC rather than play silence while the server holds this much. */
#define LOW_WATER_US 100000
/* Retry interval when the server's buffer is full. */
#define FULL_WAIT_MS 10

static int  Open(vlc_object_t *);
static void Close(vlc_object_t *);
static void Play(aout_instance_t *);
static void *Thread(void *);

vlc_module_begin();
    set_shortname("Glaze");
    set_description(_("Glaze audio output"));
    set_capability("audio output", 100);
    set_category(CAT_AUDIO);
    set_subcategory(SUBCAT_AUDIO_AOUT);
    add_shortcut("glaze");
    set_callbacks(Open, Close);
vlc_module_end();

struct aout_sys_t {
    unsigned bytes_per_frame;
    unsigned rate;
    uint64_t written;           /* frames written to the stream */
    vlc_bool_t started;         /* output kicked off (a 0-byte write) */
    uint32_t played_ms;         /* last stream clock reading */
    mtime_t heard_base;         /* heard time = mdate() - heard_base */
    int16_t *silence;           /* FRAME_SIZE frames of zeros */
    int channels;
    vlc_bool_t busy;            /* the player took the output */
    vlc_bool_t closed;          /* the stream is closed (held) */
};

/* Requests from the interface; one output exists at a time. */
static volatile int hold_req;
static volatile int taken;

void aout_glaze_hold(int hold)
{
    hold_req = hold;
}

int aout_glaze_taken(void)
{
    return taken;
}

static int Open(vlc_object_t *p_this)
{
    aout_instance_t *p_aout = (aout_instance_t *)p_this;
    audio_sample_format_t *fmt = &p_aout->output.output;
    struct aout_sys_t *p_sys;

    if (fmt->i_format == VLC_FOURCC('s', 'p', 'd', 'i'))
        return VLC_EGENERIC;

    int channels = aout_FormatNbChannels(fmt) >= 2 ? 2 : 1;
    if (fmt->i_rate < 8000 || fmt->i_rate > 48000)
        fmt->i_rate = 44100;

    int r = audio_stream_open(fmt->i_rate, channels);
    if (r < 0) {
        msg_Err(p_aout, "cannot open the audio stream (%d)", r);
        return VLC_EGENERIC;
    }

    p_sys = malloc(sizeof(*p_sys));
    if (!p_sys) {
        audio_stream_close();
        return VLC_ENOMEM;
    }
    memset(p_sys, 0, sizeof(*p_sys));

    fmt->i_format = AOUT_FMT_S16_NE;
    fmt->i_physical_channels = channels == 2 ? AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT : AOUT_CHAN_CENTER;
    aout_FormatPrepare(fmt);
    p_sys->bytes_per_frame = fmt->i_bytes_per_frame;
    p_sys->rate = fmt->i_rate;
    p_sys->channels = channels;
    taken = 0;
    p_sys->silence = calloc(FRAME_SIZE, p_sys->bytes_per_frame);
    if (!p_sys->silence) {
        free(p_sys);
        audio_stream_close();
        return VLC_ENOMEM;
    }

    p_aout->output.p_sys = p_sys;
    p_aout->output.i_nb_samples = FRAME_SIZE;
    p_aout->output.pf_play = Play;
    /* Keeps i_volume (from the saved "volume" setting); applied below. */
    aout_VolumeSoftInit(p_aout);

    if (vlc_thread_create(p_aout, "aout", Thread, VLC_THREAD_PRIORITY_OUTPUT, VLC_FALSE)) {
        msg_Err(p_aout, "cannot create the audio thread");
        free(p_sys->silence);
        free(p_sys);
        audio_stream_close();
        return VLC_ETHREAD;
    }
    msg_Dbg(p_aout, "stream open: %u Hz, %d channel(s)", fmt->i_rate, channels);
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *p_this)
{
    aout_instance_t *p_aout = (aout_instance_t *)p_this;
    struct aout_sys_t *p_sys = p_aout->output.p_sys;

    p_aout->b_die = VLC_TRUE;
    vlc_thread_join(p_aout);
    p_aout->b_die = VLC_FALSE;
    if (!p_sys->closed)
        audio_stream_close();
    free(p_sys->silence);
    free(p_sys);
}

/* The thread pulls buffers itself. */
static void Play(aout_instance_t *p_aout)
{
    (void)p_aout;
}

/* Scales S16 samples in place by vol/AOUT_VOLUME_DEFAULT, with clipping. */
static void ApplyVolume(int16_t *s, size_t n, unsigned vol)
{
    if (vol == AOUT_VOLUME_DEFAULT)
        return;
    for (size_t i = 0; i < n; i++) {
        int32_t v = ((int32_t)s[i] * (int32_t)vol) / AOUT_VOLUME_DEFAULT;
        s[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
    }
}

/* Written but not yet heard, in microseconds. */
#define PLL_SHIFT   3
#define PLL_RESET   200000

/* Reads the stream clock and, on a step, pulls the heard estimate toward
 * it. Called every few milliseconds (also while a write waits), so a step
 * is seen soon after it happens: late sightings would bias the estimate. */
static void Sample(struct aout_sys_t *p_sys)
{
    uint32_t played, queued;

    if (!p_sys->started || audio_stream_clock(&played, &queued) != 0 || played == p_sys->played_ms)
        return;
    p_sys->played_ms = played;
    mtime_t error = (mtime_t)played * 1000 - (mdate() - p_sys->heard_base);
    if (error > PLL_RESET || error < -PLL_RESET)
        p_sys->heard_base -= error;
    else
        p_sys->heard_base -= error >> PLL_SHIFT;
}

static mtime_t Latency(struct aout_sys_t *p_sys)
{
    mtime_t written = (mtime_t)(p_sys->written * 1000000 / p_sys->rate);

    if (!p_sys->started)
        return written;     /* nothing is heard before output starts */
    Sample(p_sys);
    mtime_t latency = written - (mdate() - p_sys->heard_base);
    return latency > 0 ? latency : 0;
}

/* Writes all of pcm, waiting while the server is full. Returns 0, or
 * GZ_EBUSY once the player has the output, or -1 when told to stop. */
static int WriteAll(aout_instance_t *p_aout, const uint8_t *pcm, size_t len)
{
    while (len > 0) {
        if (p_aout->b_die)
            return -1;
        uint32_t chunk = len > AUDIO_BUF_SIZE ? AUDIO_BUF_SIZE : (uint32_t)len;
        int r = audio_stream_write(pcm, chunk);
        if (r == GZ_EBUSY)
            return GZ_EBUSY;
        if (r <= 0) {
            msleep(FULL_WAIT_MS * 1000);
            Sample(p_aout->output.p_sys);
            continue;
        }
        pcm += r;
        len -= (size_t)r;
    }
    return 0;
}

static void *Thread(void *data)
{
    aout_instance_t *p_aout = data;
    struct aout_sys_t *p_sys = p_aout->output.p_sys;

    while (!p_aout->b_die) {
        if (hold_req && !p_sys->closed) {
            audio_stream_close();
            p_sys->closed = VLC_TRUE;
            msg_Dbg(p_aout, "stream closed (held)");
        } else if (!hold_req && p_sys->closed) {
            int r = audio_stream_open(p_sys->rate, p_sys->channels);
            if (r == 0) {
                p_sys->closed = p_sys->busy = p_sys->started = VLC_FALSE;
                p_sys->written = 0;
                p_sys->played_ms = 0;
                taken = 0;
                msg_Dbg(p_aout, "stream reopened");
            } else {
                msleep(100000);
            }
        }

        vlc_bool_t mute = p_sys->busy || p_sys->closed;
        mtime_t latency = mute ? 0 : Latency(p_sys);
        aout_buffer_t *p_buffer = aout_OutputNextBuffer(p_aout, mdate() + latency, VLC_FALSE);

        if (mute) {
            /* No stream: let time pass for what is dropped. */
            if (p_buffer) {
                mtime_t wait = p_buffer->end_date - p_buffer->start_date;
                aout_BufferFree(p_buffer);
                msleep(wait > 0 ? wait : 10000);
            } else {
                msleep(20000);
            }
            continue;
        }

        if (!p_buffer && latency > LOW_WATER_US) {
            msleep(FULL_WAIT_MS * 1000);
            continue;
        }

        const uint8_t *pcm;
        size_t len;
        if (p_buffer) {
            ApplyVolume((int16_t *)p_buffer->p_buffer, p_buffer->i_nb_bytes / 2,
                        p_aout->output.i_volume);
            pcm = p_buffer->p_buffer;
            len = p_buffer->i_nb_bytes;
        } else {
            /* Nothing due yet: silence up to the next buffer's date, so it
             * then starts on time, or a short one if the fifo is empty. */
            mtime_t gap = 0;
            vlc_mutex_lock(&p_aout->output_fifo_lock);
            if (p_aout->output.fifo.p_first)
                gap = p_aout->output.fifo.p_first->start_date - (mdate() + latency);
            vlc_mutex_unlock(&p_aout->output_fifo_lock);
            size_t frames = gap > 0 ? (size_t)(gap * p_sys->rate / 1000000) : FRAME_SIZE / 4;
            if (frames < 1)
                frames = 1;
            if (frames > FRAME_SIZE)
                frames = FRAME_SIZE;
            pcm = (const uint8_t *)p_sys->silence;
            len = frames * p_sys->bytes_per_frame;
        }

        int r = WriteAll(p_aout, pcm, len);
        if (r == 0) {
            p_sys->written += len / p_sys->bytes_per_frame;
            if (!p_sys->started && (p_buffer || latency + (mtime_t)len * 1000000 /
                                    (p_sys->bytes_per_frame * p_sys->rate) >= LOW_WATER_US)) {
                /* Start with the first real buffer (or a little silence)
                 * in rather than once the server thinks enough is
                 * buffered: its clock stands still until then, which would
                 * put every date written meanwhile early. */
                audio_stream_write(pcm, 0);
                p_sys->started = VLC_TRUE;
                p_sys->heard_base = mdate();
            }
        }
        if (p_buffer) {
            aout_BufferFree(p_buffer);   /* a multi-statement macro */
        }
        if (r == GZ_EBUSY) {
            msg_Warn(p_aout, "the music player took the audio output");
            p_sys->busy = VLC_TRUE;
            taken = 1;
        }
    }
    return NULL;
}
