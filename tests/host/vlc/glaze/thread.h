/*
 * The part of Glaze's glaze/thread.h that codec_mjpeg uses, over pthreads,
 * for the native test. Glaze's mutex is recursive.
 */
#ifndef VLC_HOST_GLAZE_THREAD_H
#define VLC_HOST_GLAZE_THREAD_H

#include <pthread.h>

struct gz_mutex {
    pthread_mutex_t m;
};
#define GZ_MUTEX_INIT { PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP }

static inline void gz_mutex_lock(struct gz_mutex *m)
{
    pthread_mutex_lock(&m->m);
}

static inline void gz_mutex_unlock(struct gz_mutex *m)
{
    pthread_mutex_unlock(&m->m);
}

#endif
