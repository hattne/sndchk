/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 * Copyright © 2018-2019, Johan Hattne
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted, provided
 * that the above copyright notice and this permission notice appear
 * in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef __MACH__
#    include <mach/clock.h>
#    include <mach/mach.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "ratelimit.h"


/* Nanoseconds per second
 */
#define _NSPS 1000000000ul


/* XXX
 */
static pthread_mutex_t _mutex_accuraterip = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _mutex_acoustid = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _mutex_musicbrainz = PTHREAD_MUTEX_INITIALIZER;

/* Monotonically increasing time of the last release of the lock.  The
 * impossible initialisation time indicates that the lock can be
 * released immediately.
 */
static struct timespec _clock_accuraterip = {0, _NSPS};
static struct timespec _clock_acoustid = {0, _NSPS};
static struct timespec _clock_musicbrainz = {0, _NSPS};


static int
_clock_gettime(struct timespec *tp)
{
#ifdef HAVE_CLOCK_GETTIME
    /* XXX Review the documentation @ OpenBSD
     */
    return (clock_gettime(CLOCK_MONOTONIC, tp));
#else
#  ifdef __MACH__
    /* Mac OS X does not implement the POSIX clock_gettime(2)
     * interface.  XXX Probably not so great, nor is relying on
     * __APPLE__.  Also, this does not do any error reporting.  See
     * http://stackoverflow.com/questions/11680461/monotonic-clock-on-osx
     */
    clock_serv_t cclock;
    mach_timespec_t mts;

    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);

    tp->tv_sec = mts.tv_sec;
    tp->tv_nsec = mts.tv_nsec;

    return (0);
#  endif
#endif
}


/* See e.g. timersub() and timeradd() in getitimen(3)?
 */
static void
_timespec_add(
    struct timespec *a, const struct timespec *b, const struct timespec *c)
{
    a->tv_sec = b->tv_sec + c->tv_sec;
    a->tv_nsec = b->tv_nsec + c->tv_nsec;
    if (a->tv_nsec > _NSPS) {
        a->tv_sec += 1;
        a->tv_nsec -= _NSPS;
    }
}


static void
_timespec_sub(
    struct timespec *a, const struct timespec *b, const struct timespec *c)
{
    a->tv_sec = b->tv_sec - c->tv_sec;
    a->tv_nsec = b->tv_nsec - c->tv_nsec;
    if (a->tv_nsec < 0) {
        a->tv_sec -= 1;
        a->tv_nsec += _NSPS;
    }
}


static int
_timeout(
    pthread_mutex_t *mutex, struct timespec *clock, struct timespec *timeout)
{
    struct timespec now;
    int ret;


    /* Get current monotonic time and lock the mutex.
     */
    if (_clock_gettime(&now) != 0)
        return (-1);
    if (pthread_mutex_lock(mutex) != 0)
        return (-1);


    /* Unless this is the first invocation of _timeout(), compute the
     * timeout, nanosleep(2), and update the monotonic time when the
     * lock was last released if clock + timeout - now is greater than
     * zero.
     */
    if (clock->tv_nsec < _NSPS) {
        _timespec_add(clock, clock, timeout);
        _timespec_sub(timeout, clock, &now);
        if (timeout->tv_sec >= 0 && timeout->tv_nsec >= 0) {
            do {
                ret = nanosleep(timeout, timeout);
            } while (ret == -1 && errno == EINTR);
            return (ret | (pthread_mutex_unlock(mutex) != 0 ? -1 : 0));
        }
    }


    /* No need to wait; set the time of the last release to now and
     * return.
     */
    clock->tv_sec = now.tv_sec;
    clock->tv_nsec = now.tv_nsec;
    if (pthread_mutex_unlock(mutex) != 0)
        return (-1);

    return (0);
}


int
ratelimit_accuraterip()
{
    struct timespec timeout;

    timeout.tv_sec = 0;
    timeout.tv_nsec = _NSPS / 2 + 1;

    return (_timeout(&_mutex_accuraterip, &_clock_accuraterip, &timeout));
}


/* XXX Should really store the clock-times of the last three times
 * this function returned, and hold of the earliest time is less than
 * a second ago.
 */
int
ratelimit_acoustid()
{
    struct timespec timeout;

    timeout.tv_sec = 0;
    timeout.tv_nsec = _NSPS / 3 + 1;

    return (_timeout(&_mutex_acoustid, &_clock_acoustid, &timeout));
}


int
ratelimit_musicbrainz()
{
    struct timespec timeout;

    timeout.tv_sec = 1;
    timeout.tv_nsec = 1;

    return (_timeout(&_mutex_musicbrainz, &_clock_musicbrainz, &timeout));
}
