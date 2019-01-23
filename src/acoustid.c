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

#ifdef HAVE_CONFIG_H
#    include <config.h>
#endif

#include <sys/stat.h> // XXX For command-line comparison

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <unistd.h> // XXX For command-line comparison

#include <neon/ne_request.h>
#include <neon/ne_xml.h>

#include "acoustid.h"
#include "gzip.h"
#include "metadata.h" // XXX For second parser
#include "ratelimit.h"
#include "structures.h"

//#define DEBUG 1


/* The enumerated parser states
 *
 * These integers indicate the next state of the parser when returned
 * by the startelm() callback.  A return value of 0 is reserved for
 * the initial, root, state (NE_XML_STATEROOT == 0) and a value <0
 * signals error.  It follows that all states defined here must be >0.
 */
enum _parser_state
{
    _STATE_ROOT = NE_XML_STATEROOT,
    _STATE_FINGERPRINT,
    _STATE_FINGERPRINTS,
    _STATE_FINGERPRINT_INDEX,

    _STATE_MEDIUM,
    _STATE_MEDIUMS,
    _STATE_MEDIUM_POSITION,

    _STATE_RECORDING,
    _STATE_RECORDINGS,
    _STATE_RECORDING_ID,
    _STATE_RELEASE,
    _STATE_RELEASES,
    _STATE_RELEASE_ID,
    _STATE_RELEASEGROUP,
    _STATE_RELEASEGROUPS,
    _STATE_RELEASEGROUP_ID,
    _STATE_RESPONSE,
    _STATE_RESPONSE_STATUS,
    _STATE_RESULT,
    _STATE_RESULTS,
    _STATE_RESULT_ID,
    _STATE_RESULT_SCORE,

    _STATE_TRACK,
    _STATE_TRACKS,
    _STATE_TRACK_POSITION
};


/* The acoustid_context structure encapsulates the information needed
 * to match Chromaprint fingerprints against the AcoustID database and
 * maintain state while parsing the response
 */
struct acoustid_context
{
    /* URI-escaped Chromaprint fingerprints and durations to match
     * against the remote AcoustID database
     *
     * The _fingerprints string is the concatenation of the
     * fingerprints and the durations for all streams under
     * consideration, with the appropriate separators to be appended
     * to the POST data in the request.
     *
     * XXX Is this perhaps the query string (under construction)?
     */
    ne_buffer *fingerprints;

    /* Pointer to the neon session.
     *
     * The opaque neon session structure also stores the most recent
     * error message in human-readable form.
     *
     * XXX Must allow the caller to access the return value of
     * ne_get_error(ctx->session).
     */
    ne_session *session;

    /* For mapping indices
     */
    size_t *indices;

    /* Number of fingerprint-duration pairs in fingerprints
     */
    size_t nmemb;
};


/* The _userdata structure maintains state and current progress while
 * parsing the response from AcoustID.  Except for the cluster member,
 * which accumulates the final result, any non-NULL members are
 * subject to release when the parser returns.
 */
struct _userdata
{
    /* String buffer for the currently accumulated CDATA
     */
    ne_buffer *cdata;

    /* XML parser
     *
     * The XML parser is used for error reporting.  It is not owned by
     * the _userdata structure and must be freed externally.
     *
     * XXX Would be most spectacular if this were accessible from
     * within the handlers.
     */
    ne_xml_parser *parser;

#if 0
    /* The scored recordings, and their parent releases and
     * releasegroups, that match all indexed fingerprints
     */
    struct fp3_result *cluster;

    /* The scored recordings, and their parent releases and
     * releasegroups, that match the current fingerprint.
     */
    struct fp3_result *fingerprint;

    /* The releases, and their parent releasegroups, on which the
     * current recording is present.
     */
    struct fp3_result *recording;

    /* The recordings, and their parent releases and releasegroups,
     * that correspond to the current result, i.e. the current match
     * of the current fingerprint.
     */
    struct fp3_result *result;

    /* The ID of the current recording
     */
    char *id_recording;

    /* The number of recordings for the current result
     */
    size_t n_recording;

    /* The number of database matches, or results, for the current
     * fingerprint
     */
    size_t n_result;

    /* The one-based index of the current fingerprint in the range [1,
     * cluster->n_results]
     *
     * Now: zero-based in the range [0, ctx->nmemb - 1]
     */
    unsigned long index;

    /* The AcoustID score for the current match in the range [0, 1]
     */
    float score;
#endif

    /*** XXX ADDED ***/

    /* AcoustID context for accessing the indices array
     *
     * The indices are used for transforming, or permuting, the
     * AcoustID indices to indices used by the caller.  The AcoustID
     * context is not owned by the _userdata structure and must be
     * freed externally.
     */
    const struct acoustid_context *ctx;

    struct fp3_result *response_current;
    struct fp3_result *fingerprint_current;
    struct fp3_result *result_current;

    /* ID and score of current result (fingerprint).
     */
    struct fp3_fingerprint *result_data;

    struct fp3_releasegroup *releasegroup_current;
    struct fp3_release *release_current;

    /* Note that AcoustID does not deal with discs.
     */
    struct fp3_medium *medium_current;
    struct fp3_result *recording_current;
    struct fp3_recording *track_current;

    char *recording_id_current;
    size_t index_current;
};


/* The _userdata_new() function allocates a new _userdata structure
 * and initialises all its members.  If an error occurs
 * _userdata_new() returns @c NULL and sets the global variable @c
 * errno to indicate the error.
 *
 * @param nmemb Number of files/streams submitted for analysis
 * @return      New _userdata structure.  The returned pointer may
 *              subsequently be used as an argument to the function
 *              _userdata_free().
 */
static struct _userdata *
_userdata_new(const struct acoustid_context *ctx)
{
    struct _userdata *ud;
//    size_t i;

    ud = malloc(sizeof(struct _userdata));
    if (ud == NULL)
        return (NULL);


#if 0
    /* XXX Maybe fp3_new_result() could take a nmemb argument to
     * initialise the number of results (or is it fingerprints?) for
     * each file.
     */
    ud->cluster = fp3_new_result();
    if (ud->cluster == NULL) {
        free(ud);
        return (NULL);
    }

    ud->cluster->n_results = ctx->nmemb;
    ud->cluster->results = calloc(ud->cluster->n_results, sizeof(size_t));
    if (ud->cluster->results == NULL) {
        fp3_free_result(ud->cluster);
        free(ud);
        return (NULL);
    }
    for (i = 0; i < ud->cluster->n_results; i++)
        ud->cluster->results[i] = 0;
#endif

    ud->cdata = ne_buffer_create();

#if 0
    ud->parser = NULL;
    ud->fingerprint = NULL;
    ud->recording = NULL;
    ud->result = NULL;
    ud->id_recording = NULL;
#endif

    /*** XXX ADDED ***/
    ud->ctx = ctx;

    ud->response_current = NULL;
    ud->fingerprint_current = NULL;
    ud->result_current = NULL;
    ud->result_data = NULL;
    ud->releasegroup_current = NULL;
    ud->release_current = NULL;
    ud->medium_current = NULL;
    ud->recording_current = NULL;
    ud->track_current = NULL;

    ud->recording_id_current = NULL;
    ud->index_current = 0;

    return (ud);
}


/* The _userdata_finish() function frees the _userdata structure
 * pointed to by @p ud and returns the accumulated response.  If no
 * results were accumulated, _userdata_finish() returns a pointer to
 * an empty fp3_result structure.
 *
 * @note ud->parser is not destroyed by _userdata_finish(), because it
 *       is managed outside the _userdata module.
 *
 * @param ud XXX
 * @return   The accumulated response.  The returned pointer may
 *           subsequently be used as an argument to the function
 *           fp3_result_free().
 */
static struct fp3_result *
_userdata_finish(struct _userdata *ud)
{
    struct fp3_result *response;

    ne_buffer_destroy(ud->cdata);

#if 0
    if (ud->fingerprint != NULL)
        fp3_free_result(ud->fingerprint);

    if (ud->recording != NULL)
        fp3_free_result(ud->recording);

    if (ud->result != NULL)
        fp3_free_result(ud->result);

    if (ud->id_recording != NULL)
        free(ud->id_recording);
#endif
    /*** XXX ADDED ***/
    if (ud->fingerprint_current != NULL)
        fp3_free_result(ud->fingerprint_current);

    if (ud->result_current != NULL)
        fp3_free_result(ud->result_current);

    if (ud->result_data != NULL)
        fp3_free_fingerprint(ud->result_data);

    if (ud->releasegroup_current != NULL)
        fp3_free_releasegroup(ud->releasegroup_current);

    if (ud->release_current != NULL)
        fp3_free_release(ud->release_current);

    if (ud->medium_current != NULL)
        fp3_free_medium(ud->medium_current);

    if (ud->recording_current != NULL)
        fp3_free_result(ud->recording_current);

    if (ud->track_current != NULL)
        fp3_free_recording(ud->track_current);

    if (ud->recording_id_current != NULL)
        free(ud->recording_id_current);

    response = ud->response_current;
    free(ud);

    return (response);
}


/* The _catf() function appends a formatted string to the buffer
 * pointed to by @p buf.
 *
 * XXX Is this the only code in this module that directly modifies an
 * ne_buffer structure, i.e. is this the only occurrence of
 * ne_buffer_altered()?
 *
 * @param buf    Buffer to append to
 * @param format Format string according to printf(3)
 * @param ...    Format arguments according to printf(3)
 * @return       The number of characters written, not including the
 *               trailing @c NULL
 */
static size_t
_vcatf(ne_buffer *buf, const char *format, va_list ap)
{
    va_list ap_loop;
    size_t m, n;


    /* Try to append the formatted string, and stop if it all fits
     * with at least one character to spare--note that ne_vsnprint()
     * returns the number of characters printed, not including the
     * trailing '\0'.  Otherwise, grow the buffer by 4k (which is
     * slightly larger than a typical Chromaprint fingerprint XXX
     * check this), and try again.  Because the format is constant
     * throughout the iteration, the loop is guaranteed to terminate.
     */
    m = ne_buffer_size(buf);
    for ( ; ; ) {
        va_copy(ap_loop, ap);
        n = ne_vsnprintf(buf->data + m, buf->length - m, format, ap_loop);

        if (n + 1 < buf->length - m)
            break;
        ne_buffer_grow(buf, buf->length + 4096);
    }

    ne_buffer_altered(buf);
    return (ne_buffer_size(buf) - m);
}


static size_t
_catf(ne_buffer *buf, const char *format, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, format);
    ret = _vcatf(buf, format, ap);
    va_end(ap);

    return (ret);
}


/* XXX
 *
 * The formatted string may be truncated internally by
 * ne_xml_set_error().
 *
 * @param parser XXX
 * @param format XXX
 * @param ...    XXX
 */
static void
_ne_xml_set_error(ne_xml_parser *parser, const char *format, ...)
{
    va_list ap;
    ne_buffer *msg;

    msg = ne_buffer_create();
    va_start(ap, format);
    _vcatf(msg, format, ap);
    va_end(ap);

    ne_xml_set_error(parser, msg->data);
    ne_buffer_destroy(msg);
}


struct acoustid_context *
acoustid_new()
{
    struct acoustid_context *ctx;

    ctx = malloc(sizeof(struct acoustid_context));
    if (ctx == NULL)
        return (NULL);


    /* Because it is not clear whether ne_sock_init() sets errno on
     * failure, it is set to EIO here.  Each successful invocation of
     * ne_sock_init() must have a corresponding invocation of
     * ne_sock_exit().  ne_session_create() cannot fail.
     *
     * XXX Is neon_init() reference-counted?
     *
     * XXX Should allow proxies using ne_session_proxy(), look into
     * subversion code for an example.
     *
     * See also accuraterip_new() and accuraterip_free().
     */
    if (ne_sock_init() != 0) {
        free(ctx);
        errno = EIO;
        return (NULL);
    }

    ctx->fingerprints = ne_buffer_create();
    ctx->session = ne_session_create("http", "api.acoustid.org", 80);
    ne_set_useragent(ctx->session, PACKAGE_NAME "/" PACKAGE_VERSION);
    ctx->indices = NULL;
    ctx->nmemb = 0;

    return (ctx);
}


/* ne_session_destroy() and ne_sock_exit() cannot fail.  XXX Zap this
 * comment, and the thing about ne_session_create() above after
 * synchronising with accuraterip.
 */
void
acoustid_free(struct acoustid_context *ctx)
{
    ne_buffer_destroy(ctx->fingerprints);
    ne_session_destroy(ctx->session);
    ne_sock_exit();
    if (ctx->indices != NULL)
        free(ctx->indices);
    free(ctx);
}


/* XXX Could count the number of unmatched streams in this module as
 * well; I think that might make sense.
 */
int
acoustid_add_fingerprint(
    struct acoustid_context *ctx,
    const char *fingerprint,
    unsigned int duration,
    size_t index)
{
    void *p;

    _catf(ctx->fingerprints,
          "&duration.%zd=%u"
          "&fingerprint.%zd=%s",
          ctx->nmemb + 1, duration,
          ctx->nmemb + 1, fingerprint);

    p = realloc(ctx->indices, (ctx->nmemb + 1) * sizeof(size_t));
    if (p == NULL)
        return (-1);
    ctx->indices = p;
    ctx->indices[ctx->nmemb++] = index;

    return (0);
}


#if 0
/* The _intersect_releasegroups() function removes any releases and
 * releasegroups from @p dst that are not also present in @p src.
 * _intersect_releasegroups() does not modify @p src.
 *
 * @note @p src cannot be const, because the first argument of
 *       fp3_find_releasegroup cannot be const.
 */
static void
_intersect_releasegroups(struct fp3_result *dst, struct fp3_result *src)
{
    struct fp3_release *release_dst, *release_src;
    struct fp3_releasegroup *releasegroup_dst, *releasegroup_src;
    size_t i, j;


    for (i = 0; i < dst->nmemb; ) {
        releasegroup_dst = dst->releasegroups[i];
        releasegroup_src = fp3_find_releasegroup(src, releasegroup_dst->id);


        /* If releasegroup_dst does not exist in src, remove it from
         * dst.
         */
        if (releasegroup_src == NULL) {
            fp3_erase_releasegroup(dst, i);
            continue;
        }

        for (j = 0; j < releasegroup_dst->nmemb; ) {
            release_dst = releasegroup_dst->releases[j];
            release_src = fp3_find_release(releasegroup_src, release_dst->id);


            /* If release_dst does not exist in releasegroup_src,
             * remove it from releasegroup_dst.
             */
            if (release_src == NULL) {
                fp3_erase_release(releasegroup_dst, j);
                continue;
            }

            j++;
        }


        /* If releasegroup_dst is empty, possibly due to pruning its
         * releases above, remove it from dst.
         */
        if (releasegroup_dst->nmemb == 0) {
            fp3_erase_releasegroup(dst, i);
            continue;
        }

        i++;
    }
}
#endif


#if 0
/* The _move_recordings() function moves the recordings at index @p
 * index_src in @p result_src to @p index_dst in @p result_dst.  Any
 * recordings already stored at @p index_dst in @p result_dst are
 * kept, except duplicate recordings with lower scores, which are
 * overwritten.  Releasegroups and releases are created in @p
 * result_dst as necessary and @p result_src is cleared on successful
 * completion.
 *
 * @return 0 if successful or -1 on failure.  If an error occurs, the
 *         global variable @c errno is set to indicate the error.  XXX
 *         This is only true if the functions in the structures module
 *         set @c errno properly.
 */
static int
_move_recordings(struct fp3_result *result_dst,
                 struct fp3_result *result_src,
                 size_t index_src,
                 size_t index_dst)
{
    struct fp3_recording *recording_dst, *recording_src;
    struct fp3_recording_list *recordings_dst, *recordings_src;
    struct fp3_release *release_dst, *release_src;
    struct fp3_releasegroup *releasegroup_dst, *releasegroup_src;
    size_t i, j, k;


    /* If the pointer to the ID string of the destination is identical
     * to the corresponding pointer of the source, the source pointer
     * has to be set to NULL.  Otherwise fp3_clear_result() will free
     * a live pointer.
     */
    for (i = 0; i < result_src->nmemb; i++) {
        releasegroup_src = result_src->releasegroups[i];
        releasegroup_dst = fp3_result_add_releasegroup_by_id(
            result_dst, releasegroup_src->id);
        if (releasegroup_dst == NULL)
            return (-1);

        if (releasegroup_dst->id == releasegroup_src->id)
            releasegroup_src->id = NULL;

        for (j = 0; j < releasegroup_src->nmemb; j++) {
            release_src = releasegroup_src->releases[j];
            release_dst = fp3_add_release_by_id(
                releasegroup_dst, release_src->id);
            if (release_dst == NULL)
                return (-1);

            if (release_dst->id == release_src->id)
                release_src->id = NULL;

            recordings_src = release_src->streams[index_src];
            recordings_dst = fp3_add_recording_list(release_dst, index_dst);
            if (recordings_dst == NULL)
                return (-1);

            for (k = 0; k < recordings_src->nmemb; k++) {
                recording_src = recordings_src->recordings[k];
                recording_dst = fp3_recording_list_add_recording_by_id(
                    recordings_dst, recording_src->id);
                if (recording_dst == NULL)
                    return (-1);

                if (recording_dst->id == recording_src->id)
                    recording_src->id = NULL;

                if (!isfinite(recording_dst->score) ||
                    recording_dst->score > 1 ||
                    recording_dst->score < recording_src->score) {
                    recording_dst->score = recording_src->score;
                }
            }
        }
    }

    fp3_clear_result(result_src);
    return (0);
}
#endif


/* XXX What about errno here?  Check all returns if necessary!  This
 * should really be part of the acoustid namespace.
 *
 * @param ... NULL-terminated meta strings (XXX or is it perhaps
 *            keywords or some such--check AcoustID documentation),
 *            must be followed by a NULL argument marking the end of
 *            the list
 */
static int
_dispatch(struct acoustid_context *ctx, ne_xml_parser *parser, ...)
{
    va_list ap;
    struct gzip_context *gc;
    ne_buffer *query;
    ne_request *request;
    char *meta;
    void *query_gzip;
    size_t i, size_gzip;
    int ret;


    /* Create the query string.  The caller-supplied meta elements are
     * URI-escaped; the fingerprints and the remaining constant
     * strings are guaranteed to be valid.
     *
     * XXX Here's the same 4096 approximation as in _catf()!
     *
     * XXX Should we not check that the server actually accepts
     * gzip-compressed data before feeding it?
     */
    query = ne_buffer_ncreate(512 + ctx->nmemb * 4096);
    _catf(
        query, "batch=%zd&client=" ACOUSTID_CLIENT "&format=xml", ctx->nmemb);

    va_start(ap, parser);
    for (i = 0; ; i++) {
        meta = va_arg(ap, char *);
        if (meta == NULL)
            break;
        meta = ne_path_escape(meta);
        _catf(query, i == 0 ? "&meta=%s" : "+%s", meta);
        free(meta);
    }
    va_end(ap);
    ne_buffer_zappend(query, ctx->fingerprints->data);

    query_gzip = NULL;
    size_gzip = 0;
    if (gzip_deflate(ctx->session,
                     query->data,
                     ne_buffer_size(query),
                     &query_gzip,
                     &size_gzip) != 0) {
        if (query_gzip != NULL)
            free(query_gzip);
        ne_buffer_destroy(query);
        return (-1);
    }

    printf("compressed query "
           "[%ld -> %lu, compression ratio %.2f, %zd fingerprints]\n",
           ne_buffer_size(query), size_gzip,
           1.0f * ne_buffer_size(query) / size_gzip,
           ctx->nmemb);


    /* XXX The compression ratio is often rather bad, ~1.35.  Is there
     * something wrong?
     */
    {
        struct stat sb;
        FILE *f;

        f = fopen("/tmp/t.dat", "w");
        fwrite(query->data,
               sizeof(char),
               ne_buffer_size(query),
               f);
        fclose(f);
        system("gzip /tmp/t.dat");
        stat("/tmp/t.dat.gz", &sb);
        printf("compressed query "
               "[%ld -> %lu, compression ratio %.2f, %zd fingerprints]\n"
               "                 diff to command line: %zd %zd [%zd]\n",
               query->used - 1, size_gzip,
               1.0f * (query->used - 1) / size_gzip, ctx->nmemb,
               (size_t)sb.st_size, size_gzip, (size_t)(sb.st_size - size_gzip));
        unlink("/tmp/t.dat.gz");
    }
    ne_buffer_destroy(query);


    /* Create a new request object for the non-idempotent POST method
     * and ask for gzip-compressed responses.  Only attempt to parse
     * successfully retrieved responses.  None of the neon-functions
     * in this block can fail.
     */
    gc = gzip_new(ctx->session, ne_xml_parse_v, parser);
    if (gc == NULL) {
        free(query_gzip);
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (-1);
    }

    request = ne_request_create(ctx->session, "POST", "/v2/lookup");
    ne_set_request_body_buffer(request, query_gzip, size_gzip);
    ne_add_request_header(request,
                          "Accept-Encoding",
                          "gzip");
    ne_add_request_header(request,
                          "Content-Encoding",
                          "gzip");
    ne_add_request_header(request,
                          "Content-Type",
                          "application/x-www-form-urlencoded");
    ne_add_response_body_reader(
        request, ne_accept_2xx, gzip_inflate_reader, gc);
    ne_set_request_flag(request, NE_REQFLAG_IDEMPOTENT, 0);


    /* Ensure no more requests than are allowed are dispatched per
     * unit time.
     */
    if (ratelimit_acoustid() != 0) {
        gzip_free(gc);
        ne_request_destroy(request);
        free(query_gzip);
        return (-1);
    }

    ret = ne_request_dispatch(request);
    free(query_gzip);


    /* Destroy the dispatched request, because it cannot be used
     * again.  Note that the return value of ne_get_status() is only
     * valid if ne_request_dispatch() returns NE_OK.  Either the
     * XML-parser failed, or there were server or proxy server
     * authentication errors, failures to establish connection,
     * timeouts, or non-2xx responses.
     *
     * XXX Retry in case of HTTP failure?  Probably not.
     */
    if (ret != NE_OK || ne_get_status(request)->klass != 2) {
        if (ret == NE_ERROR || ne_xml_failed(parser) != 0) {
            ne_set_error(
                ctx->session, "XML error: %s", ne_xml_get_error(parser));
        }

        printf("SESSION STATUS: ->%s<- klass %d\n", ne_get_error(ctx->session),
               ne_get_status(request)->klass);

        ne_request_destroy(request);
        gzip_free(gc);

        return (-1);
    }

    ne_request_destroy(request);
    if (gzip_free(gc) != 0) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (-1);
    }

    return (0);
}


/* The _cb_startelm() function returns the new state of the parser
 * based on its current state and the name of the starting element,
 * and performs any initialisation required by the state change.
 *
 * Omitting elements which only serve to collect one or more child
 * elements of a particular kind (fingerprints, results, etc.), the
 * response returned by the AcoustID server is structured as follows:
 *
 *   Element              | Extracted CDATA
 *   ---------------------------------------
 *   response             | status
 *     fingerprint        | index
 *       result           | score
 *         recording      | id
 *           releasegroup | id
 *             release    | id
 *               medium   | position
 *                 track  | position
 *
 * where the indentation in the element column indicates the nesting
 * of the XML response.  The parsed response is constructed mainly
 * when the respective ending elements are encountered in
 * _cb_endelm():
 *
 *   When a release ID element is ended, a new release with the given
 *   ID is appended to to the most recently added releasegroup of the
 *   current recording, unless it is inconsistent with the previously
 *   accumulated results.  For this to work, a dummy recording and
 *   releasegroup must be created when the respective elements are
 *   started.
 *
 *   When a releasegroup ID element is ended, the ID is assigned to
 *   the most recently added releasegroup of the current recording.
 *   When a releasegroup element is ended, it is immediately pruned if
 *   it is inconsistent with the previously accumulated results.
 *
 *   When a recording element is ended, its ID is assigned at index 0
 *   of all accumulated releases, and the recordings are merged with
 *   the previous recordings for the current result.
 *
 *   When a result element is ended, the score is assigned to all
 *   recordings, and the recordings are merged with the previous
 *   recordings for the current fingerprint.
 *
 *   When a fingerprint element is ended, the scored recordings, their
 *   parent releases and releasegroups are moved to the appropriate
 *   index in the list of accumulated fingerprints.  Only the
 *   intersection of already present releases and new releases is
 *   kept.
 *
 * When the XML parser is first started, the userdata's
 * cluster->n_results member must be set to the number of submitted
 * fingerprints, and the cluster->results member be allocated to hold
 * at least cluster->n_results elements.
 *
 * @note Result ID:s are declined because they are not stored in the
 *       fp3_recording structure.  XXX Now, I definitely think they
 *       should!  It's perhaps better termed fingerprint ID.
 *
 * NEW COMMENTS:
 *
 *   Have to allocated the structures when the elements are opened.
 *   It's inelegant to allocate them when they are closed, because
 *   they may be needed whenever any of their child elements are
 *   closed.
 *
 */
static int
_cb_startelm(void *userdata,
             int parent,
             const char *nspace,
             const char *name,
             const char **atts)
{
    struct _userdata *ud;

//    printf("Starting element '%s'\n", name);

    ud = (struct _userdata *)userdata;
    switch (parent) {
    case NE_XML_STATEROOT:
        if (strcmp(name, "response") == 0) {
            // Reuse old structure, if present.
            if (ud->response_current == NULL) {
                ud->response_current = fp3_new_result();
                if (ud->response_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_RESPONSE);
        }
        break;

    case _STATE_FINGERPRINT:
        if (strcmp(name, "index") == 0)
            return (_STATE_FINGERPRINT_INDEX);
        if (strcmp(name, "results") == 0)
            return (_STATE_RESULTS);
        break;

    case _STATE_FINGERPRINTS:
        if (strcmp(name, "fingerprint") == 0) {
#if 0
            ud->index = 0;
            ud->n_result = 0;
#endif
            // Reuse old structure, if present.
            if (ud->fingerprint_current == NULL) {
                ud->fingerprint_current = fp3_new_result();
                if (ud->fingerprint_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_FINGERPRINT);
        }
        break;

    case _STATE_MEDIUM:
        if (strcmp(name, "format") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "position") == 0)
            return (_STATE_MEDIUM_POSITION);
        if (strcmp(name, "title") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "track_count") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "tracks") == 0)
            return (_STATE_TRACKS);
        break;

    case _STATE_MEDIUMS:
        if (strcmp(name, "medium") == 0) {
            // Reuse old structure, if present.
            if (ud->medium_current == NULL) {
                ud->medium_current = fp3_new_medium();
                if (ud->medium_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_MEDIUM);
        }
        break;

    case _STATE_RECORDING:
        if (strcmp(name, "id") == 0)
            return (_STATE_RECORDING_ID);
        if (strcmp(name, "releasegroups") == 0)
            return (_STATE_RELEASEGROUPS);
        break;

    case _STATE_RECORDINGS:
        if (strcmp(name, "recording") == 0) {
#if 0
            if (ud->recording == NULL) {
                ud->recording = fp3_new_result();
                if (ud->recording == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
                ud->n_recording += 1;
            }
#endif
            // Reuse old structure, if present
            if (ud->recording_current == NULL) {
                ud->recording_current = fp3_new_result();
                if (ud->recording_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_RECORDING);
        }
        break;

    case _STATE_RELEASE:
        if (strcmp(name, "id") == 0)
            return (_STATE_RELEASE_ID);
        if (strcmp(name, "mediums") == 0)
            return (_STATE_MEDIUMS);
        break;

    case _STATE_RELEASES:
        if (strcmp(name, "release") == 0) {
            // Reuse old structure, if present.
            if (ud->release_current == NULL) {
                ud->release_current = fp3_new_release();
                if (ud->release_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_RELEASE);
        }
        break;

    case _STATE_RELEASEGROUP:
        if (strcmp(name, "id") == 0)
            return (_STATE_RELEASEGROUP_ID);
        if (strcmp(name, "releases") == 0)
            return (_STATE_RELEASES);
        break;

    case _STATE_RELEASEGROUPS:
        if (strcmp(name, "releasegroup") == 0) {
#if 0
            if (fp3_result_add_releasegroup_by_id(
                    ud->recording, NULL) == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
#endif
            // Reuse old structure, if present.
            if (ud->releasegroup_current == NULL) {
                ud->releasegroup_current = fp3_new_releasegroup();
                if (ud->releasegroup_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_RELEASEGROUP);
        }
        break;

    case _STATE_RESPONSE:
        if (strcmp(name, "fingerprints") == 0)
            return (_STATE_FINGERPRINTS);
        if (strcmp(name, "status") == 0)
            return (_STATE_RESPONSE_STATUS);
        break;

    case _STATE_RESULT:
        if (strcmp(name, "id") == 0)
            return (_STATE_RESULT_ID);
        if (strcmp(name, "recordings") == 0)
            return (_STATE_RECORDINGS);
        if (strcmp(name, "score") == 0)
            return (_STATE_RESULT_SCORE);
        break;

    case _STATE_RESULTS:
        if (strcmp(name, "result") == 0) {
#if 0
            ud->n_recording = 0;
            ud->n_result += 1;
            ud->score = NAN;
#endif
            // Reuse old structures if present.
            if (ud->result_current == NULL) {
                ud->result_current = fp3_new_result();
                if (ud->result_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            if (ud->result_data == NULL) {
                ud->result_data = fp3_new_fingerprint();
                if (ud->result_data == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_RESULT);
        }
        break;

    case _STATE_TRACK:
        if (strcmp(name, "artists") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "id") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "position") == 0)
            return (_STATE_TRACK_POSITION);
        if (strcmp(name, "title") == 0)
            return (NE_XML_DECLINE);
        break;

    case _STATE_TRACKS:
        if (strcmp(name, "track") == 0) {
            // Reuse old structure if present.
            if (ud->track_current == NULL) {
                ud->track_current = fp3_new_recording();
                if (ud->track_current == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            return (_STATE_TRACK);
        }
        break;
    }

    _ne_xml_set_error(
        ud->parser, "Element \"%s\" unexpected in state %d", name, parent);

    return (NE_XML_ABORT);
}


/* The _cb_cdata() callback appends the @p len characters pointed to
 * by @p cdata to the CDATA buffer in the _userdata structure pointed
 * to by @p userdata.  A neon block boundary will occasionally fall
 * within the CDATA, and this results in multiple calls to _cb_cdata()
 * without any intervening calls to _cb_startelm() or _cb_endelm().
 *
 * XXX Cannot seem to reproduce this: alternative documentation:
 *
 * The _cb_cdata() callback appends the @p len octets pointed to by @p
 * cdata to the CDATA buffer in the _userdata structure pointed to by
 * @p userdata, irrespective of the parser state.
 */
static int
_cb_cdata(void *userdata, int state, const char *cdata, size_t len)
{
    if (ne_buffer_size(((struct _userdata *)userdata)->cdata) > 0) {
        printf("    **** EXISTING CDATA ****\n"); // XXX
        exit(0);
    }

    ne_buffer_append(((struct _userdata *)userdata)->cdata, cdata, len);
    return (0);
}


#if 0
/* The _last_releasegroup() function returns a pointer to the last
 * releasegroup of the result pointed to by @p result.  If @p result
 * does not contain any releasegroups, @c NULL is returned and the
 * global variable @c errno is set to @c EFAULT.
 *
 * @param result The accumulated releases and their parent
 *               releasegroups
 * @return       A pointer to the last releasegroup
 */
static struct fp3_releasegroup *
_last_releasegroup(struct fp3_result *result)
{
    if (result->nmemb > 0)
        return (result->releasegroups[result->nmemb - 1]);
    errno = EFAULT;
    return (NULL);
}
#endif


#if 0
/* The _is_consistent_release() function checks whether the release
 * identified by @p id is consistent with the information accumulated
 * in @p result.  To be consistent, a release must have a valid ID,
 * and must be present in the accumulated results, unless all
 * previously observed fingerprints did not yield any matches.
 *
 * @param result The accumulated releases and their parent
 *               releasegroups
 * @param id     The ID of the current release
 * @return       Non-zero if the release is consistent, 0 otherwise
 */
static int
_is_consistent_release(struct fp3_result *result, const char *id)
{
    size_t i;

    if (id == NULL)
        return (0);

    if (fp3_result_find_release(result, id) == NULL) {
        for (i = 0; i < result->n_results; i++) {
            if (result->results[i] > 0)
                return (0);
        }
    }

    return (1);
}
#endif


#if 0
/* The _is_consistent_releasegroup() function checks whether the
 * releasegroup pointed to by @p releasegroup is consistent with the
 * information accumulated in @p result.  To be consistent, a
 * releasegroup must have a valid ID, have at least one release, and
 * be present in the accumulated results, unless all previously
 * observed fingerprints did not yield any matches.
 *
 * @param result       The accumulated releases and their parent
 *                     releasegroups
 * @param releasegroup The releasegroup and its releases
 * @return             Non-zero if the releasegroup is consistent, 0
 *                     otherwise
 */
static int
_is_consistent_releasegroup(
    struct fp3_result *result, struct fp3_releasegroup *releasegroup)
{
    size_t i;

    if (releasegroup == NULL ||
        releasegroup->id == NULL ||
        releasegroup->nmemb == 0) {
        return (0);
    }

    if (fp3_find_releasegroup(result, releasegroup->id) == NULL) {
        for (i = 0; i < result->n_results; i++) {
            if (result->results[i] > 0)
                return (0);
        }
    }

    return (1);
}
#endif


/* For consistency in error reporting, this function should probably
 * not call ne_xml_set_error().  XXX ne_buffer_clear() does not change
 * the data pointer, but this is undocumented behaviour.
 */
static int
_cdatatof(struct _userdata *userdata, float *dst)
{
    char *ep;

    *dst = strtof(userdata->cdata->data, &ep);
    ne_buffer_clear(userdata->cdata);
    if (*ep != '\0' || ep == userdata->cdata->data) {
        errno = EDOM;
        return (-1);
    }

    if (*dst == -HUGE_VAL || *dst == +HUGE_VAL)
        return (-1);
    else if (*dst == 0 && errno == ERANGE)
        return (-1);

    return (0);
}


static int
_cdatatol(struct _userdata *userdata, long int *dst)
{
    char *ep;

    *dst = strtoul(userdata->cdata->data, &ep, 10);
    ne_buffer_clear(userdata->cdata);
    if (*ep != '\0' || ep == userdata->cdata->data) {
        errno = EDOM;
        return (-1);
    }

    if (errno == ERANGE && (*dst == LONG_MIN || *dst == LONG_MAX))
        return (-1);

    return (0);
}


static int
_cdatatostr(struct _userdata *userdata, char **dst)
{
    *dst = strdup(userdata->cdata->data);
    ne_buffer_clear(userdata->cdata);
    if (*dst == NULL)
        return (-1);
    return (0);
}


static int
_assign_recording_index(struct fp3_result *result, size_t index)
{
//    struct fp3_disc *disc;
    struct fp3_fingerprint *fingerprint;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
//    struct fp3_recording_list *track;
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    struct fp3_stream *stream;
    size_t i, j, k, l, m, n; //, o;


    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];

            for (k = 0; k < release->nmemb_media; k++) {
                medium = release->media[k];

#if 0
                if (medium == NULL)
                    continue;

                for (l = 0; l < medium->nmemb_discs; l++) {
                    disc = medium->discs[l];

                    for (m = 0; m < disc->nmemb; m++) {
                        track = disc->tracks[m];

                        for (n = 0; n < track->nmemb; n++) {
                            recording = track->recordings[n];
//                            recording->position_stream = index;

                            for (o = 0; o < recording->nmemb; o++) {
                                fingerprint = recording->fingerprints[o];
                                fingerprint->index = index;
                            }
                        }
                    }
                }
#else
                for (l = 0; l < medium->nmemb_tracks; l++) {
                    recording = medium->tracks[l];

                    for (m = 0; m < recording->nmemb; m++) {
                        fingerprint = recording->fingerprints[m];
//                        fingerprint->index = index;

                        for (n = 0; n < fingerprint->nmemb; n++) {
                            stream = fingerprint->streams[n];
                            stream->index = index;
                        }
                    }
                }
#endif
            }
        }
    }

    return (0);
}


// XXX There should not be any fingerprint ID:s or scores present!
static int
_assign_recording_fingerprint(
    struct fp3_result *result, const struct fp3_fingerprint *fingerprint)
{
//    struct fp3_disc *disc;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
//    struct fp3_recording_list *track;
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    size_t i, j, k, l; //, m, n;


    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];

            for (k = 0; k < release->nmemb_media; k++) {
                medium = release->media[k];
#if 0
                if (medium == NULL)
                    continue;

                for (l = 0; l < medium->nmemb_discs; l++) {
                    disc = medium->discs[l];

                    for (m = 0; m < disc->nmemb; m++) {
                        track = disc->tracks[m];

                        for (n = 0; n < track->nmemb; n++) {
                            recording = track->recordings[n];

                            if (recording->nmemb != 0) { // XXX
                                printf("THIS IS WEIRD %zd\n", recording->nmemb);
                                size_t o;
                                for (o = 0; o < recording->nmemb; o++) {
                                    printf("FINGERPRINT %s %f\n",
                                           recording->fingerprints[o]->id,
                                           recording->fingerprints[o]->score);
                                }
                                sleep(10);
                            }

                            if (fp3_recording_add_fingerprint(
                                    recording, fingerprint) == NULL) {
                                return (-1);
                            }
                        }
                    }
                }
#else
                for (l = 0; l < medium->nmemb_tracks; l++) {
                    recording = medium->tracks[l];

                    if (fp3_recording_add_fingerprint(
                            recording, fingerprint) == NULL) {
                        return (-1);
                    }
                }
#endif
            }
        }
    }

    return (0);
}


static int
_assign_recording_id(struct fp3_result *result, const char *id)
{
//    struct fp3_disc *disc;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
//    struct fp3_recording_list *track;
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    size_t i, j, k, l; //, m, n;


    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];

            for (k = 0; k < release->nmemb_media; k++) {
                medium = release->media[k];

#if 0
                for (l = 0; l < medium->nmemb_discs; l++) {
                    disc = medium->discs[l];

                    for (m = 0; m < disc->nmemb; m++) {
                        track = disc->tracks[m];

                        for (n = 0; n < track->nmemb; n++) {
                            recording = track->recordings[n];

                            if (recording->id == NULL) {
                                recording->id = strdup(id);
                                if (recording->id == NULL)
                                    return (-1);
                            }
                        }
                    }
                }
#else
                for (l = 0; l < medium->nmemb_tracks; l++) {
                    recording = medium->tracks[l];

                    if (recording->id != NULL) {
                        printf("This is weird\n");
                        sleep(10);
                        free(recording->id);
                    }
                    recording->id = strdup(id);
                    if (recording->id == NULL)
                        return (-1);
                }
#endif
            }
        }
    }

    return (0);
}


/* The _cb_endelm() function populates the relevant data structures
 * pointed to by members of the userdata structure pointed to by @p
 * userdata with information extracted from the element being ended.
 * To avoid memory leakage, it is important to invalidate any pointers
 * as the data they point is migrated between the structures.  The
 * function return NE_XML_ABORT on error to abort the parse.
 */
static int
_cb_endelm(void *userdata, int state, const char *nspace, const char *name)
{
//    struct fp3_recording *recording3;
//    struct fp3_recording_list *recordings3;
//    struct fp3_release *release3;
//    struct fp3_releasegroup *releasegroup3;
    struct _userdata *ud;
//    char *ep, *id;
//    size_t i, j, k, l;
    long int l;
    float f;

//    printf("Ending element '%s'\n", name);

    ud = (struct _userdata *)userdata;
    switch (state) {
    case _STATE_FINGERPRINT:
#ifdef DEBUG
        // XXX or maybe Stream as in dump()?
        printf("Fingerprint %zd\n", ud->index_current);
#endif
#if 0
        /* Update the number of results for this index.  If the
         * fingerprint is not linked to any results, no further action
         * is taken.  If all results were pruned, the releases for the
         * current fingerprint do not overlap with the accumulated
         * releases, and the accumulated releases are cleared.
         */
        ud->cluster->results[ud->index - 1] = ud->n_result;
        if (ud->n_result == 0)
            break;

        if (ud->fingerprint->nmemb == 0) {
            fp3_clear_result(ud->cluster);
            break;
        }


        /* The releases for the fingerprint are guaranteed to be a
         * subset of the accumulated releases.  Prune the accumulated
         * releases to those of the fingerprint, and move the
         * recordings for the fingerprint to the accumulated
         * recordings at their zero-based index.
         */
        _intersect_releasegroups(ud->cluster, ud->fingerprint);
        if (_move_recordings(
                ud->cluster, ud->fingerprint, 0, ud->index - 1) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
#endif
        // Assign the index to all matches, and merge with final
        // response.
        if (_assign_recording_index(
                ud->fingerprint_current,
                ud->ctx->indices[ud->index_current - 1]) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        if (fp3_result_merge(
                ud->response_current, ud->fingerprint_current) == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_result(ud->fingerprint_current);
        break;


    case _STATE_FINGERPRINT_INDEX:
        // Parse the index of the fingerprint and determine the
        // corresponding index supplied by the caller.  Cannot assign
        // it to the recordings yet, because all recordings
        // corresponding to the fingerprint may not have been seen
        // yet.  Must check the bounds on the index because it is used
        // to determine the caller-supplied index, and that may
        // segfault.
        if (_cdatatol(ud, &l) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        } else if (l < 1 || l > ud->ctx->nmemb) {
            _ne_xml_set_error(
                ud->parser,
                "Index \"%lu\" outside range [1, %zd]",
                l, ud->ctx->nmemb);
            return (NE_XML_ABORT);
        }
//        ud->index = l;
        ud->index_current = l;
        break;


    case _STATE_MEDIUM:
#ifdef DEBUG
        printf("          Medium position %ld\n", ud->medium_current->position);
#endif
        // Add the medium to the current release, now that its
        // position is known.
        if (fp3_release_add_medium(
                ud->release_current, ud->medium_current) == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_medium(ud->medium_current);
        break;


    case _STATE_MEDIUM_POSITION:
        // The medium position must be larger than zero, because zero
        // indicates that the position has not been assigned.  The
        // upper bound cannot be checked, because there is no
        // release/medium_count element.
        if (_cdatatol(ud, &l) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        } else if (l < 1) {
            _ne_xml_set_error(ud->parser, "Position \"%ld\" < 1", l);
            return (NE_XML_ABORT);
        }
        ud->medium_current->position = l;
        break;


    case _STATE_RECORDING:
#ifdef DEBUG
        printf("    Recording ID %s\n", ud->recording_id_current);
#endif
#if 0
        /* Disregard the recording if it does not contain at least one
         * releasegroup or lacks an ID.  Otherwise, assign the ID at
         * index 0 to all recordings of the accumulated releases.
         * Then merge the recordings with those previously accumulated
         * for the result.
         */
        if (ud->recording->nmemb == 0 || ud->id_recording == NULL) {
            if (ud->id_recording != NULL) {
                free(ud->id_recording);
                ud->id_recording = NULL;
            }
            break;
        }

        for (i = 0; i < ud->recording->nmemb; i++) {
            releasegroup3 = ud->recording->releasegroups[i];

            for (j = 0; j < releasegroup3->nmemb; j++) {
                release3 = releasegroup3->releases[j];

                recordings3 = fp3_add_recording_list(release3, 0);
                if (recordings3 == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }

                if (_cdatatostr(ud->id_recording, &id) != 0) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }

                recording3 = fp3_recording_list_add_recording_by_id(
                    recordings3, id);
                if (recording3 == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
        }

        free(ud->id_recording);
        ud->id_recording = NULL;

        if (ud->result == NULL) {
            ud->result = fp3_new_result();
            if (ud->result == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }

        if (_move_recordings(ud->result, ud->recording, 0, 0) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
#endif
        // Assign the ID to all recordings under the current result
        // (which is a list of releasegroups) unless they already have
        // one.
        if (_assign_recording_id(
                ud->recording_current, ud->recording_id_current) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        if (fp3_result_merge(
                ud->result_current, ud->recording_current) == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_result(ud->recording_current);
        break;


    case _STATE_RECORDING_ID:
        // Cannot assign the recording ID to the ud->recording_current
        // yet, because all releasegroups containing the recording may
        // not have been seen yet.
        if (_cdatatostr(ud, &ud->recording_id_current) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        break;


    case _STATE_RELEASE:
#ifdef DEBUG
        printf("        Release ID %s\n", ud->release_current->id);
#endif
        // Add the release to the current releasegroup.
        if (fp3_releasegroup_add_release(
                ud->releasegroup_current, ud->release_current) == 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_release(ud->release_current);
        break;


    case _STATE_RELEASE_ID:
#if 0
        /* Disregard the release with the given ID if it is
         * inconsistent with the accumulated results.  Otherwise, add
         * a new release with the given ID to the current releasegroup
         * of the recording.
         */
        if (_cdatatostr(ud, &id) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);

        if (_is_consistent_release(ud->cluster, id) == 0) {
            free(id);
            break;
        }

        releasegroup3 = _last_releasegroup(ud->recording);
        if (releasegroup3 == NULL)  {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        release3 = fp3_add_release_by_id(releasegroup3, id);
        if (release3 == NULL)  {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        if (release3->id != id)
            free(id);
#endif
        // Assign the ID to the current release.
        if (_cdatatostr(ud, &ud->release_current->id) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        break;


    case _STATE_RELEASEGROUP:
#ifdef DEBUG
        printf("      Releasegroup ID %s\n", ud->releasegroup_current->id);
#endif
#if 0
        /* Disregard current releasegroup if it is inconsistent with
         * the accumulated information.
         */
        releasegroup3 = _last_releasegroup(ud->recording);
        if (releasegroup3 == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        if (_is_consistent_releasegroup(ud->cluster, releasegroup3) == 0) {
            fp3_erase_releasegroup(ud->recording, ud->recording->nmemb -1);
            break;
        }
#endif
        // Add the releasegroup to the results accumulated for the
        // current recording.
        if (fp3_result_add_releasegroup(
                ud->recording_current, ud->releasegroup_current) == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_releasegroup(ud->releasegroup_current);
        break;


    case _STATE_RELEASEGROUP_ID:
#if 0
        releasegroup3 = _last_releasegroup(ud->recording);
        if (releasegroup3 == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        if (_cdatatostr(ud, &releasegroup3->id) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
#endif
        // Assign the ID to the current releasegroup.
        if (_cdatatostr(ud, &ud->releasegroup_current->id) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        break;


    case _STATE_RESPONSE:
        /* If necessary, append empty recordings such that each
         * release has ud->cluster->n_results recordings.
         */
#if 0
        for (i = 0; i < ud->cluster->nmemb; i++) {
            releasegroup3 = ud->cluster->releasegroups[i];

            for (j = 0; j < releasegroup3->nmemb; j++) {
                release3 = releasegroup3->releases[j];

                for (k = release3->nmemb; k < ud->cluster->n_results; k++) {
                    if (fp3_add_recording_list(release3, k) == 0) {
                        ne_xml_set_error(ud->parser, strerror(errno));
                        return (NE_XML_ABORT);
                    }
                }
            }
        }
#endif
        break;


    case _STATE_RESPONSE_STATUS:
        if (strcmp(ud->cdata->data, "ok") != 0) {
            _ne_xml_set_error(
                ud->parser,
                "AcoustID look-up failed with status \"%s\"",
                ud->cdata->data);
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        break;


    case _STATE_RESULT:
#ifdef DEBUG
        printf("  Result ID %s score %f\n",
               ud->result_data->id, ud->result_data->score);
#endif
#if 0
        /* Assign the AcoustID score to all recordings of this match,
         * then merge them with the recordings for any previous
         * matches.  A match in the AcoustID database does not
         * necessarily mean that the fingerprint is linked to any
         * entries in MusicBrainz.  In such cases, the fingerprint's
         * result nodes do not contain any recordings, and the result
         * is discarded.
         *
         * See
         * https://groups.google.com/forum/#!topic/acoustid/UL_m_4ys3pU
         * for more information.
         */
        if (ud->n_recording == 0)
            ud->n_result -= 1;

        if (ud->result != NULL) { // XXX THIS CHECK ADDED 2015-05-28
        for (i = 0; i < ud->result->nmemb; i++) {
            releasegroup3 = ud->result->releasegroups[i];
            for (j = 0; j < releasegroup3->nmemb; j++) {
                release3 = releasegroup3->releases[j];
                for (k = 0; k < release3->nmemb; k++) {
                    recordings3 = release3->streams[k];
                    for (l = 0; l < recordings3->nmemb; l++) {
                        recording3 = recordings3->recordings[l];
                        recording3->score = ud->score;
                    }
                }
            }
        }
        }

        if (ud->fingerprint == NULL) {
            ud->fingerprint = fp3_new_result();
            if (ud->fingerprint == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }

        if (ud->result != NULL) { // XXX THIS CHECK ADDED 2015-05-28
            if (_move_recordings(ud->fingerprint, ud->result, 0, 0) != 0) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }
#endif
        // Add the result ID and the score to all recordings under the
        // current result.
        if (_assign_recording_fingerprint(
                ud->result_current, ud->result_data) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        if (fp3_result_merge(
                ud->fingerprint_current, ud->result_current) == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_result(ud->result_current);
        break;


    case _STATE_RESULT_ID:
        // Assign the result ID to the current fingerprint.
        if (_cdatatostr(ud, &ud->result_data->id) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        break;


    case _STATE_RESULT_SCORE:
        // Assign the score to the current fingerprint.
        if (_cdatatof(ud, &f) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        } else if (f < 0 || f > 1) {
            _ne_xml_set_error(
                ud->parser, "Score \"%f\" outside range [0, 1]", f);
            return (NE_XML_ABORT);
        }
//        ud->result_data->score = f; // XXX Will this actually

        // XXX I don't think this the correct place to add the stream.
        {
            struct fp3_stream *stream;
            stream = fp3_new_stream();
            stream->score = f;
            fp3_fingerprint_add_stream(ud->result_data, stream); // XXX Could fail!
        }
        break;


    case _STATE_TRACK:
#ifdef DEBUG
        printf("            Track position %ld\n", ud->track_current->position);
#endif
        // Add a recording corresponding to the track to the current
        // medium, now that its position is known.
        if (fp3_medium_add_recording(
                ud->medium_current, ud->track_current) == NULL) {
            _ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        fp3_clear_recording(ud->track_current);
        break;


    case _STATE_TRACK_POSITION:
        // The track position must be larger than zero, because zero
        // indicates that the position has not been assigned.  The
        // upper bound cannot be checked, because medium/track_count
        // may not yet have been seen.
        if (_cdatatol(ud, &l) != 0) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        } else if (l < 1) {
            _ne_xml_set_error(ud->parser, "Position \"%ld\" < 1", l);
            return (NE_XML_ABORT);
        }
        ud->track_current->position = l;
        break;
    }


    /* The CDATA buffer should be empty at this point.  Deal with it
     * gracefully if it is not.
     */
    if (ne_buffer_size(ud->cdata) > 0) {
#ifdef DEBUG
        printf("CDATA buffer not empty in state %d\n", state);
#endif
        ne_buffer_clear(ud->cdata);
    }


    /* Cannot return NE_XML_ABORT here, because not all elements may
     * require special action when ended.
     */
    return (0);
}


struct fp3_result *
acoustid_request(struct acoustid_context *ctx)
{
    struct _userdata *ud;
    struct fp3_result *response;
    int ret;


    /* Create a new parser.  ne_xml_create(), and
     * ne_xml_push_handler() cannot fail.
     */
    ud = _userdata_new(ctx);
    if (ud == NULL) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (NULL);
    }

    ud->parser = ne_xml_create();
    ne_xml_push_handler(ud->parser, _cb_startelm, _cb_cdata, _cb_endelm, ud);


    /* Dispatch the request, free the parser and its userdata.  On
     * failure, free the partially assembled response as well.
     */
    ret = _dispatch(ctx, ud->parser,
                    "recordingids",
                    "releasegroupids",
                    "releaseids",
                    "tracks",
                    NULL);

    ne_xml_destroy(ud->parser);
    response = _userdata_finish(ud);

    if (ret != 0) {
        if (response != NULL)
            fp3_free_result(response);
        return (NULL);
    }

    return (response);
}


/***********************
 * SECOND PARSER BELOW *
 ***********************/

/* The _fingerprint2 structure maintains a list of metadata
 * structures.
 *
 * XXX This is a poorly named structure!  And it should probably
 * migrate to the metadata module.
 *
 * XXX BIG BLOOMING TODO: merge this parser with the previous one.
 */
struct _fingerprint2
{
    /* List of metadata structures for the current match to the
     * AcoustID database
     */
    struct metadata **metadata;

    /* Number of metadata structures in metadata
     */
    size_t nmemb;

    /* Number of pointers to metadata structures that can be
     * accommodated in metadata without reallocation
     */
    size_t capacity;
};


/* Indexed list of metadata lists, corresponding to the matching
 * metadata structures for each file.  This structure does not need
 * maintain a separate capacity, because it is initialised with the
 * number of expected responses.
 *
 */
struct _response2
{
    /* Metadata structures for each file (XXX or is it stream?)
     */
    struct _fingerprint2 **fingerprints;

    /* Number of metadata lists in fingerprints
     */
    size_t nmemb;
};


/* XXX This should probably move to the metadata module.
 */
static struct metadata *
_add_track(struct _fingerprint2 *fingerprint)
{
    struct metadata *metadata;
    void *p;

    if (fingerprint->nmemb == fingerprint->capacity) {
        p = realloc(
            fingerprint->metadata,
            (fingerprint->nmemb + 1) * sizeof(struct metadata *));
        if (p == NULL)
            return (NULL);
        fingerprint->metadata = p;
        fingerprint->metadata[fingerprint->capacity] = NULL;
        fingerprint->capacity += 1;
    }

    metadata = fingerprint->metadata[fingerprint->nmemb];
    if (metadata == NULL) {
        metadata = metadata_new();
        if (metadata == NULL)
            return (NULL);
        fingerprint->metadata[fingerprint->nmemb] = metadata;
    }
    fingerprint->nmemb += 1;

    return (metadata);
}


/* The _last_metadata() function returns a pointer to the last
 * metadata structure of the _fingerprint2 structure pointed to by @p
 * fingerprint.  If @p fingerprint does not contain any metadata, @c
 * NULL is returned and the global variable @c errno is set to @c
 * EFAULT.
 *
 * @param fingerprint The accumulated metadata
 * @param result      A pointer to the last metadata structure
 */
static struct metadata *
_last_metadata(struct _fingerprint2 *fingerprint)
{
    if (fingerprint->nmemb > 0)
        return (fingerprint->metadata[fingerprint->nmemb - 1]);
    errno = EFAULT;
    return (NULL);
}


/* The _move_metadata() function moves the metadata item from @p src
 * to @p dst.  @p src is cleared on successful completion.  XXX This
 * should probably move to the metadata module.
 */
static void
_move_metadata(struct metadata *dst, struct metadata *src)
{
    metadata_clear(dst);

    dst->album = src->album;
    dst->album_artist = src->album_artist;
    dst->artist = src->artist;
    dst->compilation = src->compilation;
    dst->composer = src->composer;
    dst->date = src->date;
    dst->disc = src->disc;
    dst->sort_album_artist = src->sort_album_artist;
    dst->sort_artist = src->sort_artist;
    dst->sort_composer = src->sort_composer;
    dst->title = src->title;
    dst->track = src->track;


    /* XXX If the structure had a capacity for the unknown tags, this
     * (as well as the corresponding bit in the clear function) would
     * look different.
     */
    dst->unknown = src->unknown;
    dst->nmemb = src->nmemb;


    /* Cannot use metadata_clear() here, because that would free the
     * pointers in dst.
     */
    src->album = NULL;
    src->album_artist = NULL;
    src->artist = NULL;
    src->compilation = NULL;
    src->composer = NULL;
    src->date = NULL;
    src->disc = NULL;
    src->sort_album_artist = NULL;
    src->sort_artist = NULL;
    src->sort_composer = NULL;
    src->title = NULL;
    src->track = NULL;

    src->unknown = NULL;
    src->nmemb = 0;
}


/* XXX This should probably move to the metadata module.
 */
static void
_clear_fingerprint(struct _fingerprint2 *fingerprint)
{
    size_t i;

    for (i = 0; i < fingerprint->nmemb; i++)
        metadata_clear(fingerprint->metadata[i]);
    fingerprint->nmemb = 0;
}


static void
_free_fingerprint2(struct _fingerprint2 *fingerprint)
{
    size_t i;

    for (i = 0; i < fingerprint->nmemb; i++) {
        if (fingerprint->metadata[i] != NULL)
            metadata_free(fingerprint->metadata[i]);
    }

    if (fingerprint->metadata != NULL)
        free(fingerprint->metadata);
    free(fingerprint);
}


static void
_free_response2(struct _response2 *response)
{
    size_t i;

    for (i = 0; i < response->nmemb; i++) {
        if (response->fingerprints[i] != NULL)
            _free_fingerprint2(response->fingerprints[i]);
    }

    if (response->fingerprints != NULL)
        free(response->fingerprints);
    free(response);
}


static struct _fingerprint2 *
_new_fingerprint2()
{
    struct _fingerprint2 *fingerprint;

    fingerprint = malloc(sizeof(struct _fingerprint2));
    if (fingerprint == NULL)
        return (NULL);

    fingerprint->metadata = NULL;
    fingerprint->nmemb = 0;
    fingerprint->capacity = 0;

    return (fingerprint);
}


static struct _response2 *
_new_response2(size_t nmemb)
{
    struct _response2 *response;
    size_t i;

    response = malloc(sizeof(struct _response2));
    if (response == NULL)
        return (NULL);

    response->fingerprints = calloc(nmemb, sizeof(struct _fingerprint2));
    if (response->fingerprints == NULL) {
        free(response);
        return (NULL);
    }

    response->nmemb = 0;
    for (i = 0; i < nmemb; i++) {
        response->fingerprints[i] = _new_fingerprint2();
        if (response->fingerprints[i] == NULL) {
            _free_response2(response);
            return (NULL);
        }
        response->nmemb += 1;
    }

    return (response);
}


/* The _userdata2 structure maintains state and current progress while
 * parsing the response from AcoustID.  Except for the response
 * member, which accumulates the final result, any non-NULL members
 * are subject to release when the parser returns.
 */
struct _userdata2
{
    /* String buffer for the currently accumulated CDATA
     */
    ne_buffer *cdata;

    /* XML parser, for error reporting
     */
    ne_xml_parser *parser;

    /* Metadata for the current media
     */
    struct _fingerprint2 *media;

    /* Metadata for the current recordings
     */
    struct _fingerprint2 *recordings;

    /* Metadata for the current releases
     */
    struct _fingerprint2 *releases;

    /* Metadata for the current results
     */
    struct _fingerprint2 *results;

    /* Metadata for the current tracks
     */
    struct _fingerprint2 *tracks;

    /* Scored metadata for the indexed fingerprints
     */
    struct _response2 *response;

    /* Temporary storage for the joinphrase of the current artist
     */
    char *artist_joinphrase;

    /* Temporary storage for the name of the current artist
     */
    char *artist_name;

    /* Temporary storage for the position of the current medium within
     * the release
     *
     * XXX medium_position and medium_track_count should be
     * integer-valued, and so should the track_position, which has no
     * intermediate storage.
     *
     * XXX This is now position_medium in structures.h.
     */
    char *medium_position;

    /* Temporary storage for the title of the current medium
     */
    char *medium_title;

    /* Temporary storage for the total number of tracks on the current
     * medium
     */
    char *medium_track_count;

    /* Temporary storage for the ID of the current recording
     */
    char *recording_id;

    /* Temporary storage for the ID of the current release
     */
    char *release_id;

    /* Temporary storage for the ID of the current result
     */
    char *result_id;

    /* Temporary storage for the score of the current result
     *
     * XXX result_score should be real-valued.
     */
    char *result_score;

    /* The one-based index of the current fingerprint in the range [1,
     * response->nmemb]
     */
    unsigned long index;
};


/* The _userdata2_new() function allocates a new _userdata2 structure
 * and initialises all its members.  If an error occurs
 * _userdata2_new() returns @c NULL and sets the global variable @c
 * errno to indicate the error.
 *
 * @param nmemb Number of files/streams submitted for analysis
 * @return      New _userdata structure.  The returned pointer may
 *              subsequently be used as an argument to the function
 *              _userdata_free().
 */
static struct _userdata2 *
_userdata2_new(size_t nmemb)
{
    struct _userdata2 *ud;

    ud = malloc(sizeof(struct _userdata2));
    if (ud == NULL)
        return (NULL);

    ud->response = _new_response2(nmemb);
    if (ud->response == NULL) {
        free(ud);
        return (NULL);
    }

    ud->cdata = ne_buffer_create();
    ud->parser = NULL;

    ud->media = NULL;
    ud->recordings = NULL;
    ud->releases = NULL;
    ud->results = NULL;
    ud->tracks = NULL;

    ud->artist_joinphrase = NULL;
    ud->artist_name = NULL;
    ud->medium_position = NULL;
    ud->medium_title = NULL;
    ud->medium_track_count = NULL;
    ud->recording_id = NULL;
    ud->release_id = NULL;
    ud->result_id = NULL;
    ud->result_score = NULL;

    return (ud);
}


/* The _userdata2_finish() function frees the _userdata2 structure
 * pointed to by @p ud and returns the accumulated result.  If no
 * results were accumulated, _userdata2_finish() returns a pointer to
 * an empty _response2 structure.
 *
 * @note ud->parser is not destroyed by _userdata2_finish(), because
 *       it is managed outside the _userdata2 module.
 *
 * @param ud XXX
 * @return   The accumulated result.  The returned pointer may
 *           subsequently be used as an argument to the function
 *           _free_response2().
 */
static struct _response2 *
_userdata2_finish(struct _userdata2 *ud)
{
    struct _response2 *response;

    ne_buffer_destroy(ud->cdata);

    if (ud->media != NULL)
        _free_fingerprint2(ud->media);

    if (ud->recordings != NULL)
        _free_fingerprint2(ud->recordings);

    if (ud->releases != NULL)
        _free_fingerprint2(ud->releases);

    if (ud->results != NULL)
        _free_fingerprint2(ud->results);

    if (ud->tracks != NULL)
        _free_fingerprint2(ud->tracks);

    if (ud->artist_joinphrase != NULL)
        free(ud->artist_joinphrase);

    if (ud->artist_name != NULL)
        free(ud->artist_name);

    if (ud->medium_position != NULL)
        free(ud->medium_position);

    if (ud->medium_title != NULL)
        free(ud->medium_title);

    if (ud->medium_track_count != NULL)
        free(ud->medium_track_count);

    if (ud->recording_id != NULL)
        free(ud->recording_id);

    if (ud->release_id != NULL)
        free(ud->release_id);

    if (ud->result_id != NULL)
        free(ud->result_id);

    if (ud->result_score != NULL)
        free(ud->result_score);

    response = ud->response;
    free(ud);

    return (response);
}


enum _parser_state_2
{
    _STATE_ROOT_2 = NE_XML_STATEROOT,
    _STATE_ARTIST_2,
    _STATE_ARTIST_ID_2,
    _STATE_ARTIST_JOINPHRASE_2,
    _STATE_ARTIST_NAME_2,
    _STATE_ARTISTS_2,
    _STATE_FINGERPRINT_2,
    _STATE_FINGERPRINT_INDEX_2,
    _STATE_FINGERPRINTS_2,
    _STATE_MEDIUM_2,
    _STATE_MEDIUM_FORMAT_2,
    _STATE_MEDIUM_POSITION_2,
    _STATE_MEDIUM_TITLE_2,
    _STATE_MEDIUM_TRACK_COUNT_2,
    _STATE_MEDIUMS_2,
    _STATE_RECORDING_2,
    _STATE_RECORDING_ID_2,
    _STATE_RECORDINGS_2,
    _STATE_RELEASE_2,
    _STATE_RELEASE_ID_2,
    _STATE_RELEASES_2,
    _STATE_RESPONSE_2,
    _STATE_RESPONSE_STATUS_2,
    _STATE_RESULT_2,
    _STATE_RESULT_ID_2,
    _STATE_RESULT_SCORE_2,
    _STATE_RESULTS_2,
    _STATE_TRACK_2,
    _STATE_TRACK_TITLE_2,
    _STATE_TRACK_POSITION_2,
    _STATE_TRACKS_2
};


/* The _cb_startelm_2() function returns the new state of the parser
 * based on its current state and the name of the starting element,
 * and performs any initialisation required by the state change.
 * Omitting collection elements, the response returned by the AcoustID
 * server is structured as follows:
 *
 *   Element              | Extracted CDATA
 *   --------------------------------------
 *   response             | status
 *     fingerprint        | index
 *       result           | id, score
 *         recording      | id
 *           release      | id
 *             medium     | position, title, track_count
 *               track    | position, title
 *                 artist | name, joinphrase
 *
 * where the indentation in the element column indicates nesting.
 *
 *   When a artist element is ended the CDATA from its name and
 *   joinphrase are appended to the most recently added track.  For
 *   this to work, a dummy track must be created when the track
 *   element is started.
 *
 *   When the track position and track title elements are ended, their
 *   CDATA are copied directly into the metadata for the most recently
 *   added track.
 *
 *   When a medium element is ended, medium position, title, and
 *   track_count are assigned to the metadata for the accumulated
 *   child tracks, and they are merged with the tracks for any
 *   previous media.
 *
 *   When a release element is ended, the release ID is assigned to
 *   the metadata for all accumulated child tracks, and they are
 *   merged with the tracks for any previous releases.
 *
 *   When a recording element is ended, the recording ID is assigned
 *   to the metadata for all accumulated child tracks, and they are
 *   merged with the tracks for any previous recordings.
 *
 *   When a result element is ended, the result ID and score are
 *   assigned to the metadata for all accumulated child tracks, and
 *   they are merged with the tracks for any previous results.
 *
 *   When a fingerprint element is ended, the metadata for the
 *   accumulated results are moved to the response at the appropriate
 *   index.
 *
 * @note The format description as well as artist and track ID:s are
 *       declined because they cannot be stored in the metadata
 *       structure.
 */
static int
_cb_startelm_2(void *userdata,
             int parent,
             const char *nspace,
             const char *name,
             const char **atts)
{
    struct _userdata2 *ud;

    ud = (struct _userdata2 *)userdata;
    switch (parent) {
    case NE_XML_STATEROOT:
        if (strcmp(name, "response") == 0)
            return (_STATE_RESPONSE_2);
        break;

    case _STATE_ARTIST_2:
        if (strcmp(name, "id") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "joinphrase") == 0)
            return (_STATE_ARTIST_JOINPHRASE_2);
        if (strcmp(name, "name") == 0)
            return (_STATE_ARTIST_NAME_2);
        break;

    case _STATE_ARTISTS_2:
        if (strcmp(name, "artist") == 0)
            return (_STATE_ARTIST_2);
        break;

    case _STATE_FINGERPRINT_2:
        if (strcmp(name, "index") == 0)
            return (_STATE_FINGERPRINT_INDEX_2);
        if (strcmp(name, "results") == 0)
            return (_STATE_RESULTS_2);
        break;

    case _STATE_FINGERPRINTS_2:
        if (strcmp(name, "fingerprint") == 0)
            return (_STATE_FINGERPRINT_2);
        break;

    case _STATE_MEDIUM_2:
        if (strcmp(name, "format") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "position") == 0)
            return (_STATE_MEDIUM_POSITION_2);
        if (strcmp(name, "title") == 0)
            return (_STATE_MEDIUM_TITLE_2);
        if (strcmp(name, "track_count") == 0)
            return (_STATE_MEDIUM_TRACK_COUNT_2);
        if (strcmp(name, "tracks") == 0)
            return (_STATE_TRACKS_2);
        break;

    case _STATE_MEDIUMS_2:
        if (strcmp(name, "medium") == 0)
            return (_STATE_MEDIUM_2);
        break;

    case _STATE_RECORDING_2:
        if (strcmp(name, "id") == 0)
            return (_STATE_RECORDING_ID_2);
        if (strcmp(name, "releases") == 0)
            return (_STATE_RELEASES_2);
        break;

    case _STATE_RECORDINGS_2:
        if (strcmp(name, "recording") == 0)
            return (_STATE_RECORDING_2);
        break;

    case _STATE_RELEASE_2:
        if (strcmp(name, "id") == 0)
            return (_STATE_RELEASE_ID_2);
        if (strcmp(name, "mediums") == 0)
            return (_STATE_MEDIUMS_2);
        break;

    case _STATE_RELEASES_2:
        if (strcmp(name, "release") == 0)
            return (_STATE_RELEASE_2);
        break;

    case _STATE_RESPONSE_2:
        if (strcmp(name, "fingerprints") == 0)
            return (_STATE_FINGERPRINTS_2);
        if (strcmp(name, "status") == 0)
            return (_STATE_RESPONSE_STATUS_2);
        break;

    case _STATE_RESULT_2:
        if (strcmp(name, "id") == 0)
            return (_STATE_RESULT_ID_2);
        if (strcmp(name, "recordings") == 0)
            return (_STATE_RECORDINGS_2);
        if (strcmp(name, "score") == 0)
            return (_STATE_RESULT_SCORE_2);
        break;

    case _STATE_RESULTS_2:
        if (strcmp(name, "result") == 0)
            return (_STATE_RESULT_2);
        break;

    case _STATE_TRACK_2:
        if (strcmp(name, "artists") == 0)
            return (_STATE_ARTISTS_2);
        if (strcmp(name, "id") == 0)
            return (NE_XML_DECLINE);
        if (strcmp(name, "position") == 0)
            return (_STATE_TRACK_POSITION_2);
        if (strcmp(name, "title") == 0)
            return (_STATE_TRACK_TITLE_2);
        break;

    case _STATE_TRACKS_2:
        if (strcmp(name, "track") == 0) {
            if (ud->tracks == NULL) {
                ud->tracks = _new_fingerprint2();
                if (ud->tracks == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
            if (_add_track(ud->tracks) == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
            return (_STATE_TRACK_2);
        }
        break;
    }

    _ne_xml_set_error(
        ud->parser, "Element \"%s\" unexpected in state %d", name, parent);

    return (NE_XML_ABORT);
}


static int
_cb_cdata_2(void *userdata, int state, const char *cdata, size_t len)
{
    ne_buffer_append(((struct _userdata2 *)userdata)->cdata, cdata, len);
    return (0);
}


/* The length of @p *recordings can only decrease
 *
 * @p *nmemb update on return, @p *recordings not reallocated.
 *
 * Because scores need not be identical, there is a case for the
 * "average score" on merged recordings.
 */
static int
_merge_recordings(struct metadata **recordings, size_t *nmemb)
{
    ne_buffer *fingerprint_ids;
    char *ep;
    size_t i, j, n_fingerprints, n_releases;
    float score_tot;


    /* Check that all recordings have a non-NULL fingerprint ID:s and
     * real-valued scores.
     */
    for (i = 0; i < *nmemb; i++) {
        if (recordings[i]->composer == NULL) {
            errno = EDOM;
            return (-1);
        }
        strtof(recordings[i]->sort_artist, &ep);
        if (*ep != '\0') {
            errno = EDOM;
            return (-1);
        }
    }

    fingerprint_ids  = ne_buffer_create();
    n_fingerprints = 0;

    for (i = 0; i < *nmemb; i++) {
        /* Prime the comma-separated fingerprint_ids list and
         * initialise the total.
         */
        score_tot = strtof(recordings[i]->sort_artist, &ep);
        ne_buffer_zappend(fingerprint_ids, recordings[i]->composer);
        n_fingerprints = 1;
        n_releases = 1;


        /* usermeta-only matches do not have links to the MusicBrainz
         * database and lack recording ID:s.  These are always treated
         * as unique recordings.
         */
        if (recordings[i]->compilation == NULL)
            continue;


        for (j = i + 1; j < *nmemb; ) {
            /* Proceed to recording j + 1 if recordings i and j do not
             * have the same recording ID.  Artist and title need not
             * be identical, nor do fingerprints and consequently
             * scores, owing to the fuzzy matching and because a
             * recording may be associated with multiple fingerprints.
             */
            if (recordings[j]->compilation == NULL) {
                j += 1;
                continue;
            }
            if (strcmp(recordings[i]->compilation,
                       recordings[j]->compilation) != 0) {
                j += 1;
                continue;
            }


            /* Add the fingerprint ID to the comma-separated
             * fingerprint_ids list, unless it is already present.
             * Update the total score.
             *
             * XXX Idea: keep list as "fingerprint [score],
             * fingerprint [score], ...", and report the max score.
             */
            if (strstr(fingerprint_ids->data,
                       recordings[j]->composer) == NULL) {

                ne_buffer_concat(
                    fingerprint_ids, ", ", recordings[j]->composer, NULL);
#if 0
                score_tot += strtof(recordings[j]->sort_artist, &ep);
#else
                float t = strtof(recordings[j]->sort_artist, &ep);
                if (t > score_tot)
                    score_tot = t;
#endif
                n_fingerprints += 1;
            }


            /* Free the j:th item and remove it from the list.  Do not
             * increment j for the next iteration.  XXX This could be
             * a use for an erase() function.
             */
            metadata_free(recordings[j]);
            if (j + 1 < *nmemb) {
                memmove(recordings + j,
                        recordings + j + 1,
                        (*nmemb - j - 1) * sizeof(struct metadata *));
            }
            *nmemb -= 1;
            n_releases += 1;
        }


        /* If the new metadata reflects more than one release, update
         * the disc member to reflect the number of merged releases,
         * and clear the track number and release ID because they do
         * not make sense any more.
         *
         * If the new metadata reflects more than one fingerprint,
         * update the score to the average.
         */
        if (n_releases > 1) {
            snprintf(recordings[i]->disc, strlen(recordings[i]->disc),
                     "%zd", n_releases);

            if (recordings[i]->date != NULL)
                free(recordings[i]->date);
            recordings[i]->date = NULL;

            if (recordings[i]->track != NULL)
                free(recordings[i]->track);
            recordings[i]->track = NULL;

            recordings[i]->composer = strdup(fingerprint_ids->data);
            if (recordings[i]->composer == NULL) {
                ne_buffer_destroy(fingerprint_ids);
                return (-1);
            }

            if (n_fingerprints > 1) {
                if (snprintf(recordings[i]->sort_artist,
                             strlen(recordings[i]->sort_artist), "%f",
#if 0
                             score_tot / n_fingerprints
#else
                             score_tot
#endif
                        ) < 0) {
                    ne_buffer_destroy(fingerprint_ids);
                    return (-1);
                }
            }
        }


        /* Clear the list of fingerprint ID:s.
         */
        ne_buffer_clear(fingerprint_ids);
        n_fingerprints = 0;
    }


    /* Free the list of fingerprint ID:s.
     */
    ne_buffer_destroy(fingerprint_ids);
    return (0);
}


static int
_cdatatoul_2(struct _userdata2 *userdata, unsigned long int *dst)
{
    char *ep;

    *dst = strtoul(userdata->cdata->data, &ep, 10);
    if (*ep != '\0' || ep == userdata->cdata->data) {
        ne_xml_set_error(userdata->parser, strerror(errno = EDOM));
        return (NE_XML_ABORT);
    } else if (*dst == ULONG_MAX && errno == ERANGE) {
        ne_xml_set_error(userdata->parser, strerror(errno));
        return (NE_XML_ABORT);
    }
    ne_buffer_clear(userdata->cdata);
    return (0);
}


static int
_cb_endelm_2(void *userdata, int state, const char *nspace, const char *name)
{
    struct metadata *metadata;
    struct _fingerprint2 *fingerprints;
    struct _userdata2 *ud;
    ne_buffer *buf;
    char *ep;
    size_t i;


    ud = (struct _userdata2 *)userdata;
    switch (state) {
    case _STATE_ARTIST_JOINPHRASE_2:
        ud->artist_joinphrase = strdup(ud->cdata->data);
        if (ud->artist_joinphrase == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_ARTIST_NAME_2:
        ud->artist_name = strdup(ud->cdata->data);
        if (ud->artist_name == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_ARTIST_2:
        /* The artist name and joinphrase may arrive out of order in
         * the response from the AcoustID server.  This is not a
         * problem when querying the MusicBrainz server, because it
         * emits joinphrase as an attribute of the artist element.
         */
        metadata = _last_metadata(ud->tracks);
        if (metadata == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        if (ud->artist_name != NULL) {
            buf = ne_buffer_create();
            if (metadata->artist != NULL) {
                ne_buffer_concat(buf, metadata->artist,
                                 ud->artist_name, ud->artist_joinphrase, NULL);
                free(metadata->artist);
            } else {
                ne_buffer_concat(buf,
                                 ud->artist_name, ud->artist_joinphrase, NULL);
            }
            metadata->artist = ne_buffer_finish(buf);

            free(ud->artist_name);
            ud->artist_name = NULL;
        } else if (ne_buffer_size(ud->cdata) > 0) {
            metadata->artist = strdup(ud->cdata->data);
            if (metadata->artist == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
            ne_buffer_clear(ud->cdata);
        }

        if (ud->artist_joinphrase != NULL) {
            free(ud->artist_joinphrase);
            ud->artist_joinphrase = NULL;
        }

#ifdef DEBUG
        printf("            Artist \"%s\"\n", metadata->artist);
#endif

        return (0);


    case _STATE_FINGERPRINT_2:
#ifdef DEBUG
        printf("Fingerprint %zd\n", ud->index - 1);
#endif

        if (ud->results == NULL)
            return (0);
        fingerprints = ud->response->fingerprints[ud->index - 1];
        fingerprints->metadata = ud->results->metadata;
        fingerprints->nmemb = ud->results->nmemb;
        ud->results = NULL;
        return (0);


    case _STATE_FINGERPRINT_INDEX_2:
        if (_cdatatoul_2(ud, &ud->index) != 0)
            return (NE_XML_ABORT);
        if (ud->index > ud->response->nmemb) {
            _ne_xml_set_error(
                ud->parser,
                "Index \"%lu\" outside range [1, %zd]",
                ud->index, ud->response->nmemb);
            return (NE_XML_ABORT);
        }
        return (0);


    case _STATE_MEDIUM_2:
#ifdef DEBUG
        printf("        Medium %s\n", ud->medium_title);
#endif
        /* The medium title is assigned to the album title, since it
         * appears there is no way to get the release title from the
         * AcoustID server.
         */
        if (ud->tracks == NULL)
            return (0);

        if (ud->media == NULL) {
            ud->media = _new_fingerprint2();
            if (ud->media == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }

        for (i = 0; i < ud->tracks->nmemb; i++) {
            metadata = _add_track(ud->media);
            if (metadata == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
            _move_metadata(metadata, ud->tracks->metadata[i]);

            if (ud->medium_position != NULL) {
                metadata->disc = strdup(ud->medium_position);
                if (metadata->disc == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }

            if (ud->medium_title != NULL) {
                metadata->album = strdup(ud->medium_title);
                if (metadata->album == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }

            buf = ne_buffer_create();
            if (metadata->track != NULL) {
                ne_buffer_concat(buf, metadata->track, "/", NULL);
                free(metadata->track);
            } else {
                ne_buffer_zappend(buf, "?/");
            }
            if (ud->medium_track_count != NULL)
                ne_buffer_zappend(buf, ud->medium_track_count);
            else
                ne_buffer_zappend(buf, "?");
            metadata->track = ne_buffer_finish(buf);
        }
        _clear_fingerprint(ud->tracks);

        if (ud->medium_position != NULL) {
            free(ud->medium_position);
            ud->medium_position = NULL;
        }
        if (ud->medium_title != NULL) {
            free(ud->medium_title);
            ud->medium_title = NULL;
        }
        if (ud->medium_track_count != NULL) {
            free(ud->medium_track_count);
            ud->medium_track_count = NULL;
        }
        return (0);


    case _STATE_MEDIUM_POSITION_2:
        /* The position of the medium should really be on the form
         * MM/NN, but there is not enough information available to
         * accomplish that here.  Assume the CDATA for the medium
         * position is numeric, and append "/?" instead.
         */
        ne_buffer_zappend(ud->cdata, "/?");
        ud->medium_position = strdup(ud->cdata->data);
        if (ud->medium_position == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_MEDIUM_TITLE_2:
        ud->medium_title = strdup(ud->cdata->data);
        if (ud->medium_title == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        break;


    case _STATE_MEDIUM_TRACK_COUNT_2:
        ud->medium_track_count = strdup(ud->cdata->data);
        if (ud->medium_track_count == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_RECORDING_2:
        /* XXX Abuse compilation for MusicBrainz recording ID.  This
         * is unacceptable.
         */
#ifdef DEBUG
        printf("    Recording %s\n", ud->recording_id);
#endif
        if (ud->releases == NULL)
            return (0);

        if (ud->recordings == NULL) {
            ud->recordings = _new_fingerprint2();
            if (ud->recordings == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }

        for (i = 0; i < ud->releases->nmemb; i++) {
            metadata = _add_track(ud->recordings);
            if (metadata == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
            _move_metadata(metadata, ud->releases->metadata[i]);

            if (ud->recording_id != NULL) {
                metadata->compilation = strdup(ud->recording_id);
                if (metadata->compilation == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
        }
        _clear_fingerprint(ud->releases);

        if (ud->recording_id != NULL) {
            free(ud->recording_id);
            ud->recording_id = NULL;
        }
        return (0);


    case _STATE_RECORDING_ID_2:
        ud->recording_id = strdup(ud->cdata->data);
        if (ud->recording_id == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_RELEASE_2:
        /* XXX Abuse date for MusicBrainz release ID.  This is
         * unacceptable.
         */
#ifdef DEBUG
        printf("      Release %s\n", ud->release_id);
#endif
        if (ud->media == NULL)
            return (0);

        if (ud->releases == NULL) {
            ud->releases = _new_fingerprint2();
            if (ud->releases == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }

        for (i = 0; i < ud->media->nmemb; i++) {
            metadata = _add_track(ud->releases);
            if (metadata == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
            _move_metadata(metadata, ud->media->metadata[i]);

            if (ud->release_id != NULL) {
                metadata->date = strdup(ud->release_id);
                if (metadata->date == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
        }
        _clear_fingerprint(ud->media);

        if (ud->release_id != NULL) {
            free(ud->release_id);
            ud->release_id = NULL;
        }
        return (0);


    case _STATE_RELEASE_ID_2:
        ud->release_id = strdup(ud->cdata->data);
        if (ud->release_id == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_RESPONSE_STATUS_2:
        if (strcmp(ud->cdata->data, "ok") != 0) {
            _ne_xml_set_error(
                ud->parser,
                "AcoustID look-up failed with status \"%s\"",
                ud->cdata->data);
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_RESULT_2:
        /* XXX Abuse composer and sort_artist for MusicBrainz result
         * ID and score, respectively.  This is unacceptable.
         */
#ifdef DEBUG
        printf("  Result %s %s\n", ud->result_id, ud->result_score);
#endif
        if (ud->recordings == NULL)
            return (0);

        if (ud->results == NULL) {
            ud->results = _new_fingerprint2();
            if (ud->results == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
        }

        for (i = 0; i < ud->recordings->nmemb; i++) {
            metadata = _add_track(ud->results);
            if (metadata == NULL) {
                ne_xml_set_error(ud->parser, strerror(errno));
                return (NE_XML_ABORT);
            }
            _move_metadata(metadata, ud->recordings->metadata[i]);

            if (ud->result_id != NULL) {
                metadata->composer = strdup(ud->result_id);
                if (metadata->composer == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }

            if (ud->result_score != NULL) {
                metadata->sort_artist = strdup(ud->result_score);
                if (metadata->sort_artist == NULL) {
                    ne_xml_set_error(ud->parser, strerror(errno));
                    return (NE_XML_ABORT);
                }
            }
        }
        _clear_fingerprint(ud->recordings);

        if (ud->result_id != NULL) {
            free(ud->result_id);
            ud->result_id = NULL;
        }
        if (ud->result_score != NULL) {
            free(ud->result_score);
            ud->result_score = NULL;
        }
        return (0);


    case _STATE_RESULT_ID_2:
        ud->result_id = strdup(ud->cdata->data);
        if (ud->result_id == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_RESULT_SCORE_2:
        strtof(ud->cdata->data, &ep);
        if (*ep != '\0' || ep == ud->cdata->data) {
            _ne_xml_set_error(
                ud->parser,
                "Cannot parse real-valued score \"%s\"",
                ud->cdata->data);
            return (NE_XML_ABORT);
        }
        /* XXX See other checks in the cluster parser.
         */
        ud->result_score = strdup(ud->cdata->data);
        if (ud->result_score == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }
        ne_buffer_clear(ud->cdata);
        return (0);


#ifdef DEBUG
    case _STATE_TRACK_2:
        printf("          Track \"%s\"\n", _last_metadata(ud->tracks)->title);
        return (0);
#endif


    case _STATE_TRACK_POSITION_2:
        metadata = _last_metadata(ud->tracks);
        if (metadata == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        metadata->track = strdup(ud->cdata->data);
        if (metadata->track == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        ne_buffer_clear(ud->cdata);
        return (0);


    case _STATE_TRACK_TITLE_2:
        metadata = _last_metadata(ud->tracks);
        if (metadata == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        metadata->title = strdup(ud->cdata->data);
        if (metadata->title == NULL) {
            ne_xml_set_error(ud->parser, strerror(errno));
            return (NE_XML_ABORT);
        }

        ne_buffer_clear(ud->cdata);
        return (0);
    }


    if (ne_buffer_size(ud->cdata) > 0) {
#ifdef DEBUG
        printf("CDATA buffer not empty in state %d\n", state);
#endif
        ne_buffer_clear(ud->cdata);
    }

    return (0);
}


/* XXX Should return an array of metadata structures.
 */
void *
acoustid_query(struct acoustid_context *ctx)
{
    struct _userdata2 *ud;
    struct _response2 *response;
    int ret;


    /* Create a new parser.  ne_xml_create(), and
     * ne_xml_push_handler() cannot fail.
     */
    ud = _userdata2_new(ctx->nmemb);
    if (ud == NULL) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (NULL);
    }

    ud->parser = ne_xml_create();
    ne_xml_push_handler(
        ud->parser, _cb_startelm_2, _cb_cdata_2, _cb_endelm_2, ud);


    /* Dispatch the request, free the parser and its userdata.  On
     * failure, free the partially assembled response as well.
     *
     * This is the complete set!  Except releasegroups and
     * releasegroupids.  The usermeta thing should perhaps be
     * configurable?  Are there any other things (XXX keywords or
     * whatever they are called) we are missing?
     */
    ret = _dispatch(ctx, ud->parser,
                    "recordings",
                    "recordingids",
                    "releases",
                    "releaseids",
                    "tracks",
                    "usermeta",
                    NULL);
    ne_xml_destroy(ud->parser);
    response = _userdata2_finish(ud);

    if (ret != 0) {
        if (response != NULL)
            _free_response2(response);
        return (NULL);
    }


    /* XXX Should return response instead of handling it here!
     * Merging should be optional as well.  And this bit as well as
     * the _merge_recordings() function should probably move to the
     * query program.
     */
    struct _fingerprint2 *fingerprint;
    size_t i, j;
    for (i = 0; i < response->nmemb; i++) {
        printf("**** STREAM %zd / %zd ****\n", i + 1, response->nmemb);
        fingerprint = response->fingerprints[i];

#if 1
        if (_merge_recordings(
                fingerprint->metadata, &fingerprint->nmemb) != 0) {
            _free_response2(response);
            printf("_merge_recordings() failed!\n");
            return (NULL);
        }

        printf("  #### MERGED RECORDINGS, stream %zd ####\n", i + 1);
#endif

        for (j = 0; j < fingerprint->nmemb; j++) {
            printf("  **** [%zd/%zd] ****\n", j + 1, fingerprint->nmemb);
            metadata_dump(fingerprint->metadata[j]);
        }
    }
    _free_response2(response);

    return (NULL);
}
