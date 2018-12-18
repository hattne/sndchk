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

#ifndef ACCURATERIP_H
#define ACCURATERIP_H 1

#ifdef __cplusplus
#  define ACCURATERIP_BEGIN_C_DECLS extern "C" {
#  define ACCURATERIP_END_C_DECLS   }
#else
#  define ACCURATERIP_BEGIN_C_DECLS
#  define ACCURATERIP_END_C_DECLS
#endif

ACCURATERIP_BEGIN_C_DECLS

/**
 * @file accuraterip.h
 * @brief XXX
 *
 *
 * @note An Accuraterip context must be accessed only by one thread at
 *       the time.
 *
 * References
 *
 * https://acoustid.org/webservice
 */

#include <musicbrainz5/mb5_c.h>

#include "fingersum.h"
#include "structures.h"


/* Initialise the AccurateRip context.  Initialises a persistant
 * session.
 *
 * Optionally, configure a SOCKSv5 proxy server which will be used for
 * the session.
 *
 * XXX Would like to support other proxy servers?  Would then probably
 * need a structure for all the relevant information: protocol,
 * hostname, port, username, and password.  What about using proxys
 * for the other connections as well?
 *
 * @param hostname SOCKS 5 proxy server to contact
 * @param port     Port number of the SOCKS 5 proxy server
 */
struct accuraterip_context *
accuraterip_new(const char *hostname, unsigned int port);

void
accuraterip_free(struct accuraterip_context *ctx);


/**
 * @brief Retrieve result from pool context
 *
 * If all results have been retrieved, get_results() returns @c -1 and
 * sets the global variable @c errno to @c ENOMSG.
 *
 * XXX This function probably needs a better name!  And taking both a
 * bunch of fingersum_context:s AND a fp3_release is ugly!
 *
 * XXX This will break if we don't do the musicbrainz query (lookup)
 * with discids!
 *
 * @param pc    Pointer to an opaque pool context
 * @param ctx   Pointer to an opaque fingersum context
 * @param arg   Extra data XXX userdata?
 * @param flags What action to take XXX
 * @return      0 if successful, -1 otherwise.  If an error occurs,
 *              the global variable @c errno is set to indicate the
 *              error.
 */
struct _match_release *
accuraterip_url(struct accuraterip_context *ctx,
                struct fingersum_context **ctxs,
                struct fp3_release *release,
                Mb5MediumList ml);


/* XXX If the _match_release structure is general enough, this may be
 * better suited for a separate module?
 */
int
_match_release_compar(struct _match_release *mr1, struct _match_release *mr2);

int
_score_release(const struct _match_release *mr);

void
_match_release_free(struct _match_release *mr);


/********************************************************
 * ALL BELOW made public 2016-04-27 for tagger refactor *
 ********************************************************/

/* Track AccurateRip CRC
 *
 * This is equivalent to Spoon's STAcRipDiscIdent structure from
 * http://forum.dbpoweramp.com/showthread.php?20641-AccurateRip-CRC-Calculation
 *
 * XXX Maybe this is actually a  "_track" structure?
 */
struct _chunk
{
    /* AccurateRip track CRC XXX crc for consistency?
     *
     * 0 = invalid (i.e. never been ripped)
     */
    uint32_t CRC;

    /* AccurateRip track offset-finding CRC
     *
     * XXX This could be ignored.
     */
    uint32_t unk;
    
    /* AccurateRip confidence
     *
     * Times ripped, a value of 200 implies it has been been ripped
     * (and matched) 200 or more times.
     */
    int confidence;
};
    
    
/* Disc identifier
 *
 * This is equivalent to Spoon's STAcRipDiscIdent structure from
 * http://forum.dbpoweramp.com/showthread.php?20641-AccurateRip-CRC-Calculation
 *
 * XXX Is "entry" correct terminology?  It was previously also called
 * a "table".  Maybe this is actually a "_disc" structure?
 */
struct _entry
{
    /* XXX Maybe this is better named tracks (check Spoon's code).
     * Then search this module for the occurance of "chunk".
     */
    struct _chunk *chunks;

    /* Track count XXX More consistent as nmemb?  Is the number of
     * chunks?
     */
    size_t track_count;

    /* FreeDBIdent
     */
    uint32_t disc_cddb;

    /* TrackOffsetsAdded
     */
    uint32_t disc_id1;

    /* TrackOffsetsMultiplied
     */
    uint32_t disc_id2;
};


struct _block_eac
{
    uint32_t crc32;
    uint32_t count;
    uint32_t date;
};


struct _track_eac
{
    /* CRC32 for whole track
     */
    struct _block_eac *blocks_whole;
    size_t n_blocks_whole;


    /* CRC32 of part of track, used for read offset detection
     */
    struct _block_eac *blocks_part;
    size_t n_blocks_part;
};


/* This may be called a file.  All data is little-endian.
 *
 * https://tickets.metabrainz.org/browse/MBS-8345
 */
struct _entry_eac
{
    struct _track_eac *tracks;
    
    /* Number of tracks on each CD.  Data tracks do not count.
     */
    uint32_t n_tracks;

    /* EAC-internal version number.  This is not needed to verify
     * tracks.
     */
    uint32_t date;
};


/* XXX Maybe better as result, or response?  And should probably cache
 * this based on discid instead of path, because AccurateRip and EAC
 * use different paths.
 */
struct _cache
{
    /* XXX
     */
    struct _entry *entries;

    /* There is either exactly one, or zero of these.  In the latter
     * case, this should be NULL.
     */
    struct _entry_eac *entry_eac;

    /* Error message, if any.  XXX Make sure this is properly dup:ed!
     */
    char *error;

    /* Or is this better a Musicbrainz discid?  No probably not,
     * because there may be collisions.  This way, the cache-hash is
     * collision free!
     *
     * XXX Use fix-length array (with define, such as MAXPATH)?
     */
    char *path;

    /* Number of AccurateRip entries (XXX or discs?)
     */
    size_t nmemb;

    /* Neon return code XXX Maybe name it such?
     *
     * Probably better what the function should have returned?
     */
    int status; // XXX This may actually not be necessary!
};

const struct _cache *
accuraterip_get(struct accuraterip_context *ctx, Mb5Disc disc);

const struct _cache *
accuraterip_eac_get(struct accuraterip_context *ctx, Mb5Disc disc);

const struct _cache *
accuraterip_localhost_get(struct accuraterip_context *ctx, Mb5Disc disc);

ACCURATERIP_END_C_DECLS

#endif /* !ACCURATERIP_H */
