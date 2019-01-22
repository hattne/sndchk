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

#ifndef POOL_H
#define POOL_H 1

#ifdef __cplusplus
#  define POOL_BEGIN_C_DECLS extern "C" {
#  define POOL_END_C_DECLS   }
#else
#  define POOL_BEGIN_C_DECLS
#  define POOL_END_C_DECLS
#endif

POOL_BEGIN_C_DECLS

/**
 * @file pool.h
 * @brief Simple scheduler for fingersum jobs
 *
 * The pool module implements a scheduler for fingersum jobs
 * (i.e. AccurateRip checksum and Chromaprint fingerprint
 * calculations).  It maintains a single, global pool of worker
 * threads.
 *
 * Uses a thread pool to do fingersum in parallel.  Jobs are processed
 * in the order they are submitted.
 *
 * Should be equivalent to plain fingersum with nmemb == 1.  The case
 * nmemb == 0 should work as well.
 *
 * Processing starts in the order jobs are submitted.  Due to the
 * multiprocessing nature, they may not finish in that order, though.
 *
 * XXX Maybe better named fspool?  Nah...
 *
 * Implements "first come, first served" (FCFS) scheduling.
 *
 * XXX Would be nicer if we could accept a callback and user data
 * instead of the ACTIONS enumeration.
 *
 * XXX Review https://en.wikipedia.org/wiki/Monitor_(synchronization)
 *
 * XXX Could have an option to start a new thread whenever no free
 * threads are available, but that probably also requires the ability
 * to reap idle threads.  And then the whole bit about dynamic
 * resizing of a pool becomes an issue.
 *
 * All jobs/tasks have the same priority
 *
 * @note While the module is thread-safe, any particular RESULTS
 *       context may only be operated on by a single thread at a time.
 *       The pool context does not have this limitation.
 */


/**
 * @brief Calculate the AccurateRip checksum
 */
#define POOL_ACTION_ACCURATERIP 0x1

/**
 * @brief Calculate the Chromaprint
 */
#define POOL_ACTION_CHROMAPRINT 0x2


/**
 * @brief Create a pool context for job submission
 *
 * XXX This function probably needs a better name!  Maybe this is
 * actually a batch?  And the reconfiguration with the nmemb thing is
 * not so elegant (we may not want to change it every time a new
 * "queue" is created).
 *
 * @param nmemb Number of threads to use for fingersum jobs (as least
 *              as many as @p nmemb)
 * @return      Pointer to an opaque pool context
 */
struct pool_context *
pool_new_pc(size_t nmemb);


/**
 * @brief Release a pool context
 *
 * All results, as well as all submitted jobs, whether they have
 * started or not, are discarded.
 *
 * XXX This function probably needs a better name!
 *
 * @param pc Pointer to an opaque pool context
 */
void
pool_free_pc(struct pool_context *pc);


/**
 * @brief Schedules a fingersum context for processing
 *
 * Submits a job to the queue.  Unless the pool was initialised with
 * <code>nmemb == 0</code>, the add_request() function is asynchronous
 * and returns immediately.  Otherwise, the job is processed and
 * add_request() returns when the result has been pushed onto @p pc.
 *
 * If @p pc is no longer accepting submission, add_requests() returns
 * @c -1 and sets the @c errno to @c ECANCELED.  Other values of @c
 * errno arise from failures in standard library functions.
 *
 * XXX This function probably needs a better name!  Perhaps
 * pool_add_request()?  pool_submit()?  submits a job (or a task)?
 * batch_submit()?
 *
 * XXX Idea: return an ID, which can the be used to wait for the job.
 * The wait function should take a special value to indicate waiting
 * for any job.
 *
 * XXX The "corresponding" glib function, gthread_pool_push() takes an
 * addition "GError **error" argument for error reporting.
 *
 * @param pc    Pointer to an opaque pool context
 * @param ctx   Pointer to an opaque fingersum context
 * @param arg   Extra data XXX userdata?
 * @param flags What action to take (or should this be named "action") XXX
 * @return      0 if successful, -1 otherwise.  If an error occurs,
 *              the global variable @c errno is set to indicate the
 *              error.
 */
int
add_request(struct pool_context *pc,
            struct fingersum_context *ctx,
            void *arg,
            int flags);


/**
 * @brief Retrieve result from pool context
 *
 * If all results have been retrieved, get_results() returns @c -1 and
 * sets the global variable @c errno to @c ENOMSG.  XXX Should have
 * been ECHILD (see wait(2).
 *
 * XXX This function probably needs a better name!  Should mirror
 * whatever functions from the fingersum module?  No, I think
 * batch_wait() is good.
 *
 * XXX Lazy evaluation in case of pool with zero threads: call the
 * function here for the requested ID.
 *
 * @param pc    Pointer to an opaque pool context
 * @param ctx   Pointer to an opaque fingersum context
 * @param arg   Extra data XXX userdata?  Naw, data.
 * @param flags What action to take XXX
 * @return      0 if successful, -1 otherwise.  If an error occurs,
 *              the global variable @c errno is set to indicate the
 *              error.
 */
int
get_result(struct pool_context *pc,
           struct fingersum_context **ctx,
           void **arg,
           int *status);

POOL_END_C_DECLS

#endif /* !POOL_H */
