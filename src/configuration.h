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

#ifndef CONFIGURATION_H
#define CONFIGURATION_H 1

#ifdef __cplusplus
#  define CONFIGURATION_BEGIN_C_DECLS extern "C" {
#  define CONFIGURATION_END_C_DECLS   }
#else
#  define CONFIGURATION_BEGIN_C_DECLS
#  define CONFIGURATION_END_C_DECLS
#endif

CONFIGURATION_BEGIN_C_DECLS

/**
 * @file configuration.h
 * @brief XXX
 *
 *
 * @note An Configuration context must be accessed only by one thread at
 *       the time.
 *
 * References
 *
 * https://acoustid.org/webservice
 */

#include <musicbrainz5/mb5_c.h>

#include "structures.h"


/* XXX DESIGN: Really a configuration should consist of one particular
 * choice of disc for each medium, and at most one particular choice
 * of stream for each track on each medium.  A configuration should
 * have a score (the sector distance to the particular choice of
 * discs) in addition to the AccurateRip score.
 */
struct _cfg2_stream
{
    /* Index of the stream, corresponding to the recording of the
     * track.
     */
    size_t index;

    /* Residual, or the distance to be minimised.  Like a score,
     * except smaller is better.  XXX Maybe this should have been a
     * float or something to allow for general scoring/residual
     * functions?
     */
    int residual;
};


struct _cfg2_track
{
    /* Number of candidate streams for this track
     */
    size_t nmemb;

    /* The selected stream.  Must be less than nmemb.
     *
     * XXX Negative SHOULD indicate that no stream is assigned [or is
     * that perhaps the index member of the _cfg2_stream structure?]
     * Nah, this is a bigger bug [tänkte inte på det] -- see "Rat
     * Pack" for illustration.
     *
     * XXX How about this: if selected >= nmemb, then the track does
     * not have a matching stream (the stream corresponding to the
     * track is left "unselected").  The distance of the MB track
     * should count negatively but only if the difference of the
     * matching tracks is zero (otherwise it should count positively).
     */
    size_t selected;

    /* The candidate streams, sorted in order of increasing magnitude
     * of the residual.
     */
    struct _cfg2_stream *streams;
};


struct _cfg2_medium
{
    /* Selected discid index for each medium XXX Should have been a
     * string?
     *
     * It is conceivable that the same discid appears on several media.
     */
//    size_t discid;
    char *discid_str;

    /* Number of tracks on this medium XXX Should have been nmemb?
     */
    size_t n_tracks;

    /* Selected stream index for each track on this medium.  A
     * negative number means the track is not associated with a
     * stream.
     *
     * RATHER: All possible streams and their residuals to the current
     * track.  Sorted in order of increasing magnitude of the
     * residual?  The currently selected stream must be marked?
     *
     * We count zero-based position, and there could be gaps.
     */
//    ssize_t *streams;
    struct _cfg2_track *tracks;
};


struct _cfg2_cfg
{
    /* Number of media for this release
     */
    size_t n_media;


    /* Media indexed by zero-based position?  Can this contain gaps?
     * If a release has media at positions 1 and 3 but not 2 (which
     * may be a video thing)?
     *
     * Indexed by position, except it is zero-based.
     */
//    size_t *media;
    struct _cfg2_medium *media;
};


/**
 * @brief Retrieve result from pool context
 *
 * If all results have been retrieved, get_results() returns @c -1 and
 * sets the global variable @c errno to @c ENOMSG.
 *
 * XXX This function probably needs a better name!  And taking both a
 * bunch of fingersum_context:s AND a fp3_release is ugly!
 *
 * XXX This will break if we don't do the MusicBrainz query (lookup)
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
//char *
//_cfg2_next_discid(Mb5DiscList DiscList, const char *discid);

Mb5Medium
_cfg2_medium_at_position(Mb5MediumList MediumList, int position);

Mb5Track
_cfg2_track_at_position(Mb5TrackList TrackList, int position);

struct _cfg2_cfg *
_cfg2_first_disc_configuration(Mb5MediumList MediumList);

int
_cfg2_next_disc_configuration(struct _cfg2_cfg *cfg, Mb5MediumList MediumList);

int
_cfg2_first_configuration(struct _cfg2_cfg *cfg,
                          Mb5MediumList MediumList,
                          struct fp3_release *release,
                          struct fingersum_context **ctxs);

int
_cfg2_configuration_is_valid(const struct _cfg2_cfg *cfg);

long int
_cfg2_residual_configuration(const struct _cfg2_cfg *cfg);

//Mb5Disc
//_cfg2_get_disc(Mb5MediumList MediumList, int medium, const char *discid);

Mb5Disc
_cfg2_disc_with_id(Mb5Medium Medium, const char *discid);

void
_cfg2_dump_track_configuration(const struct _cfg2_cfg *cfg);

void
_cfg2_dump_disc_configuration(const struct _cfg2_cfg *cfg);

CONFIGURATION_END_C_DECLS

#endif /* !CONFIGURATION_H */
