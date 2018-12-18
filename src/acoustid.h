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

#ifndef ACOUSTID_H
#define ACOUSTID_H 1

#ifdef __cplusplus
#  define ACOUSTID_BEGIN_C_DECLS extern "C" {
#  define ACOUSTID_END_C_DECLS   }
#else
#  define ACOUSTID_BEGIN_C_DECLS
#  define ACOUSTID_END_C_DECLS
#endif

ACOUSTID_BEGIN_C_DECLS

/**
 * @file acoustid.h
 * @brief XXX
 *
 * XXX Oh my, does this ever leak memory!
 *
 * Every track can match several recordings with different scores.
 * Any recording may be featured on several releases, which may all be
 * part of different releasegroups.  We need to keep track of the
 * scored recordings for each track and the intersection of the
 * releasegroups.  Propagate the score to the releasegroups as well
 * (XXX not sure that is necessary here).
 *
 * Only requires all files to share a releasegroup and release.  Does
 * no further elimination based on e.g. score.
 *
 * @bug XXX Should allow proxies using ne_session_proxy(), look into
 *      subversion code for an example.
 *
 * @bug XXX May need to set the user agent for the session with
 *      ne_set_useragent() to comply with AcoustID rules
 *      (e.g. ne_set_useragent(ac->session, "MyUserAgent/1.0");).
 *
 * @note An AcoustID context must be accessed only by one thread at
 *       the time.
 *
 * @todo XXX Check whether this qualifies as a "module" and fix all
 *       the quotation marks if it is.  Yes it does, so fix it, then
 *       propagate this to the next module.
 *
 * @todo XXX Verify const correctness in the interface [propagate this
 *       note to other header files before committing].
 *
 * @todo XXX No need for namespace on static functions and variables
 *       [propagate this to the next module].
 *
 * @todo XXX Is there a lower limit to the length of a track for it to
 *       have an entry in the AcoustID database?
 *
 * With the length from AcoustID, it ought to be possible to warn if
 * there is more than one releasegroup after acoustid.  NO, it won't:
 * the power of TOC-match derives from all the recordings on a release
 * that are NOT matched by a stream.
 *
 * References
 *
 * https://acoustid.org/webservice
 */


/**
 * @brief Retrieve result from pool context
 *
 * If all results have been retrieved, get_results() returns @c -1 and
 * sets the global variable @c errno to @c ENOMSG.
 *
 * XXX This function probably needs a better name!  Should mirror
 * whatever functions from the fingersum "module"?
 *
 * @param pc    Pointer to an opaque pool context
 * @param ctx   Pointer to an opaque fingersum context
 * @param arg   Extra data XXX userdata?
 * @param flags What action to take XXX
 * @return      0 if successful, -1 otherwise.  If an error occurs,
 *              the global variable @c errno is set to indicate the
 *              error.
 */
struct acoustid_context *
acoustid_new();


/**
 * Free all dynamically allocated data structures for the stream
 * state.  Ignore return value.
 */
void
acoustid_free(struct acoustid_context *ctx);


/**
 * XXX Should be sufficient to just pass an fingersum_context?
 *
 * @param fingerprint The calculated Chromaprint fingerprint,
 *                    compressed and base-64-encoded with the URL-safe
 *                    scheme, as returned by
 *                    fingersum_get_fingerprint()
 * @param duration    The duration of the audio stream in seconds,
 *                    truncated to integer precision, as returned by
 *                    fingersum_get_duration()
 *
 * @param index XXXXXX New thing, motivated by the fact that this
 *                    shows up in the fp3_release structure
 */
int
acoustid_add_fingerprint(
    struct acoustid_context *ctx,
    const char *fingerprint,
    unsigned int duration,
    size_t index);


/* XXX BIG QUESTION: where is the delay?  Where to split things up for
 * proper multiprocessing?  Where is the asynchronous bit?
 *
 * Calculate the fingerprint, create and compress query (data) for
 * subsequent HTTP POST request.
 */
struct fp3_result *
acoustid_request(struct acoustid_context *ctx);


/* XXX Gah!
 */
void *
acoustid_query(struct acoustid_context *ctx);

ACOUSTID_END_C_DECLS

#endif /* !ACOUSTID_H */
