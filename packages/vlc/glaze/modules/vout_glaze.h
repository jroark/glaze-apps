/*
 * Shared between vout_glaze and the code that draws on the same surface:
 * the video output presents from its own thread, so other drawing takes
 * this lock.
 */
#ifndef VLC_GLAZE_VOUT_GLAZE_H
#define VLC_GLAZE_VOUT_GLAZE_H

void vout_glaze_lock(void);
void vout_glaze_unlock(void);

#endif
