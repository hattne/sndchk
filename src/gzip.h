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

#ifndef GZIP_H
#define GZIP_H 1

#ifdef __cplusplus
#  define GZIP_BEGIN_C_DECLS extern "C" {
#  define GZIP_END_C_DECLS   }
#else
#  define GZIP_BEGIN_C_DECLS
#  define GZIP_END_C_DECLS
#endif

GZIP_BEGIN_C_DECLS

/**
 * @file gzip.h
 * @brief XXX
 *
 * Module to deflate (compress) HTTP requests and inflate (decompress)
 * possible gzip-compressed responses.
 */

#include <neon/ne_request.h>


/* XXX FROM ne_request.h:ne_block_reader: "Callback for reading a
 * block of data.  Returns zero on success, or non-zero on error.  If
 * returning an error, the response will be aborted and the callback
 * will not be invoked again.  The request dispatch (or
 * ne_read_response_block call) will fail with NE_ERROR; the session
 * error string should have been set by the callback."
 *
 * XXX FROM ne_request.h:ne_add_response_reader: "Add a response
 * reader for the given request, with the given acceptance function.
 * userdata is passed as the first argument to the acceptance + reader
 * callbacks.  The acceptance callback is called once each time the
 * request is sent: it may be sent >1 time because of authentication
 * retries etc.  For each time the acceptance callback is called, if
 * it returns non-zero, blocks of the response body will be passed to
 * the reader callback as the response is read.  After all the
 * response body has been read, the callback will be called with a
 * 'len' argument of zero."
 *
 * @return Pointer to a gzip handler.  If an error occurs, gzip_new()
 *         returns @c NULL, updates the error string of @p parser, and
 *         sets the global variable @c errno to indicate the error.
 */
struct gzip_context *
gzip_new(ne_session *session, ne_block_reader reader, void *userdata);


/* XXX
 */
int
gzip_free(struct gzip_context *gc);


/* The _gzip_deflate() function compresses the @p src_len octets
 * pointed to by @p src to @p *dst_len octets stored in an allocated
 * buffer pointed to by @p *dst.  @p *dst may subsequently be used as
 * an argument to the function free(3).
 *
 * XXX Does not provide an error string (which would require the
 * parser or the session for context).  See also notes below about
 * folding the initialisation of @p strm into this function.  Could
 * perhaps return the zlib error code?
 *
 * @note This is a standalone helper function that does not rely on a
 *       gzip_context structure.
 *
 * @param session Pointer to the neon session XXX Needed for proper
 *                error reporting
 *
 * @return 0 if successful, -1 otherwise.  If an error occurs, the
 *         global variable @c errno is set to indicate the error.
 */
int
gzip_deflate(ne_session *session,
             void *src,
             size_t src_len,
             void **dst,
             size_t *dst_len);


/* Handles both raw and compressed, gzip-encoded data.  Functions as a
 * chainable response reader.
 *
 * XXX This comment needs some work, still.  Should handle chained
 * NULL-reader!
 *
 * @param userdata Pointer to gzip_context structure
 * @param buf      XXX
 * @param len      XXX
 * @return         Pointer to a gzip_context structure.  If an error
 *                 occurs, gzip_new() returns @c NULL, updates the
 *                 error string of @p parser, and sets the global
 *                 variable @c errno to indicate the error.
 */
int
gzip_inflate_reader(void *userdata, const char *buf, size_t len);

GZIP_END_C_DECLS

#endif /* !GZIP_H */
