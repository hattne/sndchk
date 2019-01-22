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

#ifndef RATELIMIT_H
#define RATELIMIT_H 1

#ifdef __cplusplus
#  define RATELIMIT_BEGIN_C_DECLS extern "C" {
#  define RATELIMIT_END_C_DECLS   }
#else
#  define RATELIMIT_BEGIN_C_DECLS
#  define RATELIMIT_END_C_DECLS
#endif

RATELIMIT_BEGIN_C_DECLS

/**
 * @file ratelimit.h
 * @brief Ensure no more requests per unit time than are allowed
 *
 * different rates for different services.  Acts as a lock [or lookup
 * the proper term on Wikipedia].
 *
 * XXX Ensure this sets errno properly
 */


/**
 * @brief XXX
 *
 * XXX Need to ask permission, and verify the rate of this!
 *
 * @return 0 if successful, -1 otherwise.  If an error occurs, the
 *         global variable @c errno is set to indicate the error.
 */
int
ratelimit_accuraterip();


/* Rate limiting: no more than 3 requests per second
 * [https://acoustid.org/webservice]
 */
int
ratelimit_acoustid();


/* Rate limiting: no more than 1 requests per second
 * [http://wiki.musicbrainz.org/XML_Web_Service/Rate_Limiting] XXX REREAD!
 */
int
ratelimit_musicbrainz();

RATELIMIT_END_C_DECLS

#endif /* !RATELIMIT_H */
