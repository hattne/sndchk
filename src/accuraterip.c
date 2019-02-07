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

#include <stdlib.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include <neon/ne_session.h> // XXX Which ones of these do we actually need?
#include <neon/ne_redirect.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
#include <neon/ne_xml.h>

#include "accuraterip.h"
#include "configuration.h"
#include "gzip.h"
#include "ratelimit.h"

#define USE_EAC 1


/* State structure for the _block_reader().
 */
struct _userdata
{
    /* The result XXX response?
     */
    struct _cache *result;

    /* Pointer to the neon session
     *
     * This is needed for error reporting in _block_reader().
     */
    ne_session *session;

#ifdef USE_EAC
    /* XXX EAC
     */
    ne_session *session_eac;
#endif

    ne_session *session_localhost;

    /* Pointer to the first octet of the unprocessed response
     *
     * Because this buffer holds binary data as opposed to
     * NULL-terminated strings, this cannot be a ne_buffer structure.
     */
//    uint8_t *buf;
    void *buf;

    /* Length of the buf, in octets
     *
     * This is equivalent to the used member of the ne_buffer
     * structure.
     */
    size_t len;

    /* Capacity of buf, in octets
     *
     * This is equivalent to the length member of the ne_buffer
     * structure.
     */
    size_t capacity;
};


#if 1
struct _match
{
    /* XXX Correct type for all these?  version is a bitfield
     *
     * 0x1 -- matches AccurateRip v1 checksum from DB
     * 0x2 -- matches AccurateRip v2 checksum from DB
     *
     * Both flags can be set!
     */
//    int version;

    int confidence_v1;
    int confidence_v2;

    /* This is kept mostly for cross-validation with morituri.
     *
     * XXX But see XLD's way of using both v1 and v2 checksums!
     */
    int confidence_max;

    /* XXX NEW
     */
    int confidence_total;


    /* Number of matching offsets [see struct fingersum_result]
     *
     * XXX NEW
     */
//    size_t nmemb;

    /* Array of nmemb matching offsets [see struct fingersum_result]
     *
     * XXX NEW
     */
//    ssize_t *offsets;
    ssize_t offset;
};


/* The _match_release structure hold the results of matching a set of
 * streams, which typically constitute a release
 */
struct _match_release
{
    /* XXX Kludge: total number of streams on the release, used to
     * calculate unmatched streams for comparison.
     */
    size_t n_streams;

    /* Number of streams
     */
    size_t nmemb;

    struct _match *recordings;
};


void // XXX Was static?
_match_release_free(struct _match_release *mr)
{
    if (mr->recordings != NULL)
        free(mr->recordings);
    free(mr);
}


#if 0
/* _match_compar() returns an integer less than, equal to, or greater
 * than zero if the first argument is is considered to be respectively
 * less than, equal to, or greater than the second.
 *
 * XXX This needs a bit of explaining.
 */
static int
_match_compar(struct _match *m1, struct _match *m2)
{
    if (m1->confidence < m2->confidence)
        return (-1);
    if (m1->confidence > m2->confidence)
        return (+1);

    if (m1->confidence_max < m2->confidence_max)
        return (-1);
    if (m1->confidence_max > m2->confidence_max)
        return (+1);

    return (0);
}
#endif


/* The smallest confidence for each result.  Synchronise this
 * documentation with stuff elsewhere (I think there are such things).
 */
int // XXX Was static, should probably remain static
_score_release(const struct _match_release *mr)
{
    size_t i;
    int min_confidence;

    if (mr->nmemb == 0)
        return (0);

    min_confidence = mr->recordings[0].confidence_v1 + mr->recordings[0].confidence_v2;
    for (i = 1; i < mr->nmemb; i++) {
        if (mr->recordings[i].confidence_v1 + mr->recordings[i].confidence_v2 < min_confidence)
            min_confidence = mr->recordings[i].confidence_v1 + mr->recordings[i].confidence_v2;
    }

    return (min_confidence);
}


int
_match_release_compar(struct _match_release *mr1, struct _match_release *mr2)
{
    int min_confidence1, min_confidence2;


    /* If the number of streams do not match, we are comparing apples
     * and oranges, and those are equal... or rather, the comparison
     * is undefined.
     */
    if (mr1->nmemb != mr2->nmemb)
        return (0);


    /* The largest _score_release() wins.
     *
     * XXX Could possibly repeat this with confidence_total or
     * something as well, but I'm not sure that makes a whole lot of
     * sense.
     */
    min_confidence1 = _score_release(mr1);
    min_confidence2 = _score_release(mr2);

    printf("Got min_confidence1 %d, min_confidence2 %d\n",
           min_confidence1,
           min_confidence2);

    if (min_confidence1 > min_confidence2)
        return (-1);
    else if (min_confidence1 < min_confidence2)
        return (+1);


    /* If they're equal look to the number of unassigned/unmatched
     * streams to break the tie.
     */
    {
        size_t i;
        size_t u1 = mr1->n_streams;
        size_t u2 = mr2->n_streams;

        for (i = 0; i < mr1->nmemb; i++) {
            if (mr1->recordings[i].confidence_v1 + mr1->recordings[i].confidence_v2 > 0 && u1 > 0)
                u1 -= 1;
        }

        for (i = 0; i < mr2->nmemb; i++) {
            if (mr2->recordings[i].confidence_v1 + mr2->recordings[i].confidence_v2 > 0 && u2 > 0)
                u2 -= 1;
        }

        printf("Got unmatched_1 %zd/%zd, unmatched_2 %zd/%zd\n",
               u1, mr1->n_streams,
               u2, mr2->n_streams);

        if (u1 < u2)
            return (-1);
        else if (u1 > u2)
            return (+1);
    }

    return (0);

#if 0 // OLD CODE MIGRATED FROM ELSEWHERE...
    /* XXX Note, there can be several discs.  Really not
     * sure whether confidence_max should matter when
     * scoring/comparing discs.
     */
    if (match_disc.confidence == 0 ||
        match_foobar[i].confidence < match_disc.confidence) {
        match_disc.confidence = match_foobar[i].confidence;
    }

    if (match_disc.confidence_max == 0 ||
        match_foobar[i].confidence_max > match_disc.confidence_max) {
        match_disc.confidence_max = match_foobar[i].confidence_max;
    }
#endif
}
#endif


#if 1
/* Base-1 integers, just like in libmusicbrainz
 */
struct _position
{
    int medium;

    int track;
};
#endif


/* XXX Opaque structure!
 */
struct accuraterip_context
{
    /* Cache hit rate, for diagnostic purposes
     *
     * Hits in hit_rate[0], hits + misses in hit_rate[1].  XXX See
     * http://en.wikipedia.org/wiki/Cache_(computing) for proper
     * terminology?
     */
    size_t hit_rate[2];

    /* Pointer to the neon session
     *
     * XXX Needed for proper error reporting in e.g. _block_reader().
     *
     * Since all requests are directed to the same server, a
     * persistent connection is used.
     */
    ne_session *session;

#ifdef USE_EAC
    /* XXX EAC
     */
    ne_session *session_eac;
#endif

    /* XXX Mapper
     */
    ne_session *session_localhost;

    /* The cache, a list of parsed responses
     *
     * The cache will grow until memory is exhausted, if necessary.
     * XXX Should the cache perhaps be global?  And it should be
     * limited!
     *
     * XXX Mind the access of the cache: it may be accessed by several
     * readers simultaneously, but only one writer at the time.
     */
    struct _cache *cache;

    /* Number of cached responses
     */
    size_t nmemb;
};


struct accuraterip_context *
accuraterip_new(const char *hostname, unsigned int port)
{
    struct accuraterip_context *ctx;

    ctx = malloc(sizeof(struct accuraterip_context));
    if (ctx == NULL)
        return (NULL);

    ctx->hit_rate[0] = ctx->hit_rate[1] = 0;
    ctx->session = NULL;
#ifdef USE_EAC
    ctx->session_eac = NULL;
#endif
    ctx->session_localhost = NULL;
    ctx->cache = NULL;
    ctx->nmemb = 0;


    /* Because it is not clear whether ne_sock_init() sets errno on
     * failure, it is set to EIO here.  Each successful invocation of
     * ne_sock_init() must have a corresponding invocation of
     * ne_sock_exit().  ne_session_create() cannot fail.
     *
     * XXX See http://www.webdav.org/neon/doc/html/refproxy.html
     */
    if (ne_sock_init() != 0) {
        free(ctx);
        errno = EIO;
        return (NULL);
    }
    ctx->session = ne_session_create("http", "www.accuraterip.com", 80);
    ne_set_useragent(ctx->session, PACKAGE_NAME "/" PACKAGE_VERSION);

    if (hostname != NULL) {
        /* Optionally, configure a proxy server for the session.  Note
         * that ne_session_socks_proxy() will remove any proxy servers
         * previously configured.
         */
//    ne_session_proxy(ctx->session, "localhost", 8080);
        ne_session_socks_proxy(
            ctx->session, NE_SOCK_SOCKSV5, hostname, port, NULL, NULL);
    }

#ifdef USE_EAC
    ctx->session_eac = ne_session_create("http", "www.exactaudiocopy.de", 80);
    ne_set_useragent(ctx->session_eac, PACKAGE_NAME "/" PACKAGE_VERSION);
#endif
    ctx->session_localhost = ne_session_create("http", "localhost", 1984);
    ne_set_useragent(ctx->session_localhost, PACKAGE_NAME "/" PACKAGE_VERSION);


    /* Register redirect handling.
     */
    ne_redirect_register(ctx->session_localhost);

    return (ctx);
}


void
accuraterip_free(struct accuraterip_context *ctx)
{
    struct _cache *response;
    size_t i, j;


    /* Free the cache, if allocated.
     */
    printf("Cached %zd AccurateRip response%s, hit rate %ld%% (%zd/%zd).\n",
           ctx->nmemb,
           ctx->nmemb == 1 ? "" : "s",
           ctx->hit_rate[1] > 0
           ? lrintf(100.0f * ctx->hit_rate[0] / ctx->hit_rate[1]) : 0,
           ctx->hit_rate[0], ctx->hit_rate[1]);

    if (ctx->cache != NULL) {
        for (i = 0; i < ctx->nmemb; i++) {
            response = ctx->cache + i;

            for (j = 0; j < response->nmemb; j++)
                free(response->entries[j].chunks);
            free(response->entries);

            if (response->error != NULL)
                free(response->error);

            if (response->path != NULL)
                free(response->path);
        }

        free(ctx->cache);
    }


    /* ne_session_destroy() and ne_sock_exit() cannot fail.  XXX Zap
     * this comment, and the thing about ne_session_create() above.
     */
    ne_session_destroy(ctx->session);
#ifdef USE_EAC
    ne_session_destroy(ctx->session_eac);
#endif
    ne_session_destroy(ctx->session_localhost);
    ne_sock_exit();

    free(ctx);
}


/* The _block_memcat() function appends a copy of the @p len first
 * octets pointed to by @p buf to the @p buf member of the _userdata
 * structure pointed to by @p userdata.  The @p buf and @p capacity
 * members of @p userdata are reallocated and updated if necessary.
 *
 * @param ud  Pointer to destination _userdata structure
 * @param buf Pointer to @p len octets of source data.  @p buf and @p
 *            ud->buf may not overlap.
 * @param len Length of @p buf
 * @return    @p ud->buf if successful, @c NULL otherwise.  If an
 *            error occurs the global variable @c errno is set to
 *            indicate the error.
 */
static uint8_t *
_block_memcat(struct _userdata *userdata, const uint8_t *buf, size_t len)
{
    void *p;

    if (userdata->len + len > userdata->capacity) {
        p = realloc(userdata->buf, userdata->len + len);
        if (p == NULL)
            return (NULL);
        userdata->buf = p;
        userdata->capacity = userdata->len + len;
    }

    memcpy(userdata->buf + userdata->len, buf, len);
    userdata->len += len;
    return (userdata->buf);
}


#if 0 // Currently unused
static void
_dump_ar_entry(const struct _entry *entry)
{
    struct _chunk *chunk;
    size_t i;

    printf("FreeDBIdent:            0x%08x\n", entry->disc_cddb);
    printf("TrackOffsetsAdded:      %d\n", entry->disc_id1);
    printf("TrackOffsetsMultiplied: %d\n", entry->disc_id2);

    for (i = 0; i < entry->track_count; i++) {
        chunk = entry->chunks + i;

        printf("Chunk %zd/%zd:\n", i + 1, entry->track_count);
        printf("  CRC:        0x%08x\n", chunk->CRC);
        printf("  unk:        0x%08x\n", chunk->unk);
        printf("  Confidence: %d\n", chunk->confidence);
    }
}
#endif


/* The _block_reader() implements a callback for parsing blocks of
 * data as they are read.  The function will read the @p len first
 * octets from the location pointed to by @p buf.  As soon as enough
 * data from consecutive blocks of an accepted response body have
 * been accumulated, the _block_reader() populates the @p result
 * member of the _userdata structure pointed to by @p userdata.
 * Calling _block_reader() with a @p len argument of zero indicates
 * all the data have been retrieved.  _block_reader() will exit
 * successfully if all the data has been successfully parsed.
 *
 * @param userdata Pointer to destination _userdata structure
 * @param buf      Pointer to the data block of the response
 * @param len      Number of characters in @p buf
 * @return         0 if successful, -1 otherwise.  If an error
 *                 occurs, _block_reader() will set the session error
 *                 string to indicate the error.
 */
static int
_block_reader(void *userdata, const char *buf, size_t len)
{
    struct _entry *entry;
    struct _userdata *ud;
//    uint8_t *src;
    void *src;
    void *p;
    size_t i;

//    printf("    _block_reader() %p %zd\n", buf, len);


    /* If this is the last block, i.e. if the callback is invoked with
     * len == 0, exit successfully if and only if all the buffered
     * data is processed.  Otherwise, append to the existing buffer.
     */
    ud = (struct _userdata *)userdata;
    if (len == 0) {
        len = ud->len;

        if (ud->buf != NULL)
            free(ud->buf);
        ud->capacity = 0;
        ud->len = 0;

        if (len > 0) {
            ne_set_error(ud->session, "%zd unparsed octets remain", len);
            return (-1);
        }
        return (0);
    }

    if (_block_memcat(ud, (uint8_t *)buf, len * sizeof(char)) == NULL) {
        ne_set_error(ud->session, "%s", strerror(errno));
        return (-1);
    }

    while (ud->len > 0) {
        /* If block does not contain the full header (XXX is that the
         * correct terminology?) or the full entry, return and hope
         * that it does on the next invocation.
         */
        if (ud->len < 13 + *((uint8_t *)ud->buf + 0) * 9)
            return (0);


        /* Allocate a new item in the _userdata structure's list of
         * entries and populate it with information from the entry's
         * 13-octet header.  Allocate and populate the entry's chunks
         * with data extracted from the 9-octet chunks in the block.
         *
         * XXX This is certainly broken on big-endian systems--it
         * appears AccurateRIP always returns little-endian, binary
         * data.
         */
        p = realloc(ud->result->entries,
                    (ud->result->nmemb + 1) * sizeof(struct _entry));
        if (p == NULL) {
            ne_set_error(ud->session, "%s", strerror(errno));
            return (-1);
        }
        ud->result->entries = p;

        entry = &ud->result->entries[ud->result->nmemb];
        entry->chunks = calloc(*((uint8_t *)ud->buf + 0), sizeof(struct _chunk));
        if (entry->chunks == NULL) {
            ne_set_error(ud->session, "%s", strerror(errno));
            return (-1);
        }
        entry->track_count = *((uint8_t *)(ud->buf + 0));
        entry->disc_id1 = *((uint32_t *)(ud->buf + 1));
        entry->disc_id2 = *((uint32_t *)(ud->buf + 5));
        entry->disc_cddb = *((uint32_t *)(ud->buf + 9));

        for (i = 0, src = ud->buf + 13; i < entry->track_count; i++, src += 9) {
            entry->chunks[i].confidence = *((uint8_t *)(src + 0));
            entry->chunks[i].CRC = *((uint32_t *)(src + 1));
            entry->chunks[i].unk = *((uint32_t *)(src + 5));
        }


        /* Remove the just-processed data from the buffer and adjust
         * its length.
         */
        memmove(ud->buf, src, ud->len -= (src - ud->buf));
        ud->result->nmemb += 1;
    };

//    for (i = 0; i < ud->result->nmemb; i++)
//        _dump_ar_entry(ud->result->entries + i);
//    exit(0);

    return (0);
}


#ifdef USE_EAC

/* Used to only print blocks if the count was > 0
 */
#if 0 // Currently unused
static void
_dump_eac_entry(const struct _entry_eac *entry)
{
    struct _block_eac *block;
    struct _track_eac *track;
    size_t i, j;

    printf("Date:             %08X\n", entry->date);
    for (i = 0; i < entry->n_tracks; i++) {
        track = entry->tracks + i;

        printf("  track %zd/%u\n", i + 1, entry->n_tracks);
        printf("    CRC32, whole track\n");
        for (j = 0; j < track->n_blocks_whole; j++) {
            block = track->blocks_whole + j;

            printf("      block %zd/%zd\n", j + 1, track->n_blocks_whole);
            printf("        crc32:    %08X\n", block->crc32);
            printf("        count:    %u\n", block->count);
            printf("        date2:    %08X\n", block->date);
        }

        printf("    CRC32, partial track\n");
        for (j = 0; j < track->n_blocks_part; j++) {
            block = track->blocks_part + j;

            printf("      block %zd/%zd\n", j + 1, track->n_blocks_part);
            printf("        crc32:    %08X\n", block->crc32);
            printf("        count:    %u\n", block->count);
            printf("        date2:    %08X\n", block->date);
        }
    }
}
#endif


/* XXX EAC
 */
static int
_block_reader_eac(void *userdata, const char *buf, size_t len)
{
    const uint32_t *buf32;
    struct _block_eac *block;
    struct _entry_eac *entry;
    struct _track_eac *track;
    struct _userdata *ud;
    size_t i, j;


    /* If this is the last block, i.e. if the callback is invoked with
     * len == 0, exit successfully if and only if all the buffered
     * data is processed.  Otherwise, append to the existing buffer.
     */
    ud = (struct _userdata *)userdata;
    if (0 && len == 0) { // XXX disabled: this will leak memory!
        len = ud->len;

        if (ud->buf != NULL)
            free(ud->buf);
        ud->capacity = 0;
        ud->len = 0;

        if (len > 0) {
            ne_set_error(ud->session_eac, "%zd unparsed octets remain", len);
            return (-1);
        }
        return (0);
    }

    if (_block_memcat(ud, (uint8_t *)buf, len * sizeof(char)) == NULL) {
        ne_set_error(ud->session_eac, "%s", strerror(errno));
        return (-1);
    }


    /* XXX Only try to process once we have gotten all the data.  Is
     * it possible to parse this as the blocks are coming in?
     *
     * Check for sufficient length first (to avoid buffer overflows)!
     *
     * XXX Note that this function may be called many times for the
     * same data... how to ensure we only run once?
     */
    if (len > 0)
        return (0);

    buf32 = ud->buf;
    if (buf32 + 3 > (const uint32_t *)(ud->buf + ud->len)) {
        ne_set_error(ud->session_eac, "XXX");
        return (-1);
    }

    entry = realloc(ud->result->entry_eac, sizeof(struct _entry_eac));
    if (entry == NULL) {
        ne_set_error(ud->session_eac, "%s", strerror(errno));
        return (-1);
    }
    ud->result->entry_eac = entry;


    /* Note: need to add 1 to n_tracks.  Then, check special ID.
     */
    entry->n_tracks = *buf32++ + 1;
    entry->date = *buf32++;
    if (*buf32++ != 0x9f3c29aa) {
        ne_set_error(ud->session_eac, "XXX");
        return (-1);
    }

    entry->tracks = calloc(entry->n_tracks, sizeof(struct _track_eac));
    if (entry->tracks == NULL) {
        ne_set_error(ud->session_eac, "%s", strerror(errno));
        return (-1);
    }

    for (i = 0; i < entry->n_tracks + 0; i++) {
        if (buf32 + 1 > (const uint32_t *)(ud->buf + ud->len)) {
            ne_set_error(ud->session_eac, "XXX");
            return (-1);
        }

        track = entry->tracks + i;
        track->n_blocks_whole = *buf32++;
        track->blocks_whole = calloc(
            track->n_blocks_whole, sizeof(struct _block_eac));
        if (track->blocks_whole == NULL) {
            ne_set_error(ud->session_eac, "%s", strerror(errno));
            return (-1);
        }

        for (j = 0; j < track->n_blocks_whole; j++) {
            if (buf32 + 3 > (const uint32_t *)(ud->buf + ud->len)) {
                ne_set_error(ud->session_eac, "XXX");
                return (-1);
            }

            block = track->blocks_whole + j;
            block->crc32 = *buf32++;
            block->count = *buf32++;
            block->date = *buf32++;
        }
    }


    /* Check against buffer overrun, then check special ID.
     */
    if (buf32 + 1 > (const uint32_t *)(ud->buf + ud->len) ||
        *buf32++ != 0x6ba2eac3) {
        ne_set_error(ud->session_eac, "XXX");
        return (-1);
    }

    for (i = 0; i < entry->n_tracks + 0; i++) {
        if (buf32 + 1 > (const uint32_t *)(ud->buf + ud->len)) {
            ne_set_error(ud->session_eac, "XXX");
            return (-1);
        }

        track = entry->tracks + i;
        track->n_blocks_part = *buf32++;
        track->blocks_part = calloc(
            track->n_blocks_part, sizeof(struct _block_eac));
        if (track->blocks_part == NULL) {
            ne_set_error(ud->session_eac, "%s", strerror(errno));
            return (-1);
        }

        for (j = 0; j < track->n_blocks_part; j++) {
            if (buf32 + 3 > (const uint32_t *)(ud->buf + ud->len)) {
                ne_set_error(ud->session_eac, "XXX");
                return (-1);
            }

            block = track->blocks_part + j;
            block->crc32 = *buf32++;
            block->count = *buf32++;
            block->date = *buf32++;
        }
    }


    /* Check against buffer overrun, then check special ID.
     */
    if (buf32 + 1 > (const uint32_t *)(ud->buf + ud->len) ||
        *buf32++ != 0x1e4932fe) {
        ne_set_error(ud->session_eac, "XXX");
        return (-1);
    }

    if (buf32 < (const uint32_t *)(ud->buf + ud->len)) {
        printf("XXX TRAILING OCTETS!\n");
        exit(0);
    } else if (buf32 > (const uint32_t *)(ud->buf + ud->len)) {
        printf("XXX NOTREACHED!\n");
        exit(0);
    }

//    _dump_eac_entry(entry);
//    exit(0);
//    printf("All done\n");

/*
FROM ILWT:

TRACK 01:
This is the CRC32            : 0xae978b13
This is the CRC32 (skip zero): 0x980e0d58

TRACK 02:
This is the CRC32            : 0xea3f577a
This is the CRC32 (skip zero): 0xfb43d2b9

TRACK 03:
This is the CRC32            : 0x8ec143f5
This is the CRC32 (skip zero): 0xb4ec7bc2

TRACK 04:
This is the CRC32            : 0xcc7de034
This is the CRC32 (skip zero): 0x5f602e6a

TRACK 05:
This is the CRC32            : 0x53b4c303
This is the CRC32 (skip zero): 0xfb3a86dd

TRACK 06:
This is the CRC32            : 0xf6e2dd27
This is the CRC32 (skip zero): 0x69182399

TRACK 07:
This is the CRC32            : 0x7183a90c
This is the CRC32 (skip zero): 0xc2235a16

TRACK 08:
This is the CRC32            : 0xdfe560b1
This is the CRC32 (skip zero): 0x16066757

TRACK 09:
This is the CRC32            : 0xd9c608d1
This is the CRC32 (skip zero): 0xa642f94c

TRACK 10:
This is the CRC32            : 0x13c2eb43
This is the CRC32 (skip zero): 0x951744ee

TRACK 11:
This is the CRC32            : 0xeac919a3
This is the CRC32 (skip zero): 0xb2d86a8f


FROM FEARLESS:

TRACK 01:
This is the CRC32            : 0x51cb3989
This is the CRC32 (skip zero): 0x235e4e12

TRACK 02:
This is the CRC32            : 0xad2c3af6
This is the CRC32 (skip zero): 0xd390738d

TRACK 03:
This is the CRC32            : 0x76a13ba8
This is the CRC32 (skip zero): 0xd93b05dd

TRACK 04:
This is the CRC32            : 0xc1ab6142
This is the CRC32 (skip zero): 0xe58a2caa

TRACK 05:
This is the CRC32            : 0x0837d9e7
This is the CRC32 (skip zero): 0xe2943c01

TRACK 06:
This is the CRC32            : 0xefe03478
This is the CRC32 (skip zero): 0xacbd0e79

TRACK 07:
This is the CRC32            : 0xeeffa772
This is the CRC32 (skip zero): 0x4a78c198

TRACK 08:
This is the CRC32            : 0xe3a2e002
This is the CRC32 (skip zero): 0x2935da47

TRACK 09:
This is the CRC32            : 0x7efd2eee
This is the CRC32 (skip zero): 0x3c69bb4a

TRACK 10:
This is the CRC32            : 0xcd888289
This is the CRC32 (skip zero): 0x15e4eedd

TRACK 11:
This is the CRC32            : 0x70f000f1
This is the CRC32 (skip zero): 0x14c51a76

TRACK 12:
This is the CRC32            : 0xf88f6045
This is the CRC32 (skip zero): 0xc4aa5946

TRACK 13:
This is the CRC32            : 0x68a9d80e
This is the CRC32 (skip zero): 0xc4f1e25e
*/

    return (0);
}
#endif


static int
_block_reader_localhost(void *userdata, const char *buf, size_t len)
{
//    const uint32_t *buf32;
//    struct _block_eac *block;
//    struct _entry_eac *entry;
//    struct _track_eac *track;
    struct _userdata *ud;
//    size_t i, j;


    /* If this is the last block, i.e. if the callback is invoked with
     * len == 0, exit successfully if and only if all the buffered
     * data is processed.  Otherwise, append to the existing buffer.
     */
    ud = (struct _userdata *)userdata;
    if (0 && len == 0) { // XXX disabled: this will leak memory!
        len = ud->len;

        if (ud->buf != NULL)
            free(ud->buf);
        ud->capacity = 0;
        ud->len = 0;

        if (len > 0) {
            ne_set_error(ud->session_eac, "%zd unparsed octets remain", len);
            return (-1);
        }
        return (0);
    }

    if (_block_memcat(ud, (uint8_t *)buf, len * sizeof(char)) == NULL) {
        ne_set_error(ud->session_eac, "%s", strerror(errno));
        return (-1);
    }


    /* XXX Only try to process once we have gotten all the data.  Is
     * it possible to parse this as the blocks are coming in?
     *
     * Check for sufficient length first (to avoid buffer overflows)!
     *
     * XXX Note that this function may be called many times for the
     * same data... how to ensure we only run once?
     */
    if (len > 0)
        return (0);

    printf("THIS IS WHAT WE GOT [%zd] ->%s<-\n", ud->len, (char *)ud->buf);

    return (0);
}


/* The _sum_digits() function returns the sum of all the decimal
 * digits in its argument @p x.
 */
static int
_sum_digits(unsigned long x)
{
    int s;
    for (s = 0; x > 0; x /= 10)
        s += x % 10;
    return (s);
}


/* XXX Documentation, sets errno, etc, etc
 *
 * XXX Maybe it does make sense with a separate function for the URL
 * in order to distinguish between failure modes which warrant retry
 * and fatal errors [memory exhausting, etc].  This is why _mkpath()
 * was implemented as a separate function.
 *
 * See https://en.wikipedia.org/wiki/CDDB.
 *
 * @bug This function is broken for discs with data tracks: see
 * Coldplay's Live album.
 *
 * @param disc
 * @return The URI-escaped, RFC-blablabla-compliant path.  This path
 *         is guaranteed to conform to the abs_path definition in
 *         RFC2396 and URI-escaped XXX verify that it is so.  May be
 *         used as an argument to free(3).
 */
static char *
_mkpath(Mb5Disc disc)
{
    Mb5Offset offset;
    Mb5OffsetList offset_list;
    char *path;
    void *p;
    int i, j, l, n, o;
    uint32_t cddbDiscId, discId1, discId2;


    /* Compute the disc identifiers from the sector offsets of its
     * tracks.  Note that all identifiers are computed relative to the
     * end of the initial 2-second, or equivalently, 150-sector, gap.
     * The corrected sector-offset of the first track, l, is needed to
     * calculate the length of disc later.
     */
    offset_list = mb5_disc_get_offsetlist(disc);
    if (offset_list == NULL) {
        errno = ENOMSG;
        return (NULL);
    }
    n = mb5_offset_list_size(offset_list);

    cddbDiscId = discId1 = discId2 = 0;
    for (i = 0; i < n; i++) {
        offset = mb5_offset_list_item(offset_list, i); // XXX offset == NULL?
        o = mb5_offset_get_offset(offset);

        if (i == 0) {
            if (o < 150) {
                errno = EINVAL;
                return (NULL);
            }
            l = o - 150;
        }
        o -= 150;

        cddbDiscId += _sum_digits(o / 75 + 2);
        discId1 += o;
        discId2 += (o == 0 ? 1 : o) * mb5_offset_get_position(offset);

//        printf("ADDING %02d: %d\n", mb5_offset_get_position(offset), o);
    }


    /* Finalise the calculation of the identifiers using the leadout
     * offset, the offset of the first sector after the last track of
     * the disc.  The leadout offset may only be defined if the disc
     * contains at least one track, in which case the loop above
     * guarantees that it is greater or equal to 150 sectors.
     *
     * XXX Second term was ((o / 75 - 0 / 75) << 8), should work now!
     */
    if (n <= 0) {
        errno = ENOMSG;
        return (NULL);
    }
#if 1
    o = mb5_disc_get_sectors(disc) - 150;

    cddbDiscId = ((cddbDiscId % 255) << 24) + ((o / 75 - l / 75) << 8) + n;
#else
    // leadout and data track for ILWT
    o = 332849 + 1;
    cddbDiscId += _sum_digits(209765 / 75 + 2);

    cddbDiscId = ((cddbDiscId % 255) << 24) + ((o / 75 - l / 75) << 8) + n + 1;
#endif
    discId1 += o;
    discId2 += o * (n + 1);


    /* Construct the fixed-length path from the disc identifiers, 58
     * characters plus a trailing null-character, in a loop that is
     * guaranteed to terminate.
     */
    path = NULL;
    for (i = 58 + 1, j = i + 1; i < j; i = j + 1) {
        p = realloc(path, i * sizeof(char));
        if (p == NULL) {
            if (path != NULL)
                free(path);
            return (NULL);
        }
        path = p;

        j = snprintf(
            path, i,
            "/accuraterip/%.1x/%.1x/%.1x/dBAR-%.3d-%.8x-%.8x-%.8x.bin",
            discId1 & 0xf,
            (discId1 >> 4) & 0xf,
            (discId1 >> 8) & 0xf,
            n,
            discId1,
            discId2,
            cddbDiscId);
    }

#if 0
    char ID[256];
    mb5_disc_get_id(disc, ID, 256);
    printf("HATTNE 2 is checking ->%s<- DIFF %.8x (%d)\n",
           ID, 0x0013682b - discId1, 0x0013682b - discId1);
    printf("HATTNE 2 is checking ->%s<- sectors %d\n",
           path, mb5_disc_get_sectors(disc));
    exit(0);
#endif

    return (path);
}


#ifdef USE_EAC
static char *
_mkpath_eac(Mb5Disc disc)
{
    ne_buffer *id;
    char *path;
    Mb5OffsetList offset_list;
    int d, m, n;
    unsigned char *u64;
    size_t i, len;


    /* XXX EAC
     *
     * Why the base64 encoding?  I would think DiscID are composed of
     * some sensible character set already.
     */
#if 0
    // Metallica - Load
    const char *id = "lI.6b5AOMYmZCHgbpkcXlFbyF5o-";
    size_t n_tracks = 14;
#endif

#if 0
    // bob hund - bob hund [404]
    const char *id = "dvjO8hoVkv7a_eAcn7iEet82RgY-";
    size_t n_tracks = 6;
#endif

#if 0
    // The Ark - ILWT
    const char *id = "7vqa6F_exuZ4paBsKHn0GO.tCrw-";
    size_t n_tracks = 11;
#endif

#if 0
    // Taylor Swift - Fearless
    const char *id = "PxZK3ZKKn.CQSSNworNSCmRGL1U-";
    size_t n_tracks = 13;
#endif

#if 0
    // Queen + Vanguard - Flash [404]
    // In FreeDB: http://www.freedb.org/freedb/rock/2806f604
    // In AccurateRip: DiscID: 00035015-000bc47d-2806f604
    const char *id = "fRovc1tARQUreSsbP4zvcg6HCO4-";
    size_t n_tracks = 3;
#endif

#if 0
    // Coldplay - Live 2003
    const char *id = "ELycL.Syz4kvJT.qkCNjBvHRSJA-";
    size_t n_tracks = 12;

    // MB5 DiscID WITH data track: 55_SX389JIzIwI6973Tm1zYzU3k-
    // 404
//    const char *id = "55_SX389JIzIwI6973Tm1zYzU3k-";
//    size_t n_tracks = 13;
#endif

#if 0
    // Rage against the machine - Renegades
    const char *id = "JqOFWCbyVERQL3LwLc_uDM_YW7U-";
    size_t n_tracks = 14;
#endif

#if 0
    // Duke Ellington - The Count meets The Duke
    const char *id = "5BJng9Sw7t5H6danZiEOEARr9A4-";
    size_t n_tracks = 16;
#endif


    /* MusicBrainz disc identifiers are 28 characters, plus one
     * character for NULL-termination.
     *
     * Get the MusicBrainz disc ID for this medium.
     */
    id = ne_buffer_ncreate(28 + 1);
    mb5_disc_get_id(disc, id->data, id->length);
    for (i = 0; i < id->length; i++) {
        switch (id->data[i]) {
        case '.':
            id->data[i] = '+';
            break;
        case '_':
            id->data[i] = '/';
            break;
        case '-':
            id->data[i] = '=';
            break;
        default:
            break;
        }
    }
    ne_buffer_altered(id);

    len = ne_unbase64(id->data, &u64);
    if (len <= 0) {
        ne_buffer_destroy(id);
        errno = EINVAL; // XXX DECODING ERROR
        return (NULL);
    }

    ne_buffer_clear(id);
    ne_buffer_snprintf(id, 13 + 1, "/crc/%1X/%1X/%1X/%1X/",
                       (u64[0] & 0xf0) >> 4, u64[0] & 0x0f,
                       (u64[1] & 0xf0) >> 4, u64[1] & 0x0f);

    for (i = 0; i < len; i++)
        ne_buffer_snprintf(id, 2 + 1, "%02X", u64[i]);
    free(u64);


    /* Determine n, the number of audio tracks on the disc, and the d,
     * the number of digits in n.
     */
    offset_list = mb5_disc_get_offsetlist(disc);
    if (offset_list == NULL) {
        ne_buffer_destroy(id);
        errno = ENOMSG;
        return (NULL);
    }
    n = mb5_offset_list_size(offset_list);
    if (n <= 0) {
        ne_buffer_destroy(id);
        errno = ENOMSG;
        return (NULL);
    }

    for (d = 0, m = n; m > 0; d++, m /= 10)
        ;
    ne_buffer_snprintf(id, d + 5 + 1, "-%d.bin", n);
    path = strndup(id->data, id->used - 1);
    ne_buffer_destroy(id);

    return (path);
}
#endif


/* Will return +1 if the disc is nonsense (first offset less than 150,
 * leadout_offset less than 150, no offset_list, not in the database,
 * etc) if this happens, one should try the next disc.
 *
 * XXX This documentation needs more work!  Better named
 * _get_response()?
 *
 * The return value must not be modified by the caller [returns a
 * pointer to an internal cache entry].
 *
 * @param ctx  XXX AccurateRip context
 * @param path The path as constructed by _mkpath() XXX Make sure it's
 *             not leaked!
 * @return     -1 error [<=> NULL]
 *             +1 No result found in AccurateRip [nmemb == 0]
 *             0 Success
 */
static const struct _cache *
_get_accuraterip(struct accuraterip_context *ctx, const char *path)
{
    struct _userdata ud;
    struct gzip_context *gc;
    ne_request *request;
//    const ne_status *status;
    void *p;
    size_t i;
    int ret;


    /* Traverse the cache in reverse order and return the first
     * matching entry.
     */
    ctx->hit_rate[1] += 1;
    for (i = ctx->nmemb; i-- > 0; ) {
        if (strcmp(ctx->cache[i].path, path) == 0) {
            ctx->hit_rate[0] += 1;
            return (ctx->cache + i);
        }
    }
//    printf("   Looking up ->%s<-\n", path);


    /* Create and initialise a new result entry in the cache.
     * Initialise the userdata structure for _block_reader().
     */
    p = realloc(ctx->cache, (ctx->nmemb + 1) * sizeof(struct _cache));
    if (p == NULL) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (NULL);
    }
    ctx->cache = p;

    ctx->cache[ctx->nmemb].entries = NULL;
    ctx->cache[ctx->nmemb].entry_eac = NULL;
    ctx->cache[ctx->nmemb].error = NULL;
    ctx->cache[ctx->nmemb].path = NULL;
    ctx->cache[ctx->nmemb].nmemb = 0;
    ctx->cache[ctx->nmemb].status = -1;

    ud.result = ctx->cache + ctx->nmemb;
    ud.session = ctx->session;
    ud.buf = NULL;
    ud.len = 0;
    ud.capacity = 0;


    /* Create the request, only accepting successful responses, and
     * ensure not to dispatch more requests per unit time than are
     * allowed.  Note that the ne_request_create() function cannot
     * fail, and that the case where there is no entry in the
     * AccurateRip database (code 404) is handled separately.
     *
     * XXX Synchronise comment with AcoustID ditto.
     */
    gc = gzip_new(ctx->session, _block_reader, &ud);
    if (gc == NULL) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (NULL); // XXX should be -1?
    }

#if 0
    /* Tour de France: nodata
     *
FreeDBIdent:            0x2303ea03
TrackOffsetsAdded:      134136
TrackOffsetsMultiplied: 463364
Chunk 1/3:
  CRC:        0xe6c80974
  unk:        0xec89da94
  Confidence: 2
Chunk 2/3:
  CRC:        0x10391d71
  unk:        0xf5a9f194
  Confidence: 2
Chunk 3/3:
  CRC:        0xe9e5348b
  unk:        0x1b3e4725
  Confidence: 2
    */
    path = "/accuraterip/8/f/b/dBAR-003-00020bf8-00071204-2303ea03.bin";
#endif
#if 0
    /* Tour de France: data (note mismatch between 003 and last 04;
     * will get a 404 if replacing 003 with 004).

FreeDBIdent:            0x30065d04
TrackOffsetsAdded:      181162
TrackOffsetsMultiplied: 651468
Chunk 1/3:
  CRC:        0xe6c80974
  unk:        0xec89da94
  Confidence: 72
Chunk 2/3:
  CRC:        0x10391d71
  unk:        0xf5a9f194
  Confidence: 71
Chunk 3/3:
  CRC:        0xe9e5348b
  unk:        0x1b3e4725
  Confidence: 71
FreeDBIdent:            0x30065d04
TrackOffsetsAdded:      181162
TrackOffsetsMultiplied: 651468
Chunk 1/3:
  CRC:        0x2f9c1b65
  unk:        0x00000000
  Confidence: 62
Chunk 2/3:
  CRC:        0xdb989eef
  unk:        0x00000000
  Confidence: 62
Chunk 3/3:
  CRC:        0xe889436d
  unk:        0x00000000
  Confidence: 61
     */
    path = "/accuraterip/a/a/3/dBAR-003-0002c3aa-0009f0cc-30065d04.bin";
#endif
    printf("PATH: %s\n", path);

    request = ne_request_create(ctx->session, "GET", path);
    ne_add_request_header(request,
                          "Accept-Encoding",
                          "gzip");
    ne_add_response_body_reader(
        request, ne_accept_2xx, gzip_inflate_reader, gc);
    if (ratelimit_accuraterip() != 0) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (NULL);
    }


    /* The return value of ne_request_dispatch() only indicates
     * whether the request was sent and its response was read
     * successfully.  The status as retrieved by ne_get_status() only
     * makes sense if ne_request_dispatch() succeeded.
     *
     * XXX Update and synchronise comment!
     */
    ret = ne_request_dispatch(request);
    switch (ret) {
    case NE_OK:
//        printf("SESSION STATUS: ->%s<- klass %d\n",
//               ne_get_error(ctx->session), ne_get_status(request)->klass);

        switch (ne_get_status(request)->klass) {
        case 2:
            /* Successful lookup: increase the count of cached
             * responses.
             */
            ctx->nmemb += 1;
            ud.result->path = strdup(path);
            if (ud.result->path == NULL) {
                ne_set_error(ctx->session, "%s", strerror(errno));
                return (NULL);
            }
            ud.result->status = 0;
            break;

        case 4:
            /* Handle 404 separately.  XXX This should probably be
             * cached, too, because repeated lookup should yield the
             * same result.  Same applies to the case below.
             */
            ud.result->status = +1;
            break;

        default:
            ud.result->status = -1;
            break;
        }
        break;

    default:
        printf("SESSION FAILED: ->%s<-\n", ne_get_error(ctx->session));
        ud.result->status = -1;
#if 0
        // XXX Does this snippet belong anywhere?
        if (ret == NE_ERROR || ne_xml_failed(parser) != 0) {
            ne_set_error(
                ctx->session, "XML error: %s", ne_xml_get_error(parser));
        }
#endif
        break;
    }

//    printf("Marker #3\n");

    /* Destroy the dispatched request, because it cannot be used
     * again.
     */
    ne_request_destroy(request);
    if (gzip_free(gc) != 0) {
        ne_set_error(ctx->session, "%s", strerror(errno));
        return (NULL); // XXX Should be -1?
    }
    return (ud.result);
}


#if USE_EAC
/* XXX EAC
 */
static const struct _cache *
_get_eac(struct accuraterip_context *ctx, const char *path)
{
    struct _userdata ud;
    struct gzip_context *gc;
    ne_request *request;
//    const ne_status *status;
    void *p;
    size_t i;
    int ret;


    /* Traverse the cache in reverse order and return the first
     * matching entry.
     */
    ctx->hit_rate[1] += 1;
    for (i = ctx->nmemb; i-- > 0; ) {
        if (strcmp(ctx->cache[i].path, path) == 0) {
            ctx->hit_rate[0] += 1;
            return (ctx->cache + i);
        }
    }
//    printf("   Looking up ->%s<-\n", path);


    /* Create and initialise a new result entry in the cache.
     * Initialise the userdata structure for _block_reader().
     */
    p = realloc(ctx->cache, (ctx->nmemb + 1) * sizeof(struct _cache));
    if (p == NULL) {
        ne_set_error(ctx->session_eac, "%s", strerror(errno));
        return (NULL);
    }
    ctx->cache = p;

    ctx->cache[ctx->nmemb].entries = NULL;
    ctx->cache[ctx->nmemb].entry_eac = NULL;
    ctx->cache[ctx->nmemb].error = NULL;
    ctx->cache[ctx->nmemb].path = NULL;
    ctx->cache[ctx->nmemb].nmemb = 0;
    ctx->cache[ctx->nmemb].status = -1;

    ud.result = ctx->cache + ctx->nmemb;
    ud.session_eac = ctx->session_eac;
    ud.buf = NULL;
    ud.len = 0;
    ud.capacity = 0;


    /* Create the request, only accepting successful responses, and
     * ensure not to dispatch more requests per unit time than are
     * allowed.  Note that the ne_request_create() function cannot
     * fail, and that the case where there is no entry in the
     * AccurateRip database (code 404) is handled separately.
     *
     * XXX Synchronise comment with AcoustID ditto.
     */
    gc = gzip_new(ctx->session_eac, _block_reader_eac, &ud);
    if (gc == NULL) {
        ne_set_error(ctx->session_eac, "%s", strerror(errno));
        return (NULL); // XXX should be -1?
    }

    printf("Attempting to fetch ->%s<-\n", path);

    request = ne_request_create(ctx->session_eac, "GET", path);
    ne_add_request_header(request,
                          "Accept-Encoding",
                          "gzip");
    ne_add_response_body_reader(
        request, ne_accept_2xx, gzip_inflate_reader, gc);
    if (ratelimit_accuraterip() != 0) {
        ne_set_error(ctx->session_eac, "%s", strerror(errno));
        return (NULL);
    }


    /* The return value of ne_request_dispatch() only indicates
     * whether the request was sent and its response was read
     * successfully.  The status as retrieved by ne_get_status() only
     * makes sense if ne_request_dispatch() succeeded.
     *
     * XXX Update and synchronise comment!
     */
    ret = ne_request_dispatch(request);
    switch (ret) {
    case NE_OK:
//        printf("SESSION STATUS: ->%s<- klass %d\n",
//               ne_get_error(ctx->session_eac), ne_get_status(request)->klass);

        switch (ne_get_status(request)->klass) {
        case 2:
            /* Successful lookup: increase the count of cached
             * responses.
             */
            printf("GOT CASE 2\n");
            ctx->nmemb += 1;
            ud.result->path = strdup(path);
            if (ud.result->path == NULL) {
                ne_set_error(ctx->session_eac, "%s", strerror(errno));
                return (NULL);
            }
            ud.result->status = 0;
            break;

        case 4:
            /* Handle 404 separately.  XXX This should probably be
             * cached, too, because repeated lookup should yield the
             * same result.  Same applies to the case below.
             */
            printf("GOT CASE 4\n");
            ud.result->status = +1;
            break;

        default:
            printf("GOT CASE default\n");
            ud.result->status = -1;
            break;
        }
        break;

    default:
        printf("SESSION FAILED: ->%s<-\n", ne_get_error(ctx->session_eac));
        ud.result->status = -1;
#if 0
        // XXX Does this snippet belong anywhere?
        if (ret == NE_ERROR || ne_xml_failed(parser) != 0) {
            ne_set_error(
                ctx->session_eac, "XML error: %s", ne_xml_get_error(parser));
        }
#endif
        break;
    }


    /* Destroy the dispatched request, because it cannot be used
     * again.
     */
    ne_request_destroy(request);
    if (gzip_free(gc) != 0) {
        ne_set_error(ctx->session_eac, "%s", strerror(errno));
        return (NULL); // XXX Should be -1?
    }
    return (ud.result);
}
#endif


/* XXX Maybe path is redundant here: deal with the fallback logic
 * outside this function?
 */
static const struct _cache *
_get_localhost(struct accuraterip_context *ctx, const char *discid, const char *path)
{
    struct _userdata ud;
    struct gzip_context *gc;
    ne_request *request;
//    const ne_status *status;
    void *p;
//    size_t i;
    int ret;

#if 0
    /* Traverse the cache in reverse order and return the first
     * matching entry.
     */
    ctx->hit_rate[1] += 1;
    for (i = ctx->nmemb; i-- > 0; ) {
        if (strcmp(ctx->cache[i].path, path) == 0) {
            ctx->hit_rate[0] += 1;
            return (ctx->cache + i);
        }
    }
//    printf("   Looking up ->%s<-\n", path);
#endif


    /* Create and initialise a new result entry in the cache.
     * Initialise the userdata structure for _block_reader().
     */
    p = realloc(ctx->cache, (ctx->nmemb + 1) * sizeof(struct _cache));
    if (p == NULL) {
        ne_set_error(ctx->session_localhost, "%s", strerror(errno));
        return (NULL);
    }
    ctx->cache = p;

    ctx->cache[ctx->nmemb].entries = NULL;
    ctx->cache[ctx->nmemb].entry_eac = NULL;
    ctx->cache[ctx->nmemb].error = NULL;
    ctx->cache[ctx->nmemb].path = NULL;
    ctx->cache[ctx->nmemb].nmemb = 0;
    ctx->cache[ctx->nmemb].status = -1;

    ud.result = ctx->cache + ctx->nmemb;
    ud.session_localhost = ctx->session_localhost;
    ud.buf = NULL;
    ud.len = 0;
    ud.capacity = 0;


    /* Create the request, only accepting successful responses, and
     * ensure not to dispatch more requests per unit time than are
     * allowed.  Note that the ne_request_create() function cannot
     * fail, and that the case where there is no entry in the
     * AccurateRip database (code 404) is handled separately.
     *
     * XXX Synchronise comment with AcoustID ditto.
     */
    gc = gzip_new(ctx->session_localhost, _block_reader_localhost, &ud);
    if (gc == NULL) {
        ne_set_error(ctx->session_localhost, "%s", strerror(errno));
        return (NULL); // XXX should be -1?
    }

    printf("Attempting to fetch ->%s<- ->%s<-\n", discid, path);

    request = ne_request_create(ctx->session_localhost, "GET", discid);
    ne_add_request_header(request,
                          "Accept-Encoding",
                          "gzip");
    ne_add_response_body_reader(
        request, ne_accept_2xx, gzip_inflate_reader, gc);
#if 0
    if (ratelimit_accuraterip() != 0) {
        ne_set_error(ctx->session_localhost, "%s", strerror(errno));
        return (NULL);
    }
#endif


    /* The return value of ne_request_dispatch() only indicates
     * whether the request was sent and its response was read
     * successfully.  The status as retrieved by ne_get_status() only
     * makes sense if ne_request_dispatch() succeeded.
     *
     * XXX Update and synchronise comment!
     */
    ret = ne_request_dispatch(request);
    switch (ret) {
    case NE_OK:
//        printf("SESSION STATUS: ->%s<- klass %d\n",
//               ne_get_error(ctx->session_localhost), ne_get_status(request)->klass);

        switch (ne_get_status(request)->klass) {
        case 2:
            /* Successful lookup: increase the count of cached
             * responses.
             */
            printf("GOT CASE 2\n");
            ctx->nmemb += 1;
            ud.result->path = strdup(path);
            if (ud.result->path == NULL) {
                ne_set_error(ctx->session_localhost, "%s", strerror(errno));
                return (NULL);
            }
            ud.result->status = 0;
            break;

        case 4:
            /* Handle 404 separately.  XXX This should probably be
             * cached, too, because repeated lookup should yield the
             * same result.  Same applies to the case below.
             */
            printf("GOT CASE 4\n");
            ud.result->status = +1;
            /* XXX Better construct path from disc here, but then
             * connection issues should also go this route.
             *
             * XXX This should not bypass the cache!
             */
            return (_get_accuraterip(ctx, path));

        default:
            printf("GOT CASE default, klass %d\n", ne_get_status(request)->klass);
            ud.result->status = -1;
            break;
        }
        break;

    case NE_CONNECT:
        /* "Connection refused" (the server is not running).  XXX
         * Should treat this the same way as a 404.
         */
       ud.result->status = +1;
       return (_get_accuraterip(ctx, path));

    case NE_REDIRECT:
        /* This is the interesting bit
         */
    {
        const ne_uri *redirect = ne_redirect_location(ctx->session_localhost);
        if (redirect != NULL) {
            printf("Got the redirect: chase ->%p<-\n", redirect);
            printf("  Scheme   ->%s<-\n", redirect->scheme);
            printf("  Host     ->%s<-\n", redirect->host);
            printf("  Userinfo ->%s<-\n", redirect->userinfo);
            printf("  Port     ->%d<-\n", redirect->port);
            printf("  Path     ->%s<-\n", redirect->path);
            printf("  Query    ->%s<-\n", redirect->query);
            printf("  Fragment ->%s<-\n", redirect->fragment);
            printf("  FALLBACK ->%s<-\n", path);

            /* XXX Is the session_localhost still sending requests to
             * localhost, or will it have to be reinitialised?  Seems
             * it won't have to be reinitialised.
             *
             * XXX What about the case where redirect cannot be
             * parsed?  And redirect must be freed?
             */
            return (_get_accuraterip(ctx, redirect->path));
        }
    }

    default:
        printf("SESSION FAILED [%d]: ->%s<-\n", ret, ne_get_error(ctx->session_localhost));
        ud.result->status = -1;
#if 0
        // XXX Does this snippet belong anywhere?
        if (ret == NE_ERROR || ne_xml_failed(parser) != 0) {
            ne_set_error(
                ctx->session_localhost, "XML error: %s", ne_xml_get_error(parser));
        }
#endif
        break;
    }


    /* Destroy the dispatched request, because it cannot be used
     * again.
     */
    ne_request_destroy(request);
    if (gzip_free(gc) != 0) {
        ne_set_error(ctx->session_localhost, "%s", strerror(errno));
        return (NULL); // XXX Should be -1?
    }
    return (ud.result);
}


/* Query the AccurateRip database.  The response must not be freed
 * because it is internal to the cache.
 *
 * Made public 2016-04-27 for tagger refactor
 */
const struct _cache *
accuraterip_get(struct accuraterip_context *ctx, Mb5Disc disc)
{
    const struct _cache *response;
    char *path;


    /* XXX See other invocations in this file and the checking of
     * errno.
     */
    path = _mkpath(disc);
    if (path == NULL)
        return (NULL);


    /* Fail if the disc is not found in the AccurateRip database.
     */
    response = _get_accuraterip(ctx, path);
    free(path);
    if (response == NULL || response->status != 0)
        return (NULL);

    return (response);
}


#if USE_EAC
const struct _cache *
accuraterip_eac_get(struct accuraterip_context *ctx, Mb5Disc disc)
{
    const struct _cache *response;
    char *path;


    /* XXX See other invocations in this file and the checking of
     * errno.
     */
    path = _mkpath_eac(disc);
    if (path == NULL)
        return (NULL);


    /* Fail if the disc is not found in the AccurateRip database.
     */
    response = _get_eac(ctx, path);
    free(path);
    if (response == NULL || response->status != 0)
        return (NULL);
    return (response);
}
#endif

const struct _cache *
accuraterip_localhost_get(struct accuraterip_context *ctx, Mb5Disc disc)
{
    const struct _cache *response;
    ne_buffer *id;
    char *path;


    /* XXX See other invocations in this file and the checking of
     * errno.
     */
    path = _mkpath(disc);
    if (path == NULL)
        return (NULL);

    id = ne_buffer_ncreate(28 + 1);
    mb5_disc_get_id(disc, id->data, id->length);
    ne_buffer_altered(id);


    /* Fail if the disc is not found in the AccurateRip database.
     */
    response = _get_localhost(ctx, id->data, path);
    free(path);
    ne_buffer_destroy(id);
    if (response == NULL || response->status != 0)
        return (NULL);

    return (response);
}


/**** REFACTOR START ****/

/* Return the smallest position from all the media containing a
 * recording with id @p id in @p medium_list greater or equal to @p
 * pos.  The function returns <0 if there no such medium exists.
 *
 * XXX Candidate for a new configuration/layout module?
 *
 * @param ids ID of the recording that must be contained
 * @param nmemb Number of identifiers
 * @param pos Threshold
 */
#if 0
static int
_next_track(
    Mb5TrackList track_list, struct fp3_recording_list *recordings, int pos)
{
    Mb5Recording recording;
    Mb5Track track;
    ne_buffer *id;
    size_t j;
    int i, p, pos_min;


    /* MusicBrainz identifiers are 36 characters, plus one character
     * for NULL-termination.
     */
    id = ne_buffer_ncreate(36 + 1);
    pos_min = -1;

    for (i = 0; i < mb5_track_list_size(track_list); i++) {
        track = mb5_track_list_item(track_list, i);
        if (track == NULL)
            continue;


        /* Skip the track if its position is smaller than the
         * threshold.
         */
        p = mb5_track_get_position(track);
        if (p < pos)
            continue;


        /* Skip the track if it does not match the requested
         * recording.
         */
        recording = mb5_track_get_recording(track);
        if (recording == NULL)
            continue;

        ne_buffer_grow(id, mb5_recording_get_id(recording, NULL, 0) + 1);
        mb5_recording_get_id(recording, id->data, id->length);
        ne_buffer_altered(id);

        for (j = 0; j < recordings->nmemb; j++) {
            if (strcmp(id->data, recordings->recordings[j]->id) == 0)
                break;
        }
        if (j == recordings->nmemb)
            continue;


        /* Update the smallest position if this is the first valid
         * track, or if the current position is the smallest so far.
         */
        if (pos_min < 0 || p < pos_min)
            pos_min = p;
    }

    ne_buffer_destroy(id);
    return (pos_min);
}
#endif


/* This whole configuration business may be suited for its own module
 * and for reuse earlier
 *
 * XXX Candidate for a new configuration/layout module?
 *
 * XXX Should return a medium instead?
 */
#if 0
static int
_next_medium(
    Mb5MediumList medium_list, struct fp3_recording_list *recordings, int pos)
{
    Mb5Medium medium;
    Mb5TrackList track_list;
    ne_buffer *format;
    int i, p, pos_min;


    /* XXX What's the longest format?  CD?  DVD?
     */
    format = ne_buffer_ncreate(36 + 1);
    pos_min = -1;

    for (i = 0; i < mb5_medium_list_size(medium_list); i++) {
        medium = mb5_medium_list_item(medium_list, i);
        if (medium == NULL)
            continue;


        /* XXX Added 2015-09-03: skip the medium if it aint't a CD.
         */
        ne_buffer_grow(format, mb5_medium_get_format(medium, NULL, 0) + 1);
        mb5_medium_get_format(medium, format->data, format->length);
        ne_buffer_altered(format);
        if (strcmp(format->data, "CD") != 0)
            continue;


        /* Skip the medium if its position is smaller than the
         * threshold.
         */
        p = mb5_medium_get_position(medium);
        if (p < pos)
            continue;


        /* Skip the medium if none of its tracks match the requested
         * recording.  XXX Could possible make use of _next_track()
         * here?
         */
        track_list = mb5_medium_get_tracklist(medium);
        if (track_list == NULL || _next_track(track_list, recordings, 0) < 0)
            continue;


        /* Update the smallest position if this is the first valid
         * medium, or if the current position is the smallest so far.
         */
        if (pos_min < 0 || p < pos_min)
            pos_min = p;
    }

    ne_buffer_destroy(format);

    return (pos_min);
}
#endif


/* The _medium_at_position() function returns the Mb5Medium from @p
 * medium_list at position @p position.  If @p medium_list does not
 * contain a medium at position @p position, the function returns @c
 * NULL.
 *
 * XXX Candidate for a new configuration/layout module?
 */
#if 0
static Mb5Medium
_medium_at_position(Mb5MediumList medium_list, int position)
{
    Mb5Medium medium;
    int i;

    for (i = 0; i < mb5_medium_list_size(medium_list); i++) {
        medium = mb5_medium_list_item(medium_list, i);
        if (medium != NULL && mb5_medium_get_position(medium) == position)
            return (medium);
    }

    return (NULL);
}
#endif


/* Get the track list of the current medium.  This should always
 * generate a valid configuration (no, I don't think so).  Comment on that!
 *
 * XXX Candidate for a new configuration/layout module?
 *
 * XXX Could make the positions struct into its own array (which would
 * mean combining it with the nmemb member, and return a pointer to an
 * allocated object like that instead?
 *
 * 0 Success
 * -1 Failure
 */
#if 0
static int
_first_configuration(
    Mb5MediumList medium_list, struct fp3_release *release, struct _position *pos)
{
    Mb5Medium medium;
    Mb5TrackList track_list;
    size_t i;

    for (i = 0; i < release->nmemb; i++) {
        pos[i].medium = _next_medium(medium_list, release->recordings2[i], 0);
        if (pos[i].medium < 0)
            return (-1);

        medium = _medium_at_position(medium_list, pos[i].medium);
        if (medium == NULL)
            return (-1);

        track_list = mb5_medium_get_tracklist(medium);
        if (track_list == NULL)
            return (-1);

        pos[i].track = _next_track(track_list, release->recordings2[i], 0);
        if (pos[i].track < 0)
            return (-1);
    }

    return (0);
}
#endif


/* XXX This should always generate a valid configuration (no I don't
 * think so)!  Maybe this is really stuff for another module?
 *
 * XXX Candidate for a new configuration/layout module?
 *
 * -1 failure
 * 0 OK
 * +1 exhausted
 */
#if 0
static int
_next_configuration(
    Mb5MediumList medium_list, struct fp3_release *release, struct _position *pos, size_t nmemb)
{
    Mb5Medium medium;
    Mb5TrackList track_list;
    size_t i;
    int newpos;


    /* Get track list of current medium
     *
     * Attempt to increase a medium position, leaving the track
     * positions untouched.  OR THE OTHER WAY AROUND?
     *
     * It was possible to increase this medium position--reset
     * all previous tracks to their initial value.
     *
     * Significant overlap to _first_configuration() function.  This
     * is like a generator in Python.
     */
    for (i = 0; i < nmemb; i++) {
        medium = _medium_at_position(medium_list, pos[i].medium);
        if (medium == NULL)
            return (-1);

        track_list = mb5_medium_get_tracklist(medium);
        if (track_list == NULL)
            return (-1);

        newpos = _next_track(
            track_list, release->recordings2[i], pos[i].track + 1);
        if (newpos > 0) {
            pos[i].track = newpos;

            while (i-- > 0) {
                pos[i].medium = _next_medium(
                    medium_list, release->recordings2[i], 0);
                if (pos[i].medium < 0)
                    return (-1);

                medium = _medium_at_position(medium_list, pos[i].medium);
                if (medium == NULL)
                    return (-1);

                track_list = mb5_medium_get_tracklist(medium);
                if (track_list == NULL)
                    return (-1);

                pos[i].track = _next_track(
                    track_list, release->recordings2[i], 0);
                if (pos[i].track < 0)
                    return (-1);
            }

            return (0);
        }
    }


    /* Attempt to increase a medium position, resetting the track
     * numbers if it succeeds.
     *
     * Success: reset track of current stream, track and medium of all
     * previous streams.
     */
    for (i = 0; i < nmemb; i++) {
        newpos = _next_medium(
            medium_list, release->recordings2[i], pos[i].medium + 1);
        if (newpos > 0) {
            pos[i].medium = newpos;

            while (i-- > 0) {
                pos[i].medium = _next_medium(
                    medium_list, release->recordings2[i], 0);
                if (pos[i].medium < 0)
                    return (-1);

                medium = _medium_at_position(medium_list, pos[i].medium);
                if (medium == NULL)
                    return (-1);

                track_list = mb5_medium_get_tracklist(medium);
                if (track_list == NULL)
                    return (-1);

                pos[i].track = _next_track(
                    track_list, release->recordings2[i], 0);
                if (pos[i].track < 0)
                    return (-1);
            }

            return (0);
        }
    }

    return (+1);
}
#endif


/* The _isvalid() function returns non-zero if the configuration
 * pointed to by @pos and @p nmemb is valid, and zero otherwise.  A
 * configuration is valid if and only if each disc and track position
 * occurs at most once.
 *
 * XXX Candidate for a new configuration/layout module?
 *
 * @param pos   Pointer to the configuration
 * @param nmemb Pointer to the configuration
 * @return xxxx Non-zero if the configuration is valid, zero
 *              otherwise
 */
#if 0
static int
_is_valid(const struct _position *pos, size_t nmemb)
{
    size_t i, j;

    for (i = 0; i < nmemb; i++) {
        for (j = i + 1; j < nmemb; j++) {
            if (pos[i].medium == pos[j].medium && pos[i].track == pos[j].track)
                return (0);
        }
    }

    return (1);
}
#endif


/* XXX This may be duplicated elsewhere (but I just checked, and I
 * don't think so).  This is probably not the best way to do it.
 *
 * XXX Check for NULL from Mb5!
 */
#if 0
static int
_assign_position(struct fp3_release *release, Mb5Medium medium)
{
    Mb5Recording Recording; // XXX Correct convention (uppercase for Mb5)?
    Mb5Track Track; // XXX Correct convention (uppercase for Mb5)?
    Mb5TrackList track_list;
    ne_buffer *id; // XXX Free it when done!
    struct fp3_recording *recording;
    struct fp3_recording_list *recordings;
    size_t j, k;
    int i;


    /* MusicBrainz identifiers are 36 characters, plus one character
     * for NULL-termination.
     */
    id = ne_buffer_ncreate(36 + 1);

    track_list = mb5_medium_get_tracklist(medium);
    if (track_list == NULL)
        return (-1); // XXX or just return?

    for (i = 0; i < mb5_track_list_size(track_list); i++){
        Track = mb5_track_list_item(track_list, i);
        if (Track == NULL)
            continue;

        Recording = mb5_track_get_recording(Track);
        if (Recording == NULL)
            continue;

        ne_buffer_grow(id, mb5_recording_get_id(Recording, NULL, 0) + 1);
        mb5_recording_get_id(Recording, id->data, id->length);
        ne_buffer_altered(id);
        if (ne_buffer_size(id) <= 0)
            continue;

        for (j = 0; j < release->nmemb; j++) {
            recordings = release->recordings2[j];
            if (recordings == NULL)
                continue;

            for (k = 0; k < recordings->nmemb; k++) {
                recording = recordings->recordings[k];
                if (recording == NULL || recording->id == NULL)
                    continue;

                if (strcmp(id->data, recording->id) == 0) {
                    recording->position_medium = mb5_medium_get_position(medium);
                    recording->position_track = mb5_track_get_position(Track);
                    // XXX Should have no duplicates!

//                    printf("Stream %zd: set to (%zd, %zd)\n",
//                           j,
//                           recording->position_medium,
//                           recording->position_track);
                }
            }
        }
    }

    ne_buffer_destroy(id);

    return (0);
}
#endif


/* XXX Documentation!  Maybe better named _match_track()?
 * _score_track() will fail if and only if fingersum_get_checksum() or
 * fingersum_get_checksum2() fail.
 *
 * XXX Probably want to rethink this: seek the disc (pressing) in
 * accuraterip with the best score.  The best pressing is the one
 * where the smallest confidence is the largest?
 *
 * @param track Zero-based index
 * @return XXX 0 on success, -1 otherwise, and errno is set
 */
static int
_score_track(const struct _cache *response,
             size_t track,
             struct fingersum_context *ctx_leader,
             struct fingersum_context *ctx,
             struct fingersum_context *ctx_trailer,
             struct _match *match)
{
//    uint32_t crcs1[3], crcs2[3], crcs[2];
    struct _chunk *chunk;
//    struct fingersum_result *result;
    size_t i, j;


    /* Extract the AR CRC:s from the stream.
     */
//    if (fingersum_get_checksum(ctx, NULL, crcs1) != 0)
//        return (-1);
//    if (fingersum_get_checksum2(ctx, NULL, crcs2) != 0)
//        return (-1);

//    match->version = 0;
    match->confidence_v1 = 0;
    match->confidence_v2 = 0;
    match->confidence_max = 0;
    match->confidence_total = 0;


    /* Accumulate the confidence for the track, considering both v1
     * and v2 checksums.  Keep track of maximum and total confidence.
     * For each match, record what version(s) of the checksum matched.
     *
     * Use different checksum depending on the position of the track
     * on the medium.  The first and last track have their checksums
     * truncated.
     */
//    printf("Got %zd responses\n", response->nmemb);
    for (i = 0; i < response->nmemb; i++) {
//        printf("  MARKER #0 %zd %zd\n",
//               track, response->entries[i].track_count);
        if (track >= response->entries[i].track_count)
            continue;

//        printf("  MARKER #1\n");

        chunk = &response->entries[i].chunks[track];
        match->confidence_total += chunk->confidence;
        if (chunk->confidence > match->confidence_max)
            match->confidence_max = chunk->confidence;

//        if (track == 0) {
//            crcs[0] = crcs1[0];
//            crcs[1] = crcs2[0];
//        } else if (track + 1 == response->entries[i].track_count) {
//            crcs[0] = crcs1[2];
//            crcs[1] = crcs2[2];
//        } else {
//            crcs[0] = crcs1[1];
//            crcs[1] = crcs2[1];
//        }


        /* XXX PLAYGROUND BELOW
         */
#if 0
        result = fingersum_check_checksum1(
            ctx_leader, ctx, ctx_trailer, chunk->CRC);
        if (result != NULL) {
            for (j = 0; j < result->nmemb; j++) {
                match->confidence += chunk->confidence;
                match->version |= 0x1;
                //_add_offset(match, result->offsets[j]);
                match->offset = result->offsets[j];
            }
            fingersum_result_free(result);
        }
#endif

#if 1
#if 0
        // XXX Why is this done twice (and the second time chunk->unk
        // is always zero)?
//        ssize_t offset;
               ctx_leader, ctx, ctx_trailer);
        if (fingersum_find_offset(ctx, chunk->unk) != 0) {
            printf("    Found offset(s) (using 0x%08X)\n",
                   chunk->unk);
        }

        result = fingersum_check_checksum2(
            ctx_leader, ctx, ctx_trailer, chunk->CRC);
        if (result != NULL) {
            for (j = 0; j < result->nmemb; j++) {
                match->confidence += chunk->confidence;
                match->version |= 0x2;
                //_add_offset(match, result->offsets[j]);
                match->offset = result->offsets[j];
            }
            fingersum_result_free(result);
        }
#endif
        // New code, makes the old stuff redundant.
        struct fp3_ar *result_3;

//        printf("  MARKER #2\n");
        result_3 = fingersum_get_result_3(ctx_leader, ctx, ctx_trailer);
//        printf("  MARKER #3 %p [%zd]\n",
//               result_3, result_3 != NULL ? result_3->nmemb : 0);
        if (result_3 != NULL) {
            for (j = 0; j < result_3->nmemb; j++) {
                /*
                printf("RESULT: offset %lld\n", result_3->checksums[j].offset);
                printf("    v1: 0x%08X\n", result_3->checksums[j].checksum_v1);
                printf("    v2: 0x%08X\n", result_3->checksums[j].checksum_v2);
                */
                if (result_3->checksums[j].checksum_v1 == chunk->CRC) {
//                    match->version |= 0x1;
                    match->confidence_v1 += chunk->confidence;
                    match->offset = result_3->checksums[j].offset;

                } else if (result_3->checksums[j].checksum_v2 == chunk->CRC) {
//                    match->version |= 0x2;
                    match->confidence_v2 += chunk->confidence;
                    match->offset = result_3->checksums[j].offset;
                }
            }
        }
#endif
        continue;

        /* XXX PLAYGROUND ABOVE
         */

        /* XXX Uncomment the else-if clause for comparison with
         * morituri.
         *
         * XXX Actually, rethink the whole business about ignoring the
         * pressings in AccurateRip.  It looks like we should perhaps
         * match an entire pressing on its own?!
         */
#if 0
        printf("SILLY %zd HATTNE MATCHING (%08x, %08x) against %08x\n",
               i, crcs[0], crcs[1], chunk->CRC);
#endif
//        if (crcs[0] == chunk->CRC)
//            match->version |= 0x1;
//        else if (crcs[1] == chunk->CRC)
//            match->version |= 0x2;
//        else
//            continue;

//        match->confidence += chunk->confidence;
    }

    return (0);
}


/* Score the configuration against the AccurateRip database.  This
 * queries accurate rip.
 *
 * The only ambiguity at this point is which discs match best.  But
 * really, a disc either matches or it doesn't--there's no room for
 * fuzz.  Also the disc coverages of the streams are guaranteed not to
 * overlap (are they, really?), so here we just eliminate all but the
 * best-matching disc.
 *
 * Not sure if it's good style to have this modify release or not.
 *
 * @param pos The configuration XXX so maybe name it cfg?
 */
#if 0
static struct _match
_score_configuration(struct accuraterip_context *ctx, Mb5MediumList medium_list, const struct _position *pos, size_t nmemb, struct fp3_release *release, struct fingersum_context **ctxs)
{
//    uint32_t crcs1[3], crcs2[3], crcs[2];
    Mb5Disc disc;
    Mb5DiscList disc_list;
    Mb5Medium medium;
    Mb5TrackList track_list;
    const struct _cache *response;
//    struct _chunk *chunk;
    char *path;
    ne_buffer *id;
    size_t ii;
    int i, j, k, mpos, ntracks;
//    int confidence_release; // XXX


    /* MusicBrainz disc identifiers are 28 characters, plus one
     * character for NULL-termination.
     */
    id = ne_buffer_ncreate(28 + 1);

//    confidence_release = INT_MAX; // XXX Also want the confidence_min?

    // XXX Use confidence_max to distinguish between not_found and
    // mismatch.

    /* XXX Could extract all the CRC:s and store them, but that would
     * make memory management a bit hairier
     */
    struct _match match_release;
    match_release.version = 0;
    match_release.confidence = 0;
    match_release.confidence_max = 0;
    match_release.confidence_total = 0;
    match_release.offset = 0;

    struct _match match_disc;
    struct _match match_disc_best;
    match_disc_best.version = 0;
    match_disc_best.confidence = 0;
    match_disc_best.confidence_max = 0;
    match_disc_best.confidence_total = 0;
    match_disc_best.offset = 0;
//    Mb5Disc disc_best;

    struct _match *match_medium;
    void *p;


    match_medium = NULL;

    for (i = 0; i < mb5_medium_list_size(medium_list); i++) {
        medium = mb5_medium_list_item(medium_list, i);
        if (medium == NULL)
            continue; // XXX Update scores

        /* Zero-base position of the medium.
         */
        mpos = mb5_medium_get_position(medium) - 1;
        if (mpos < 0 || mpos >= release->n_media)
            continue; // XXX update scores

        disc_list = mb5_medium_get_disclist(medium);
        if (disc_list == NULL)
            continue; // XXX update scores

        track_list = mb5_medium_get_tracklist(medium);
        if (track_list == NULL)
            continue; // XXX update scores

        ntracks = mb5_track_list_size(track_list);

        /* Try all the discs for the medium XXX then choose the best
         * one!
         */
//        disc_best = NULL;
        match_disc_best.version = 0;
        match_disc_best.confidence = 0;
        match_disc_best.confidence_max = 0;
        match_disc_best.confidence_total = 0;


        /* XXX Clumsy: we may not actually need to reallocate, if we
         * keep track of the capacity.
         */
        p = realloc(match_medium, ntracks * sizeof(struct _match));
        if (p == NULL)
            ; // XXX
        match_medium = p;


        /* Assign the medium and track positions for the recordings
         * in release.  XXX Better done elsewhere?
         */
        if (_assign_position(release, medium) != 0)
            ; // XXX

        for (j = 0; j < mb5_disc_list_size(disc_list); j++) {
            disc = mb5_disc_list_item(disc_list, j);
            if (disc == NULL)
                continue; // XXX update scores


            /* Get the MusicBrainz disc ID for this medium.
             */
            ne_buffer_grow(id, mb5_disc_get_id(disc, NULL, 0) + 1);
            mb5_disc_get_id(disc, id->data, id->length);
            ne_buffer_altered(id);


            /* Skip the disc if it is not listed in the release at the
             * appropriate position.
             */
            for (ii = 0; ii < release->media[mpos]->nmemb; ii++) {
                if (strcmp(id->data, release->media[mpos]->discids[ii]) == 0)
                    break;
            }
            if (ii == release->media[mpos]->nmemb)
                continue; // XXX update scores


            /* Skip the disc if it does not have track offset
             * information, or if the offsets are bogus.
             */
            path = _mkpath(disc);
            if (path == NULL) {
                if (errno == EINVAL || errno == ENOMSG)
                    continue;
                ne_buffer_destroy(id);
//                return (-1);
                return (match_release);
            }


            /* Skip the disc if it is not found in the AccurateRip
             * database.  The response must not be freed because it is
             * internal to the cache.
             */
            printf("HATTNE 1 is checking discid ->%s<-\n"
                   "                        url ->%s<-\n", id->data, path);
            response = _get_accuraterip(ctx, path);
            free(path);

            if (response == NULL || response->status < 0) {
                ne_buffer_destroy(id);
                printf("FATAL ERROR: ->%s<-\n", ne_get_error(ctx->session));
                errno = EIO;
//                return (-1);
                return (match_release);
            } else if (response->status > 0) {
                printf("LOOKUP FAILED %d ->%s<-\n",
                       response->status, ne_get_error(ctx->session));
                continue; // XXX Update scores
            }


            /* Match the response to the streams.
             */
            match_disc.version = 0;
            match_disc.confidence = 0;
            match_disc.confidence_max = 0;
            match_disc.confidence_total = 0;

            for (k = 0; k < ntracks; k++) {
                fingersum_context *ctx_leader;
                fingersum_context *ctx;
                fingersum_context *ctx_trailer;


                /* Find the stream corresponding to the k:th track in
                 * the current configuration.  The configuration is
                 * one-based, while the indices are zero-based.
                 *
                 * XXX The first loop is a bit clumsy.  Added while
                 * testing.
                 */
                ctx = ctx_leader = ctx_trailer = NULL;
                for (ii = 0; ii < nmemb; ii++) {
                    if (pos[ii].medium == i + 1 && pos[ii].track == k)
                        ctx_leader = ctxs[ii];
                    if (pos[ii].medium == i + 1 && pos[ii].track == k + 1)
                        ctx = ctxs[ii];
                    if (pos[ii].medium == i + 1 && pos[ii].track == k + 2)
                        ctx_trailer = ctxs[ii];
                }
                for (ii = 0; ii < nmemb; ii++) {
                    if (pos[ii].medium == i + 1 && pos[ii].track == k + 1)
                        break;
                }
                if (ii == nmemb)
                    continue;

                 // XXX INDEXING BROKEN BELOW!  Assumes position in
                 // array is same as position on disc.
                if (_score_track(
                        response, pos[ii].track - 1,
                        ctx_leader,
                        ctx,
                        ctx_trailer,
                        match_medium + k) != 0) {
                    ; // XXX
                }


                /* XXX Note, there can be several discs.  Really not
                 * sure whether confidence_max should matter when
                 * scoring/comparing discs.
                 */
//                if (confidence_track < confidence_release)
//                    confidence_release = confidence_track;
                if (match_disc.confidence == 0 ||
                    match_medium[k].confidence < match_disc.confidence) {
                    match_disc.confidence = match_medium[k].confidence;
                }

                if (match_disc.confidence_max == 0 ||
                    match_medium[k].confidence_max > match_disc.confidence_max) {
                    match_disc.confidence_max = match_medium[k].confidence_max;
                }

//                printf("  current disc %d %d\n",
//                       match_disc.confidence, match_disc.confidence_max);
            } // track loop

            printf("current disc %d %d\n",
                   match_disc.confidence, match_disc.confidence_max);


            /* Now that the best disc is found, repeat the matching
             * against accurate rip (results are cached), and store
             * the results in the release structure.
             */
            struct fp3_recording *recording;
            struct fp3_recording_list *recordings;
            size_t jj;
            if (_match_compar(&match_disc_best, &match_disc) < 0) {
//                disc_best = disc;
                match_disc_best = match_disc;

                for (k = 0; k < ntracks; k++) {

                    for (ii = 0; ii < release->nmemb; ii++) {
                        recordings = release->recordings2[ii];

                        for (jj = 0; jj < recordings->nmemb; jj++) {
                            recording = recordings->recordings[jj];

                            /* XXX Don't count on i being position of
                             * medium, or k being position of track!
                             *
                             * XXX MUST ENSURE position_medium and
                             * position_track are actually assigned!
                             */
                            if (recording->position_medium == i + 1 &&
                                recording->position_track == k + 1) {

//                                printf("Will assign (%zd, %zd)\n",
//                                       recording->position_medium,
//                                       recording->position_track);

                                recording->version =
                                    match_medium[k].version;
                                recording->confidence =
                                    match_medium[k].confidence;
                                recording->confidence_max =
                                    match_medium[k].confidence_max;
                                recording->confidence_total =
                                    match_medium[k].confidence_total;
                                recording->confidence_total =
                                    match_medium[k].offset;
                            }
                        }
                    }
                }
            }

            printf("best disc %d %d\n",
                   match_disc_best.confidence, match_disc_best.confidence_max);
        } // disc loop [over j]


        /* XXX Remove all but the best disc, and repeat the matching
         * with that (and do it such that the confidences are stored
         * in release).  Not sure that is good design.
         */

        /* The worst matching "best" disc determines the match of the
         * release.
         */
        if (i == 0 || _match_compar(&match_release, &match_disc_best) > 0) {
            match_release = match_disc_best;
        }
    } // medium loop [over i]

    free(match_medium); // XXX This may need to be done elsewhere as well!

    printf("Returning %d %d\n",
           match_release.confidence, match_release.confidence_max);

    ne_buffer_destroy(id);
//    return (confidence_release);
    return (match_release);
}
#endif
/**** REFACTOR STOP ****/


/**** NEW IMPLEMENTATION START ****/

#if 0
struct _cfg2_entry
{
    int medium;
    int disc;
    int track;

    /* Number of streams
     */
    size_t nmemb;

    /* XXX Should be constant contexts.  In particular, must not be
     * freed!
     *
     * No, now it's stream indices instead!
     */
//    struct fingersum_context **ctxs;
    size_t *ctxs;
};
#endif


#if 0
/* XXX Then, this structure is not really a configuration, but
 * describes all configuration possible.
 */
struct _cfg2_release
{
    /* This is the total number of tracks on the release
     */
    size_t nmemb;

    struct _cfg2_entry **entries;
};
#endif


#if 0
static struct _cfg2_entry *
_cfg2_entry_new()
{
    struct _cfg2_entry *entry;

    entry = malloc(sizeof(struct _cfg2_entry));
    if (entry == NULL)
        return (NULL);

    entry->medium = 0;
    entry->disc = 0;
    entry->track = 0;
    entry->nmemb = 0;
    entry->ctxs = NULL;

    return (entry);
}
#endif


#if 0
static void
_cfg2_entry_free(struct _cfg2_entry *entry)
{
    if (entry->ctxs != NULL)
        free(entry->ctxs);
    free(entry);
}
#endif


#if 0
static void
_cfg2_release_free(struct _cfg2_release *cfg)
{
    size_t i;

    for (i = 0; i < cfg->nmemb; i++)
        _cfg2_entry_free(cfg->entries[i]);
    free(cfg);
}
#endif


#if 0
static struct _cfg2_entry *
_cfg2_release_append(struct _cfg2_release *cfg)
{
    struct _cfg2_entry *entry;
    void *p;

    p = realloc(cfg->entries, (cfg->nmemb + 1) * sizeof(struct _cfg2_entry *));
    if (p == NULL)
        return (NULL);
    cfg->entries = p;

    entry = _cfg2_entry_new();
    if (entry == NULL)
        return (NULL);
    cfg->entries[cfg->nmemb++] = entry;

    return (entry);
}
#endif


#if 0
static int
_cfg2_entry_add_stream(struct _cfg2_entry *entry, size_t ctx_i)
{
    void *p;

    p = realloc(entry->ctxs, (entry->nmemb + 1) * sizeof(size_t));
    if (p == NULL)
        return (-1);

    entry->ctxs = p;
    entry->ctxs[entry->nmemb] = ctx_i;
    entry->nmemb += 1;

    return (0);
}
#endif


#if 0
/* XXX There must be as many contexts as there are recordings in the
 * release.  This is not checked.
 *
 * XXX What's the difference between mb5_track_list_size() and
 * mb5_track_list_get_count()?
 */
static struct _cfg2_release *
_cfg2_release_new(Mb5MediumList medium_list, struct fp3_release *release, struct fingersum_context **ctxs)
{
    Mb5Medium medium;
    Mb5Recording recording;
    Mb5Track track;
    Mb5TrackList track_list;
    struct _cfg2_release *cfg;
    struct _cfg2_entry *entry;
    struct fp3_recording_list *recordings;
    ne_buffer *id; // XXX Free it when done!
    int i, j;
    size_t k, l;


    /* Allocate and initialise the configuration.  This could have
     * been _cfg2_release_new(), but it is not.
     */
    cfg = malloc(sizeof(struct _cfg2_release));
    if (cfg == NULL)
        return (NULL);
    cfg->nmemb = 0;
    cfg->entries = NULL;


    /* MusicBrainz identifiers are 36 characters, plus one character
     * for NULL-termination.
     */
    id = ne_buffer_ncreate(36 + 1);


    /* XXX Break (i.e. return NULL) or "continue" on error?
     */
    for (i = 0; i < mb5_medium_list_size(medium_list); i++) {
        medium = mb5_medium_list_item(medium_list, i);
        if (medium == NULL)
            continue;

        track_list = mb5_medium_get_tracklist(medium);
        if (track_list == NULL)
            continue;

        for (j = 0; j < mb5_track_list_size(track_list); j++) {
            /* Get the NULL-terminated MusicBrainz identifier of the
             * recording corresponding to the given track.
             */
            track = mb5_track_list_item(track_list, j);
            if (track == NULL)
                continue;

            recording = mb5_track_get_recording(track);
            if (recording == NULL)
                continue;

            ne_buffer_grow(id, mb5_recording_get_id(recording, NULL, 0) + 1);
            mb5_recording_get_id(recording, id->data, id->length);
            ne_buffer_altered(id);
            if (ne_buffer_size(id) <= 0)
                continue;

            /* Create new entry for the medium/track combination in
             * the configuration.
             */
            entry = _cfg2_release_append(cfg);
            if (entry == NULL) {
                ne_buffer_destroy(id);
                _cfg2_release_free(cfg);
                return (NULL);
            }
            entry->medium = i;
            entry->disc = 0; // XXX
            entry->track = j;


            /* Append all contexts possibly associated with the
             * medium/track combination.
             */
            for (k = 0; k < release->nmemb; k++) {
                recordings = release->recordings2[k];

                for (l = 0; l < recordings->nmemb; l++) {
//                    printf("*** Adding %3zd ctxs[%zd]\n", k, l);
                    if (recordings->recordings[l]->id == NULL)
                        printf("BUGGER ALL -- THIS SHOULD NOT HAPPEN\n");

                    if (recordings->recordings[l]->id != NULL &&
                        strcmp(id->data, recordings->recordings[l]->id) == 0) {
//                        printf("  *** Adding %3zd ctxs[%zd] = %p\n", k, l, ctxs[l]);

                        if (_cfg2_entry_add_stream(entry, k) != 0) {
                            ne_buffer_destroy(id);
                            _cfg2_release_free(cfg);
                            return (NULL);
                        }
                    }
                }
            }
        }
    }

    return (cfg);
}
#endif


/* Scores ONE PARTICULAR CONFIGURATION: Note: this does not do any
 * additional matching; streams are expected to be assigned to their
 * correct positions.
 *
 * @return Should be used as an argument to _match_release_free().
 */
static struct _match_release *
_cfg2_score_configuration(
    struct accuraterip_context *ctx,
    struct _cfg2_cfg *cfg,
    Mb5MediumList MediumList,
    struct fingersum_context **ctxs)
{
    Mb5Disc Disc;
    Mb5Medium Medium;
    const struct _cache *response;
    struct _match *match_stream;
    struct _match_release *match_release;
    char *path;
    size_t i, j;

    struct _cfg2_track *track;
    size_t t;


    /* Find the number of streams, i.e. one more than the largest
     * index referred to in the configuration.  Allocate and
     * initialise a sufficiently large result _match_release
     * structure.  Note that calloc(3) will initialise the space to
     * zero.
     */
    match_release = malloc(sizeof(struct _match_release));
    if (match_release == NULL)
        return (NULL);

    printf("HATTNE MARKER TODAY _cfg2_score_configuration() #0 %zd\n",
           cfg->n_media);

    match_release->n_streams = 0;
    match_release->nmemb = 0;
    for (i = 0; i < cfg->n_media; i++) {
        match_release->n_streams += cfg->media[i].n_tracks;

        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            track = cfg->media[i].tracks + j;


            // XXX Skip if unassigned
            if (track->nmemb == 0 || track->selected >= track->nmemb)
                continue;

            t = track->streams[track->selected].index;

//            if (cfg->media[i].streams[j] + 1 > match_release->nmemb)
            if (t + 1 > match_release->nmemb)
                match_release->nmemb = t + 1;
        }
    }

    if (match_release->nmemb <= 0) {
        free(match_release);
        return (NULL);
    }

    printf("HATTNE MARKER TODAY _cfg2_score_configuration() #1\n");

    match_release->recordings = calloc(
        match_release->nmemb, sizeof(struct _match));
    if (match_release->recordings == NULL) {
        free(match_release);
        return (NULL);
    }


    /* Get the AccurateRip results for the selected disc for each
     * medium.
     *
     * Skip the disc if... [from old comments]: (i) if it is not
     * listed in the release at the appropriate position (ii) if it
     * does not have track offset information, or if the offsets are
     * bogus.
     *
     * XXX Should probably work with DiscID instead of indices
     * throughout the new code.
     */
    for (i = 0; i < cfg->n_media; i++) {
        Medium = _cfg2_medium_at_position(MediumList, i + 1);
        if (Medium == NULL)
            continue;

        Disc = _cfg2_disc_with_id(Medium, cfg->media[i].discid_str);
        if (Disc == NULL)
            continue;


        /* Query the AccurateRip database.  The response must not be
         * freed because it is internal to the cache.
         */
        path = _mkpath(Disc);
        if (path == NULL) {
            if (errno == EINVAL || errno == ENOMSG)
                continue;
            return (NULL);
        }

        printf("HATTNE 2 is checking url ->%s<-\n", path);
        response = _get_accuraterip(ctx, path);
        free(path);


        /* Skip the disc if it is not found in the AccurateRip
         * database.
         */
        if (response == NULL || response->status < 0) {
            printf("FATAL ERROR: ->%s<-\n", ne_get_error(ctx->session));
            errno = EIO;
            return (NULL);
        } else if (response->status > 0) {
            printf("LOOKUP FAILED %d ->%s<-\n",
                   response->status, ne_get_error(ctx->session));
            continue;
        }


        /* Score the streams corresponding to the tracks on this disc.
         * If _get_accuraterip() returned status > 0, the scores
         * provided by the initialising calloc(3) above are good.
         */
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            track = cfg->media[i].tracks + j;

            // XXX Does this make sense?
            if (track->nmemb > 0 && track->selected < track->nmemb) {
                struct fingersum_context *ctx;
                struct fingersum_context *ctx_leader;
                struct fingersum_context *ctx_trailer;
                struct _cfg2_track *test_track;
                size_t k;

                t = track->streams[track->selected].index;
                ctx = ctxs[t];

                ctx_leader = NULL;
                if (j > 0) {
                    test_track = cfg->media[i].tracks + j - 1;
                    if (test_track->nmemb > 0 &&
                        test_track->selected < test_track->nmemb) {
                        k = test_track->streams[test_track->selected].index;
                        ctx_leader = ctxs[k];
                    }
                }

                ctx_trailer = NULL;
                if (j + 1 < cfg->media[i].n_tracks) {
                    test_track = cfg->media[i].tracks + j + 1;
                    if (test_track->nmemb > 0 &&
                        test_track->selected < test_track->nmemb) {
                        k = test_track->streams[test_track->selected].index;
                        ctx_trailer = ctxs[k];
                    }
                }

/*
                match_stream = &match_release->recordings[cfg->media[i].streams[j]];
                if (_score_track(response,
                             j,
                                 ctxs[cfg->media[i].streams[j]],
                                 match_stream) != 0) {
                    _match_release_free(match_release);
                    return (NULL);
                }
*/

                // XXX INDEXING BROKEN BELOW!  Assumes position in
                // array is same as position on disc.
                match_stream = &match_release->recordings[t];
                if (_score_track(response,
                                 j,
                                 ctx_leader, //t > 0 ? ctxs[t - 1] : NULL,
                                 ctx, //ctxs[t],
                                 ctx_trailer, //t + 1 < cfg->media[i].n_tracks ? ctxs[t + 1] : NULL,
                                 match_stream) != 0) {
                    _match_release_free(match_release);
                    return (NULL);
                }

#if 1 // DUMPER
                printf("%3zd/%3zd: %d+%d/%d [%d]\n",
                       i, j,
                       match_stream->confidence_v1,
                       match_stream->confidence_v2,
                       match_stream->confidence_max,
                       match_stream->confidence_total);
#endif
            }
        }
    }

    return (match_release);
}


/* Returns the selected Recording id of the stream.  If the
 * configuration is valid, position on the release should be
 * associated with at most one stream.
 *
 * @param index Stream index
 */
static char *
_cfg2_id_of_stream(Mb5MediumList MediumList,
                   const struct _cfg2_cfg *cfg,
                   size_t index)
{
    Mb5Medium Medium;
    Mb5Recording Recording;
    Mb5Track Track;
    Mb5TrackList TrackList;
    ne_buffer *id;
    struct _cfg2_track *track;
    size_t i, j;


    /* Find the medium and track position of the index.  If the loop
     * does not find anything, the _cfg2_medium_at_position() and
     * _cfg2_track_at_position() functions should return NULL, so no
     * need to check afterwards.
     */
    for (i = 0; i < cfg->n_media; i++) {
        Medium = _cfg2_medium_at_position(MediumList, i + 1);
        if (Medium == NULL)
            continue;

        TrackList = mb5_medium_get_tracklist(Medium);
        if (TrackList == NULL)
            continue;

        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            Track = _cfg2_track_at_position(TrackList, j + 1);
            if (Track == NULL)
                continue;

            Recording = mb5_track_get_recording(Track);
            if (Recording == NULL)
                continue;


            // XXX Does this make sense?
            track = cfg->media[i].tracks + j;
            if (track->nmemb > 0 &&
                track->selected < track->nmemb &&
                track->streams[track->selected].index == index) {

                id = ne_buffer_ncreate(
                    mb5_recording_get_id(Recording, NULL, 0) + 1);
                mb5_recording_get_id(Recording, id->data, id->length);
                ne_buffer_altered(id);
                return (ne_buffer_finish(id));
            }
        }
    }

    return (NULL);
}


static int
_cfg2_apply_accuraterip_result(Mb5MediumList MediumList,
                               struct fp3_release *release,
                               const struct _cfg2_cfg *cfg,
                               const struct _match_release *result)
{
    struct fp3_recording_list *recordings;
    char *id;
    size_t i; //, j;


    /* This will happen if the release has tracks not covered by
     * AccurateRip or vice versa.  For the purpose of this function,
     * it is probably harmless.
     */
    if (release->nmemb != result->nmemb) {
//        printf("Harry Potter says \"bloody hell\" %zd %zd\n",
//               release->nmemb, result->nmemb);
//        return (-1);
    }

//    printf("MARKER #0 %zd\n", release->nmemb);
    for (i = 0; i < release->nmemb; i++) {

        /* Get the ID of the recording associated with the stream.
         * Skip the stream if it does not have an associated
         * recording.
         */
        id = _cfg2_id_of_stream(MediumList, cfg, i);
        if (id == NULL)
            continue; // XXX

//        printf("MARKER #1 %p\n", release->recordings2 + i);
        recordings = release->streams[i];
//        printf("APPLYING %zd, retaining ->%s<-\n", i, id);


        /* Eliminate all but the matching recordings.
         * recordings->nmemb should be zero or one after this.
         */
//        printf("MARKER #2 %zd %p %p\n",
//               recordings->nmemb, recordings->recordings[0]->id, id);
        while (recordings->nmemb > 0 && strcmp(
                   recordings->recordings[0]->id, id) != 0) {
//            printf("  will erase at index 0 ->%s<- vs ->%s<\n",
//                   recordings->recordings[0]->id, id);
            fp3_erase_recording(recordings, 0);
        }
//        printf("MARKER #3 %zd\n", recordings->nmemb);
        while (recordings->nmemb > 1)
            fp3_erase_recording(recordings, 1);
//        printf("MARKER #4 %zd\n", recordings->nmemb);
        if (recordings->nmemb > 1) {
//            printf("Ronald Weasley says \"bloody hell\" %zd\n",
//                   recordings->nmemb);
        }
        free(id);


        /* Transport match per-track scores to the release.
         *
         * XXX Why is the recordings->nmemb check necessary?
         */
//        printf("MARKER #1 %zd\n", recordings->nmemb);
        if (recordings->nmemb > 0) {
//            recordings->recordings[0]->version =
//                result->recordings[i].version;

// 2016-08-25: migrated to fp3_track -- XXX ensure that it is written there!
/*
            recordings->recordings[0]->confidence_v1 =
                result->recordings[i].confidence_v1;
            recordings->recordings[0]->confidence_v2 =
                result->recordings[i].confidence_v2;
            recordings->recordings[0]->confidence_max =
                result->recordings[i].confidence_max;
            recordings->recordings[0]->confidence_total =
                result->recordings[i].confidence_total;
            recordings->recordings[0]->offset =
                result->recordings[i].offset;
*/
        }
    }

    // XXX 2015-09-01 addition [adapted 2015-12-04]
    release->confidence_min = _score_release(result);

    return (0);
}

/**** NEW IMPLEMENTATION STOP ****/


/**** NEWER IMPLEMENTATION START ****/
// XXX OUCH! There could be more than one matching track for the
// position!  Need to fold this loop into the code below!
// Only return the first match for now!
static struct fingersum_context *
_ctx_at_position(
    struct fingersum_context **ctxs, struct fp3_disc *disc, size_t position)
{
    struct fp3_track *track;
    size_t i;

    for (i = 0; i < disc->nmemb; i++) {
        track = disc->tracks[i];

        if (track->position == position && track->nmemb > 0)
            return (ctxs[track->indices[0]]);
    }

    return (NULL);
}


// XXX  Fix stuff up here!
/* If release knew about the AccurateRip ID, MediumList probably would
 * not be needed here!
 */
void
_cfg3_check_release(struct accuraterip_context *ctx,
                    struct fingersum_context **ctxs,
                    struct fp3_release *release,
                    Mb5MediumList MediumList)
{
    size_t i, j, k;
    struct fp3_medium *medium;
    struct fp3_disc *disc;
    struct fp3_track *track;
    Mb5Medium Medium;
    Mb5Medium Disc;
    char *path;
    const struct _cache *response;
    struct fingersum_context *leader, *center, *trailer;


    printf("check #1 %zd\n", release->nmemb_media);
    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

        printf("Searching for medium_at_position %zd (%d)\n",
               medium->position, mb5_medium_list_size(MediumList));
        Medium = _cfg2_medium_at_position(MediumList, medium->position);
        if (Medium == NULL)
            continue;

        printf("check #2 %zd\n", medium->nmemb_discs);
        for (j = 0; j < medium->nmemb_discs; j++) {
            disc = medium->discs[j];

            Disc = _cfg2_disc_with_id(Medium, disc->id);
            if (Disc == NULL)
                continue;


            /* Query the AccurateRip database.  The response must not
             * be freed because it is internal to the cache.
             */
            path = _mkpath(Disc);
            if (path == NULL) {
                if (errno == EINVAL || errno == ENOMSG)
                    continue;
                return; // (NULL);
            }

            printf("HATTNE 3 is checking url  ->%s<-\n", path);
            printf("HATTNE 3 is checking disc ->%s<-\n", disc->id);
#if 0
            response = _get_accuraterip(ctx, path);
#else
            response = _get_localhost(ctx, disc->id, path);
#endif
            free(path);


            /* Skip the disc if it is not found in the AccurateRip
             * database.
             */
            if (response == NULL || response->status < 0) {
                printf("FATAL ERROR: ->%s<-\n", ne_get_error(ctx->session));
                errno = EIO;
                return; // (NULL);
            } else if (response->status > 0) {
                printf("LOOKUP FAILED %d ->%s<-\n",
                       response->status, ne_get_error(ctx->session));
                continue;
            }

            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];

                leader = _ctx_at_position(ctxs, disc, track->position - 1);
                center = _ctx_at_position(ctxs, disc, track->position);
                trailer = _ctx_at_position(ctxs, disc, track->position + 1);

#if 1
                struct _chunk *chunk;
                struct fp3_ar *result_3;
                size_t l, m;

                for (l = 0; l < response->nmemb; l++) {
                    chunk = &response->entries[l].chunks[track->position - 1];

                    track->confidence_total += chunk->confidence;
                    if (chunk->confidence > track->confidence_max)
                        track->confidence_max = chunk->confidence;


                    /* XXX Should also transport CRC, CRC (EAC), and
                     * the signatures.
                     */
                    result_3 = fingersum_get_result_3(leader, center, trailer);

                    if (result_3 != NULL) {
                        for (m = 0; m < result_3->nmemb; m++) {

#if 0
printf("%02zd %02zd %02zd: COMPARING v1 0x%08x and 0x%08x [offset %lld, confidence %d]\n",
k, l, m, result_3->checksums[m].checksum_v1, chunk->CRC, result_3->checksums[m].offset, chunk->confidence);
printf("%02zd %02zd %02zd: COMPARING v2 0x%08x and 0x%08x [offset %lld, confidence %d]\n",
k, l, m, result_3->checksums[m].checksum_v2, chunk->CRC, result_3->checksums[m].offset, chunk->confidence);
#endif

                            if (fp3_track_add_checksum(
                                track,
                                result_3->checksums[m].offset,
                                result_3->checksums[m].checksum_v1 == chunk->CRC ? chunk->confidence : 0,
                                result_3->checksums[m].checksum_v2 == chunk->CRC ? chunk->confidence : 0) != 0) {
                                ; // XXX
                            }
                        }
                    }
                }
#else
                size_t l;
                struct _match match;

                // XXX Return value?
//                match.version = 0;
                match.confidence_max = 0;
                match.confidence_total = 0;
                match.offset = 0;

                _score_track(response,
                             track->position - 1, // XXX zero-based!
                             leader,
                             _ctx_at_position(ctxs, disc, track->position),
                             trailer,
                             &match); // XXX

                printf("GOT MATCH (position %zd):\n"
                       "  confidence_v1    : %d\n"
                       "  confidence_v2    : %d\n"
                       "  confidence_max   : %d\n"
                       "  confidence_total : %d\n"
                       "  offset           : %zd\n",
                       track->position,
                       match.confidence_v1,
                       match.confidence_v2,
                       match.confidence_max,
                       match.confidence_total,
                       match.offset);

                track->confidence_v1 = match.confidence_v1;
                track->confidence_v2 = match.confidence_v2;
                track->confidence_max = match.confidence_max;
                track->confidence_total = match.confidence_total;
                track->offset = match.offset;
#endif
            }


            /* POPULATE WITH EAC DATA XXX could better fold this into
             * the above.
             */
            path = _mkpath_eac(Disc);
            if (path == NULL) {
                if (errno == EINVAL || errno == ENOMSG)
                    continue;
                return;
            }

            printf("HATTNE 2 is checking url  ->%s<-\n", path);
            printf("HATTNE 2 is checking disc ->%s<-\n", disc->id);
            response = _get_eac(ctx, path);
            free(path);


            /* Skip the disc if it is not found in the AccurateRip
             * database.
             */
            if (response == NULL || response->status < 0) {
                printf("FATAL ERROR: ->%s<-\n", ne_get_error(ctx->session_eac));
                errno = EIO;
                return; // (NULL);
            } else if (response->status > 0) {
                printf("LOOKUP FAILED %d ->%s<-\n",
                       response->status, ne_get_error(ctx->session_eac));
                continue;
            }

            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];

                leader = _ctx_at_position(ctxs, disc, track->position - 1);
                center = _ctx_at_position(ctxs, disc, track->position);
                trailer = _ctx_at_position(ctxs, disc, track->position + 1);

                //struct _chunk *chunk;
                struct _block_eac * block_eac;
                struct _track_eac *track_eac;
                struct fp3_ar *result_3;
                size_t l, m;

                if (response->entry_eac != NULL) {
                    if (track->position > response->entry_eac->n_tracks + 1)
                        ; // XXX
                    track_eac = response->entry_eac->tracks + track->position - 1 + 1; // XXX NOTE EXTRA 1, because zero is something else in EAC

                    // XXX Think about this

                    for (l = 0; l < track_eac->n_blocks_whole; l++) {
                        block_eac = track_eac->blocks_whole + l;

                        track->confidence_eac_total += block_eac->count;
                        if (block_eac->count > track->confidence_eac_max)
                            track->confidence_eac_max = block_eac->count;


                        /* XXX Should also transport CRC, CRC (EAC), and
                         * the signatures.
                         */
                        result_3 = fingersum_get_result_3(leader, center, trailer);

                        if (result_3 != NULL) {
                            for (m = 0; m < result_3->nmemb; m++) {
#if 0
                                printf("TRACK[%lld, %zd]: %zu\n",
                                       result_3->checksums[m].offset, l,
                                       track->position);
                                printf("  CRC32 file: 0x%08x\n",
                                       result_3->checksums[m].crc32_eac);
                                printf("  CRC32 net:  0x%08x\n",
                                       block_eac->crc32);
//                                sleep(3);
#endif

                                if (fp3_track_add_eac_checksum(
                                        track,
                                        result_3->checksums[m].offset,
                                        result_3->checksums[m].crc32_eac == block_eac->crc32 ? block_eac->count : 0)) {
                                ; // XXX
                                }
                            }
                        }
                    }

//            if (response->entry_eac != NULL) {
//                printf("HAVE A VALID EAC entry\n");
//                exit(0);
//            }
                }
            }
        }
    }

    return;
}


/**** NEWER IMPLEMENTATION STOP ****/
/* Returns -1 on error, +1 on mismatch, and 0 on match.
 *
 * XXX Should possible take a nmemb argument for the number of
 * streams, instead of working it out from the other structures.  Or
 * better yet: put the sector counts into the fp3_release structure?
 *
 * Should probably take the Mb5Disc as an argument instead!  And this
 * requires that someone saved the best disc before!
 *
 * XXX This comment is severely broken!
 */
struct _match_release *
accuraterip_url(struct accuraterip_context *ctx,
                struct fingersum_context **ctxs,
                struct fp3_release *release,
//                size_t nmemb,
                Mb5MediumList ml)
{

#if 1
    _cfg3_check_release(ctx, ctxs, release, ml);
    return (NULL);
#endif

    struct _position *best, *positions;
//    size_t i;
//    int confidence, confidence_max;


    // We have release->nmemb streams.
    positions = calloc(release->nmemb, sizeof(struct _position));
    if (positions == NULL)
        ; // XXX

    best = calloc(release->nmemb, sizeof(struct _position));
    if (best == NULL)
        ; // XXX

//    confidence_max = 0;

#if 1
    // XXX Test new stuff
    struct _cfg2_cfg *cfg_cfg;
    struct _match_release *result, *result_best;
//    long int d;

    cfg_cfg = _cfg2_first_disc_configuration(ml);
    if (cfg_cfg == NULL) {
        fprintf(stderr, "  _cfg2_first_disc_configuration() failed\n");
        exit(-1);
    }


    /* XXX This loops way too much for Weather report, which starts
     * iterating into the discs of release--most of the discs on this
     * release are not used, yet I feel this has to be fixed better!
     *
     * IDEA: The discs are all independent, and can be optimised
     * separately.  The media probably need something similar to the
     * "selected" mechanism as implemented for tracks.  And then
     * revisit the ideas about the "lower bound" and the pruning steps
     * as implemented in tagger.cpp.
     *
     * IDEA: Reintroduce the lower bound; don't even bother with any
     * of this if the lower bound is greater than the current best.
     */
    result_best = NULL;
    for ( ; ; ) {
        printf("### New iteration\n");
        _cfg2_dump_disc_configuration(cfg_cfg);


        /* Get the first valid track configuration
         *
         * XXX This function should probably accept a valid
         * configuration.  It is not clear how to deal with the case
         * when there are no valid configurations (i.e. what are the
         * confidences in that case)?.
         *
         * XXX No need to try to score against AccurateRip unless the
         * residual is is zero!
         */
        if (_cfg2_first_configuration(cfg_cfg, ml, release, ctxs) == 0 &&
            _cfg2_residual_configuration(cfg_cfg) == 0) {

            result = _cfg2_score_configuration(ctx, cfg_cfg, ml, ctxs);
            if (result == NULL)
                ; // XXX

            printf("CHECKING FOR UPDATE...\n");
            if (result_best == NULL) {
                printf("UPDATING BEST #1\n");
                result_best = result;
            } else if(_match_release_compar(result_best, result) > 0) {
                printf("UPDATING BEST #2\n");
                _match_release_free(result_best);
                result_best = result;
            } else {
                _match_release_free(result);
            }
        }
//        sleep(1);


        /* Generate the next disc configuration!
         */
        if (_cfg2_next_disc_configuration(cfg_cfg, ml) != 0)
            break;
    }


    /* XXX Need to think about this!  Should probably return the
     * configuration for this release and continuously update the best
     * configuration outside this function, and THEN apply the best
     * configuration if appropriate.
     */
    printf("EXHAUSTED\n");

    if (result_best != NULL) {
        printf("applying\n");
        if (_cfg2_apply_accuraterip_result(
                ml, release, cfg_cfg, result_best) != 0) {
            printf("releasing 1\n");
            _match_release_free(result_best);
            printf("  _cfg2_apply_accuraterip_result() failed\n");
            exit(-1);
        }
        printf("releasing 2\n");
//        _match_release_free(result_best); // Cannot free return value!
    }

//    printf("All done, call again! Confidence %d, confidence_max %d\n",
//           match_release_best.confidence, match_release_best.confidence_max);

    return (result_best);
#else
    int ret;
    struct _match match_release, match_release_best;

    match_release_best.confidence = 0;
    match_release_best.confidence_max = 0;

    if (_first_configuration(ml, release, positions) != 0)
        ; // XXX

    for ( ; ; ) {
/*
        printf("CURRENT CONFIGURATION:\n");
        for (i = 0; i < release->nmemb; i++) {
            printf("  Medium %2d track %2d\n",
                   positions[i].medium, positions[i].track);
        }
*/

        /* If the current configuration is valid, score it, update the
         * best configuration if appropriate.
         */
        if (_is_valid(positions, release->nmemb) != 0) {
            match_release = _score_configuration(
                ctx, ml, positions, release->nmemb, release, ctxs);

            if (_match_compar(&match_release_best, &match_release) < 0) {
                match_release_best = match_release;
                for (i = 0; i < release->nmemb; i++) {
                    best[i].medium = positions[i].medium;
                    best[i].track = positions[i].track;
                }
            }
        }

        /* Generate the next configuration
         */
        ret = _next_configuration(ml, release, positions, release->nmemb);
        if (ret < 0) {
            printf("FAILURE\n"); // XXX
            break;
        }
        if (ret > 0) {
            printf("EXHAUSTED\n");
            break;
        }

        /* Once we try to match things against AccurateRip, there
         * should really only be one (valid) configuration left!
         */
        printf("THIS SHOULD NOT HAPPEN\n");
    }


    /* XXX Prune all but the best configuration, or prune all broken
     * configurations -- see the score function.
     */
    printf("All done, call again! Confidence %d, confidence_max %d\n",
           match_release_best.confidence, match_release_best.confidence_max);

    release->confidence_min = match_release_best.confidence; // XXX 2015-09-01 addition

//    return (release->confidence_min);
    return (0);
#endif
}
