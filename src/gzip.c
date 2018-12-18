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

#include <errno.h>
#include <string.h>

#include <zlib.h>

#include "gzip.h"


/* State for reader will inflate and parse a response.  XXX More,
 * better, name?  Functions as a chainable reader, see
 * gzip_inflate_reader().
 */
struct
gzip_context
{
    /* Stream state for decompression (inflate)
     */
    z_stream strm;

    /* Needed for chaining.
     *
     * Not managed by this module, in particular it is not freed.
     *
     * XXX NEW
     */
    ne_block_reader reader;

    /* Pointer to the neon session
     *
     * XXX Needed for proper error reporting in e.g. _block_reader().
     *
     * Since all requests are directed to the same server, a
     * persistent connection is used.
     *
     * XXX Comment duplicated from accuraterip_context structure.
     *
     * XXX NEW
     */
    ne_session *session;

    /* XXX NEW
     *
     * Required here, because unlike the XML parser, the
     * ne_block_reader does not keep track of its own userdata.
     */
    void *userdata;

    /* XXX Comment this, may need better name.  Note special
     * interpretation when NULL.
     */
    void *out;

    /* XXX Comment this, may need better name
     */
    size_t len;
};


/* _gzip_get_error() sets the global variable @c errno and returns a
 * string to indicate the error after a failed call to any function in
 * zlib.  The return value describes the compression error, or is a
 * default error string.
 */
static const char *
_gzip_get_error(z_streamp strm, int errnum)
{
    switch (errnum) {
    case Z_BUF_ERROR:
    case Z_DATA_ERROR:
    case Z_NEED_DICT:
    case Z_STREAM_ERROR:
    default:
        errno = EPROTO;
        break;

    case Z_MEM_ERROR:
        errno = ENOMEM;
        break;

    case Z_OK:
        return (strerror(0));

    case Z_VERSION_ERROR:
        errno = EPROTONOSUPPORT;
        break;
    }


    /* Get the last error message from zlib, if any.  Could possibly
     * have used the undocumented zError() function.
     */
    if (strm->msg != NULL)
        return (strm->msg);
    return (strerror(errno));
}


struct gzip_context *
gzip_new(ne_session *session, ne_block_reader reader, void *userdata)
{
    struct gzip_context *gc;
    int ret;


    gc = malloc(sizeof(struct gzip_context));
    if (gc == NULL) {
        ne_set_error(session, "%s", strerror(errno));
        return (NULL);
    }


    /* Initialise gzip-encoded decompression, using default windowBits
     * 15 and default memLevel 8.  Allocate output buffer for
     * decompressed response.  XXX Do we *have* to set windowBits to
     * 15 for gzip?
     */
    gc->strm.avail_in = 0;
    gc->strm.next_in = Z_NULL;
    gc->strm.opaque = Z_NULL;
    gc->strm.zalloc = Z_NULL;
    gc->strm.zfree = Z_NULL;

    ret = inflateInit2(&gc->strm, 15 + 16);
    if (ret != Z_OK) {
        ne_set_error(session, "%s", _gzip_get_error(&gc->strm, ret));
        free(gc);
        return (NULL);
    }


    /* XXX The output buffer size needs a better estimate!  Perhaps
     * the neon buffer size multiplied by the average compression
     * ratio of the XML-response.
     */
    gc->reader = reader;
    gc->userdata = userdata;
    gc->session = session;

    gc->len = 10240;
    gc->out = malloc(gc->len);
    if (gc->out == NULL) {
        ne_set_error(session, "%s", strerror(errno));
        return (NULL);
    }

    return (gc);
}


int
gzip_free(struct gzip_context *gc)
{
    int ret;

    ret = 0;
    if (inflateEnd(&gc->strm) != Z_OK) {
        _gzip_get_error(&gc->strm, ret);
        ret = -1;
    }
    if (gc->out != NULL)
        free(gc->out);
    free(gc);

    return (ret);
}


int
gzip_deflate(ne_session *session,
             void *src,
             size_t src_len,
             void **dst,
             size_t *dst_len)
{
    z_stream strm;
    void *p;
    int ret;


    /* Compress, or deflate, the query string using default windowBits
     * 15 and default memLevel 8.  Release the raw query string.
     */
    strm.next_in = Z_NULL;
    strm.opaque = Z_NULL;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;

    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                       15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        ne_set_error(session, "%s", _gzip_get_error(&strm, ret));
        return (-1);
    }


    /* XXX Document the whole realloc(3)-business.
     */
    strm.avail_in = src_len;
    strm.avail_out = deflateBound(&strm, src_len);
    strm.next_in = src;
    if (*dst != NULL && *dst_len >= strm.avail_out) {
        strm.avail_out = *dst_len;
        strm.next_out = *dst;
    } else {
        p = realloc(*dst, strm.avail_out);
        if (p == NULL) {
            ne_set_error(session, "%s", strerror(ENOMEM));
            deflateEnd(&strm);
            return (-1);
        }
        strm.next_out = p;
    }


    /* deflate() should return Z_STREAM_END, because the size of the
     * output buffer is guaranteed to be at least as large as the
     * return value of deflateBound().
     */
    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        ne_set_error(session, "%s", _gzip_get_error(&strm, ret));
        deflateEnd(&strm);
        if (*dst == NULL)
            free(p);
        return (-1);
    }

    ret = deflateEnd(&strm);
    if (ret != Z_OK) {
        ne_set_error(session, "%s", _gzip_get_error(&strm, ret));
        if (*dst == NULL)
            free(p);
        return (-1);
    }

    if (*dst == NULL)
        *dst = p;
    *dst_len = strm.total_out;

    return (0);
}


int
gzip_inflate_reader(void *userdata, const char *buf, size_t len)
{
    struct gzip_context *gc;
    uLong total_in, total_out;
    int ret;


    /* If no more data is expected, reset the stream and signal the
     * downstream parser with a zero-length invocation.
     */
    gc = (struct gzip_context *)userdata;
    if (len == 0) {

        if (gc->out != NULL) {
            printf("uncompressed response "
                   "[%ld -> %lu, compression ratio %.2f]\n",
                   gc->strm.total_in, gc->strm.total_out,
                   1.0f * gc->strm.total_out / gc->strm.total_in);
        } else {
//            printf("LOOKS LIKE RESPONSE WAS NOT COMPRESSED\n");
        }

        if (inflateReset(&gc->strm) != Z_OK) {
            errno = EPROTO;
            return (-1);
        }

        return (gc->reader(gc->userdata, NULL, 0));
    }


    /* Assume the response is raw if there is no buffer for the
     * uncompressed data.
     */
    if (gc->out == NULL)
        return (gc->reader(gc->userdata, buf, len));


    /* deflate() and ne_xml_parse() until all the currently available
     * input is consumed.  If the first invocation of inflate()
     * returns Z_DATA_ERROR, dispose of the buffer for uncompressed
     * data and assume the response is raw.
     */
    gc->strm.next_in = (void *)buf;
    gc->strm.avail_in = len;
    while (gc->strm.avail_in > 0 || gc->strm.avail_out == 0) {
        gc->strm.next_out = gc->out;
        gc->strm.avail_out = gc->len;

        total_in = gc->strm.total_in;
        ret = inflate(&gc->strm, Z_NO_FLUSH);

        if (ret == Z_OK || ret == Z_STREAM_END) {
            total_out = gc->len - gc->strm.avail_out;
#if 0 /* XXX To dump the raw response */
            size_t i;
            for (i = 0; i < total_out; i++)
                printf("%c", ((char *)gc->out)[i]);
#endif
            if (total_out > 0 && gc->reader(
                    gc->userdata, gc->out, total_out) != 0) {
                return (-1);
            }
            continue;
            
        } else if (total_in == 0 && ret == Z_DATA_ERROR) {
            free(gc->out);
            gc->len = 0;
            gc->out = NULL;
            return (gc->reader(gc->userdata, buf, len));

        } else {
            ne_set_error(gc->session, "%s", _gzip_get_error(&gc->strm, ret));
            return (-1);
        }
    }

    return (0);
}
