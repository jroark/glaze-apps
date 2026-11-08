/*
 * The Glaze thread primitives behind VLC's GLAZE_THREADS backend
 * (glaze_threads.h): condition variables over kernel semaphores, and
 * joinable threads over gz_thread_create.
 */
#include <stdlib.h>
#include <glaze/sys.h>

#include "glaze_threads.h"

/*
 * A waiter counts itself in under the cond's own lock before it releases
 * the caller's mutex, and a signal posts the semaphore only for a counted
 * waiter, so a signal sent between the release and the semaphore wait is
 * kept rather than lost. A signal with nobody waiting does nothing, as
 * with pthreads.
 *
 * The kernel has 256 semaphores for the whole system, and VLC gives every
 * object (each module included) a cond it mostly never waits on, so the
 * semaphore is made on the first wait and kept until destroy, and the
 * cond's own lock, like VLC's mutexes, makes its semaphore on first use.
 * If none can be had, the wait degrades to a short sleep: a spurious
 * wakeup, which callers that re-check their condition already allow for.
 */
#define COND_POLL_MS 10

int glaze_cond_init(struct glaze_cond *c)
{
    c->lock = (struct gz_mutex)GZ_MUTEX_INIT;
    c->waiters = 0;
    c->sem = 0;
    return 0;
}

int glaze_cond_destroy(struct glaze_cond *c)
{
    gz_mutex_destroy(&c->lock);
    if (c->sem > 0)
        gz_sem_destroy(c->sem);
    c->sem = 0;
    return 0;
}

int glaze_cond_signal(struct glaze_cond *c)
{
    /* No semaphore yet: nobody has ever waited. A waiter makes it while
     * holding the caller's mutex, which a signaller that needs the wakeup
     * holds too, so this cannot miss a waiter that is counting itself in. */
    if (c->sem <= 0)
        return 0;
    gz_mutex_lock(&c->lock);
    if (c->waiters > 0) {
        c->waiters--;
        gz_sem_post(c->sem);
    }
    gz_mutex_unlock(&c->lock);
    return 0;
}

int glaze_cond_wait(struct glaze_cond *c, struct gz_mutex *m)
{
    gz_mutex_lock(&c->lock);
    if (c->sem <= 0) {
        int sem = gz_sem_create(0);
        if (sem < 0) {
            gz_mutex_unlock(&c->lock);
            gz_mutex_unlock(m);
            gz_sleep_ms(COND_POLL_MS);
            gz_mutex_lock(m);
            return 0;
        }
        c->sem = sem;
    }
    c->waiters++;
    gz_mutex_unlock(&c->lock);

    gz_mutex_unlock(m);
    int r = gz_sem_wait(c->sem, -1);
    gz_mutex_lock(m);
    return r;
}

/* A thread posts done after its function returns; join waits for it. */
struct glaze_thread {
    void *(*func)(void *);
    void *arg;
    int done;
};

static void start(void *p)
{
    struct glaze_thread *t = p;

    t->func(t->arg);
    gz_sem_post(t->done);       /* t may be freed from here on */
}

int glaze_thread_create(vlc_thread_t *out, void *(*func)(void *), void *arg)
{
    struct glaze_thread *t = malloc(sizeof(*t));

    *out = NULL;
    if (!t)
        return GZ_ENOMEM;
    t->func = func;
    t->arg = arg;
    t->done = gz_sem_create(0);
    if (t->done < 0) {
        int r = t->done;
        free(t);
        return r;
    }
    int tid = gz_thread_create(start, t);
    if (tid < 0) {
        gz_sem_destroy(t->done);
        free(t);
        return tid;
    }
    *out = t;
    return 0;
}

int glaze_thread_join(vlc_thread_t t)
{
    if (!t)
        return GZ_EINVAL;
    int r = gz_sem_wait(t->done, -1);
    gz_sem_destroy(t->done);
    free(t);
    return r;
}
