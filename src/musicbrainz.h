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

#ifndef MUSICBRAINZ_H
#define MUSICBRAINZ_H 1

#ifdef __cplusplus
#  define MUSICBRAINZ_BEGIN_C_DECLS extern "C" {
#  define MUSICBRAINZ_END_C_DECLS   }
#else
#  define MUSICBRAINZ_BEGIN_C_DECLS
#  define MUSICBRAINZ_END_C_DECLS
#endif

MUSICBRAINZ_BEGIN_C_DECLS

/**
 * @file musicbrainz.h
 * @brief XXX
 *
 * @note [XXX for somewhere other than here] Coding convention:
 *       MusicBrainz structures in CamelCase (just like in their
 *       headers), our structures lower_case_separated_by_underscore.
 *
 * XXX Would it be possible to merge this with the pool code?  There
 * is a fair amount of duplication!
 *
 * @bug Only works for releases, not recordings or anything else.
 */


/**
 * @brief XXX
 *
 * @param pc    Pointer to an opaque pool context
 * @param ctx   Pointer to an opaque fingersum context
 * @param arg   Extra data XXX userdata?
 * @param flags What action to take XXX
 * @return      0 if successful, -1 otherwise.  If an error occurs,
 *              the global variable @c errno is set to indicate the
 *              error.
 */
struct musicbrainz_ctx *
musicbrainz_new();

void
musicbrainz_free(struct musicbrainz_ctx *ctx);


/* Asynchronous and cached.  The musicbrainz_query() function returns
 * immediately; queries will be executed asynchronously in the order
 * they are submitted, subject to rate limiting constraints.
 */
int
musicbrainz_query(struct musicbrainz_ctx *ctx,
                  const char *entity,
                  const char *id,
                  const char *resource,
                  size_t num_params,
                  char **param_names,
                  char **param_values);


/**
 * The _releasegroup_get_release() function returns the release within
 * a releasegroup @p ReleaseList with identifier @p id.
 *
 * @param ReleaseList XXX ReleaseGroup or ReleaseList?
 * @param id          XXX
 * @return            The release, or @c NULL if @p ReleaseList does
 *                    not contain a release with id @p id
 */
Mb5Release
musicbrainz_get_release(struct musicbrainz_ctx *ctx,
                        const char *entity,
                        const char *id,
                        const char *resource,
                        size_t num_params,
                        char **param_names,
                        char **param_values,
                        const char *release_id);

MUSICBRAINZ_END_C_DECLS

#endif /* !MUSICBRAINZ_H */
