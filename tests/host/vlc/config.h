/*
 * VLC's configuration for the native test (Linux, glibc): the device's
 * (packages/vlc/glaze/config.h), so the same features and fallbacks are
 * built, with upstream's pthreads and clock in place of Glaze's.
 */
#ifndef VLC_HOST_CONFIG_H
#define VLC_HOST_CONFIG_H

#include "../../../packages/vlc/glaze/config.h"

#undef GLAZE_THREADS
#define PTHREAD_COND_T_IN_PTHREAD_H 1
#define HAVE_NANOSLEEP          1
#define HAVE_DECL_NANOSLEEP     1
#define SYS_LINUX               1
/* glibc has no strlcpy before 2.38: VLC's own then. */
#undef HAVE_STRLCPY
#undef VERSION_MESSAGE
#define VERSION_MESSAGE         "vlc 0.8.6i Janus (native test)"

#endif
