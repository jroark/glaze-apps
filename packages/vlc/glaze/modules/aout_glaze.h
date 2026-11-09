/*
 * Between aout_glaze and the interface. The audio stream is shared with the
 * Music player, so the interface holds the output (the stream is closed)
 * while nothing should be heard: paused, or the app sent to the background.
 * The audio thread opens and closes the stream itself when it sees the
 * request. aout_glaze_taken says the Music player took the output while
 * VLC was playing; releasing the hold tries to get it back.
 */
#ifndef VLC_GLAZE_AOUT_GLAZE_H
#define VLC_GLAZE_AOUT_GLAZE_H

void aout_glaze_hold(int hold);
int aout_glaze_taken(void);

#endif
