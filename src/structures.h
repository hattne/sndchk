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

#ifndef STRUCTURES_H
#define STRUCTURES_H 1

#ifdef __cplusplus
#  define STRUCTURES_BEGIN_C_DECLS extern "C" {
#  define STRUCTURES_END_C_DECLS   }
#else
#  define STRUCTURES_BEGIN_C_DECLS
#  define STRUCTURES_END_C_DECLS
#endif

STRUCTURES_BEGIN_C_DECLS

/**
 * @file structures.h
 * @brief XXX
 *
 * fp_ was once for "fingerprint" (or maybe not, or not anymore), and
 * we are currently at version 3
 *
 * XXX Can the structures from libmusicbrainz be reused here?  That'll
 * be difficult, becauses libmusicbrainz doesn't really allow stuff to
 * be removed from its list structures.  Also, we need to extend the
 * structures, e.g. a recording has a score.
 *
 * XXX Use these homegrown structures in the function that determines
 * the optimal releasegroup/release from the AcoustID results.
 *
 * XXX THIS MODULE SHOULD NEVER STRDUP?
 *
 * XXX All these functions must set errno properly!
 *
 * XXX "structures" is a poor name for this module.  Is "struct" better?
 *
 * XXX The whole business with the capacity is motivated by the
 * acoustid module, here the structures are continously grown and
 * shrunk.  Instead of actually shrinking them, they are only grown.
 * To further exploit this idea, all the string should probably be
 * something like ne_buffer(), but that will have issues with wide
 * characters.
 *
 * XXX Except the capacity business runs into trouble, because we must
 * mark slots (such as tracks on a medium) as invalid without
 * deallocating them.
 *
 * @bug XXX Oddity with opaque structures: clang emits warnings if the
 *      first function with an opaque structure takes it as argument.
 *      If first function declares it as a return value, all is well.
 *      See http://en.wikipedia.org/wiki/Opaque_pointer.
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

struct fingersum_context; // XXX


struct fp3_stream
{
    size_t index;

    float score;
};


struct fp3_fingerprint
{
    /* ID of the fingerprint
     */
    char *id;

    /* The index of the matching stream
     *
     * XXX Now need an indices structure (with score): there may be
     * more than one index per fingerprint (see e.g. the short spoken
     * tracks on Snatch).
     */
//    size_t index;

    /* AcoustID score
     */
//    float score;

    struct fp3_stream **streams;

    size_t nmemb;
    size_t capacity;
};


/* And this would have members for title, sort_title, artist,
 * sort_artits, etc, etc?  As would the release, and possibly the
 * release group?
 *
 * If this works, is there any possibility the AcoustID code could
 * stuff it into these structures directly?
 *
 * XXX Should this, perhaps, have been the metadat structure instead?
 */
struct fp3_recording
{
    char *id;

//    size_t index; // This may require some more thought, possibly not
                  // necessary as of comment below.

    /* These may not be necessary if the MusicBrainz track or
     * recording contains the same information.
     *
     * It appears this information is accessible from medium in a
     * release and its tracklist.
     */
//    size_t medium_position;
//    size_t track_position;

    /* One-based position of the media XXX Should go?
     */
    size_t position_medium;

    /* One-based position of the track XXX Should go?
     */
    size_t position_track;

    /* XXX Maybe this could do with a better name.  And this we
     * actually need!  It is the same as the index above.
     */
//    size_t position_stream;

    /* AcoustID score
     *
     * Initialised to zero (XXX or is it NAN) by all functions in this
     * module.  The score is probably not all that useful (but that
     * does not matter).
     *
     * XXX This should probably be a list of a score-id tuple, where
     * the id is the ID of the fingerprint.  This is implemented in
     * the fingerprints array below.
     *
     * XXX Therefore, this should probably go!
     */
    float score;

    /* For AccurateRip verification. XXX Verify types XXX Maybe this
     * is better suit for the disc?  Each disc has a number of these
     * structures, one for each offset?
     *
     * XXX YES MOVE THIS TO TRACK!
     */
//    int version;
//    int confidence_v1;
//    int confidence_v2;
//    int confidence_max;
//    int confidence_total;
//    ssize_t offset;

    /* List of matching fingerprints and the streams they were matched
     * to.
     */
    struct fp3_fingerprint **fingerprints;

    /* Number of matching fingerprints; length of fingerprints array.
     */
    size_t nmemb;

    size_t capacity;

    /* One-based position of the track on the parent medium.  Must be
     * less than nmemb_tracks in the parent medium.  Zero means it has
     * not been assigned.
     */
    size_t position;
};


/* XXX Probably makes sense to keep the Mb5Recording here as well,
 * just as was done for the release below!
 *
 * XXX This should probably disappear
 */
struct fp3_recording_list
{
    struct fp3_recording ** recordings;

    /* Number of recordings to hold without reallocation
     */
    size_t capacity;

    /* Number of valid recordings for this stream or file
     */
    size_t nmemb;

};


/* The AccurateRip checksums are offset-dependent.  XXX Why is this
 * not an offset_list?  I think it is in the new code!
 */
struct fp3_checksums
{
    /* XXX the offset type should probably be something different.
     */
    int64_t offset;

    /* "AccurateRip v1 signature" in XLD
     *
     * XXX Is this really a count or confidence?  The confidence
     * members should probably be used instead!  See
     * fp3_add_checksum() in structures.c.
     */
    uint32_t checksum_v1;

    /* "AccurateRip v2 signature" in XLD
     *
     * XXX See above
     */
    uint32_t checksum_v2;

    /* XXX Maybe the checksums and confidences are better as 2-long
     * arrays?
     *
     * XXX Should make use of this!
     */
//    size_t confidence_v1;
//    size_t confidence_v2;

    /* XXX This is new (for EAC)
     *
     * XXX Should store the CRC32 as well--not done for, for
     * conformance with AccurateRip stuff.
     */
    uint32_t crc32_eac;
    size_t count_eac;
};


/* XXX Structure now misnamed, because it contains results from both
 * AccurateRip and EAC database
 */
struct fp3_ar
{
    /**
     * @brief Checksums for each of the @p nmemb requested offsets
     */
    struct fp3_checksums *checksums;

    /**
     * @brief Length of the @p checksums array
     *
     * This is the number of unique offsets for which checksums are
     * reported.
     */
    size_t nmemb;

    /**
     * @brief CRC32 checksum of all samples
     *
     * This is an offset-independent checksum.  "CRC32 hash" in XLD.
     */
    uint32_t crc32;

    /**
     * @brief CRC32 checksum of all non-zero samples
     *
     * This is an offset-independent checksum.  "CRC32 hash (skip
     * zero)" in XLD.  XXX Find some documentation on this!  For
     * instance: http://cue.tools/wiki/CUETools_log
     *
     * XXX Comment incorrect?  Member should go?  See thing in
     * fp3_checksums above.
     */
//    uint32_t crc32_eac;
};


/* XXX This appears to be a bit redundant with fp3_recording.  Can
 * they be folded together?  I don't think so; these are different
 * things: tracks are "physical" where "recordings" are virtual (a
 * representation).
 */
struct fp3_track
{
    /* Stream indices for streams the lengths of which match exactly
     * to the disc length.
     *
     * Later: stream indices for streams which have a matching
     * offset-finding checksum.
     *
     * Even later: stream indices for streams which have a matching
     * (full) checksum.
     */
    size_t *indices;

    size_t nmemb;
    size_t capacity;

    /* Index of the selected stream.  XXX is this sane?
     */
//    size_t selected;

    /* Position of the track on the medium.  XXX One-based or what?
     */
    size_t position;

    /* For AccurateRip verification. XXX Verify types XXX Maybe this
     * is better suit for the disc?  Each disc has a number of these
     * structures, one for each offset?  For a track, it probably
     * makes sense to keep only the best offset.
     */
//    int version;
#if 0
    int confidence_v1;
    int confidence_v2;
    int confidence_max;
    int confidence_total;
    ssize_t offset;
#else
    // XXX This should probably be a fp3_ar structure (and the max and
    // total should migrate there, too
    int confidence_max;
    int confidence_total;

    int confidence_eac_max;
    int confidence_eac_total;

    struct fp3_checksums **checksums;
    size_t nmemb_checksums;
    size_t capacity_checksums;
#endif
};


struct fp3_offset_list
{
    /* List of unique offsets.  XXX Should be same type as in
     * fingersum_add_offset().
     */
    ssize_t *offsets;

    size_t nmemb;
    size_t capacity;
};


struct fp3_disc
{
    /* The disc ID.  This is a different kind of string (always ends
     * in dash, etc).
     */
    char *id;

    /* XXX Should have the AccurateRip ID here as well!
     */

    /* This list is kept in order of position on the medium, i.e. base
     * zero.  If an element is NULL, the recording is missing.  XXX
     * This implies that there is at most one recording at each
     * position, is that really true?
     *
     * Now stuff is reversed: every track should have one recording,
     * but every recording can have more than one (stream) offset.
     *
     * XXX tracks should probably move to the medium, and be an array
     * of recording:s, instead of an array of recording_list:s.
     */
//    struct fp3_recording_list **tracks;
    struct fp3_track **tracks;

    /* Number of recordings, or tracks, on this disc
     */
    size_t nmemb;
    size_t capacity;

    /* XXX Will probably need a list of integer offsets from
     * AccurateRip here.  Then: if a medium does not have any discs =>
     * there were no matching discs in MusicBrainz; if a disc does not
     * have any offsets => there were no matching discs in
     * AccurateRip.
     */

    /* Store the .  For each possible stream on each track need: (i)
     * AccurateRip stuff (ii) sector length discrepancies.  That has
     * to be kept synchronized with the information on the medium.
     *
     * Make a difference: a medium has recordings, a disc has tracks?
     */

    /* XXX Should have been an offset_list: this must be the UNION of
     * the offsets for all tracks on the disc.  All possible offsets
     * must be calculated for all tracks on the discs, otherwise the
     * algorithm breaks down.
     */
//    ssize_t *offsets;
//    size_t nmemb_offsets;
//    size_t capacity_offsets;
    struct fp3_offset_list *offset_list;
};


struct fp3_medium
{
    /* List of unique discids for this medium.  XXX Would it have made
     * sense to store a pointer to the MusicBrainz Disc object
     * instead?
     */
    char **discids;
    size_t nmemb;
    size_t capacity;

    /* Should probably remove the above in favour of the below.  Nah,
     * maybe this is suitable to hold the AccurateRip stuff?
     */
    struct fp3_disc **discs;
    size_t nmemb_discs;
    size_t capacity_discs;

    /* XXX Migration from fp3_disc, in progress.  If an element in
     * tracks is NULL it means no recording has been assigned at the
     * position (yet).
     */
    struct fp3_recording **tracks; // XXX Should have been recordings
                                   // to avoid confusion with the
                                   // tracks member in the fp3_disc
                                   // structure.
    size_t nmemb_tracks;
    size_t capacity_tracks;

    /* One-based position on the release.  The number must be less
     * than nmemb_media in the parent fp3_release structure.  Zero
     * means it has not been assigned yet.
     */
    size_t position;
};


struct fp3_release
{
    /* The matching recordings for each stream.  There can be zero or
     * more matching recordings for each stream.
     *
     * XXX This is probably not correct.  Would need to have a mediums
     * structure which containst a track structure which contains any
     * number of recordings.  The recording structure then needs to
     * know its stream index.
     *
     * Or possibly better: an array of the length of the number of
     * streams, indexed by their order, which contains one or more
     * recordings for each.
     *
     * XXX streams or files?
     *
     * It is conceivable, but unlikely, that a recording occurs more
     * than once on a release.  XXX The code should be able to handle
     * that!
     *
     * The release should also include the barcode, which should be
     * printed somewhere, because I actually use it quite frequently.
     */
//    struct fp3_recording **recordings;

    struct fp3_recording_list **streams; // XXX Is this used at all?

    /* MusicBrainz ID of the release
     */
    char *id;

    /* XXX Is this perhaps better as n_streams?
     */
    size_t capacity;
    size_t nmemb;

    size_t track_count; // XXX Need medium_count as well?

    /* XXX Added, not sure this is correct.  Score could (should) be
     * identical to the sum of the scores for the recordings once
     * everything is reduced.  Maybe write a consistency check for
     * that!
     *
     * XXX This type for mb_release is wrong!
     */
    long int distance;

    /* Total score (XXX correct?)
     */
//    float score; // XXX Zap -- compute on the fly internally instead!
    void *mb_release;

    /* XXX Added for discids.  This list is kept in order of position,
     * i.e. base zero.  If an element is NULL, the medium is missing.
     */
    struct fp3_medium **media;
    size_t nmemb_media;
    size_t capacity_media;

    /* XXX Added for AccurateRip
     */
    int confidence_min;

    /* XXX Added for Levenshtein distance
     */
    size_t metadata_distance;
};

struct fp3_releasegroup
{
    struct fp3_release **releases;
    char *id;
    size_t capacity;

    /* Number of releases
     */
    size_t nmemb;

    /* XXX Added, see release above, should possible be toc_score
     * structure.
     */
    long int distance;
//    float score; // XXX Zap -- see above note about computing on the fly.
};


/* Don't care about the result id, it is declined by the parser.  This
 * is actually not a result as defined by AcoustID, so maybe we should
 * use another name?  Candidates?
 *
 * XXX Or (after reading the Picard documentation) this is actually a
 * cluster?
 *
 * XXX Would help if this had a list of the sector lengths for each
 * stream as well!
 *
 * XXX This should probably have been fp3_response, because a result
 * is something different in AcoustID.
 *
 * AcoustID results are not separated by the fp3_results structure.
 * Hence it does not store result ID (a.k.a. fingerprint ID:s).  YES!
 * Now they are!
 */
struct fp3_result
{
    struct fp3_releasegroup **releasegroups;
    size_t capacity;
    size_t nmemb;

    /* XXX New additions
     *
     * XXX Number of results (or is it fingerprints) for each
     * stream/file
     *
     * XXX Name?  fingerprints?  results?  No, I think matches is best!
     */
    size_t *results;

    /* XXX Number of streams (NOT number of results!)
     */
    size_t n_results;
};


/*******
 * NEW *
 *******/

/* XXX For consistency, the noun should proceed the verb,
 * e.g. fp3_medium_new(), fp3_recording_free(), etc.
 */
struct fp3_track *
fp3_new_track();

struct fp3_offset_list *
fp3_new_offset_list();

struct fp3_disc *
fp3_new_disc();

struct fp3_fingerprint *
fp3_new_fingerprint();

struct fp3_medium *
fp3_new_medium();

struct fp3_recording *
fp3_new_recording();

struct fp3_recording_list *
fp3_new_recording_list();

struct fp3_release *
fp3_new_release();

struct fp3_releasegroup *
fp3_new_releasegroup();

struct fp3_result *
fp3_new_result();

struct fp3_stream *
fp3_new_stream();


/********
 * FREE *
 ********/
void
fp3_ar_free(struct fp3_ar *result);

void
fp3_free_fingerprint(struct fp3_fingerprint *fingerprint);

void
fp3_free_track(struct fp3_track *track);

void
fp3_free_offset_list(struct fp3_offset_list *offset_list);

void
fp3_free_disc(struct fp3_disc *disc);

void
fp3_free_medium(struct fp3_medium *medium);

void
fp3_free_recording(struct fp3_recording *recording);

void
fp3_free_recording_list(struct fp3_recording_list *recording_list);

void
fp3_free_release(struct fp3_release *release);

void
fp3_free_releasegroup(struct fp3_releasegroup *releasegroup);

void
fp3_free_result(struct fp3_result *result);

void
fp3_stream_free(struct fp3_stream *stream);


/*******
 * ADD *
 *******/
/* XXX This is all broken.  Perhaps want to return an object and let
 * the caller populate it?  But that make trouble with all the
 * recursive structures...
 */
int
fp3_track_add_checksum(struct fp3_track *track,
                       ssize_t offset,
                       int32_t checksum_v1,
                       int32_t checksum_v2);

int
fp3_track_add_eac_checksum(struct fp3_track *track,
                           ssize_t offset,
                           size_t count);

int
fp3_track_add_index(struct fp3_track *track, size_t index);

int
fp3_offset_list_add_offset(struct fp3_offset_list *offset_list, ssize_t offset);

int
fp3_offset_list_add_offset_list(
    struct fp3_offset_list *dst, struct fp3_offset_list *src);

int
fp3_disc_add_offset(struct fp3_disc *disc, ssize_t offset);

struct fp3_offset_list *
fp3_disc_add_offset_list(
    struct fp3_disc *disc, const struct fp3_offset_list *offset_list);

struct fp3_track *
fp3_disc_add_track(struct fp3_disc *disc,
                   const struct fp3_track *track);

struct fp3_stream *
fp3_fingerprint_add_stream(struct fp3_fingerprint *fingerprint,
                           const struct fp3_stream *stream);

struct fp3_disc *
fp3_add_disc_by_id(struct fp3_medium *medium, const char *id);

char *
fp3_add_discid(struct fp3_medium *medium, const char *id);

int
fp3_disc_add_index(struct fp3_disc *disc, size_t index);

struct fp3_disc *
fp3_medium_add_disc(struct fp3_medium *medium, const struct fp3_disc *disc);

struct fp3_fingerprint *
fp3_recording_add_fingerprint(
    struct fp3_recording *recording, const struct fp3_fingerprint *fingerprint);

struct fp3_recording *
fp3_medium_add_recording(struct fp3_medium *medium,
                         const struct fp3_recording *recording);

struct fp3_recording *
fp3_recording_list_add_recording(struct fp3_recording_list *recording_list,
                                 const struct fp3_recording *recording);

/* XXX Should this perhaps have taken additional parameters: size_t
 * index, size_t position_medium, size_t position_track, float score?
 */
struct fp3_recording *
fp3_recording_list_add_recording_by_id(
    struct fp3_recording_list *recording_list, const char *id);

struct fp3_recording_list *
fp3_add_recording_list(struct fp3_release *release, size_t index);

struct fp3_medium *
fp3_release_add_medium(struct fp3_release *release,
                       const struct fp3_medium *medium);

struct fp3_release *
fp3_releasegroup_add_release(
    struct fp3_releasegroup *releasegroup, const struct fp3_release *release);

struct fp3_release *
fp3_add_release_by_id(struct fp3_releasegroup *releasegroup, char *id);

struct fp3_releasegroup *
fp3_result_add_releasegroup(
    struct fp3_result *result, const struct fp3_releasegroup *releasegroup);

struct fp3_releasegroup *
fp3_result_add_releasegroup_by_id(struct fp3_result *result, const char *id);


/********
 * GROW *
 ********/
//int
//fp3_grow_disc(struct fp3_disc *disc, size_t nmemb);

int
fp3_grow_recording_list(struct fp3_release *release, size_t index);

int
fp3_grow_release(struct fp3_release *release, size_t nmemb);

void
fp3_erase_disc(struct fp3_medium *medium, ssize_t i);

void
fp3_fingerprint_erase_stream(struct fp3_fingerprint *fingerprint, ssize_t i);

void
fp3_recording_erase_fingerprint(struct fp3_recording *recording, ssize_t i);

void
fp3_erase_recording(struct fp3_recording_list *recordings, ssize_t i);

void
fp3_erase_release(struct fp3_releasegroup *releasegroup, size_t i);

void
fp3_erase_releasegroup(struct fp3_result *result, size_t i);


/********
 * FIND *
 ********/
struct fp3_stream *
fp3_fingerprint_find_stream(struct fp3_fingerprint *fingerprint, size_t index);

struct fp3_disc *
fp3_medium_find_disc(struct fp3_medium *medium, const char *id);

struct fp3_fingerprint *
fp3_recording_find_fingerprint(struct fp3_recording *recording, const char *id);

struct fp3_recording *
fp3_recording_list_find_recording(
    struct fp3_recording_list *recording_list, const char *id);

struct fp3_recording *
fp3_release_find_recording_by_id(struct fp3_release *release, const char *id);

struct fp3_recording *
fp3_release_find_recording_by_index(
    struct fp3_release *release, unsigned long index);

struct fp3_release * // fp3_releasegroup_find_release
fp3_find_release(struct fp3_releasegroup *releasegroup, const char *id);

struct fp3_release *
fp3_result_find_release(struct fp3_result *result, const char *id);

struct fp3_releasegroup * // XXX fp3_result_find_releasegroup
fp3_find_releasegroup(struct fp3_result *result, const char *id);


/*********
 * CLEAR *
 *********/
/* XXX Is this the proper name?  What's it called in the STL?
 */
void
fp3_clear_disc(struct fp3_disc *disc);

void
fp3_clear_recording(struct fp3_recording *recording);

void
fp3_clear_medium(struct fp3_medium *medium);

void
fp3_clear_offset_list(struct fp3_offset_list *offset_list);

void
fp3_clear_result(struct fp3_result *result);

void
fp3_clear_release(struct fp3_release *release);

void
fp3_clear_releasegroup(struct fp3_releasegroup *releasegroup);

void
fp3_clear_stream(struct fp3_stream *stream);

void
fp3_clear_track(struct fp3_track *track);


/********
 * DUMP *
 ********/
int
fp3_disc_dump(const struct fp3_disc *disc, int indentation, int level);

int
fp3_medium_dump(const struct fp3_medium *medium, int indentation, int level);

int
fp3_recording_dump(
    const struct fp3_recording *recording, int indentation, int level);
int
fp3_release_dump(const struct fp3_release *release, int indentation, int level);

int
fp3_releasegroup_dump(
    const struct fp3_releasegroup *releasegroup, int indentation, int level);

int
fp3_result_dump(const struct fp3_result *result, int indentation, int level);

int
fp3_track_dump(const struct fp3_track *track, int indentation, int level);


/*******************
 * DUP (deep copy) *
 *******************/
struct fp3_fingerprint *
fp3_fingerprint_dup(const struct fp3_fingerprint *fingerprint);

struct fp3_recording *
fp3_recording_dup(const struct fp3_recording *recording);

struct fp3_recording_list *
fp3_recording_list_dup(const struct fp3_recording_list *recording_list);


/*********
 * MERGE *
 *********/
struct fp3_offset_list *
fp3_offset_list_merge(
    struct fp3_offset_list *dst, const struct fp3_offset_list *src);

struct fp3_releasegroup *
fp3_releasegroup_merge(
    struct fp3_releasegroup *dst, const struct fp3_releasegroup *src);

struct fp3_result *
fp3_result_merge(struct fp3_result *dst, const struct fp3_result *src);

/* XXX This could perhaps be folded into the fp3_sort_result()
 * function.
 */
int
fp3_permute_result(
    struct fp3_result *result, const size_t *permutation, size_t nmemb);

/* Sort the releases within a releasegroup
 */
void
fp3_sort_releasegroup(struct fp3_releasegroup *releasegroup);

/* Sort the releasegroups within a result.  XXX Should this really
 * recursively sort the releases as it currently does?
 */
void
fp3_sort_result(struct fp3_result *result);

void
fp3_sort_disc(struct fp3_disc *disc);

void
fp3_sort_medium(struct fp3_medium *medium);

STRUCTURES_END_C_DECLS

#endif /* !STRUCTURES_H */
