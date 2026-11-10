/*
 * What the native test's capture outputs saw: the audio (S16, as the Glaze
 * output takes it) and the video (RV16 pictures, as the Glaze output takes
 * them), for tests/native/vlc.c to check.
 */
#ifndef VLC_HOST_CAPTURE_H
#define VLC_HOST_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

struct vlc_capture {
    /* audio */
    unsigned rate, channels;
    uint64_t frames;
    unsigned crossings[2];          /* sign changes, per channel */
    int16_t last[2];
    int16_t peak;
    uint8_t *pcm;                   /* everything played, interleaved */
    size_t pcm_len, pcm_cap;
    /* video */
    int pictures;
    unsigned width, height;
    uint16_t first_center;          /* the first picture's middle pixel */
};

extern struct vlc_capture vlc_capture;

void vlc_capture_reset(void);

#endif
