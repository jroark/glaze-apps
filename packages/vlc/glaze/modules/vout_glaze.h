/*
 * Shared between vout_glaze and the interface, which draw on the same
 * surface: the video output presents from its own thread, so the interface
 * takes the lock around its drawing.
 *
 * Pictures reach the screen only while the interface shows the video
 * (vout_glaze_show). The overlay function, if set, is called with the lock
 * held after each picture is drawn, to put the controls over it.
 * vout_glaze_repaint (lock held) clears the screen and draws the latest
 * picture again, for when the controls change while nothing new arrives.
 */
#ifndef VLC_GLAZE_VOUT_GLAZE_H
#define VLC_GLAZE_VOUT_GLAZE_H

#include <glaze/gfx.h>

void vout_glaze_lock(void);
void vout_glaze_unlock(void);
void vout_glaze_show(int on);
void vout_glaze_set_overlay(void (*draw)(gfx_surface *s));
/* A video output exists (a picture may have been shown). */
int vout_glaze_active(void);
void vout_glaze_repaint(void);

#endif
