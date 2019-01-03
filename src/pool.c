/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 * Copyright (c) 2014, Johan Hattne
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
 *
 * $Id:$
 */

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>

#include "fingersum.h"
#include "pool.h"
#include "simpleq.h"


/* The internal request structure.  Identical to result structure?  So
 * maybe this is a job, or a task?  XXX If so, rename it to _job
 */
struct _pool_request
{
    /* Additional data of the job XXX should be data or perhaps
     * userdata or whatever like in neon?  Where to find an example of
     * this?  It is data in glib (user_data appears to be used for
     * data shared among all threads in the pool).
     */
    void *arg;

    /* Pointer to opaque fingersum context
     */
    struct fingersum_context *ctx;

    /* Context for storing the result of the job
     */
    struct pool_context *result;

    /* XXX What action to take
     */
    int flags;

    /* 0 if successful, -1 otherwise.  This member is only valid after
     * the action has been carried out.
     *
     * XXX Relationship to flags
     */
    int status;

    /* Simple queue
     */
    SIMPLEQ_ENTRY(_pool_request) requests;
};


/* XXX Define the _pool_head structure, required for the global
 * _pool_requests variable and the pool_context structure defined
 * below.
 *
 * XXX A SIMPLEQ might have been sufficient (we need to add at end of
 * list, and remove arbitrary elements--check that no simpler queue
 * accomplishes that, and that this is actually what we need), but it
 * is not universally implemented, hence a copy of the code from
 * OpenBSD is provided.
 */
SIMPLEQ_HEAD(_pool_head, _pool_request); // XXX _pool_head -> _job_head?


/* Opaque thing here.
 */
struct pool_context
{
    /* Mutex to ensure exclusive access to the context
     */
    pthread_mutex_t mutex;

    /* Named semaphore for results, counts the number of jobs in this
     * context's results queue.
     */
    char *name;

    /* XXX See above
     */
    sem_t *sem;

    /* Number of jobs in _pool_requests that belong to this context.
     * This is increased by add_request() and decreased by
     * get_result().  Could probably have been a semaphore, but
     * sem_getvalue() is not implemented in Mac OS X.
     */
    size_t inprogress;

    /* Non-zero if the context is active and accepts new submissions,
     * zero if the context is being destructed.
     */
    int active;

    /* FIFO queue for results (completed jobs)
     */
    struct _pool_head results;
};


/* The initialisation of the global variables below ensures that the
 * module is functional even if _pool_init() has not been invoked.  It
 * will function as pool with no threads.  _pool_free() should be able
 * to run at all times.
 */

/* Head of linked lists (naw, it's a FIFO queue) of requests
 */
struct _pool_head _pool_requests = SIMPLEQ_HEAD_INITIALIZER(_pool_requests);   // XXX -> _jobs?

/* Mutex to ensure exclusive access to the global variables in this
 * module
 */
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;

/* Global array of worker threads
 */
static pthread_t *_pool_threads = NULL; // XXX -> _threads?

/* Name of the named request semaphore
 */
static char *_pool_requests_name = NULL; // XXX -> _name?

/* The named request semaphore
 */
static sem_t *_pool_requests_sem = SEM_FAILED; // XXX -> _sem or _semaphore?

/* Number of worker threads in _pool_threads
 */
static size_t _pool_nmemb = 0; // XXX -> _nmemb?

/* Number of successful invocations of _pool_init().  If the module
 * has not been initialised _init_state is zero.  If it is less than
 * zero, initialisation failed.
 */
static ssize_t _init_state = 0; // XXX better name?


/* Cleanup routine for worker threads, called when _start() returns or
 * is cancelled.  Note that chromaprint_free() is not thread-safe if
 * Chromaprint was compiled with FFTW.
 */
static void
_cleanup(void *arg)
{
    if (pthread_mutex_lock(&_mutex) == 0) {
        chromaprint_free((ChromaprintContext *)arg);
        pthread_mutex_unlock(&_mutex);
    }
}


/* If the thread was cancelled _join() will always attempt to join it,
 * even if the pool mutex could not be unlocked.  This ensures the
 * consistency of the thread pool.  _join() returns 0 if successful,
 * -1 otherwise.  If an error occurs, the global variable @c errno is
 * set to indicate the error.
 *
 * XXX All threads should exit with CANCEL-something.  Note here, and
 * check in caller?  It should be safe to call this from several
 * places simulataneously.
 */
static int
_join(pthread_t thread, void **value_ptr)
{
    int ret;

    printf("  _join() is calling pthread_cancel...\n");
    ret = pthread_cancel(thread);
    if (ret != 0) {
        errno = ret;
        return (-1);
    }

    // XXX 2018-11-02: this is where stuff hangs!
    printf("  _join() is calling pthread_join()...\n");
    ret = pthread_join(thread, value_ptr);
    if (ret != 0) {
        errno = ret;
        return (-1);
    }

    printf("  Leaving _join()...\n");
    return (0);
}


/* The _process() function processes a job.  It calculates the
 * AccurateRip checksum and/or the Chromaprint fingerprint as
 * requested and sets the status flag accordingly.  It then pushes the
 * completed job (whether it succeeded or not) to the results queue of
 * the submitting context and posts its semaphore.  The _process()
 * function returns 0 if successful, -1 otherwise.  If an error
 * occurs, the global variable @c errno is set to indicate the error.
 */
static int
_process(struct _pool_request *r, ChromaprintContext *cc)
{
    //printf("    Processing job %zd, posting to %p\n",
    //        (size_t)(r->arg), r->result->sem);

    r->status = 0;
    if (r->flags & POOL_ACTION_ACCURATERIP) {
//        if (fingersum_get_checksum(r->ctx, cc, NULL) == 0)
//            r->status |= POOL_ACTION_ACCURATERIP;
        if (fingersum_get_result_3(NULL, r->ctx, NULL) != NULL)
            r->status |= POOL_ACTION_ACCURATERIP;
    }
    if (r->flags & POOL_ACTION_CHROMAPRINT) {
        if (fingersum_get_fingerprint(r->ctx, cc, NULL) == 0)
            r->status |= POOL_ACTION_CHROMAPRINT;
    }

    if (pthread_mutex_lock(&r->result->mutex) != 0)
        return (-1);
    SIMPLEQ_INSERT_TAIL(&r->result->results, r, requests);
    if (pthread_mutex_unlock(&r->result->mutex) != 0)
        return (-1);
    if (sem_post(r->result->sem) != 0)
        return (-1);

    return (0);
}


/* The _sem_open() function opens a named semaphore, and initialises
 * its value to zero.  The name and the semaphore are stored in the
 * addresses pointed to by @p name and @p sem, respectively.  If
 * sem_open(3) fails because the semaphore is already in use, possibly
 * by a different user, try again.
 *
 * This module uses named semaphores, because unnamed semaphores as
 * constructed by sem_init(3) are not implemented in Mac OS X.  These
 * semaphores must be removed on exit.
 */
static int
_sem_open(char **name, sem_t **sem)
{
    char *n;
    sem_t *s;
    size_t i;
    int fd;


    n = NULL;
    s = SEM_FAILED;
    for (i = 0; i < 256; i++) { // XXX HARDCODED LIMIT!  See below as well.
        if (n != NULL)
            free(n);

        n = strdup("/tmp/sem_req.XXXXXX");
        if (n == NULL)
            break;

        fd = mkstemp(n);
        if (fd == -1)
            break;

        s = sem_open(n, O_CREAT | O_EXCL, 0644,0);
        if (close(fd) != 0 || unlink(n) != 0)
            break;
        if (s != SEM_FAILED) {
            //printf("Opened semaphore %s at %p\n", n, s);
            *name = n;
            *sem = s;
            return (0);
        } else if (errno == EPERM) {
            errno = 0;
        }
    }

    if (i == 256)
        errno = EBUSY; // XXX Is this a sensible message?

    if (s != SEM_FAILED)
        sem_close(s);
    if (n != NULL)
        free(n);
    return (-1);
}


/* Start routine for the checksumming and fingerprinting threads.
 * Extract queued jobs from _pool_requests as they are submitted and
 * process them according to the value of their flags.  Processed
 * fingersum contexts are pushed into the result queues of their
 * respective pool contexts.  This function does not exit, but is
 * stopped by pthread_cancel(3).  It also does not do any memory
 * management; jobs are simply moved from one queue to another.
 */
static void *
_start(void *arg)
{
    ChromaprintContext *cc;
    struct _pool_request *r;
    int oldstate;

    //printf("    Thread started\n");

    /* Construct a thread-local Chromaprint context.  Note that
     * chromaprint_new() is not thread-safe if Chromaprint was
     * compiled with FFTW.
     */
    if (pthread_mutex_lock(&_mutex) != 0)
        return (NULL);
    cc = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
    if (cc == NULL) {
        pthread_mutex_unlock(&_mutex);
        return (NULL);
    }
    pthread_cleanup_push(&_cleanup, cc);
    if (pthread_mutex_unlock(&_mutex) != 0)
        return (NULL);


    for ( ; ; ) {
        /* Block unless a request is pending, and retry if
         * interrupted.  This introduces a cancellation point:
         * sem_wait(3) will not return if the thread is cancelled.
         *
         * There is no guarantee that the request at the top of the
         * queue at the time sem_wait(3) returns is the same request
         * returned by SIMPLEQ_FIRST(3).  Because this does not really
         * matter, it is a benign race condition.
         */
//        printf("    Waiting for data on %p\n", _pool_requests_sem);
        if (sem_wait(_pool_requests_sem) != 0) {
            if (errno == EINTR) {
                errno = 0;
                continue;
            }
            return (NULL);
        }
//        printf("    Got the semaphore\n");


        /* Disable cancellation to ensure atomicity: a job must be
         * either processed in full or not at all.  Lock the mutex to
         * ensure exclusive access the request queue and pop the
         * pending request.  Unlock the mutex to allow new jobs to be
         * queued while the job is processed.
         *
         * When a pool_context is destroyed and pulls its unstarted
         * jobs from the _pool_requests queue, the queue may be empty
         * even though the semaphore is signalled.
         *
         * 2018-11-02: race condition? It would seem that cancel
         * requests are not properly queued.  If a pthread_cancel is
         * called while PTHREAD_CANCEL_DISABLE, this code may hang
         * indefinetely.  Really, there's no need to restore the
         * cancel state, since the process will finish, but what about
         * the cleanup function?
         */
        if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) != 0)
            return (NULL);
//        printf("    set the state\n");
        if (pthread_mutex_lock(&_mutex) != 0) {
            pthread_setcancelstate(oldstate, NULL);
            return (NULL);
        }
//        printf("    locked the mutex\n");

        if (SIMPLEQ_EMPTY(&_pool_requests)) {
            if (pthread_mutex_unlock(&_mutex) != 0) {
                pthread_setcancelstate(oldstate, NULL);
                return (NULL);
            }
            if (pthread_setcancelstate(oldstate, NULL) != 0)
                return (NULL);
            continue;
        }

        r = SIMPLEQ_FIRST(&_pool_requests);
        SIMPLEQ_REMOVE_HEAD(&_pool_requests, requests);
        if (r == NULL)
            printf("*** SIMPLEQ BOGUS #1 ***\n");

        if (pthread_mutex_unlock(&_mutex) != 0) {
            pthread_setcancelstate(oldstate, NULL);
            return (NULL);
        }
        printf("    unlocked the mutex\n");
        if (_process(r, cc) != 0) {
            pthread_setcancelstate(oldstate, NULL);
            return (NULL);
        }
        printf("    processed request\n");
        if (pthread_setcancelstate(oldstate, NULL) != 0)
            return (NULL);
        printf("    reverted state\n");
        pthread_testcancel(); // XXX 2018-11-02: force a cancellation point
    }


    /* NOTREACHED
     */
    pthread_cleanup_pop(1);
    return (NULL);
}


/* The _pool_free() function releases the global pool of worker
 * threads.  The function must be called once all use of this module
 * is complete.  Any submitted jobs that have not yet been started are
 * discarded.  Started jobs are allowed to finish.
 *
 * The global pool of worker threads is reference-counted; the only
 * call to _pool_free() that will have any effect is the one
 * corresponding to the first invocation of of _pool_init().
 *
 * XXX Maybe better called destroy(), because this touches the global
 * context?  Or maybe close()?  Or exit(), as in neon()
 * [ne_sock_exit()]?
 *
 * Ignore any pthread-related errors, because there is nothing that
 * can be done about them here.  _pool_free() must reset everything to
 * the initial state to permit subsequent re-initalisation.
 */
static void
_pool_free()
{
    struct _pool_request *r;
    size_t i;


    /* Handle the reference-counting.  _pool_free() does nothing if
     * initialisation failed, or if this is not expected to be the
     * last invocation.
     */
    pthread_mutex_lock(&_mutex);
    if (_init_state <= 0 || --_init_state > 0) {
        pthread_mutex_unlock(&_mutex);
        return;
    }
    pthread_mutex_unlock(&_mutex);
    printf("    adjusted state\n");
//    printf("ACTUALLY CLOSING DOWN\n");


    /* Cancel and join all the threads in the pool, then free the
     * pool.  This guarantees that the requests semaphore can be
     * closed, because there will be no threads left to wait for it.
     */
    for (i = 0; i < _pool_nmemb; i++) {// XXX Hang here 2016-10-14, 2016-10-16, 2016-11-28, 2018-09-13
        printf("Joining thread %zd/%zd...\n", i, _pool_nmemb);
        _join(_pool_threads[i], NULL);
        printf("... joined!\n");
    }
    printf("    joined threads\n");

    pthread_mutex_lock(&_mutex);
    if (_pool_threads != NULL) {
        free(_pool_threads);
        _pool_threads = NULL;
    }
    printf("    freed pool\n");

    if (_pool_requests_sem != NULL) {
        sem_close(_pool_requests_sem);
        sem_unlink(_pool_requests_name);
        _pool_requests_sem = SEM_FAILED;
    }
    printf("    freed semaphore\n");

    if (_pool_requests_name != NULL) {
        free(_pool_requests_name);
        _pool_requests_name = NULL;
    }
    printf("    freed name\n");


    /* Discard any queued requests.
     */
    while (!SIMPLEQ_EMPTY(&_pool_requests)) {
        r = SIMPLEQ_FIRST(&_pool_requests);
        if (r == NULL)
            printf("*** SIMPLEQ BOGUS #2 ***\n");
        SIMPLEQ_REMOVE_HEAD(&_pool_requests, requests);
        free(r);
    }
    printf("    discarded requests\n");

    pthread_mutex_unlock(&_mutex);
}


/* The _pool_init() function performs one-time initialisation of the
 * pool module.  It adjusts the size of the global pool of worker
 * threads to @p nmemb members.  It must be called before any other
 * functions from the pool module.  It is thread-safe and can be
 * called multiple times.
 *
 * @param nmemb Number of threads to use for fingersum jobs (as least
 *              as many as @p nmemb XXX see pool_new_pc() in pool.h)
 * @return      0 if successful, -1 otherwise.  If an error occurs,
 *              the global variable @c errno is set to indicate the
 *              error.
 */
static int
_pool_init(size_t nmemb)
{
    void *p;


    /* To allow proper cleanup in _pool_free(), the module must be in
     * an initialised state as soon as _pool_init() is called.  Create
     * a named requests semaphore.  This must be done before the
     * threads are started, because the _start() routine will wait for
     * it.
     */
    if (pthread_mutex_lock(&_mutex) != 0)
        return (-1);

    if (_init_state++ < 0) {
        pthread_mutex_unlock(&_mutex);
        _pool_free();
        return (_init_state = -1);
    }

    if (_pool_requests_name == NULL && _pool_requests_sem == SEM_FAILED) {
        if (_sem_open(&_pool_requests_name, &_pool_requests_sem) != 0) {
            pthread_mutex_unlock(&_mutex);
            _pool_free();
            return (_init_state = -1);
        }
    }
    if (pthread_mutex_unlock(&_mutex) != 0) {
        _pool_free();
        return (_init_state = -1);
    }


    /* Allocate and create a pool of nmemb worker threads, all running
     * the _start() function.  _mutex must not be locked when entering
     * the if- and else-clauses, but will be locked on exit.
     */
    if (_pool_nmemb < nmemb) {
        /* Grow the thread pool.  Lengthen the array of threads, then
         * create new threads.  The pool mutex must be held while the
         * pool array is adjusted.
         */
        if (pthread_mutex_lock(&_mutex) != 0) {
            _pool_free();
            return (_init_state = -1);
        }
        p = realloc(_pool_threads, nmemb * sizeof(pthread_t));
        if (pthread_mutex_unlock(&_mutex) != 0) {
            _pool_free();
            return (_init_state = -1);
        }
        if (p == NULL) {
            _pool_free();
            return (_init_state = -1);
        }
        _pool_threads = p;

        while (_pool_nmemb < nmemb) {
            if (pthread_create(
                    &_pool_threads[_pool_nmemb++], NULL, _start, NULL) != 0) {
                _pool_free();
                return (_init_state = -1);
            }
        }

        if (pthread_mutex_lock(&_mutex) != 0) {
            _pool_free();
            return (_init_state = -1);
        }


    } else if (_pool_nmemb > nmemb) {
#if 0 // XXX Zap this!
        /* Shrink the thread pool.  Join superfluous threads, then
         * shorten the array of threads.  The pool mutex cannot be
         * held while joining the threads.
         *
         * XXX Would be nice (elegant) if this were to join just the
         * next threads as they finish, which would allow the pool
         * size to be shrunken, without discarding any jobs.
         */
        while (_pool_nmemb > nmemb) {
            if (_join(_pool_threads[_pool_nmemb - 1], NULL) != 0) {
                _pool_free();
                return (_init_state = -1);
            }
            if (pthread_mutex_lock(&_mutex) != 0) {
                _pool_free();
                return (_init_state = -1);
            }
            _pool_nmemb -= 1;
            if (pthread_mutex_unlock(&_mutex) != 0) {
                _pool_free();
                return (_init_state = -1);
            }
        }

        if (pthread_mutex_lock(&_mutex) != 0) {
            _pool_free();
            return (_init_state = -1);
        }
        p = realloc(_pool_threads, nmemb * sizeof(pthread_t));
        if (p == NULL) {
            _pool_free();
            return (_init_state = -1);
        }
        _pool_threads = p;
#endif
    }

    if (pthread_mutex_unlock(&_mutex) != 0) {
        _pool_free();
        return (_init_state = -1);
    }

    return (0);
}


void
pool_free_pc(struct pool_context *pc)
{
    struct _pool_request *r, *s;


    /* Inactivate the context to prevent new submissions.
     */
    //printf("  locking\n");
    if (pthread_mutex_lock(&pc->mutex) != 0)
        return;
    pc->active = 0;
    //printf("  unlocking\n");
    if (pthread_mutex_unlock(&pc->mutex) != 0)
        return;


    /* Discard any queued jobs of the pool context.  This will cause
     * the queue and the semaphore to loose synchronization.
     *
     * XXX Maybe want a separate function for removing a job from the
     * pool context?  Or subtracting the inprogress counter?
     */
//    printf("  deactivated\n");
    if (pthread_mutex_lock(&_mutex) == 0) {
        printf("    locked again\n");
        while (!SIMPLEQ_EMPTY(&_pool_requests)) {
//            printf("    loop #1\n");
            r = SIMPLEQ_FIRST(&_pool_requests);
            if (r->result != pc)
                break;

            if (pthread_mutex_lock(&pc->mutex) != 0)
                return;
            SIMPLEQ_REMOVE_HEAD(&_pool_requests, requests);
            pc->inprogress -= 1;
            if (pthread_mutex_unlock(&pc->mutex) != 0)
                return;
        }

        if (!SIMPLEQ_EMPTY(&_pool_requests)) {
            for (r = SIMPLEQ_FIRST(&_pool_requests); ; ) {
//                printf("    loop #2\n");
                s = SIMPLEQ_NEXT(r, requests);
                if (s == NULL)
                    break;
                if (s->result == pc) {
                    if (pthread_mutex_lock(&pc->mutex) != 0)
                        return;
                    SIMPLEQ_REMOVE_AFTER(&_pool_requests, r, requests);
                    pc->inprogress -= 1;
                    if (pthread_mutex_unlock(&pc->mutex) != 0)
                        return;
                } else {
                    r = s;
                }
            }
        }

        printf("  unlocking\n");
        pthread_mutex_unlock(&_mutex);
    }


    /* Wait for any running jobs to finish.
     */
    printf("  waiting\n");
    while (get_result(pc, NULL, NULL, NULL) == 0)
        ;
    if (errno != ENOMSG)
        return;
    errno = 0;


    /* Close and remove the semaphore, discard any queue results
     */
    printf("  discarding\n");
    if (pthread_mutex_lock(&pc->mutex) == 0) {
        printf("  locked\n");
        if (pc->sem != NULL) {
            sem_close(pc->sem);
            sem_unlink(pc->name);
        }
        printf("  removed\n");
        if (pc->name != NULL)
            free(pc->name);


        /* This while loops appears to hang every once in a while
         * 2016-10-03.
         */
        while (!SIMPLEQ_EMPTY(&pc->results)) {
            printf("    queue not empty\n");
            r = SIMPLEQ_FIRST(&pc->results);
            if (r == NULL)
                printf("*** SIMPLEQ BOGUS #3 ***\n");
            printf("    got head\n");
            SIMPLEQ_REMOVE_HEAD(&pc->results, requests);
            printf("    removed head\n");
            free(r);
            printf("    freed head\n");
        }
        printf("  discarded\n");
        pthread_mutex_unlock(&pc->mutex);
    }

    printf("  unlocked\n");
    free(pc);


    /* XXX This may not be called if pool_free_pc() has failed above!
     * However, most of them are related to mutex-locking, which
     * should never fail!
     */
    printf("  freed\n"); // Hang here 2016-10-10
    _pool_free();
    printf("  _pool_free() done\n");
}


/* Initialise the pool context.  This ensures pool_free_pc() will run.
 */
struct pool_context *
pool_new_pc(size_t nmemb)
{
    struct pool_context *pc;


    if (_pool_init(nmemb) != 0)
        return (NULL);

    pc = malloc(sizeof(struct pool_context));
    if (pc == NULL)
        return (NULL);
    SIMPLEQ_INIT(&pc->results);
    pc->name = NULL;
    pc->sem = NULL;
    pc->inprogress = 0;
    pc->active = 1;

    if (pthread_mutex_init(&pc->mutex, NULL) != 0) {
        pool_free_pc(pc);
        return (NULL);
    }

    if (_sem_open(&pc->name, &pc->sem) != 0) {
        pool_free_pc(pc);
        return (NULL);
    }

    return (pc);
}


int
add_request(struct pool_context *pc,
            struct fingersum_context *ctx,
            void *arg,
            int flags)
{
    struct _pool_request *r;
    int ret;


    /* Allocate and initialise the job.
     */
    r = malloc(sizeof(struct _pool_request));
    if (r == NULL)
        return (-1);
    r->ctx = ctx;
    r->arg = arg;
    r->result = pc;
    r->flags = flags;


    /* Do not allow new jobs to be enqueued if the context is
     * inactive.
     */
    if (pthread_mutex_lock(&pc->mutex) != 0) {
        free(r);
        return (-1);
    }
    if (!pc->active) {
        pthread_mutex_unlock(&pc->mutex);
        free(r);
        errno = ECANCELED;
        return (-1);
    }


    /* Special case for serial processing.  Increase the inprogress
     * counter to notify get_result() that a result is pending.
     */
    if (_pool_nmemb <= 0) {
        pc->inprogress += 1;
        if (pthread_mutex_unlock(&pc->mutex) != 0) {
            free(r);
            return (-1);
        }

        if (_process(r, NULL) != 0) {
            free(r);
            return (-1);
        }

        return (0);
    }


    /* Parallel processing: push the job onto the global queue.
     * Increase the inprogress counter and post the semaphore to less
     * the worker processes know a job is pending.  If the mutexes
     * cannot be unlocked or the semaphore cannot be posted, the job
     * queue will be inconsistent!  At least attempt them both.
     */
    if (pthread_mutex_lock(&_mutex) != 0) {
        pthread_mutex_unlock(&pc->mutex);
        free(r);
        return (-1);
    }

    SIMPLEQ_INSERT_TAIL(&_pool_requests, r, requests);
    pc->inprogress += 1;

    ret = pthread_mutex_unlock(&pc->mutex);
    ret |= pthread_mutex_unlock(&_mutex);

    //printf("      posting to %p\n", _pool_requests_sem);
    ret |= sem_post(_pool_requests_sem);

    return (ret);
}


int
get_result(struct pool_context *pc,
           struct fingersum_context **ctx,
           void **arg,
           int *status)
{
    struct _pool_request *r;


    for ( ; ; ) {
        /* Unless no results are expected, wait for a result to become
         * available.  Cannot hold the pool context's mutex while
         * waiting.
         */
        if (pthread_mutex_lock(&pc->mutex) != 0)
            return (-1);
        if (pc->inprogress == 0) {
            pthread_mutex_unlock(&pc->mutex);
            errno = ENOMSG;
            return (-1);
        }
        if (pthread_mutex_unlock(&pc->mutex) != 0)
            return (-1);

//        printf("0 ");
//        fflush(stdout);

        /* XXX See rare hangs here!  Are the threads deadlocked?  The
         * main thread is not holding on to any locks!  This appears
         * to be resolved (haven't seen any hangs in a long time).
         */
        if (sem_wait(pc->sem) != 0)
            return (-1);

//        printf("1 ");
//        fflush(stdout);


        /* Extract the result from the queue, and release it.  Store
         * results if requested.
         */
        if (pthread_mutex_lock(&pc->mutex) != 0)
            return (-1);

        if (SIMPLEQ_EMPTY(&pc->results)) {
            printf("*** SIMPLEQ BOGUS #4 ***\n");
            pthread_mutex_unlock(&pc->mutex);
            errno = ENOMSG;
            return (-1);
        }

        r = SIMPLEQ_FIRST(&pc->results);
        if (r == NULL)
            printf("*** SIMPLEQ BOGUS #5 ***\n");
        SIMPLEQ_REMOVE_HEAD(&pc->results, requests);

        pc->inprogress -= 1;
        if (pthread_mutex_unlock(&pc->mutex) != 0)
            return (-1);

        if (ctx != NULL)
            *ctx = r->ctx;
        if (arg != NULL)
            *arg = r->arg;
        if (status != NULL)
            *status = r->status;

        free(r);
        return (0);
    }


    /* NOTREACHED
     */
    return (-1);
}
