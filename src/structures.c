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

#include <sys/queue.h>

#include <stdio.h>
#include <stdlib.h>

#include <limits.h>
#include <math.h>
#include <string.h>
#include <wchar.h> // XXX for wprintf(3)

#include "fingersum.h"
#include "structures.h"


/* Assumes ID:s are general, null-terminated strings.
 *
 * Appears musicbrainz library uses regular strings, so this is
 * what we'll do here as well.
 *
 * XXX Sort out the types: size_t instead of unsigned int in several
 * places.
 *
 * XXX Think about const correctness here: e.g. what about all the
 * char data+q
 *
 * XXX This really calls from some macros or even templates!
 *
 * XXX Could tune the capacity growth with statistics from MusicBrainz
 */
struct fp3_track *
fp3_new_track()
{
    struct fp3_track *track;

    track = malloc(sizeof(struct fp3_track));
    if (track == NULL)
        return (NULL);

    track->indices = NULL;
    track->nmemb = 0;
    track->capacity = 0;
    track->position = 0;

//    track->confidence_v1 = 0;
//    track->confidence_v2 = 0;
    track->confidence_max = 0;
    track->confidence_total = 0;

    track->confidence_eac_max = 0;
    track->confidence_eac_total = 0;

//    track->offset = 0;
    track->checksums = NULL;
    track->nmemb_checksums = 0;
    track->capacity_checksums = 0;

    return (track);
}


struct fp3_offset_list *
fp3_new_offset_list()
{
    struct fp3_offset_list *offset_list;

    offset_list = malloc(sizeof(struct fp3_offset_list));
    if (offset_list == NULL)
        return (NULL);

    offset_list->offsets = NULL;
    offset_list->nmemb = 0;
    offset_list->capacity = 0;

    return (offset_list);
}


struct fp3_disc *
fp3_new_disc()
{
    struct fp3_disc *disc;

    disc = malloc(sizeof(struct fp3_disc));
    if (disc == NULL)
        return (NULL);

    disc->id = NULL;
    disc->tracks = NULL;
    disc->nmemb = 0;
    disc->capacity = 0;

//    disc->offsets = NULL;
//    disc->nmemb_offsets = 0;
//    disc->capacity_offsets = 0;

    disc->offset_list = NULL;

    return (disc);
}


struct fp3_fingerprint *
fp3_new_fingerprint()
{
    struct fp3_fingerprint *fingerprint;

    fingerprint = malloc(sizeof(struct fp3_fingerprint));
    if (fingerprint == NULL)
        return (NULL);

    fingerprint->id = NULL;
    fingerprint->streams = NULL;
    fingerprint->nmemb = 0;
    fingerprint->capacity = 0;

    return (fingerprint);
}


struct fp3_medium *
fp3_new_medium()
{
    struct fp3_medium *medium;

    medium = malloc(sizeof(struct fp3_medium));
    if (medium == NULL)
        return (NULL);
    medium->discids = NULL;
    medium->nmemb = 0;
    medium->capacity = 0;


    /* New stuff
     */
    medium->discs = NULL;
    medium->nmemb_discs = 0;
    medium->capacity_discs = 0;

    medium->tracks = NULL;
    medium->nmemb_tracks = 0;
    medium->capacity_tracks = 0;

    medium->position = 0;

    return (medium);
}


struct fp3_recording *
fp3_new_recording()
{
    struct fp3_recording *recording;

    recording = malloc(sizeof(struct fp3_recording));
    if (recording == NULL)
        return (NULL);
    recording->id = NULL;
//    recording->index = 0;
//    recording->position_medium = 0;
//    recording->position_track = 0;

//    recording->position_stream = 0; // XXX This is actually a valid position!

    recording->score = 0; // XXX NAN;

//    recording->version = 0;
//    recording->confidence_v1 = 0;
//    recording->confidence_v2 = 0;
//    recording->confidence_max = 0;
//    recording->confidence_total = 0;
//    recording->confidence_eac_max = 0;
//    recording->confidence_eac_total = 0;
//    recording->offset = 0;

    // New stuff
    recording->fingerprints = NULL;
    recording->nmemb = 0;
    recording->capacity = 0;

    recording->position = 0; // XXX This is NOT a valid position!
    
    return (recording);
}


struct fp3_recording_list *
fp3_new_recording_list()
{
    struct fp3_recording_list *recordings;
    
    recordings = malloc(sizeof(struct fp3_recording_list));
    if (recordings == NULL)
        return (NULL);
    recordings->recordings = NULL;
    recordings->capacity = 0;
    recordings->nmemb = 0;
    
    return (recordings);
}


struct fp3_result *
fp3_new_result()
{
    struct fp3_result *result;

    result = malloc(sizeof(struct fp3_result));
    if (result == NULL)
        return (NULL);
    result->releasegroups = NULL;
    result->capacity = 0;
    result->nmemb = 0;

    /* XXX New additions
     */
    result->results = NULL;
    result->n_results = 0;
    
    return (result);
}


struct fp3_release *
fp3_new_release()
{
    struct fp3_release *release;
    
    release = malloc(sizeof(struct fp3_release));
    if (release == NULL)
        return (NULL);
//    release->recordings = NULL;
    release->streams = NULL;
    release->id = NULL;
    release->capacity = 0;
    release->nmemb = 0;
    release->track_count = 0;

    /* XXX Uncertain stuff
     */
    release->distance = LONG_MAX;
//    release->score = NAN;
    release->mb_release = NULL;

    /* XXX More uncertain stuff
     */
    release->media = NULL;
    release->nmemb_media = 0;
    release->capacity_media = 0;

    release->confidence_min = 0;
    release->metadata_distance = 0;

    return (release);
}


struct fp3_releasegroup *
fp3_new_releasegroup()
{
    struct fp3_releasegroup *releasegroup;
    
    releasegroup = malloc(sizeof(struct fp3_releasegroup));
    if (releasegroup == NULL)
        return (NULL);
    releasegroup->releases = NULL;
    releasegroup->id = NULL;
    releasegroup->capacity = 0;
    releasegroup->nmemb = 0;

    return (releasegroup);
}


struct fp3_stream *
fp3_new_stream()
{
    struct fp3_stream *stream;

    stream = malloc(sizeof(struct fp3_stream));
    if (stream == NULL)
        return (NULL);

    stream->index = 0; // XXX This is actually a valid position!
    stream->score = NAN;

    return (stream);
}


void
fp3_ar_free(struct fp3_ar *result)
{
    if (result->checksums != NULL)
        free(result->checksums);
    free(result);
}


void
fp3_stream_free(struct fp3_stream *stream)
{
    free(stream);
}


void
fp3_free_track(struct fp3_track *track)
{
    if (track->indices != NULL)
        free(track->indices);
    free(track);
}


void
fp3_free_offset_list(struct fp3_offset_list *offset_list)
{
    if (offset_list->offsets != NULL)
        free(offset_list->offsets);
    free(offset_list);
}


void
fp3_free_disc(struct fp3_disc *disc)
{
    size_t i;

    if (disc->id != NULL)
        free(disc->id);

    if (disc->tracks != NULL) {
        for (i = 0; i < disc->capacity; i++) {
            if (disc->tracks[i] != NULL) {
                // XXX This will probably crash now, because
                // recordings are "stolen" and owned by someone else.
                // Might need a deep copy for recordings.
                ; // fp3_free_recording_list(disc->recordings[i]);

                fp3_free_track(disc->tracks[i]);
            }
        }
        free(disc->tracks);
    }
    
//    if (disc->offsets != NULL)
//        free(disc->offsets);

    if (disc->offset_list != NULL)
        fp3_free_offset_list(disc->offset_list);

    free(disc);
}


void
fp3_free_fingerprint(struct fp3_fingerprint *fingerprint)
{
    size_t i;

    if (fingerprint->id != NULL)
        free(fingerprint->id);
    for (i = 0; i < fingerprint->nmemb; i++) {
        if (fingerprint->streams[i] != NULL)
            fp3_stream_free(fingerprint->streams[i]);
    }
    free(fingerprint);
}


void
fp3_free_medium(struct fp3_medium *medium)
{
    size_t i;

    for (i = 0; i < medium->capacity; i++) {
        if (medium->discids[i] != NULL)
            free(medium->discids[i]);
    }
    free(medium->discids);

    for (i = 0; i < medium->nmemb_discs; i++) {
        if (medium->discs[i] != NULL)
            fp3_free_disc(medium->discs[i]);
    }
    free(medium->discs);

    for (i = 0; i < medium->nmemb_tracks; i++) {
        if (medium->tracks[i] != NULL)
            fp3_free_recording(medium->tracks[i]);
    }
    free(medium->tracks);

    free(medium);
}


/* XXX Where to check for NULL in all this?  I guess it should be
 * assumed that the argument is not NULL (the caller checks).
 *
 * XXX Make all these functions call the _clear_ functions!
 */
void
fp3_free_recording(struct fp3_recording *recording)
{
    size_t i;

    if (recording->id != NULL)
        free(recording->id);

    if (recording->fingerprints != NULL) {
        for (i = 0; i < recording->capacity; i++) {
            if (recording->fingerprints[i] != NULL)
                fp3_free_fingerprint(recording->fingerprints[i]);
        }
        free(recording->fingerprints);
    }

    free(recording);
}


void
fp3_free_recording_list(struct fp3_recording_list *recording_list)
{
    size_t i;

    for (i = 0; i < recording_list->capacity; i++) {
        if (recording_list->recordings[i] != NULL)
            fp3_free_recording(recording_list->recordings[i]);
    }
    free(recording_list);
}


void
fp3_free_release(struct fp3_release *release)
{
    size_t i;

    for (i = 0; i < release->capacity; i++) {
        if (release->streams[i] != NULL)
            fp3_free_recording_list(release->streams[i]);
    }
    if (release->capacity != 0)
        free(release->streams);
    if (release->id != NULL)
        free(release->id);
    free(release);
}


void
fp3_free_releasegroup(struct fp3_releasegroup *releasegroup)
{
    size_t i;

    for (i = 0; i < releasegroup->capacity; i++) {
        if (releasegroup->releases[i] != NULL)
            fp3_free_release(releasegroup->releases[i]);
    }
    if (releasegroup->releases != NULL)
        free(releasegroup->releases);
    if (releasegroup->id != NULL)
        free(releasegroup->id);
    free(releasegroup);
}


void
fp3_free_result(struct fp3_result *result)
{
    size_t i;

    for (i = 0; i < result->capacity; i++)
        fp3_free_releasegroup(result->releasegroups[i]);
    if (result->releasegroups != NULL)
        free(result->releasegroups);
    free(result);
}


void
fp3_clear_fingerprint(struct fp3_fingerprint *fingerprint)
{
    struct fp3_stream *stream;
    size_t i;

    if (fingerprint->id != NULL) {
//        printf("Clearing fingerprint %s %f\n",
//               fingerprint->id, fingerprint->score);
        free(fingerprint->id);
        fingerprint->id = NULL;
    }

    for (i = 0; i < fingerprint->nmemb; i++) {
        stream = fingerprint->streams[i];
        if (stream != NULL)
            fp3_clear_stream(stream);
    }
    fingerprint->nmemb = 0;
}


/* Cannot clear anything beyond nmemb, because that must already be
 * cleared.  But it shouldn't crash, even so!
 */
void
fp3_clear_recording(struct fp3_recording *recording)
{
    struct fp3_fingerprint *fingerprint;
    size_t i;

    if (recording->id != NULL) {
//        printf("Clearing recording %s\n", recording->id);
        free(recording->id);
        recording->id = NULL;
    }

    for (i = 0; i < recording->nmemb; i++) {
        fingerprint = recording->fingerprints[i];
        if (fingerprint != NULL)
            fp3_clear_fingerprint(fingerprint);
    }
    recording->nmemb = 0;

    // XXX Reset all the other members?  YES WE SHOULD!
    recording->score = NAN; // XXX Oscillating between zero and
                            // NAN... I think NAN makes more sense.
}


void
fp3_clear_recordings(struct fp3_recording_list *recordings)
{
    struct fp3_recording *recording;
    size_t i;

    for (i = 0; i < recordings->nmemb; i++){
        recording = recordings->recordings[i];
        if (recording != NULL)
            fp3_clear_recording(recording);
    }
    recordings->nmemb = 0;
}


void
fp3_clear_stream(struct fp3_stream *stream)
{
    stream->index = 0;
    stream->score = NAN;
}


void
fp3_clear_track(struct fp3_track *track)
{
    track->nmemb = 0;
    track->position = 0;
}


void
fp3_clear_offset_list(struct fp3_offset_list *offset_list)
{
    offset_list->nmemb = 0;
}


void
fp3_clear_disc(struct fp3_disc *disc)
{
    size_t i;

    if (disc->id != NULL) {
//        printf("Clearing disc %s\n", disc->id);
        free(disc->id);
        disc->id = NULL;
    }

    for (i = 0; i < disc->nmemb; i++) {
        if (disc->tracks[i] != NULL)
            fp3_clear_track(disc->tracks[i]);
    }
    disc->nmemb = 0;

    if (disc->offset_list != NULL)
        fp3_clear_offset_list(disc->offset_list);
}


void
fp3_clear_medium(struct fp3_medium *medium)
{
    struct fp3_disc *disc;
    struct fp3_recording *recording;
    size_t i;

//    printf("Clearing medium at %p...", medium);
//    fflush(stdout);

    for (i = 0; i < medium->nmemb; i++) {
        if (medium->discids[i] != NULL) {
            free(medium->discids[i]);
            medium->discids[i] = NULL;
        }
    }
    medium->nmemb = 0;

    for (i = 0; i < medium->nmemb_discs; i++) {
        disc = medium->discs[i];
        if (disc != NULL)
            fp3_free_disc(disc);
        medium->discs[i] = NULL;
    }
    medium->nmemb_discs = 0;

    for (i = 0; i < medium->nmemb_tracks; i++) {
        recording = medium->tracks[i];
        if (recording != NULL)
            fp3_free_recording(recording);
        medium->tracks[i] = NULL;
    }
    medium->nmemb_tracks = 0;

//    printf("DONE\n");
}


void
fp3_clear_release(struct fp3_release *release)
{
    struct fp3_medium *medium;
    struct fp3_recording_list *recordings;
    size_t i;

    if (release->id != NULL) {
        free(release->id);
        release->id = NULL;
    }
    for (i = 0; i < release->capacity; i++) {
        recordings = release->streams[i];
        if (recordings != NULL)
            fp3_clear_recordings(recordings);
    }
    release->nmemb = 0; // XXX Reset all the other members?

    for (i = 0; i < release->capacity_media; i++) {
        medium = release->media[i];
        if (medium != NULL)
            fp3_clear_medium(medium);
    }
    release->nmemb_media = 0;
}


void
fp3_clear_releasegroup(struct fp3_releasegroup *releasegroup)
{
    struct fp3_release *release;
    size_t i;

    for (i = 0; i < releasegroup->nmemb; i++) {
        release = releasegroup->releases[i];
        if (release != NULL)
            fp3_clear_release(release);
    }

    releasegroup->nmemb = 0; // XXX Reset distance and score?
    if (releasegroup->id != NULL) {
        free(releasegroup->id);
        releasegroup->id = NULL;
    }
}


void
fp3_clear_result(struct fp3_result *result)
{
    struct fp3_releasegroup *releasegroup;
    size_t i;

    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        if (releasegroup != NULL)
            fp3_clear_releasegroup(releasegroup);
    }

    result->nmemb = 0;
}


struct fp3_stream *
fp3_fingerprint_add_stream(
    struct fp3_fingerprint *fingerprint, const struct fp3_stream *stream)
{
    struct fp3_stream *dst;
    void *p;

    if (fingerprint->capacity <= fingerprint->nmemb) {
        p = realloc(
            fingerprint->streams,
            (fingerprint->capacity + 1) * sizeof(struct fp3_fingerprint *));
        if (p == NULL)
            return (NULL);
        fingerprint->streams = p;
        fingerprint->streams[fingerprint->capacity++] = NULL;
    }

    dst = fingerprint->streams[fingerprint->nmemb];
    if (dst != NULL) {
        fp3_clear_stream(dst);
    } else {
        dst = fingerprint->streams[fingerprint->nmemb] = fp3_new_stream();
        if (dst == NULL)
            return (NULL);
    }

    if (stream != NULL) {
        dst->index = stream->index;
        dst->score = stream->score;
    }
    fingerprint->nmemb += 1;

    return (dst);
}


struct fp3_fingerprint *
fp3_recording_add_fingerprint(
    struct fp3_recording *recording, const struct fp3_fingerprint *fingerprint)
{
    struct fp3_fingerprint *dst;
    void *p;
    size_t i;

    if (recording->capacity <= recording->nmemb) {
        p = realloc(
            recording->fingerprints,
            (recording->capacity + 1) * sizeof(struct fp3_fingerprint *));
        if (p == NULL)
            return (NULL);
        recording->fingerprints = p;
        recording->fingerprints[recording->capacity++] = NULL;
    }

    dst = recording->fingerprints[recording->nmemb];
    if (dst != NULL) {
        fp3_clear_fingerprint(dst);
    } else {
        dst = recording->fingerprints[recording->nmemb] = fp3_new_fingerprint();
        if (dst == NULL)
            return (NULL);
    }

    if (fingerprint != NULL) { // XXX New idiom: allow NULL pointer
        if (fingerprint->id != NULL) {
            dst->id = strdup(fingerprint->id);
            if (dst->id == NULL)
                return (NULL);
        }
//        dst->index = fingerprint->index;
//        dst->score = fingerprint->score;
        for (i = 0; i < fingerprint->nmemb; i++) {
            if (fp3_fingerprint_add_stream(dst, fingerprint->streams[i]) == NULL) {
                return (NULL);
            }
        }
    }
    recording->nmemb += 1;

    return (dst);
}


int
fp3_track_add_checksum(struct fp3_track *track,
                       ssize_t offset,
                       int32_t checksum_v1, // XXX Should have been confidence
                       int32_t checksum_v2)
{
    void *p;
    size_t i;

    for (i = 0; i < track->nmemb_checksums; i++) {
        if (track->checksums[i]->offset == offset) {
            track->checksums[i]->checksum_v1 += checksum_v1;
            track->checksums[i]->checksum_v2 += checksum_v2;
            return (0);
        }
    }

    if (track->capacity_checksums <= track->nmemb_checksums) {
        p = realloc(
            track->checksums,
            (track->capacity_checksums + 1) * sizeof(struct fp3_checksums *));
        if (p == NULL)
            return (-1);
        track->checksums = p;
        track->capacity_checksums += 1;
    }

    track->checksums[track->nmemb_checksums] = malloc(
        sizeof(struct fp3_checksums));
    if (track->checksums[track->nmemb_checksums] == NULL)
        return (-1);

    track->checksums[track->nmemb_checksums]->offset = offset;
    track->checksums[track->nmemb_checksums]->checksum_v1 = checksum_v1;
    track->checksums[track->nmemb_checksums]->checksum_v2 = checksum_v2;

    track->checksums[track->nmemb_checksums]->count_eac = 0;
    track->nmemb_checksums += 1;

    return (0);
}


int
fp3_track_add_eac_checksum(struct fp3_track *track,
                           ssize_t offset,
                           size_t count)
{
    void *p;
    size_t i;

    for (i = 0; i < track->nmemb_checksums; i++) {
        if (track->checksums[i]->offset == offset) {
            track->checksums[i]->count_eac += count;
            return (0);
        }
    }

    if (track->capacity_checksums <= track->nmemb_checksums) {
        p = realloc(
            track->checksums,
            (track->nmemb_checksums + 1) * sizeof(struct fp3_checksums *));
        if (p == NULL)
            return (-1);
        track->checksums = p;
        track->capacity_checksums = track->nmemb_checksums + 1;
    }

    track->checksums[track->nmemb_checksums] = malloc(
        sizeof(struct fp3_checksums));
    if (track->checksums[track->nmemb_checksums] == NULL)
        return (-1);

    track->checksums[track->nmemb_checksums]->offset = offset;
    track->checksums[track->nmemb_checksums]->checksum_v1 = 0;
    track->checksums[track->nmemb_checksums]->checksum_v2 = 0;
    track->checksums[track->nmemb_checksums]->count_eac = count;
    track->nmemb_checksums += 1;

    return (0);
}


// Do not add the index if it is already present.
int
fp3_track_add_index(struct fp3_track *track, size_t index)
{
    void *p;
    size_t i;

    for (i = 0; i < track->nmemb; i++) {
        if (track->indices[i] == index)
            return (0);
    }

    if (track->capacity <= track->nmemb) {
        p = realloc(track->indices, (track->capacity + 1) * sizeof(size_t));
        if (p == NULL)
            return (-1);
        track->indices = p;
        track->capacity += 1;
    }

    track->indices[track->nmemb++] = index;
    return (0);
}


int
fp3_offset_list_add_offset(struct fp3_offset_list *offset_list, ssize_t offset)
{
    void *p;
    size_t i;

    for (i = 0; i < offset_list->nmemb; i++) {
        if (offset_list->offsets[i] == offset)
            return (0);
    }

    if (offset_list->capacity <= offset_list->nmemb) {
        p = realloc(
            offset_list->offsets,
            (offset_list->capacity + 1) * sizeof(ssize_t));
        if (p == NULL)
            return (-1);
        offset_list->offsets = p;
        offset_list->capacity += 1;
    }

    offset_list->offsets[offset_list->nmemb++] = offset;
    return (0);
}


int
fp3_offset_list_add_offset_list(
    struct fp3_offset_list *dst, struct fp3_offset_list *src)
{
    size_t i;

    for (i = 0; i < src->nmemb; i++) {
        if (fp3_offset_list_add_offset(dst, src->offsets[i]) != 0)
            return (-1);
    }

    return (0);
}


int
fp3_disc_add_offset(struct fp3_disc *disc, ssize_t offset)
{
#if 0
    void *p;
    size_t i;


//    printf("MARKER #0 %p %zd %zd\n",
//           disc->offsets, disc->capacity_offsets, disc->nmemb_offsets);

    for (i = 0; i < disc->nmemb_offsets; i++) {
        if (disc->offsets[i] == offset)
            return (0);
    }

//    printf("MARKER #1\n");

    if (disc->capacity_offsets <= disc->nmemb_offsets) {
        p = realloc(
            disc->offsets,
            (disc->capacity_offsets + 1) * sizeof(ssize_t));
        if (p == NULL)
            return (-1);
        disc->offsets = p;
        disc->capacity_offsets += 1;
    }

    disc->offsets[disc->nmemb_offsets++] = offset;
    return (0);
#else
    if (disc->offset_list == NULL) {
        disc->offset_list = fp3_new_offset_list();
        if (disc->offset_list == NULL)
            return (0);
    }

    return (fp3_offset_list_add_offset(disc->offset_list, offset));
#endif
}


struct fp3_offset_list *
fp3_disc_add_offset_list(
    struct fp3_disc *disc, const struct fp3_offset_list *offset_list)
{
    size_t i;

    if (disc->offset_list == NULL) {
        disc->offset_list = fp3_new_offset_list();
        if (disc->offset_list == NULL)
            return (NULL);
    }

    for (i = 0; i < offset_list->nmemb; i++) {
        if (fp3_offset_list_add_offset(
                disc->offset_list, offset_list->offsets[i]) != 0)
            return (NULL);
    }

    return (disc->offset_list);
}


struct fp3_track *
fp3_disc_add_track(struct fp3_disc *disc, const struct fp3_track *track)
{
    struct fp3_track *dst;
    void *p;
    size_t i;

    if (disc->capacity <= disc->nmemb) {
        p = realloc(
            disc->tracks,
            (disc->capacity + 1) * sizeof(struct fp3_track *));
        if (p == NULL)
            return (NULL);
        disc->tracks = p;
        disc->tracks[disc->capacity++] = NULL;
    }

    dst = disc->tracks[disc->nmemb];
    if (dst != NULL) {
        fp3_clear_track(dst);
    } else {
        dst = disc->tracks[disc->nmemb] = fp3_new_track();
        if (dst == NULL)
            return (NULL);
    }

    if (track != NULL) {
        for (i = 0; i < track->nmemb; i++) {
            if (fp3_track_add_index(dst, track->indices[i]) != 0)
                return (NULL);
        }
        dst->position = track->position;

        for (i = 0; i < track->nmemb_checksums; i++) {
            if (fp3_track_add_checksum(dst,
                                       track->checksums[i]->offset,
                                       track->checksums[i]->checksum_v1,
                                       track->checksums[i]->checksum_v2) != 0) {
                return (NULL);
            }
        }

//        dst->confidence_v1 = track->confidence_v1;
//        dst->confidence_v2 = track->confidence_v2;
        dst->confidence_max = track->confidence_max;
        dst->confidence_total = track->confidence_total;
        dst->confidence_eac_max = track->confidence_eac_max;
        dst->confidence_eac_total = track->confidence_eac_total;
//        dst->offset = track->offset;
    }
    disc->nmemb += 1;

    return (dst);
}


struct fp3_disc *
fp3_medium_add_disc(struct fp3_medium *medium, const struct fp3_disc *disc)
{
    struct fp3_disc *dst;
    void *p;
    size_t i;

//    printf("_mad() marker #0\n");
    if (medium->capacity_discs <= medium->nmemb_discs) {
        p = realloc(
            medium->discs,
            (medium->capacity_discs + 1) * sizeof(struct fp3_disc *));
        if (p == NULL)
            return (NULL);
        medium->discs = p;
        medium->discs[medium->capacity_discs++] = NULL;
    }

//    printf("_mad() marker #1 %zd %zd\n", medium->nmemb_discs, medium->capacity_discs);
    dst = medium->discs[medium->nmemb_discs];
    if (dst != NULL) {
        fp3_clear_disc(dst);
    } else {
        dst = medium->discs[medium->nmemb_discs] = fp3_new_disc();
        if (dst == NULL)
            return (NULL);
    }

//    printf("_mad() marker #2\n");
    if (disc != NULL) {
        if (disc->id != NULL) {
            dst->id = strdup(disc->id);
            if (dst->id == NULL)
                return (NULL);
        }

#if 1
        for (i = 0; i < disc->nmemb; i++) {
            if (fp3_disc_add_track(dst, disc->tracks[i]) == NULL)
                return (NULL);
        }

        for (i = 0; disc->offset_list != NULL && i < disc->offset_list->nmemb; i++) {
            if (fp3_disc_add_offset(dst, disc->offset_list->offsets[i]) != 0)
                return (NULL);
        }
#endif
    }
//    printf("_mad() marker #3\n");
    medium->nmemb_discs += 1;

    return (dst);
}


struct fp3_recording *
fp3_medium_add_recording(
    struct fp3_medium *medium, const struct fp3_recording *recording)
{
    struct fp3_recording *dst;
    void *p;
    size_t i;

    if (medium->capacity_tracks <= medium->nmemb_tracks) {
        p = realloc(
            medium->tracks,
            (medium->capacity_tracks + 1) * sizeof(struct fp3_recording *));
        if (p == NULL)
            return (NULL);
        medium->tracks = p;
        medium->tracks[medium->capacity_tracks++] = NULL;
    }

    dst = medium->tracks[medium->nmemb_tracks];
    if (dst != NULL) {
        fp3_clear_recording(dst);
    } else {
        dst = medium->tracks[medium->nmemb_tracks] = fp3_new_recording();
        if (dst == NULL)
            return (NULL);
    }

    if (recording != NULL) { // New idiom: allow NULL pointer
        if (recording->id != NULL) {
            dst->id = strdup(recording->id);
            if (dst->id == NULL)
                return (NULL);
        }
        for (i = 0; i < recording->nmemb; i++) {
            if (fp3_recording_add_fingerprint(
                    dst, recording->fingerprints[i]) == NULL) {
                return (NULL);
            }
        }

        dst->position_medium = recording->position_medium;
        dst->position_track = recording->position_track;
//        dst->position_stream = recording->position_stream;
        dst->score = recording->score;
//        dst->version = recording->version;
//        dst->confidence_v1 = recording->confidence_v1;
//        dst->confidence_v2 = recording->confidence_v2;
//        dst->confidence_max = recording->confidence_max;
//        dst->confidence_total = recording->confidence_total;
//        dst->confidence_eac_max = recording->confidence_eac_max;
//        dst->confidence_eac_total = recording->confidence_eac_total;
//        dst->offset = recording->offset;
        dst->position = recording->position;
    }
    medium->nmemb_tracks += 1;

    return (dst);
}


struct fp3_medium *
fp3_release_add_medium(
    struct fp3_release *release, const struct fp3_medium *medium)
{
    struct fp3_medium *dst;
    void *p;
    size_t i;

    if (release->capacity_media <= release->nmemb_media) {
        p = realloc(
            release->media,
            (release->capacity_media + 1) * sizeof(struct fp3_medium *));
        if (p == NULL)
            return (NULL);
        release->media = p;
        release->media[release->capacity_media++] = NULL;
    }

    dst = release->media[release->nmemb_media];
    if (dst != NULL) {
        fp3_clear_medium(dst);
    } else {
        dst = release->media[release->nmemb_media] = fp3_new_medium();
        if (dst == NULL)
            return (NULL);
    }

    if (medium != NULL) { // New idiom: allow NULL pointer
        for (i = 0; i < medium->nmemb_discs; i++) {
            if (fp3_medium_add_disc(dst, medium->discs[i]) == NULL)
                return (NULL);
        }
        for (i = 0; i < medium->nmemb_tracks; i++) {
            if (fp3_medium_add_recording(dst, medium->tracks[i]) == NULL)
                return (NULL);
        }
        dst->position = medium->position;
    }
    release->nmemb_media += 1;

    return (dst);
}


struct fp3_disc *
fp3_add_disc_by_id(struct fp3_medium *medium, const char *id)
{
    struct fp3_disc *disc;
    size_t i;

    for (i = 0; i < medium->nmemb_discs; i++) {
        disc = medium->discs[i];
        if (disc->id != NULL && id != NULL && strcmp(disc->id, id) == 0)
            return (disc);
    }

    disc = fp3_new_disc();
    if (disc == NULL)
        return (NULL);

    if (id == NULL) {
        disc->id = NULL;
    } else {
        disc->id = strdup(id);
        if (disc->id == NULL) {
            fp3_free_disc(disc);
            return (NULL);
        }
    }

    return (fp3_medium_add_disc(medium, disc));
}


char *
fp3_add_discid(struct fp3_medium *medium, const char *id)
{
    void *p;
    size_t i;

    /* If the discid is already present, do nothing.
     */
    for (i = 0; i < medium->nmemb; i++) {
        if (strcmp(medium->discids[i], id) == 0)
            return (medium->discids[i]);
    }

    if (medium->capacity == medium->nmemb) {
        p = realloc(
            medium->discids,
            (medium->capacity + 1) * sizeof(char *));
        if (p == NULL)
            return (NULL);
        medium->discids = p;
        medium->capacity += 1;
    }

    medium->discids[medium->nmemb] = strdup(id); // XXX THIS MODULE
                                                 // SHOULD NEVER
                                                 // STRDUP?
    if (medium->discids[medium->nmemb] == NULL)
        return (NULL);

    return (medium->discids[medium->nmemb++]);
}


struct fp3_recording *
fp3_recording_list_add_recording(struct fp3_recording_list *recording_list,
                                 const struct fp3_recording *recording)
{
    struct fp3_recording *dst;
    void *p;
    size_t i;

    if (recording_list->capacity == recording_list->nmemb) {
        p = realloc(
            recording_list->recordings,
            (recording_list->capacity + 1) * sizeof(struct fp3_recording *));
        if (p == NULL)
            return (NULL);
        recording_list->recordings = p;
        recording_list->recordings[recording_list->capacity++] = NULL;
    }

    dst = recording_list->recordings[recording_list->nmemb];
    if (dst != NULL) {
        fp3_clear_recording(dst);
    } else {
        dst = fp3_new_recording();
        if (dst == NULL)
            return (NULL);
    }

    if (recording != NULL) { // XXX New idiom: allow NULL pointer
        if (recording->id != NULL) {
            dst->id = strdup(recording->id);
            if (dst->id == NULL) {
                fp3_free_recording(dst);
                return (NULL);
            }
        }

        dst->position_medium = recording->position_medium;
        dst->position_track = recording->position_track;
//        dst->position_stream = recording->position_stream;
        dst->score = recording->score;
//        dst->version = recording->version;
//        dst->confidence_v1 = recording->confidence_v1;
//        dst->confidence_v2 = recording->confidence_v2;
//        dst->confidence_max = recording->confidence_max;
//        dst->confidence_total = recording->confidence_total;
//        dst->confidence_eac_max = recording->confidence_eac_max;
//        dst->confidence_eac_total = recording->confidence_eac_total;
//        dst->offset = recording->offset;

        for (i = 0; i < recording->nmemb; i++) {
            if (fp3_recording_add_fingerprint(
                    dst, recording->fingerprints[i]) == NULL) {
                fp3_free_recording(dst);
                return (NULL);
            }
        }
    }

    return (recording_list->recordings[recording_list->nmemb++] = dst);
}


struct fp3_recording *
fp3_recording_list_add_recording_by_id(
    struct fp3_recording_list *recording_list, const char *id)
{
    struct fp3_recording *recording;
    size_t i;

    if (id != NULL) {
        for (i = 0; i < recording_list->nmemb; i++) {
            recording = recording_list->recordings[i];
            if (strcmp(recording->id, id) == 0)
                return (recording);
        }
    }

    recording = fp3_new_recording();
    if (recording == NULL)
        return (NULL);

    if (id == NULL) {
        recording->id = NULL;
    } else {
        recording->id = strdup(id);
        if (recording->id == NULL) {
            fp3_free_recording(recording);
            return (NULL);
        }
    }
//    recording->score = 0; // XXX Something else need assigning?

//    printf("BEFORE add %p %zd %zd\n", recording_list, recording_list->nmemb, recording_list->capacity);
//    recording = fp3_recording_list_add_recording(recording_list, recording);
//    printf("AFTER  add %p %zd %zd\n", recording_list, recording_list->nmemb, recording_list->capacity);

    return (recording);
}


struct fp3_release *
fp3_releasegroup_add_release(
    struct fp3_releasegroup *releasegroup, const struct fp3_release *release)
{
    struct fp3_release *dst;
    void *p;
    size_t i;

    if (releasegroup->capacity == releasegroup->nmemb) {
        p = realloc(
            releasegroup->releases,
            (releasegroup->capacity + 1) * sizeof(struct fp3_release *));
        if (p == NULL)
            return (NULL);
        releasegroup->releases = p;
        releasegroup->releases[releasegroup->capacity] = NULL;
        releasegroup->capacity += 1;
    }

    dst = releasegroup->releases[releasegroup->nmemb];
    if (dst == NULL) {
        dst = fp3_new_release();
        if (dst == NULL)
            return (NULL);
    }
    fp3_clear_release(dst);

    if (release->id != NULL) {
        dst->id = strdup(release->id);
        if (dst->id == NULL) {
            fp3_free_release(dst);
            return (NULL);
        }
    }

    for (i = 0; i < release->nmemb_media; i++) {
        if (fp3_release_add_medium(dst, release->media[i]) == NULL) {
            fp3_free_release(dst);
            return (NULL);
        }
    }

    return (releasegroup->releases[releasegroup->nmemb++] = dst);
}


/* Steals the id pointer, so it cannot be const!
 */
struct fp3_release *
fp3_add_release_by_id(struct fp3_releasegroup *releasegroup, char *id)
{
    struct fp3_release *release;
    size_t i;

    if (id != NULL) {
        for (i = 0; i < releasegroup->nmemb; i++) {
            release = releasegroup->releases[i];
            if (strcmp(release->id, id) == 0)
                return (release);
        }
    }

    release = fp3_new_release();
    if (release == NULL)
        return (NULL);
    release->id = id;

    release = fp3_releasegroup_add_release(releasegroup, release);
    if (release == NULL) {
        fp3_free_release(release);
        return (NULL);
    }

    return (release);
}


/* Should perhaps have been "merge", and it needs to be very clear
 * that merge frees the operand, unlike copy which does not not.
 *
 * XXX This may lead to releasegroup duplicates (same id) in the
 * result.
 */
struct fp3_releasegroup *
fp3_result_add_releasegroup(
    struct fp3_result *result, const struct fp3_releasegroup *releasegroup)
{
    struct fp3_releasegroup *dst;
    void *p;
    size_t i;


    /* XXX This is fp3_add_releasegroup()... sort of.  Did I mean
     * fp3_new_releasegroup()?
     */
    if (result->capacity == result->nmemb) {
        p = realloc(
            result->releasegroups,
            (result->capacity + 1) * sizeof(struct fp3_releasegroup *));
        if (p == NULL)
            return (NULL);
        result->releasegroups = p;
        result->releasegroups[result->capacity] = NULL;
        result->capacity += 1;
    }

    dst = result->releasegroups[result->nmemb];
    if (dst == NULL) {
        dst = fp3_new_releasegroup();
        if (dst == NULL)
            return (NULL);
    }
    fp3_clear_releasegroup(dst);

    if (releasegroup->id != NULL) {
        dst->id = strdup(releasegroup->id);
        if (dst->id == NULL) {
            fp3_free_releasegroup(dst);
            return (NULL);
        }
    }

    for (i = 0; i < releasegroup->nmemb; i++) {
        if (fp3_releasegroup_add_release(
                dst, releasegroup->releases[i]) == NULL) {
            fp3_free_releasegroup(dst);
            return (NULL);
        }
    }

    return (result->releasegroups[result->nmemb++] = dst);
}


/* Find releasegroup if it exists, otherwise, add new.  Shallow copy
 * of id (and this prevents it from being a const pointer parameter).
 *
 * id will be NULL when adding a dummy releasegroup during
 * construction in acoustid.c::startelm().
 */
struct fp3_releasegroup *
fp3_result_add_releasegroup_by_id(struct fp3_result *result, const char *id)
{
    struct fp3_releasegroup *releasegroup;
    size_t i;

    if (id != NULL) {
        for (i = 0; i < result->nmemb; i++) {
            releasegroup = result->releasegroups[i];
            if (strcmp(releasegroup->id, id) == 0)
                return (releasegroup);
        }
    }

    releasegroup = fp3_new_releasegroup();
    if (releasegroup == NULL)
        return (NULL);

    if (id == NULL) {
        releasegroup->id = NULL;
    } else {
        releasegroup->id = strdup(id);
        if (releasegroup->id == NULL) {
            fp3_free_releasegroup(releasegroup);
            return (NULL);
        }
    }


    /* XXX This is fp3_add_releasegroup()... sort of.  Did I mean
     * fp3_new_releasegroup()?
     */
    return (fp3_result_add_releasegroup(result, releasegroup));
}


#if 0
/* XXX Maybe better as set_track_count or some such, but that implies
 * that the track count can be shrunk as well!
 */
int
fp3_grow_disc(struct fp3_disc *disc, size_t nmemb)
{
    void *p;

    if (disc->nmemb >= nmemb)
        return (0);

    if (disc->capacity < nmemb) {
        p = realloc(disc->tracks, nmemb * sizeof(struct fp3_recording_list *));
        if (p == NULL)
            return (-1);
        disc->tracks = p;
        do {
            disc->tracks[disc->capacity] = fp3_new_recording_list();
            if (disc->tracks[disc->capacity] == NULL)
                return (-1);
        } while (++disc->capacity < nmemb);
        disc->nmemb = disc->capacity;
    }

    return (0);
}
#endif


/* XXX This is a funny function--it should probably be reserve() like
 * in C++?  And index should be size or something.
 */
int
fp3_grow_recording_list(struct fp3_release *release, size_t index)
{
    void *p;
    size_t i;

    if (release->capacity > index)
        return (0);

    p = realloc(
        release->streams,
        (index + 1) * sizeof(struct fp3_recording_list *));
    if (p == NULL)
        return (-1);

    release->streams = p;
    release->capacity = index + 1;

    for (i = release->nmemb; i < release->capacity; i++)
        release->streams[i] = NULL;

    return (0);
}


int
fp3_grow_release(struct fp3_release *release, size_t nmemb)
{
    void *p;

    if (release->nmemb_media >= nmemb)
        return (0);

    p = realloc(release->media, nmemb * sizeof(struct fp3_medium *));
    if (p == NULL)
        return (-1);

    release->media = p;
    do {
        release->media[release->nmemb_media] = NULL;
    } while (++release->nmemb_media < nmemb);

    return (0);
}


struct fp3_recording_list *
fp3_add_recording_list(struct fp3_release *release, size_t index)
{

    if (fp3_grow_recording_list(release, index) != 0)
        return (NULL);

    if (release->nmemb <= index)
        release->nmemb = index + 1;

    if (release->streams[index] != NULL)
        return (release->streams[index]);

    release->streams[index] = fp3_new_recording_list();
    return (release->streams[index]);
}


struct fp3_stream *
fp3_fingerprint_find_stream(struct fp3_fingerprint *fingerprint, size_t index)
{
    struct fp3_stream *stream;
    size_t i;

    for (i = 0; i < fingerprint->nmemb; i++) {
        stream = fingerprint->streams[i];

        if (stream->index == index)
            return (stream);
    }

    return (NULL);
}


struct fp3_fingerprint *
fp3_recording_find_fingerprint(struct fp3_recording *recording, const char *id)
{
    struct fp3_fingerprint *fingerprint;
    size_t i;

    for (i = 0; i < recording->nmemb; i++) {
        fingerprint = recording->fingerprints[i];

        if (strcmp(fingerprint->id, id) == 0)
            return (fingerprint);
    }

    return (NULL);
}


struct fp3_recording *
fp3_recording_list_find_recording(
    struct fp3_recording_list *recording_list, const char *id)
{
    struct fp3_recording *recording;
    size_t i;

    for (i = 0; i < recording_list->nmemb; i++) {
        recording = recording_list->recordings[i];
        if (recording == NULL)
            continue;
        if (recording->id == NULL)
            continue;
        if (strcmp(recording->id, id) == 0)
            return (recording);
    }

    return (NULL);
}


/* XXX Need the NULL-checks, because there may be gaps in the array.
 * But only loop over valid releases, so check against nmemb, not capacity.
 *
 * Cannot take const argument, because it returns non-const recording.
 */
struct fp3_recording *
fp3_release_find_recording_by_id(struct fp3_release *release, const char *id)
{
    struct fp3_recording *recording;
    struct fp3_recording_list *recordings;
    size_t i;

    for (i = 0; i < release->nmemb; i++) {
        recordings = release->streams[i];
        if (recordings == NULL)
            continue;

        recording = fp3_recording_list_find_recording(recordings, id);
        if (recording != NULL)
            return (recording);
    }

    return (NULL);
}


// XXX This function should probably not be?!  Or maybe it should be
// something like has_index, and should be defined closer to the
// (single) function that uses it?
struct fp3_recording *
fp3_release_find_recording_by_index(
    struct fp3_release *release, unsigned long index)
{
//    struct fp3_disc *disc;
    struct fp3_fingerprint *fingerprint;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
    struct fp3_stream *stream;
//    struct fp3_recording_list *track;
    size_t i, j, k, l; //, m;

    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

#if 0
        for (j = 0; j < medium->nmemb_discs; j++) {
            disc = medium->discs[j];

            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];

                for (l = 0; l < track->nmemb; l++) {
                    recording = track->recordings[l];

                    for (m = 0; m < recording->nmemb; m++) {
                        fingerprint = recording->fingerprints[m];

                        if (fingerprint->index == index)
                            return (recording);
                    }
                }
            }
        }
#else
        for (j = 0; j < medium->nmemb_tracks; j++) {
            recording = medium->tracks[j];
            if (recording == NULL)
                continue;

            for (k = 0; k < recording->nmemb; k++) {
                fingerprint = recording->fingerprints[k];

                for (l = 0; l < fingerprint->nmemb; l++) {
                    stream = fingerprint->streams[l];
                    if (stream->index == index)
                        return (recording);
                }
            }
        }
#endif
    }

    return (NULL);
}


struct fp3_release *
fp3_find_release(struct fp3_releasegroup *releasegroup, const char *id)
{
    struct fp3_release *release;
    size_t i;

    for (i = 0; i < releasegroup->nmemb; i++) {
        release = releasegroup->releases[i];
        if (release == NULL)
            continue;
        if (release->id == NULL)
            continue;
        if (strcmp(release->id, id) == 0)
            return (release);
    }

    return (NULL);
}


/* Note that @c NULL matches @c NULL here.
 */
struct fp3_disc *
fp3_medium_find_disc(struct fp3_medium *medium, const char *id)
{
    struct fp3_disc *disc;
    size_t i;

    for (i = 0; i < medium->nmemb_discs; i++) {
        disc = medium->discs[i];

        if (disc->id != NULL) {
            if (id != NULL && strcmp(disc->id, id) == 0)
                return (disc);
        } else {
            if (id == NULL)
                return (disc);
        }
    }

    return (NULL);
}


/* True (non-NULL) if at least one of the fingerprint's result lists
 * the given release ID.
 *
 * If there were no results, always return true.  NO, don't do that!
 *
 * XXX Comment migrated from old fp2-code, may no longer be valid.
 */
struct fp3_release *
fp3_result_find_release(struct fp3_result *result, const char *id)
{
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    size_t i;

    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        if (releasegroup == NULL)
            continue;

        release = fp3_find_release(releasegroup, id);
        if (release != NULL)
            return (release);
    }
    
    return (NULL);
}


struct fp3_releasegroup *
fp3_find_releasegroup(struct fp3_result *result, const char *id)
{
    struct fp3_releasegroup *releasegroup;
    size_t i;

    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        if (releasegroup == NULL)
            continue;
        if (releasegroup->id == NULL)
            continue;
        if(strcmp(releasegroup->id, id) == 0)
            return (releasegroup);
    }

    return (NULL);
}


int
fp3_stream_dump(
    const struct fp3_stream *stream, int indentation, int level)
{
    return (printf("%*sStream index %zd score %f\n",
                   level * indentation, "",
                   stream->index, stream->score));
}


int
fp3_fingerprint_dump(
    const struct fp3_fingerprint *fingerprint, int indentation, int level)
{
    size_t i;
    int ret, tot;

    tot = printf("%*sFingerprint %s\n",
                 level * indentation, "", fingerprint->id);

    if (tot < 0)
        return (tot);

    for (i = 0; i < fingerprint->nmemb; i++) {
        tot += ret = fp3_stream_dump(
            fingerprint->streams[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

    return (tot);
}


int
fp3_recording_dump(
    const struct fp3_recording *recording, int indentation, int level)
{
    size_t i;
    int ret, tot;

    tot = printf("%*sRecording %zd %s (%zd fingerprints)\n",
                 level * indentation, "", recording->position, recording->id, recording->nmemb);
    if (tot < 0)
        return (tot);

    for (i = 0; i < recording->nmemb; i++) {
        tot += ret = fp3_fingerprint_dump(
            recording->fingerprints[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

    return (tot);
}


int
fp3_recording_list_dump(
    const struct fp3_recording_list *recording_list, int indentation, int level)
{
    size_t i;
    int ret, tot;

    tot = printf("%*sTrack (%zd recordings)\n",
                 level * indentation, "", recording_list->nmemb);
    if (tot < 0)
        return (tot);

    for (i = 0; i < recording_list->nmemb; i++) {
        tot += ret = fp3_recording_dump(
            recording_list->recordings[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

#if 0
    /* XXX Fishy: unassigned?
     */
    if (recording_list == NULL || recording_list->nmemb == 0) {
        printf("      --- NO RECORDINGS ---\n");
        continue;
    }

    for (i = 0; i < recording_list->nmemb; i++) {
        recording = recordings->recordings[i];
        printf(

//            "      Recording ID %s [%zd %zd:%zd %f]\n",
            "      Recording ID %s score %f\n",
            recording->id,
//            recording->index,
//            recording->position_medium,
//            recording->position_track,
            recording->score);
    }
#endif

    return (tot);
}


int
fp3_track_dump(const struct fp3_track *track, int indentation, int level)
{
    size_t i;
    int ret, tot;

    tot = printf("%*sTrack %zd (%zd indices)\n",
                 level * indentation, "", track->position, track->nmemb);
    if (tot < 0)
        return (tot);

    tot += ret = printf("%*sIndices:", (level + 1) * indentation, "");
    if (ret < 0)
        return (ret);

    for (i = 0; i < track->nmemb; i++) {
        tot += ret = printf(" %zd", track->indices[i]);
        if (ret < 0)
            return (ret);
    }

    tot += ret = printf("\n");
    if (ret < 0)
        return (ret);
    printf("HAVE %zd checksums\n", track->nmemb_checksums); // *** TEST ***

    return (tot);
}


int
fp3_disc_dump(const struct fp3_disc *disc, int indentation, int level)
{
    size_t i;
    int ret, tot;

    tot = printf("%*sDisc %s (%zd tracks)\n", level * indentation, "", disc->id, disc->nmemb);
    if (tot < 0)
        return (tot);

    tot += ret = printf("%*sOffsets (%zd):", (level + 1) * indentation, "", disc->offset_list->nmemb);
    if (ret < 0)
        return (ret);

    for (i = 0; disc->offset_list != NULL && i < disc->offset_list->nmemb; i++) {
        tot += ret = printf(" %zd", disc->offset_list->offsets[i]);
        if (ret < 0)
            return (ret);
    }

    tot += ret = printf("\n");
    if (ret < 0)
        return (ret);
    
    for (i = 0; i < disc->nmemb; i++) {
        tot += ret = fp3_track_dump(disc->tracks[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

    return (tot);
}


/* XXX Loose the ability to say "Medium NN/MM (XX discs)".
 *
 * This function may now actually be called fp3_recording_array_dump()
 * and take a list of recordings and a size_t nmemb.
 */
int
fp3_medium_dump(const struct fp3_medium *medium, int indentation, int level)
{
    size_t i;
    int ret, tot;

    tot = printf("%*sMedium %zd (%zd discs, %zd tracks)\n",
                 level * indentation, "", medium->position, medium->nmemb_discs, medium->nmemb_tracks);
    if (tot < 0)
        return (tot);

    for (i = 0; i < medium->nmemb_discs; i++) {
        tot += ret = fp3_disc_dump(medium->discs[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

    for (i = 0; i < medium->nmemb_tracks; i++) {
        tot += ret = fp3_recording_dump(
            medium->tracks[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

    return (tot);
}


int
fp3_release_dump(const struct fp3_release *release, int indentation, int level)
{
    size_t i;
    int ret, tot;

//    printf("    release       at %p\n", release);

//    tot = printf("%*sRelease %s\n", level * indentation, "", release->id);
    tot = printf("%*sRelease %s at %p\n", level * indentation, "", release->id, release); // XXX
    if (tot < 0)
        return (tot);

//    printf("    streams       at %p (%zd)\n", release, release->nmemb);
//    for (i = 0; i < release->nmemb; i++)
//        printf("      Stream %zd %p\n", i, release->streams[i]);

    for (i = 0; i < release->nmemb_media; i++) {

        if (release->media[i] == NULL) // XXX
            continue;

        tot += ret = fp3_medium_dump(release->media[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }
    
    return (tot);
}


int
fp3_releasegroup_dump(
    const struct fp3_releasegroup *releasegroup, int indentation, int level)
{
    size_t i;
    int ret, tot;

//    printf("    releasegroup  at %p\n", releasegroup);

    tot = printf("%*sReleasegroup %s\n",
                 level * indentation, "", releasegroup->id);
    if (tot < 0)
        return (tot);

    for (i = 0; i < releasegroup->nmemb; i++) {
        tot += ret = fp3_release_dump(
            releasegroup->releases[i], indentation, level + 1);
        if (ret < 0)
            return (ret);
    }

    return (tot);
}


int
fp3_result_dump(const struct fp3_result *result, int indentation, int level)
{
    size_t i;
    int ret, tot;

    //printf("    result        at %p\n", result);

    tot = 0;
    for (i = 0; i < result->nmemb; i++) {
        tot += ret = fp3_releasegroup_dump(
            result->releasegroups[i], indentation, level + 0);
        if (ret < 0)
            return (ret);
    }

    return (tot);
}


struct fp3_stream *
fp3_stream_dup(const struct fp3_stream *stream)
{
    struct fp3_stream *dst;

    dst = fp3_new_stream();
    if (dst == NULL)
        return (NULL);

    dst->index = stream->index;
    dst->score = stream->score;

    return (dst);
}


struct fp3_fingerprint *
fp3_fingerprint_dup(const struct fp3_fingerprint *fingerprint)
{
    struct fp3_fingerprint *dst;
    size_t i;

    dst = fp3_new_fingerprint();
    if (dst == NULL)
        return (NULL);

    dst->id = strdup(fingerprint->id);
    if (dst->id == NULL) {
        fp3_free_fingerprint(dst);
        return (NULL);
    }

    for (i = 0; i < fingerprint->nmemb; i++) {
        if (fp3_fingerprint_add_stream(dst, fingerprint->streams[i]) == NULL) {
            fp3_free_fingerprint(dst);
            return (NULL);
        }
    }

    return (dst);
}


struct fp3_recording *
fp3_recording_dup(const struct fp3_recording *recording)
{
    struct fp3_recording *dst;

    dst = fp3_new_recording();
    if (dst == NULL)
        return (NULL);

    dst->id = strdup(recording->id);
    if (dst->id == NULL) {
        fp3_free_recording(dst);
        return (NULL);
    }
    
    dst->position_medium = recording->position_medium;
    dst->position_track = recording->position_track;
//    dst->position_stream = recording->position_stream;
    
    dst->score = recording->score;

//    dst->version = recording->version;
//    dst->confidence_v1 = recording->confidence_v1;
//    dst->confidence_v2 = recording->confidence_v2;
//    dst->confidence_max = recording->confidence_max;
//    dst->confidence_total = recording->confidence_total;
//    dst->confidence_eac_max = recording->confidence_eac_max;
//    dst->confidence_eac_total = recording->confidence_eac_total;
//    dst->offset = recording->offset;

    return (dst);
}


struct fp3_recording_list *
fp3_recording_list_dup(const struct fp3_recording_list *recording_list)
{
    struct fp3_recording_list *dst;
    struct fp3_recording *recording;
    char *id;
    size_t i;


    dst = fp3_new_recording_list();
    if (dst == NULL)
        return (NULL);


    /* Allocate dst once to size of the source recording_list.
     */
    dst->recordings = calloc(
        recording_list->nmemb, sizeof(struct fp3_recording *));
    if (dst->recordings == NULL) {
        fp3_free_recording_list(dst);
        return (NULL);
    }
    dst->capacity = recording_list->nmemb;


    /* strdup, required for deep copy
     */
    for (i = 0; i < recording_list->nmemb; i++) {
        id = strdup(recording_list->recordings[i]->id);
        if (id == NULL) {
            fp3_free_recording_list(dst);
            return (NULL);
        }

        recording = fp3_recording_list_add_recording_by_id(dst, id);
        if (recording == NULL) {
            fp3_free_recording_list(dst);
            return (NULL);
        }

//        recording->index = recording_list->recordings[i]->index;
//        recording->position_medium = recording_list->recordings[i]->position_medium;
//        recording->position_track = recording_list->recordings[i]->position_track;
        recording->score = recording_list->recordings[i]->score;
    }
    

    return (dst);
}


#if 0 // Currently not used
static struct fp3_recording_list *
_recording_list_merge(struct fp3_recording_list *dst, const struct fp3_recording_list *src)
{
    struct fp3_recording *recording_dst, *recording_src;
    size_t i;

    for (i = 0; i < src->nmemb; i++) {
        recording_src = src->recordings[i];
        recording_dst = fp3_recording_list_find_recording(
            dst, recording_src->id);

        // XXX Kludge: ensure the index is the same!  Should really
        // add the index to the index list in _recording_merge()
        // instead!  Actually, maybe not: maybe we need to keep the
        // current structure with multiple recordings per track in the
        // light of the fact that sometimes a fingerprint matches
        // several different recordings on the same release.
        //
        // XXX New idea: put the index with the fingerprint instead!

//        if (recording_dst != NULL && recording_dst->position_stream != recording_src->position_stream) {
//            recording_dst = NULL;
//        }

        if (recording_dst != NULL) {
            if (_recording_merge(recording_dst, recording_src) == NULL)
                return (NULL);
        } else {
            if (fp3_recording_list_add_recording(dst, recording_src) == NULL)
                return (NULL);
        }
    }

    return (dst);
}
#endif


struct fp3_offset_list *
fp3_offset_list_merge(
    struct fp3_offset_list *dst, const struct fp3_offset_list *src)
{
    size_t i;

    for (i = 0; i < src->nmemb; i++) {
        if (fp3_offset_list_add_offset(dst, src->offsets[i]) == 0)
            return (NULL);
    }

    return (dst);
}


#if 0 // XXX Not currently used
/* Merge by position
 */
static struct fp3_disc *
_disc_merge(struct fp3_disc *dst, const struct fp3_disc *src)
{
#if 0 // XXX NEEDS WORK!
    size_t i;

    for (i = 0; i < src->nmemb; i++) {
        if (i < dst->nmemb && dst->tracks[i] != NULL) {
            if (_recording_list_merge(dst->tracks[i], src->tracks[i]) == NULL)
                return (NULL);
        } else {
            if (fp3_disc_add_track(dst, src->tracks[i], i) == NULL)
                return (NULL);
        }
    }
#endif
    return (dst);
}
#endif


static void
_stream_merge(struct fp3_stream *dst, const struct fp3_stream *src)
{
    if (dst->score < src->score) {
        /* XXX This really should not happen!  The same fingerprint
         * should always give the same score!
         *
         * But different streams may match the same fingerprint with
         * different scores!  This is now handled by
         * _recording_merge() below.
         */
        dst->score = src->score;
    }
}


static struct fp3_fingerprint *
_fingerprint_merge(
    struct fp3_fingerprint *dst, const struct fp3_fingerprint *src)
{
    struct fp3_stream *stream_dst, *stream_src;
    size_t i;

    for (i = 0; i < src->nmemb; i++) {
        stream_src = src->streams[i];
        stream_dst = fp3_fingerprint_find_stream(dst, stream_src->index);

        if (stream_dst != NULL) {
            _stream_merge(stream_dst, stream_src);
        } else {
            if (fp3_fingerprint_add_stream(dst, stream_src) == NULL)
                return (NULL);
        }
    }

#if 0
    if (dst->index == src->index) {
        if (dst->score < src->score) {
            dst->score = src->score;
        } else {
        }
    } else {
        ; // XXX
    }
#endif

    return (dst);
}


static struct fp3_recording *
_recording_merge(struct fp3_recording *dst, const struct fp3_recording *src)
{
    struct fp3_fingerprint *fingerprint_dst, *fingerprint_src;
    size_t i;

    for (i = 0; i < src->nmemb; i++) {
        fingerprint_src = src->fingerprints[i];
        fingerprint_dst = fp3_recording_find_fingerprint(
            dst, fingerprint_src->id);

        if (fingerprint_dst != NULL /* && fingerprint_dst->index == fingerprint_src->index */) {
            _fingerprint_merge(fingerprint_dst, fingerprint_src);
        } else {
            if (fp3_recording_add_fingerprint(dst, fingerprint_src) == NULL)
                return (NULL);
        }
    }

    return (dst);
}


/* Merge by id, note caveat about ID:s that are NULL.  NO: now, merge
 * by position.  In addition, the ID:s of the recordings must either
 * both be unassigned or both assigned.  If both are assigned, they
 * must be identical.
 */
static struct fp3_medium *
_medium_merge(struct fp3_medium *dst, const struct fp3_medium *src)
{
    size_t i, j;
    int merged;

#if 0 // XXX NEEDS WORK!
    for (i = 0; i < src->nmemb_discs; i++) {
        disc_src = src->discs[i];
        disc_dst = fp3_medium_find_disc(dst, disc_src->id);

        if (disc_dst != NULL) {
            if (_disc_merge(disc_dst, disc_src) == NULL)
                return (NULL);
        } else {
            if (fp3_medium_add_disc(dst, disc_src) == NULL)
                return (NULL);
        }
    }
#endif

    for (i = 0; i < src->nmemb_tracks; i++) {
        merged = 0;
        for (j = 0; j < dst->nmemb_tracks; j++) {
            if (dst->tracks[j]->position == src->tracks[i]->position &&
                ((dst->tracks[j]->id == NULL && src->tracks[i] == NULL) ||
                 (dst->tracks[j]->id != NULL && src->tracks[i] != NULL &&
                  strcmp(dst->tracks[j]->id, src->tracks[i]->id) == 0))) {
                merged = 1;
                if (_recording_merge(dst->tracks[j], src->tracks[i]) == NULL)
                    return (NULL);
            }
        }

        if (merged == 0) {
            if (fp3_medium_add_recording(dst, src->tracks[i]) == NULL)
                return (NULL);
        }
    }

    return (dst);
}


/* Merge by position
 */
static struct fp3_release *
_release_merge(struct fp3_release *dst, const struct fp3_release *src)
{
    size_t i, j;
    int merged;

    for (i = 0; i < src->nmemb_media; i++) {
        merged = 0;
        for (j = 0; j < dst->nmemb_media; j++) {
            if (dst->media[j]->position == src->media[i]->position) {
                merged = 1;
                if (_medium_merge(dst->media[j], src->media[i]) == NULL)
                    return (NULL);
            }
        }
        if (merged == 0) {
            if (fp3_release_add_medium(dst, src->media[i]) == NULL)
                return (NULL);
        }
    }

    return (dst);
}


/* Merge by ID
 */
struct fp3_releasegroup *
fp3_releasegroup_merge(
    struct fp3_releasegroup *dst, const struct fp3_releasegroup *src)
{
    size_t i, j;
    int merged;

    for (i = 0; i < src->nmemb; i++) {
        merged = 0;
        for (j = 0; j < dst->nmemb; j++) {
            if (strcmp(dst->releases[j]->id,
                       src->releases[i]->id) == 0) {
                merged = 1;
                if (_release_merge(dst->releases[j], src->releases[i]) == NULL)
                    return (NULL);
            }
        }
        if (merged == 0) {
            if (fp3_releasegroup_add_release(dst, src->releases[i]) == NULL)
                return (NULL);
        }
    }

    return (dst);
}


struct fp3_result *
fp3_result_merge(struct fp3_result *dst, const struct fp3_result *src)
{
    size_t i, j;
    int merged;

    for (i = 0; i < src->nmemb; i++) {
        merged = 0;
        for (j = 0; j < dst->nmemb; j++) {
            if (strcmp(dst->releasegroups[j]->id,
                       src->releasegroups[i]->id) == 0) {
                merged = 1;
                if (fp3_releasegroup_merge(dst->releasegroups[j],
                                           src->releasegroups[i]) == NULL) {
                    return (NULL);
                }
            }
        }

        if (merged == 0) {
            if (fp3_result_add_releasegroup(dst, src->releasegroups[i]) == NULL)
                return (NULL);
        }
    }

    return (dst);
}


/* Calculate average score for a release.  Use highest score at each
 * position.
 */
static float
_fp3_release_score(const struct fp3_release *release)
{
    struct fp3_recording_list *recordings;
    struct fp3_recording *recording;
    size_t i, j;
    float score, score_tot;


    score_tot = 0;
    for (i = 0; i < release->nmemb; i++) {
        recordings = release->streams[i];

        if (recordings != NULL) {
            score = 0;
            for (j = 0; j < recordings->nmemb; j++) {
                recording = recordings->recordings[j];
                if (recording != NULL && recording->score > score)
                    score = recording->score;
            }
            score_tot += score;
        }
    }
    
    return (score_tot);
}


int
fp3_permute_result(
    struct fp3_result *result, const size_t *permutation, size_t nmemb)
{
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    struct fp3_recording_list** recordings;
    size_t i, j, k;


    if (result->n_results != nmemb)
        return (-1); // XXX result is inconsistent!

    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        if (releasegroup == NULL)
            continue;

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];
            if (release == NULL || release->streams == NULL)
                continue;

            if (release->nmemb != nmemb)
                return (-1); // XXX release is inconsistent!

            recordings = calloc(nmemb, sizeof(struct fp3_recording_list *));
            if (recordings == NULL)
                return (-1); // XXX ENOMEM

            for (k = 0; k < nmemb; k++) {
                if (permutation[k] >= nmemb)
                    return (-1); // XXX permutation is inconsistent.
                                 // It is nowhere checked that the
                                 // elements of permuation are unique
                recordings[k] = release->streams[permutation[k]];
            }

            free(release->streams);
            release->streams = recordings;
            release->capacity = nmemb;
        }
    }
    
    return (0);
}


static int
_compar_recording(const void *a, const void *b)
{
    const struct fp3_recording *ra = *((struct fp3_recording **)a);
    const struct fp3_recording *rb = *((struct fp3_recording **)b);

    if (ra->position < rb->position)
        return (-1);
    if (ra->position > rb->position)
        return (+1);

    return (0);
}


void
fp3_sort_medium(struct fp3_medium *medium)
{
    qsort(medium->tracks,
          medium->nmemb_tracks,
          sizeof(struct fp3_track *),
          _compar_recording);
}


static int
_compar_track(const void *a, const void *b)
{
    const struct fp3_track *ta = *((struct fp3_track **)a);
    const struct fp3_track *tb = *((struct fp3_track **)b);

    if (ta->position < tb->position)
        return (-1);
    if (ta->position > tb->position)
        return (+1);

    return (0);
}


void
fp3_sort_disc(struct fp3_disc *disc)
{
    qsort(disc->tracks,
          disc->nmemb,
          sizeof(struct fp3_track *),
          _compar_track);
}


/* Sort by distance, then score, and lastly lexicographically by ID.
 */
static int
_compar_release(const void *a, const void *b)
{
    const struct fp3_release *ra = *((struct fp3_release **)a);
    const struct fp3_release *rb = *((struct fp3_release **)b);
    float score_a, score_b;

    if (ra->distance < rb->distance)
        return (-1);
    if (ra->distance > rb->distance)
        return (+1);

    score_a = _fp3_release_score(ra);
    score_b = _fp3_release_score(rb);
    if (score_a > score_b)
        return (-1);
    if (score_a < score_b)
        return (+1);

    return (strcmp(ra->id, rb->id));
}


void
fp3_sort_releasegroup(struct fp3_releasegroup *releasegroup)
{
    qsort(releasegroup->releases,
          releasegroup->nmemb,
          sizeof(struct fp3_release *),
          _compar_release);
}


/* XXX From tagger.cpp: Releasegroups should probably be sorted by
 * size (cardinality), or sum-total score?
 */
static int
_compar_releasegroup(const void *a, const void *b)
{
    const struct fp3_releasegroup *ra = *((struct fp3_releasegroup **)a);
    const struct fp3_releasegroup *rb = *((struct fp3_releasegroup **)b);

    if (ra->distance < rb->distance)
        return (-1);
    if (ra->distance > rb->distance)
        return (+1);
//    if (ra->score > rb->score)
//        return (-1);
//    if (ra->score < rb->score)
//        return (+1);
    return (0);
}


void
fp3_sort_result(struct fp3_result *result)
{
    struct fp3_releasegroup *releasegroup;
    struct fp3_release *release;
    size_t i;

    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        fp3_sort_releasegroup(releasegroup);

        if (releasegroup->nmemb > 0) {
            release = releasegroup->releases[0];

            /* Make the score of the releasegroup equal to the score
             * of its best release?
             */
/*
            printf("Changing releasegroup scores:"
                   "%ld -> %ld, %f -> %f\n",
                   releasegroup->distance, release->distance,
                   releasegroup->score, release->score);
*/

            releasegroup->distance = release->distance;
//            releasegroup->score = release->score;
        } else {
            printf("releasegroup has no releases!\n");

            releasegroup->distance = LONG_MAX;
//            releasegroup->score = 0;
        }
    }
    

    qsort(result->releasegroups,
          result->nmemb,
          sizeof(struct fp3_releasegroup *),
          _compar_releasegroup);
}


void
fp3_erase_disc(struct fp3_medium *medium, ssize_t i)
{
    struct fp3_disc *disc;

    if (i < 0)
        i += medium->nmemb_discs;
    if (i < 0 || i > medium->nmemb_discs)
        return;

    disc = medium->discs[i];
    if (disc != NULL)
        fp3_free_disc(disc);

    if (medium->nmemb_discs > i + 1) {
        memmove(
            medium->discs + i,
            medium->discs + i + 1,
            (medium->nmemb_discs - i - 1) * sizeof(struct fp3_disc *));
    }

    medium->nmemb_discs -= 1;
}


void
fp3_fingerprint_erase_stream(struct fp3_fingerprint *fingerprint, ssize_t i)
{
    struct fp3_stream *stream;

    if (i < 0)
        i += fingerprint->nmemb;
    if (i < 0 || i >= fingerprint->nmemb)
        return;

    stream = fingerprint->streams[i];
    if (stream != NULL)
        fp3_clear_stream(stream);

    if (fingerprint->nmemb > i + 1) {
        memmove(
            fingerprint->streams + i,
            fingerprint->streams + i + 1,
            (fingerprint->nmemb - i - 1) * sizeof(struct fp3_stream *));
        fingerprint->streams[fingerprint->nmemb - 1] = stream;
    }

    fingerprint->nmemb -= 1;
}


void
fp3_recording_erase_fingerprint(struct fp3_recording *recording, ssize_t i)
{
    struct fp3_fingerprint *fingerprint;

    if (i < 0)
        i += recording->nmemb;
    if (i < 0 || i >= recording->nmemb)
        return;

    fingerprint = recording->fingerprints[i];
    if (fingerprint != NULL)
        fp3_clear_fingerprint(fingerprint);

    if (recording->nmemb > i + 1) {
        memmove(
            recording->fingerprints + i,
            recording->fingerprints + i + 1,
            (recording->nmemb - i - 1) * sizeof(struct fp3_fingerprint *));
        recording->fingerprints[recording->nmemb - 1] = fingerprint;
    }

    recording->nmemb -= 1;
}


/* Now supports Python-like negative indices.
 */
void
fp3_erase_recording(struct fp3_recording_list *recordings, ssize_t i)
{
    struct fp3_recording *recording;

    if (i < 0)
        i += recordings->nmemb;
    if (i < 0 || i >= recordings->nmemb)
        return;

    recording = recordings->recordings[i];
    if (recording != NULL)
        fp3_clear_recording(recording);

    if (recordings->nmemb > i + 1) {
        memmove(
            recordings->recordings + i,
            recordings->recordings + i + 1,
            (recordings->nmemb - i - 1) * sizeof(struct fp3_recording *));
        recordings->recordings[recordings->nmemb - 1] = recording;
    }

    recordings->nmemb -= 1;
}


/* Does nothing if @p i is out of range (XXX but maybe it should let
 * the caller do that--what does the STL do?).  Must use memmove(3)
 * instead of memcpy(3), because the memory may overlap.
 */
void
fp3_erase_release(struct fp3_releasegroup *releasegroup, size_t i)
{
    struct fp3_release *release;

    if (i >= releasegroup->nmemb)
        return;

    release = releasegroup->releases[i];
    if (release != NULL)
        fp3_clear_release(release);

    if (releasegroup->nmemb > i + 1) {
        memmove(
            releasegroup->releases + i,
            releasegroup->releases + i + 1,
            (releasegroup->nmemb - i - 1) * sizeof(struct fp3_release *));
        releasegroup->releases[releasegroup->nmemb - 1] = release;
    }

    releasegroup->nmemb -= 1;
}


/* Clear the releasegroup and shift it to the first unused slot (the
 * first slot beyond the decremented result->nmemb).  Decrement
 * result->nmemb, but do not touch result->capacitiy.
 */
void
fp3_erase_releasegroup(struct fp3_result *result, size_t i)
{
    struct fp3_releasegroup *releasegroup;

    if (i >= result->nmemb)
        return;

    releasegroup = result->releasegroups[i];
    if (releasegroup != NULL)
        fp3_clear_releasegroup(releasegroup);

    if (result->nmemb > i + 1) {
        memmove(
            result->releasegroups + i,
            result->releasegroups + i + 1,
            (result->nmemb - i - 1) * sizeof(struct fp3_releasegroup *));
        result->releasegroups[result->nmemb - 1] = releasegroup;
    }

    result->nmemb -= 1;
}
