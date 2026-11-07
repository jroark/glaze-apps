/*
 * VLC's thread types on Glaze (the GLAZE_THREADS branch of vlc_threads.h).
 *
 * Mutexes are libc's gz_mutex. Glaze has no condition variable, so one is
 * built here from a kernel semaphore and a waiter count; threads carry a
 * completion semaphore so they can be joined. Priorities are ignored: the
 * kernel schedules every thread alike. Implemented in glaze/threads.c.
 */
#ifndef GLAZE_VLC_THREADS_H
#define GLAZE_VLC_THREADS_H

#include <glaze/thread.h>

struct glaze_thread;
typedef struct glaze_thread *vlc_thread_t;

struct glaze_cond {
    struct gz_mutex lock;       /* guards waiters */
    int waiters;                /* threads blocked or about to block */
    int sem;                    /* one post per woken waiter */
};

int glaze_cond_init(struct glaze_cond *c);
int glaze_cond_destroy(struct glaze_cond *c);
int glaze_cond_signal(struct glaze_cond *c);
int glaze_cond_wait(struct glaze_cond *c, struct gz_mutex *m);

/* Starts func(arg); *out is the handle glaze_thread_join takes (and frees). */
int glaze_thread_create(vlc_thread_t *out, void *(*func)(void *), void *arg);
int glaze_thread_join(vlc_thread_t t);

#endif
