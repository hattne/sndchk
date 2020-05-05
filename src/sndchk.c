/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

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
 *
 * XXX This program works on a cluster of tracks (or files or streams)
 */

#include <sys/time.h>
#include <sys/queue.h>

#include <err.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <unistd.h> // XXX for sleep
#include <wchar.h>

#include <musicbrainz5/mb5_c.h>

// XXX These should probably not be here!
#include <neon/ne_session.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
#include <neon/ne_xml.h>

#include "accuraterip.h"
#include "acoustid.h"
#include "fingersum.h"
#include "metadata.h"
#include "musicbrainz.h"
#include "pool.h"
#include "ratelimit.h"
#include "structures.h"

//#define DEBUG 1
//#define MB_RETRIES 5
//#define MB_SLEEP 5


#if 0
/* Returns first match -- there shouldn't be more, but that is not
 * checked!
 */
static Mb5Recording
find_in_mediumlist(Mb5MediumList ml, const char *id)
{
    char *rid;
    void *p;
    Mb5TrackList tl;
    Mb5Recording r;
    int i, j, k, len;


    len = 0;
    rid = NULL;

    for (i = 0; i < mb5_medium_list_size(ml); i++) {
        tl = mb5_medium_get_tracklist(mb5_medium_list_item(ml, i));
        for (j = 0; j < mb5_track_list_size(tl); j++) {
            r = mb5_track_get_recording(mb5_track_list_item(tl, j));

            k = mb5_recording_get_id(r, NULL, 0);
            if (k != 36)
                printf("        **** mb5_recording_get_id() said %d ****\n", k);

            if (k > len) {
                p = realloc(rid, (k + 1) * sizeof(char));
                if (p == NULL) {
                    if (rid != NULL)
                        free(rid);
                    return (NULL);
                }
                rid = p;
                len = k;
            }

            mb5_recording_get_id(r, rid, len + 1);
            if (strcmp(rid, id) == 0) {
                free(rid);
                return (r);
            }
        }
    }

    if (rid != NULL)
        free(rid);
    return (NULL);
}
#endif


#if 0
static int
length_in_mediumlist(Mb5MediumList ml, const char *id)
{
    char *rid;
    void *p;
    Mb5TrackList tl;
    Mb5Recording r;
    int i, j, k, len;


    len = 0;
    rid = NULL;

    for (i = 0; i < mb5_medium_list_size(ml); i++) {
        tl = mb5_medium_get_tracklist(mb5_medium_list_item(ml, i));
        for (j = 0; j < mb5_track_list_size(tl); j++) {
            r = mb5_track_get_recording(mb5_track_list_item(tl, j));

            k = mb5_recording_get_id(r, NULL, 0);
            if (k != 36)
                printf("        **** mb5_recording_get_id() said %d ****\n", k);

            if (k > len) {
                p = realloc(rid, (k + 1) * sizeof(char));
                if (p == NULL) {
                    if (rid != NULL)
                        free(rid);
                    return (-1);
                }
                rid = p;
                len = k;
            }

            mb5_recording_get_id(r, rid, len + 1);
            if (strcmp(rid, id) == 0) {
                free(rid);
                return (mb5_track_get_length(mb5_track_list_item(tl, j)));
            }
        }
    }

    if (rid != NULL)
        free(rid);
    return (-1);
}
#endif


#if 0 // XXX Should be covered by fp3_find_release()?
static struct fp3_release *
find_release(struct fp3_releasegroup *releasegroup, Mb5Release release)
{
    char *id;
    size_t i;
    int j;


    j = mb5_release_get_id(release, NULL, 0);
    if (j <= 0)
        return (NULL);

    id = calloc(j + 1, sizeof(char));
    if (id == NULL)
        return (NULL);

    mb5_release_get_id(release, id, j + 1);

    for (i = 0; i < releasegroup->nmemb; i++) {
        if (strcmp(id, releasegroup->releases[i]->id) == 0)
            return (releasegroup->releases[i]);
    }

    return (NULL);
}
#endif


/* Should probably calculate the fuzzy distance
 *
 * XXX Explain why we can't use the fuzzy match on the server: because
 * then we'd have to look up the TOC for every release (and there may
 * be many of them) instead of just looking up the releasegroup.  But
 * is this explanation valid?
 *
 * Returns -1 on error, +1 on mismatch, and 0 on match.  Nah, should
 * probably return the minimum distance (and adjust the release
 * accordingly, so it cannot be const).
 *
 * XXX Should possible take a nmemb argument for the number of
 * streams, instead of working it out from the other structures.  Or
 * better yet: put the sector counts into the fp3_release structure?
 *
 * For each medium try all discs and score the selection of each disc
 * for each medium.
 *
 * Assumption: a recording only occurs once on a release.
 */
struct toc_score
{
    long int distance;
    float score;
};


#if 0
static int
toc_match(struct fingersum_context **ctxs,
          struct fp3_release *release,
          Mb5MediumList ml,
          struct toc_score *toc_score)
{
    size_t i, j;
    struct fp3_recording_list *recordings;
    struct fp3_recording *recording;
    long int sectors;
    long int s;
    int k, l, m, n, p;

    Mb5Recording r;
    Mb5TrackList tl;
    Mb5DiscList dl;
    Mb5OffsetList ol;
    Mb5Track t;
    Mb5Offset o;
    char id[256]; // XXX
    char did[256]; // XXX
    char *dids[256]; // XXX
    int matches;
    float d, d_min;

    //void *p;

    struct fp3_recording_list *not_found;


    /* XXX The length of the recording is different from the length of
     * the track!
     *
     * Convert track length in milliseconds from musicbrainz, and
     * round up.
     */
    not_found = fp3_new_recording_list();
    for (k = 0; k < mb5_medium_list_size(ml); k++) {

        dl = mb5_medium_get_disclist(mb5_medium_list_item(ml, k));
        tl = mb5_medium_get_tracklist(mb5_medium_list_item(ml, k));

        for (l = 0; l < mb5_disc_list_size(dl); l++) {
            ol = mb5_disc_get_offsetlist(mb5_disc_list_item(dl, l));

            mb5_disc_get_id(mb5_disc_list_item(dl, l), did, sizeof(did));
            printf("  Checking discid ->%s<- for medium %d\n", did, k);

            for (m = 0; m < mb5_offset_list_size(ol); m++) {
                o = mb5_offset_list_item(ol, m);
                p = mb5_offset_get_position(o);

                if (m + 1 < mb5_offset_list_size(ol)) {
                    s = mb5_offset_get_offset(mb5_offset_list_item(ol, m + 1)) -
                        mb5_offset_get_offset(o);
                } else {
                    s = mb5_disc_get_sectors(mb5_disc_list_item(dl, l)) -
                        mb5_offset_get_offset(o);
                }


                /* Find the corresponding track, extract the
                 * recording.
                 */
                for (n = 0; n < mb5_track_list_size(tl); n++) {
                    t = mb5_track_list_item(tl, n);
                    if (mb5_track_get_position(t) == p)
                        break;
                }
                if (n == mb5_track_list_size(tl))
                    ; // XXX

                r = mb5_track_get_recording(t);
                mb5_recording_get_id(r, id, sizeof(id));


                /* Find the recording in the release.
                 *
                 * XXX fingersum_get_sectors() could fail, as could
                 * the lrint() stuff from MusicBrainz!
                 */
                matches = 0;
                d_min = INFINITY;
                for (i = 0; i < release->nmemb; i++) {
                    recordings = release->streams[i];
                    sectors = fingersum_get_sectors(ctxs[i]);
                    if (sectors < 0)
                        ; // XXX


                    /* XXX Why is this necessary again? Is it because
                     * a stream may not have been matched?
                     */
                    if (recordings == NULL) {
                        printf("  Stream %zd EMPTY SPACE\n", i);
                        continue;
                    }
                    if (recordings->nmemb > 1) {
                        printf("  Stream %zd has %zd recordings\n",
                               i, recordings->nmemb);
                    }

                    for (j = 0; j < recordings->nmemb; j++) {
                        recording = recordings->recordings[j];

                        if (strcmp(id, recording->id) != 0)
                            continue;

                        d = fabs(s - sectors);
                        if (d < d_min)
                            d_min = d;
                        if (d <= 0) {
                            dids[matches] = strdup(did); // XXX LEAK!
                            matches += 1;
                        }
                    }
                }

                if (matches == 0) {
                    if (isfinite(d_min)) {
                        printf("  %d:%d: mismatch %ld ->%s<- %d [%f]\n",
                               k, m, s, id, matches, d_min);
                        continue; // return (0);
                    }


                    /* This recording was not found.  Append to the
                     * list.
                     */
                    printf("  %d:%d: NOT FOUND\n", k, l);

/*
                    p = realloc(not_found, (nfs + 1) * sizeof(char *));
                    if (p == NULL)
                        ; // XXX
                    not_found = p;
                    not_found[nfs++] = strdup(id);
*/

                    recording = fp3_recording_list_add_recording_by_id(not_found, strdup(id));
                    if (recording == NULL)
                        ; // XXX

                    // XXX To avoid NAN for e.g. Snatch, but what
                    // about position_medium and position_track?
                    recording->score = 0;
                } else {
                    printf("  %d:%d: MATCH %ld ->%s<- %d [%f]\n",
                           k, m, s, id, matches, d_min);
                }
            }
        }


#if 0
        for (l = 0; l < mb5_track_list_size(tl); l++) {
            t = mb5_track_list_item(tl, l);
            r = mb5_track_get_recording(t);
            s = lrint(ceil(75.0 * mb5_track_get_length(t) / 1000));


            /* Find the recording in the release.
             *
             * XXX fingersum_get_sectors() could fail, as could the
             * lrint() stuff from MusicBrainz!
             */
            matches = 0;
            d_min = INFINITY;
            mb5_recording_get_id(r, id, sizeof(id));
            for (i = 0; i < release->nmemb; i++) {
                recordings = release->streams[i];
                sectors = fingersum_get_sectors(ctxs[i]);
                if (sectors < 0)
                    ; // XXX


                /* XXX Why is this necessary again?  Is it because a
                 * stream may not have been matched?
                 */
                if (recordings == NULL) {
                    printf("  Stream %zd EMPTY SPACE\n", i);
                    continue;
                }
                if (recordings->nmemb > 1) {
                    printf("  Stream %zd has %zd recordings\n",
                           i, recordings->nmemb);
                }

                for (j = 0; j < recordings->nmemb; j++) {
                    recording = recordings->recordings[j];

                    if (strcmp(id, recording->id) != 0)
                        continue;

                    d = fabs(s - sectors);
                    if (d < d_min)
                        d_min = d;
                    if (d <= 0)
                        matches += 1;
                }
            }

            if (matches == 0) {
                if (isfinite(d_min)) {
                    printf("  %d:%d: mismatch %ld ->%s<- %d [%f]\n",
                           k, l, s, id, matches, d_min);
                    continue; //return (0);
                }


                /* This recording was not found.  Append it to the list.
                 */
                printf("  %d:%d: NOT FOUND\n", k, l);
/*
                p = realloc(not_found, (nfs + 1) * sizeof(char *));
                if (p == NULL)
                    ; // XXX
                not_found = p;
                not_found[nfs++] = strdup(id);
*/
                struct fp3_recording *recording;

                recording = fp3_recording_list_add_recording_by_id(not_found, strdup(id));
                if (recording == NULL)
                    ; // XXX

                // XXX To avoid NAN for e.g. Snatch, but what about
                // position_medium and position_track?
                recording->score = 0;

            } else {
                printf("  %d:%d: MATCH %ld ->%s<- %d [%f]\n",
                       k, l, s, id, matches, d_min);
            }
        }
#endif
    }


    /* Copy the not_found set to all streams with without matches.
     * XXX What if release->nmemb == 0 but there are unassigned
     * streams?
     *
     * XXX In general: all cases of where the release->nmemb is not
     * equal to the number of unassigned streams.
     */
    for (i = 0; i < release->nmemb; i++) {
        if (release->streams[i] != NULL)
            continue;

        release->streams[i] = fp3_recording_list_dup(not_found);
        if (release->streams[i] == NULL)
            ; // XXX
    }


    /* DIAGNOSTIC
     */
    for (k = 0; k < matches; k++)
        printf("    MATCHING DISCID %d: ->%s<-\n", k, dids[k]);


    /* NEXT STEP STUFF
     */
    size_t *best, *current;
    size_t theor, practice;

    best = (size_t *)calloc(release->nmemb, sizeof(size_t));
    if (best == NULL)
        ; // XXX
    current = (size_t *)calloc(release->nmemb, sizeof(size_t));
    if (current == NULL)
        ; // XXX

    theor = 1;
    for (i = 0; i < release->nmemb; i++) {
        best[i] = 0;
        current[i] = 0;
        theor *= release->streams[i]->nmemb;
    }

    toc_score->distance = LONG_MAX;
    toc_score->score = 0;
    practice = 0;
    do {
        /* Check whether the combination is valid, i.e. that any
         * recording occurs at most once.  This will set j to
         * release->nmemb if, and only if, it is valid.
         */
        practice += 1;
        for (i = 0; i < release->nmemb; i++) {
            for (j = i + 1; j < release->nmemb; j++) {
                if (strcmp(
                        release->streams[i]->recordings[current[i]]->id,
                        release->streams[j]->recordings[current[j]]->id)
                    == 0) {
                    i = release->nmemb;
                    break;
                }
            }
        }


        // EVALUATE SCORE for current, update the score if better.  If
        // length_in_mediumlist() fails there are more streams than
        // recordings in the medium list and the length of the stream
        // will count towards the distance.
        //
        // XXX But what if release->streams[i] is NULL?  Can that
        // even happen?
        //
        // If there's a tie, then the first seen recording is chosen
        //
        // If the id is not found in the medium list the length of the
        // stream will count towards the distance.
        //
        // XXX combo -> configuration
        if (j == release->nmemb) {
            float f;
            long int s;

            f = 0;
            s = 0;
            for (i = 0; i < release->nmemb; i++) {
                int l;


                // Old way
                l = length_in_mediumlist(
                    ml,
                    release->streams[i]->recordings[current[i]]->id);


                // New way
/*
                l = length_in_mediumlist2(
                    ml,
                    release->streams[i]->recordings[current[i]]->id,
                    did);
*/
                if (l < 0)
                    l = 0;

                f += release->streams[i]->recordings[current[i]]->score;
                s += labs(lrint(ceil(75.0 * l / 1000)) -
                          fingersum_get_sectors(ctxs[i])); // XXX What if sectors fails (is negative)?
            }

            // Add the number of sectors for all the stuff in the
            // medium that is not assigned
            for (k = 0; k < mb5_medium_list_size(ml); k++) {
                tl = mb5_medium_get_tracklist(mb5_medium_list_item(ml, k));

                for (l = 0; l < mb5_track_list_size(tl); l++) {
                    t = mb5_track_list_item(tl, l);
                    r = mb5_track_get_recording(t);

                    mb5_recording_get_id(r, id, sizeof(id)); // XXX
                    for (i = 0; i < release->nmemb; i++) {
                        if (strcmp(
                                release->streams[i]->recordings[current[i]]->id,
                                id) == 0) {
                            break;
                        }
                    }

                    if (i == release->nmemb)
                        s += lrint(ceil(75.0 * mb5_track_get_length(t) / 1000));
                }
            }

            if (s < toc_score->distance || (s == toc_score->distance && f > toc_score->score)) {
                toc_score->score = f;
                toc_score->distance = s;
                for (i = 0; i < release->nmemb; i++)
                    best[i] = current[i];
            }

        } else {
            ; //printf("       +++ This combo is invalid! +++\n");
        }


        /* Generate next trial in current, break out of the outer loop
         * if all trials exhausted.
         */
        for (i = 0; i < release->nmemb; i++) {
            if (current[i] + 1 < release->streams[i]->nmemb) {
                current[i] += 1;
                while (i > 0)
                    current[--i] = 0;
                break;
            }
        }
    } while (i < release->nmemb);

    printf("DONE FOR NOW %zd %zd [distance %ld score %f]\n", theor, practice, toc_score->distance, toc_score->score);
    if (theor != practice)
        printf("   ### MISMATCH: %zd vs %zd\n", theor, practice);


    /* XXX What if there are streams without any matches left at this
     * stage?
     */

    /* For all the streams where we do not have exactly one match try
     * all possible combinations.
     */

    /* XXX What if nfs (number of recordings in not_found) > 0 at this
     * stage (recordings in musicbrainz for which there is no matching
     * stream)?
     */

    /* XXX Eliminate all but the best choice, and free the arrays.
     */
    for (i = 0; i < release->nmemb; i++) {
        for (j = 0; j < release->streams[i]->nmemb; j++) {
            if (j == best[i]) {
                release->streams[i]->recordings[0] =
                    release->streams[i]->recordings[j];
            } else {
                fp3_free_recording(release->streams[i]->recordings[j]);
            }
        }
        release->streams[i]->nmemb = 1;
    }

    free(best);
    free(current);


    /* This message is probably obsolete now.  Is there a way to check?
     */
#if 0
    printf("FOUND A MATCH\n");
    for (i = 0; i < not_found->nmemb; i++)
        printf("  Recording ->%s<- was not found\n", not_found->recordings[i]->id);
#endif

    return (1);
}
#endif


#if 0
/* XXX MUST RETURN LENGTH IN SECTORS!
 */
static int
length_in_mediumlist2(Mb5MediumList ml, const char *id, int *di_current)
{
    char *rid;
    void *p;
    Mb5DiscList dl;
    Mb5TrackList tl;
    Mb5OffsetList ol;
    Mb5Disc d;
    Mb5Track t;
    Mb5Offset o;
    Mb5Recording r;
    int i, j, k, l, len;

    len = 0;
    rid = NULL;

    for (i = 0; i < mb5_medium_list_size(ml); i++) {
        tl = mb5_medium_get_tracklist(mb5_medium_list_item(ml, i));
        for (j = 0; j < mb5_track_list_size(tl); j++) {
            r = mb5_track_get_recording(mb5_track_list_item(tl, j));

            k = mb5_recording_get_id(r, NULL, 0);
            if (k != 36)
                printf("        **** mb5_recording_get_id() said %d ****\n", k);

            if (k > len) {
                p = realloc(rid, (k + 1) * sizeof(char));
                if (p == NULL) {
                    if (rid != NULL)
                        free(rid);
                    return (-1);
                }
                rid = p;
                len = k;
            }

            mb5_recording_get_id(r, rid, len + 1);
            if (strcmp(rid, id) == 0) {


                /* This is the old way of doing it: return the length
                 * of the track.
                 */
                //return (mb5_track_get_length(mb5_track_list_item(tl, j)));

                t = mb5_track_list_item(tl, j);
                dl = mb5_medium_get_disclist(mb5_medium_list_item(ml, i));
                d = mb5_disc_list_item(dl, di_current[i]);
                ol = mb5_disc_get_offsetlist(d);

                for (l = 0; l < mb5_offset_list_size(ol); l++) {
                    o = mb5_offset_list_item(ol, l);

                    if (mb5_offset_get_position(o) ==
                        mb5_track_get_position(t)) {

                        if (l + 1 < mb5_offset_list_size(ol)) {
                            return (mb5_offset_get_offset(
                                        mb5_offset_list_item(ol, l + 1)) -
                                    mb5_offset_get_offset(o));
                        } else {
                            return (mb5_disc_get_sectors(d) -
                                    mb5_offset_get_offset(o));
                        }
                    }
                }
            }
        }
    }

    if (rid != NULL) // XXX not freed on other returns!
        free(rid);
    return (-1);
}
#endif


#if 0
/* For the given choice of discids for each medium, find the best
 * (valid) selection of recordings and calculate its score.
 *
 * This will return high score if the release does not have any
 * discids.  Alternatively match against the length of the tracks
 * instead, see tagger.cpp.bak-06.
 *
 * Assumes order of discs in disc_list is constant (invariant?).
 *
 * XXX BUG The number of inner loops should be independent of the
 * discid?  Unless there are unassigned streams!
 *
 * THIS IS THE MANHATTAN DISTANCE!  If the release does not have a
 * TOC, fall back on track lengths (but those may not be available
 * either, in that case)?
 *
 * XXX Why does looping over discs take so long?  As far as I recall,
 * no remote lookups are necessary!  See Lenny Kravitz's "5".
 *
 * XXX This appears to crash if the "00.  XXX - Hidden Track One
 * Audio.flac" is still there.
 */
static int
toc_match_discids(struct fingersum_context **ctxs,
                  struct fp3_release *release,
                  Mb5MediumList ml,
                  int *di_current,
                  size_t *re_best,
                  struct toc_score *toc_score)
{
    size_t i, j, nmemb;
    size_t *re_current;
    int k, l;
    Mb5TrackList tl;
    Mb5Track t;
    Mb5Recording r;
    char id[256]; // XXX


    /* See comment regarding release->nmemb in toc_match2() below.
     */
    nmemb = release->nmemb;
    re_current = (size_t *)calloc(nmemb, sizeof(size_t));
    if (re_current == NULL)
        ; // XXX

    toc_score->distance = LONG_MAX;
    toc_score->score = 0;
    size_t inner_loops = 0;
    do {
        inner_loops += 1;

        /* Check whether the current combination is valid, i.e. that
         * any recording occurs at most once.  This will set j to
         * release->nmemb if, and only if, it is valid.
         */
//        printf("hej0\n");
        for (i = 0; i < release->nmemb; i++) {
            for (j = i + 1; j < release->nmemb; j++) {
                /* XXX Hattne added 2015-09-08: is this for tracks
                 * without AcoustID matches?  This happened when
                 * release b280f378-37f5-4abd-8041-7fb6e8177d44 was
                 * confused with c195cfcb-0bc9-4e94-8612-8b582b8b7e66
                 * due to lacking fingerprints, and the latter does
                 * not include all the tracks on the former.
                 */
                if (release->streams[i]->nmemb == 0 ||
                    release->streams[j]->nmemb == 0) {
                    continue;
                }

                if (strcmp(
                        release->streams[i]->recordings[re_current[i]]->id,
                        release->streams[j]->recordings[re_current[j]]->id)
                    == 0) {
                    i = release->nmemb;
                    break;
                }
            }
        }
//        printf("hej1 %zd %zd\n", j, release->nmemb);

        if (j == release->nmemb) {
            // Compute distance for current trial, update best score
            // if appropriate.
            float f;
            long int s;


            /* Evaluate score for current.  If length_in_mediumlist2()
             * fails, there are more streams than recordings in the
             * medium list (or a stream is not in the set of
             * recordings of the current set of discs) and the length
             * of the stream will count towards the distance.
             *
             * XXX But what if release->-streams[i] is NULL?  Can
             * that even happen?
             *
             * If there is a tie, then the first seen recording is chosen.
             *
             * If the id is not found in the medium list, the length
             * of the stream will count towards the distance.
             *
             * XXX Any streams without recordings will have to be
             * completed with the set of leftover recordings!
             */
            f = 0;
            s = 0;
            for (i = 0; i < release->nmemb; i++) {
                int l;


                /* XXX Hattne added 2015-09-08: see above, for tracks
                 * without AcoustID matches?
                 */
                if (release->streams[i]->nmemb == 0)
                    continue;

                l = length_in_mediumlist2(
                    ml,
                    release->streams[i]->recordings[re_current[i]]->id,
                    di_current);
                if (l < 0)
                    l = 0;

                f += release->streams[i]->recordings[re_current[i]]->score;
                s += labs(l - (int)fingersum_get_sectors(ctxs[i])); // XXX FISHY CAST!  And fingersum_get_sectors() can fail
            }
//            printf("hej2\n");


            /* Add the number of sectors for all the stuff in the
             * media that is not assigned.
             */
            for (k = 0; k < mb5_medium_list_size(ml); k++) {
                tl = mb5_medium_get_tracklist(mb5_medium_list_item(ml, k));

                for (l = 0; l < mb5_track_list_size(tl); l++) {
                    t = mb5_track_list_item(tl, l);
                    r = mb5_track_get_recording(t);

                    mb5_recording_get_id(r, id, sizeof(id)); // XXX
                    for (i = 0; i < release->nmemb; i++) {
                        if (strcmp(
                                release->streams[i]->recordings[re_current[i]]->id, id) == 0) {
                            break;
                        }
                    }

                    if (i == release->nmemb) {
                        int l;

                        l = length_in_mediumlist2(ml, id, di_current);
                        if (l < 0) // XXX I don't think this will happen?!
                            l = 0;
                        s += l;
                    }
                }
            }
//            printf("hej3\n");


            /* Update the score if better
             */
            if (s < toc_score->distance ||
                (s == toc_score->distance &&
                 f > toc_score->score)) {
                toc_score->score = f;
                toc_score->distance = s;
                for (i = 0; i < release->nmemb; i++)
                    re_best[i] = re_current[i];
            }
        } else {
            ; //printf("        +++ This combo is invalid! +++\n");
        }


        /* Generate next trial in current, break out of the outer loop
         * if all trials exhausted.
         */
//        printf("hej4\n");
        for (i = 0; i < release->nmemb; i++) {
//            printf("hej5 %zd %zd [%zd, %zd]\n",
//                   i, release->nmemb, re_current[i],
//                   release->streams[i]->nmemb);
            if (re_current[i] + 1 < release->streams[i]->nmemb) {
                re_current[i] += 1;
                while (i > 0)
                    re_current[--i] = 0;
                break;
            }
        }
//        printf("hej6 %zd\n", i);
//        sleep(1);
    } while (i < release->nmemb);

//    printf("hej3\n");
//    printf("Ran %zd inner loop\n", inner_loops); // XXX Commented out 2015-11-09


    // XXX Really want to print the discid here as well!
    for (k =0; k < mb5_medium_list_size(ml); k++) {
        Mb5DiscList dl;
        Mb5Disc d;

        dl = mb5_medium_get_disclist(mb5_medium_list_item(ml, k));
        d = mb5_disc_list_item(dl, di_current[k]);
        mb5_disc_get_id(d, id, sizeof(id));
//        printf("  Medium %d, disc ->%s<- [%d]\n", k, id, mb5_disc_list_size(dl));  // XXX Commented out 2015-11-09
    }

//    printf("  Distance %ld, score %f\n", toc_score->distance, toc_score->score);   // XXX Commented out 2015-11-09

    return (0);
}
#endif


#if 0
/* XXX MERGE IN toc_match() and make sure all the diagnostics are
 * properly transferred!
 *
 * XXX THIS IS WAY TOO SLOW FOR Overbombing, and even worse for Dylan
 */
static int
toc_match2(struct fingersum_context **ctxs,
           struct fp3_release *release,
           Mb5MediumList ml,
           struct toc_score *toc_score)
{
    size_t i, k;
//    Mb5Medium m;
    Mb5DiscList dl;
    int j;
    int *di_best, *di_current;
    size_t *re_best, *re_current;

    size_t nmemb;
    struct toc_score ts_current;


    /* The best and the current choice of discid for each medium.  XXX
     * These guys must be freed!
     */
    di_best = (int *)calloc(mb5_medium_list_size(ml), sizeof(int));
    if (di_best == NULL)
        ; // XXX

    di_current = (int *)calloc(mb5_medium_list_size(ml), sizeof(int));
    if (di_current == NULL)
        ; // XXX

    for (j = 0; j < mb5_medium_list_size(ml); j++) {
        di_best[j] = 0;
        di_current[j] = 0;
    }

//    printf("  HATTNE toc_match2() #1\n");


    /* The best and the current choice of recording for each stream.
     * XXX These guys must be freed!
     *
     * The number of streams.  XXX Should possibly take this an
     * argument instead of working it out.  Or better yet: put the
     * sector counts into the fp3_release structure?
     *
     * XXX This int vs size_t business is a tad stupid.  Use int in
     * our structures instead of size_t?
     *
     * For the current selection of discids, the best selection of
     * recordings for each stream?
     */
    nmemb = release->nmemb;
    re_best = (size_t *)calloc(nmemb, sizeof(size_t));
    if (re_best == NULL)
        ; // XXX

    re_current = (size_t *)calloc(nmemb, sizeof(size_t));
    if (re_current == NULL)
        ; // XXX

    for (i = 0; i < nmemb; i++) {
        re_best[i] = 0;
        re_current[i] = 0;
    }

//    printf("  HATTNE toc_match2() #2\n");


    /* Test each combination.
     *
     * XXX Really, should remember all permutations that yield the
     * best result.
     *
     * XXX This is walking through all the discids again.  That's not
     * strictly necessary!  To save some time, could use the
     * previously determined discids instead of resetting them here!
     */
    for (i = 0; i < release->nmemb_media; i++)
        fp3_clear_medium(release->media[i]);

    toc_score->distance = LONG_MAX;
    toc_score->score = 0;
    size_t permutations = 0;
    do {
        permutations += 1;

//        printf("    HATTNE toc_match2() #3.0\n");


        /* Evaluate the score for the current selection of discids.
         * Update the best score and best configuration if the current
         * is better than the best.
         */
        if (toc_match_discids(
                ctxs, release, ml, di_current, re_current, &ts_current) != 0) {
            ; // XXX
        }

//        printf("    HATTNE toc_match2() #3.1\n");
        if (ts_current.distance < toc_score->distance ||
            (ts_current.distance == toc_score->distance &&
             ts_current.score > toc_score->score)) {

//            printf("    HATTNE toc_match2() #3.2\n");

            for (j = 0; j < mb5_medium_list_size(ml); j++)
                di_best[j] = di_current[j];
            for (i = 0; i < nmemb; i++)
                re_best[i] = re_current[i];
            toc_score->distance = ts_current.distance;
            toc_score->score = ts_current.score;

//            printf("    HATTNE toc_match2() #3.3\n");


            /* Reset the discids lists, and add all the discids to the
             * list.
             */
            for (i = 0; i < release->nmemb_media; i++) {
                char id[256];

                dl = mb5_medium_get_disclist(mb5_medium_list_item(ml, i));
                Mb5Disc d = mb5_disc_list_item(dl, di_current[i]);
                mb5_disc_get_id(d, id, sizeof(id));

                fp3_clear_medium(release->media[i]);
                if (fp3_add_discid(release->media[i], id) == NULL)
                    ; // XXX
            }

//            printf("    HATTNE toc_match2() #3.4\n");
        } else if (ts_current.distance == toc_score->distance) {
            /* Add all the discids to their respective lists.
             */
//            printf("    HATTNE toc_match2() #3.5\n");
            for (i = 0; i < release->nmemb_media; i++) {
                char id[256];

                dl = mb5_medium_get_disclist(mb5_medium_list_item(ml, i));
                Mb5Disc d = mb5_disc_list_item(dl, di_current[i]);
                mb5_disc_get_id(d, id, sizeof(id));

                if (fp3_add_discid(release->media[i], id) == NULL)
                    ; // XXX
            }
        }
//        printf("    HATTNE toc_match2() #3.6\n");


        /* Generate the next trial in di_current, break out of the
         * outer loop if all trials exhausted.
         */
        for (j = 0; j < mb5_medium_list_size(ml); j++) {
            dl = mb5_medium_get_disclist(mb5_medium_list_item(ml, j));
            if (di_current[j] + 1 < mb5_disc_list_size(dl)) {
                di_current[j] += 1;
                while (j > 0)
                    di_current[--j] = 0;
                break;
            }
        }
    } while (j < mb5_medium_list_size(ml));
//    printf("Tried %zd permutations\n", permutations);


    /* Eliminate all but the best release for each stream.
     *
     * XXX Am I sure this makes sense?  Could it not be the case that
     * two recordings match equally well?
     */
    for (i = 0; i < release->nmemb; i++) {
        if (release->streams[i]->nmemb == 0) {
            printf("NOW THAT'S WHAT I CALL A STUPID ERROR\n");
            continue; // XXX Correct?
        }

        if (release->streams[i]->nmemb == 1)
            continue;

        for (k = 0; k < release->streams[i]->nmemb; k++) {
            if (k != re_best[i]) {
                fp3_free_recording(release->streams[i]->recordings[k]);
            } else {
                release->streams[i]->recordings[0] =
                    release->streams[i]->recordings[k];
            }
        }
        release->streams[i]->nmemb = 1;
    }

    return (0);
}
#endif


/*********************************
 * MUSICBRAINZ UTILITY FUNCTIONS *
 *********************************/
#if 0 // XXX Not currently used!
/* XXX What if more than one release matches?
 */
static Mb5Recording
_medium_list_get_recording(Mb5MediumList MediumList, const char *id)
{
    Mb5Medium Medium;
    Mb5Recording Recording;
    Mb5Track Track;
    Mb5TrackList TrackList;
    ne_buffer *ID;
    int i, j;


    /* MusicBrainz identifiers are 36 characters, plus one character
     * for NULL-termination.
     */
    ID = ne_buffer_ncreate(36 + 1);

    for (i = 0; i < mb5_medium_list_size(MediumList); i++) {
        Medium = mb5_medium_list_item(MediumList, i);
        if (Medium == NULL)
            continue;

        TrackList = mb5_medium_get_tracklist(Medium);
        if (TrackList == NULL)
            continue;

        for (j = 0; j < mb5_track_list_size(TrackList); j++) {
            Track = mb5_track_list_item(TrackList, j);
            if (Track == NULL)
                continue;

            Recording = mb5_track_get_recording(Track);
            if (Recording == NULL)
                continue;

            ne_buffer_grow(ID, mb5_recording_get_id(Recording, NULL, 0) + 1);
            mb5_recording_get_id(Recording, ID->data, ID->length);
            ne_buffer_altered(ID);

            if (strcmp(ID->data, id) == 0) {
                ne_buffer_destroy(ID);
                return (Recording);
            }
        }
    }

    ne_buffer_destroy(ID);
    return (NULL);
}
#endif


#if 0 // XXX Not currently used!
static Mb5Recording
_release_get_recording(Mb5Release Release, const char *id)
{
    Mb5MediumList MediumList;

    MediumList = mb5_release_get_mediumlist(Release);
    if (MediumList == NULL)
        return (NULL);
    return (_medium_list_get_recording(MediumList, id));
}
#endif


// XXX I think (but I am not sure): before this is run, need to
// complete the indices: streams without matches should be assigned to
// all remaining recordings, which may require.  NO: see comment within!
//
// @return Number of matched streams
static ssize_t
_filter_incomplete(struct fp3_result *response)
{
//    struct fp3_disc *disc;
    struct fp3_fingerprint *fingerprint;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
//    struct fp3_recording_list *track;
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    struct fp3_stream *stream;
    size_t *indices;
    void *p;
    size_t i, j, k, l, m, n, nmemb, o; //, q, nmemb;


    /* Find all stream indices that had at least one matching recording.
     */
    indices = NULL;
    nmemb = 0;
    for (i = 0; i < response->nmemb; i++) {
        releasegroup = response->releasegroups[i];

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

                            for (o = 0; o < recording->nmemb; o++) {
                                fingerprint = recording->fingerprints[o];

                                for (q = 0; q < nmemb; q++) {
                                    if (indices[q] == fingerprint->index)
                                        break;
                                }

                                if (q >= nmemb) {
                                    p = realloc(
                                        indices,
                                        (nmemb + 1) * sizeof(size_t));
                                    if (p == NULL) {
                                        if (indices != NULL)
                                            free(indices);
                                        return (-1);
                                    }
                                    indices = p;
                                    indices[nmemb++] = fingerprint->index;
                                }
                            }
                        }
                    }
                }
#else
                for (l = 0; l < medium->nmemb_tracks; l++) {
                    recording = medium->tracks[l];
//                    if (recording == NULL)
//                        continue;

                    for (m = 0; m < recording->nmemb; m++) {
                        fingerprint = recording->fingerprints[m];

                        for (n = 0; n < fingerprint->nmemb; n++) {
                            stream = fingerprint->streams[n];

                            for (o = 0; o < nmemb; o++) {
                                if (indices[o] == stream->index)
                                    break;
                            }

                            if (o >= nmemb) {
                                p = realloc(
                                    indices, (nmemb + 1) * sizeof(size_t));
                                if (p == NULL) {
                                    if (indices != NULL)
                                        free(indices);
                                    return (-1);
                                }
                                indices = p;
                                indices[nmemb++] = stream->index;
                            }
                        }
                    }
                }
#endif
            }
        }
    }


    /* Remove a release if it does not contain all matched streams.
     * Remove a releasegroup if all its releases have been removed.
     */
    for (i = 0; i < response->nmemb; i++) {
        releasegroup = response->releasegroups[i];

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];
            for (k = 0; k < nmemb; k++) {
                if (fp3_release_find_recording_by_index(
                        release, indices[k]) == NULL) {
                    fp3_erase_release(releasegroup, j);
                    j--;
                    break;
                }
            }
        }

        if (releasegroup->nmemb == 0) {
            fp3_erase_releasegroup(response, i);
            i--;
        }
    }

#if 1
    if (indices != NULL)
        free(indices);
    return (nmemb);
#else // XXX Need to do this when we actually know what tracks to
      // expect on a medium.

    size_t *indices_unmatched;
    size_t nmemb_unmatched;


    /* Create a list of all unmatched indices
     */
    indices_unmatched = NULL;
    nmemb_unmatched = 0;
    for (i = 0; i <= index_max; i++) {
        for (j = 0; j < nmemb_matched; j++) {
            if (indices_matched[j] == i)
                break;
        }

        if (j == nmemb_matched) {
            p = realloc(
                indices_unmatched,
                (nmemb_unmatched + 1) * sizeof (size_t));
            if (p == NULL) {
                if (indices_matched != NULL)
                    free(indices_matched);
                if (indices_unmatched != NULL)
                    free(indices_unmatched);
                return (-1);
            }
            indices_unmatched = p;
            indices_unmatched[nmemb_unmatched++] = i;
        }
    }

    if (indices_matched != NULL)
        free(indices_matched);


    /* Add dummy fingerprints of all unmatched streams to any
     * recording that did not match a stream.
     */
    struct fp3_recording *recording, *recording_dummy;

    recording_dummy = fp3_recording_new();
    if (recording_dummy == NULL)
        ; // XXX

    struct fp3_fingerprint *fingerprint = fp3_fingerprint_new();
    if (recording == NULL)
        ; // XXX

    for (i = 0; i < response->nmemb; i++) {
        releasegroup = response->releasegroups[i];

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];

            for (k = 0; k <= index_max; k++) {

                for (l = 0; l < release->nmemb_media; l++) {
                    medium = release->media[l];
                    if (release->medium->tracks[l]->position == k)

            for (k = 0; k <= index_max; k++) {
                recording = fp3_release_find_recording_by_index(release, k);
                if (recording == NULL)
                    fp3_release_add_recording(

                if (recording != NULL && recording->nmemb > 0)
                    continue;
                    for (l = 0; l < nmemb_unmatched; l++) {
                        fingerprint->index = l;
                        if (fp3_recording_add_fingerprint(recording, fingerprint) == NULL)
                            ; // XXX
                        fp3_clear_fingerprint(fingerprint);
                    }

                fp3

    if (indices_unmatched != NULL)
        free(indices_unmatched);
#endif
}


#if 0 // XXX Currently not used
/* Erase all the recordings from @p release that are nowhere found in
 * @p MediumList.  This "synchronises" @p release with @p MediumList,
 * in case AcoustID gave recordings that are not present in the
 * MusicBrainz database.
 *
 * XXX This is a common pattern in the code.  Make sure it's
 * synchronised?
 */
static void
_prune_release(struct fp3_release *release, Mb5MediumList MediumList)
{
    struct fp3_recording *recording;
    struct fp3_recording_list *recordings;
    size_t i, j;


    for (i = 0; i < release->nmemb; i++) {
        recordings = release->streams[i];
        if (recordings == NULL)
            continue;

        for (j = 0; j < recordings->nmemb; ) {
            recording = recordings->recordings[j];
            if (_medium_list_get_recording(MediumList, recording->id) == NULL)
                fp3_erase_recording(recordings, j);
            else
                j++;
        }
    }
}
#endif


/* Iterate through the Disc's OffsetList because it may
 * (theoretically) be out of order.
 *
 * @param Position One-based position
 * @return XXXXXXX The length of the track at position @p Position on
 *                 disc @p Disc, in sectors
 */
static int
_disc_get_sectors_item(Mb5Disc Disc, int Position)
{
    Mb5Offset Offset;
    Mb5OffsetList OffsetList;
    int i, p, sector_beg, sector_end;


    OffsetList = mb5_disc_get_offsetlist(Disc);
    if (OffsetList == NULL)
        return (-1);

    sector_beg = -1;
    if (mb5_offset_list_size(OffsetList) == Position)
        sector_end = mb5_disc_get_sectors(Disc);
    else
        sector_end = -1;

    for (i = 0; i < mb5_offset_list_size(OffsetList); i++) {
        Offset = mb5_offset_list_item(OffsetList, i);
        if (Offset == NULL)
            return (-1);

        p = mb5_offset_get_position(Offset);
        if (p == Position)
            sector_beg = mb5_offset_get_offset(Offset);
        else if (p == Position + 1)
            sector_end = mb5_offset_get_offset(Offset);

        if (sector_beg >= 0 && sector_end >= 0)
            return (sector_end - sector_beg);
    }

    return (-1);
}


/* This finds the recording by position instead of by MusicBrainz ID,
 * which is more robust in case AcoustID and MusicBrainz are
 * temporarily out of sync.
 */
static struct fp3_recording *
_medium_recording_at_position(struct fp3_medium *medium, size_t position)
{
    struct fp3_recording *recording;
    size_t i;


    for (i = 0; i < medium->nmemb_tracks; i++) {
        recording = medium->tracks[i];
        if (recording->position == position)
            return (recording);
    }

    return (NULL);
}


/* This does not handle the (erroneous) case where there is more than
 * one medium at the given position.
 */
static struct fp3_recording *
_release_recording_at_position(
    struct fp3_release *release, size_t medium_position, size_t recording_position)
{
    struct fp3_medium *medium;
    size_t i;


    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];
        if (medium->position == medium_position)
            return (_medium_recording_at_position(medium, recording_position));
    }

    return (NULL);
}


/* Check the offset-finding crc of all streams associated with @p
 * track against the AccurateRip response.  Add the offsets from the
 * matching entry to the disc.
 */
static int
_disc_add_chunk(struct fp3_disc *disc,
                struct fp3_track *track,
                const struct _chunk *chunk,
                struct fingersum_context **ctxs)
{
    struct fp3_offset_list *offset_list;
    size_t i;


    for (i = 0; i < track->nmemb; i++) {
        offset_list = fingersum_find_offset(
            ctxs[track->indices[i]], chunk->unk);
        if (offset_list != NULL) {
            if (fp3_disc_add_offset_list(disc, offset_list) == NULL) {
                fp3_free_offset_list(offset_list);
                return (-1);
            }
            fp3_free_offset_list(offset_list);
        }
    }

    return (0);
}


// XXX Populate this once we can use EAC to determine offsets
static int
_disc_add_eac_track(struct fp3_disc *disc,
                    struct fp3_track *track,
                    struct _track_eac *track_eac,
                    struct fingersum_context **ctxs)
{
    struct fp3_offset_list *offset_list;
    struct _block_eac *block;
    size_t i, j;


    /* XXX ADDITION: EAC offset--keep these separate?
     */
    for (i = 0; i < track->nmemb; i++) {
        for (j = 0; j < track_eac->n_blocks_part; j++) {
            block = track_eac->blocks_part + j;
            offset_list = fingersum_find_offset_eac(
                ctxs[track->indices[i]], block->crc32);

            if (offset_list != NULL) {
                if (fp3_disc_add_offset_list(disc, offset_list) == NULL) {
                    fp3_free_offset_list(offset_list);
                    return (-1);
                }
                fp3_free_offset_list(offset_list);
            }
        }
    }

    return (0);
}


/* Find the track corresponding to the j:th chunk.  It should not be
 * an error if the track cannot be found, because we can tolerate gaps
 * in the disc at this stage.
 */
static int
_disc_add_response(struct fp3_disc *disc,
                   const struct _cache *response,
                   struct fingersum_context **ctxs)
{
    const struct _chunk *chunk;
    const struct _entry *entry;
    struct fp3_track *track;
    size_t i, j, k;


    for (i = 0; i < response->nmemb; i++) {
        entry = response->entries + i;

        for (j = 0; j < entry->track_count; j++) {
            chunk = entry->chunks + j;

            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];
                if (track->position == j + 1) {
                    if (_disc_add_chunk(disc, track, chunk, ctxs) != 0)
                        return (-1);
                    break;
                }
            }
        }
    }


    /* NEW STUFF: transport the EAC response -- XXX but this is just
     * to determine what offsets to use?
     */
    struct _track_eac *track_eac;
    if (response->entry_eac != NULL) {
        for (j = 0; j < response->entry_eac->n_tracks; j++) {
            track_eac = response->entry_eac->tracks + j;

/*
            struct _block_eac *block_eac;
            for (k = 0; k < track_eac->n_blocks_whole; k++) {
                block_eac = track_eac->blocks_whole + k;
                printf("TRACK: %zd [%p] block whole: %zd/%zd CRC32 0x%08x\n",
                       j, track_eac, k, track_eac->n_blocks_part, block_eac->crc32);
            }
            for (k = 0; k < track_eac->n_blocks_part; k++) {
                block_eac = track_eac->blocks_part + k;
                printf("TRACK: %zd [%p] block part: %zd/%zd CRC32 0x%08x\n",
                       j, track_eac, k, track_eac->n_blocks_part, block_eac->crc32);
            }
*/

            printf("Disc has %zd tracks\n", disc->nmemb);
            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];
                if (track->position == j + 0) { // XXX Note the +0!
                    if (_disc_add_eac_track(disc, track, track_eac, ctxs) != 0)
                        return (-1);
                    break;
                }
            }
        }
    }

    return (0);
}


/* @p id ID of the recording to exclude
 */
static int
_index_occurs_elsewhere(
    struct fp3_release *release, const char *id, size_t index)
{
    struct fp3_fingerprint *fingerprint;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
    struct fp3_stream *stream;
    size_t i, j, k, l;


    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

        for (j = 0; j < medium->nmemb_tracks; j++) {
            /* XXX Why is the recording->id == NULL necessary for The
             * Doors?  It used to have one missing stream, because
             * another was duplicated.
             */
            recording = medium->tracks[j];
            if (recording->id == NULL || strcmp(recording->id, id) == 0)
                continue;

            for (k = 0; k < recording->nmemb; k++) {
                fingerprint = recording->fingerprints[k];

                for (l = 0; l < fingerprint->nmemb; l++) {
                    stream = fingerprint->streams[l];

                    if (stream->index == index)
                        return (1);
                }
            }
        }
    }

    return (0);
}


/* Add track to disc.  Always add if sector-length matches.  Should
 * perhaps have been _track_add_medium or so.  XXX The return value
 * does not distinguish between errors (e.g. out of memory) and
 * failure to add track (e.g. sector length mismatch).  After this
 * step, only streams with matching sector lengths will be present on
 * the disc; matching sector length is a prerequisite for AccurateRip
 * verification.
 *
 * @param sectors Sector length of the track of the disc
 */
static int
_disc_add_tracks(struct fp3_disc *disc,
                 struct fingersum_context **ctxs,
                 unsigned int sectors, // XXX long int?
                 struct fp3_recording *recording,
                 struct fp3_release *release)
{
    struct fp3_fingerprint *fingerprint;
    struct fp3_stream *stream;
    struct fp3_track *track;
    size_t i, j;


    track = fp3_new_track();
    if (track == NULL)
        return (-1);

    for (i = 0; i < recording->nmemb; i++) {
        fingerprint = recording->fingerprints[i];

        for (j = 0; j < fingerprint->nmemb; j++) {
            stream = fingerprint->streams[j];

//            printf("  Checking %zd/%zd index %zd sectors: %d\n",
//                   i, recording->nmemb, stream->index, sectors);


            // XXX fingersum_get_sectors() can fail!
            if (fingersum_get_sectors(ctxs[stream->index]) == sectors) {
                /* Keep the stream if its sector length equals the
                 * sector length of the track under consideration.
                 */
//                printf("    Adding index %zd\n", stream->index);
                if (fp3_track_add_index(track, stream->index) != 0) {
                    fp3_free_track(track);
                    return (-1);
                }
            } else if (recording->id != NULL) {
                /* Fail if this is the only occurrence of the stream
                 * on the release.
                 *
                 * recording->id appears to be NULL for Summer hits,
                 * where some recordings are not matched by
                 * fingerprints.  But how can that lead to this
                 * symptom?
                 */
                if (_index_occurs_elsewhere(
                        release, recording->id, stream->index) == 0) {
                    fp3_free_track(track);
                    return (-1);
                }
            }
        }
    }

    if (track->nmemb > 0) {
        track->position = recording->position;

        if (fp3_disc_add_track(disc, track) == NULL) {
            fp3_free_track(track);
            return (-1);
        }
    } else {
        // XXX Think about this!
        printf("Skipping because nmemb == 0, %zd\n", recording->nmemb);
    }
    fp3_free_track(track);

    return (0);
}


/* ADD DISCS TO MEDIUM but only if they have zero distance to the
 * streams.
 */
static int
_medium_add_discs(struct fp3_medium *medium,
                  struct fingersum_context **ctxs,
                  size_t nmemb,
                  struct accuraterip_context *ar_ctx,
                  Mb5Medium Medium,
                  struct fp3_release *release)
{
    Mb5Disc Disc;
    Mb5DiscList DiscList;
    Mb5Track Track;
    Mb5TrackList TrackList;

    ne_buffer *ID;

    const struct _cache *response;

    struct fp3_disc *disc;
    struct fp3_recording *recording;

    int i, j, position, sectors;


    DiscList = mb5_medium_get_disclist(Medium);
    if (DiscList == NULL)
        return (0);

    TrackList = mb5_medium_get_tracklist(Medium);
    if (TrackList == NULL)
        return (0);

    disc = fp3_new_disc();
    if (disc == NULL)
        return (-1);

    // XXX Should have been length of discid
    ID = ne_buffer_ncreate(36 + 1);


    /* Loop over all possible discs for the MusicBrainz medium.
     */
    for (i = 0; i < mb5_disc_list_size(DiscList); i++) {
        Disc = mb5_disc_list_item(DiscList, i);
        if (Disc == NULL)
            continue;


        /* Compare the sector length of each track on @p Disc against
         * the streams under consideration for the corresponding
         * position on @p medium.  Matching streams are transported to
         * the disc.  The sector length of a stream must equal the
         * sector length of the track, unless it is also considered at
         * other positions on the release.  If this is not the case,
         * @p Disc cannot be valid.
         */
        fp3_clear_disc(disc);
        for (j = 0; j < mb5_track_list_size(TrackList); j++) {
            Track = mb5_track_list_item(TrackList, j);
            if (Track == NULL)
                continue;

            position = mb5_track_get_position(Track);
            recording = _medium_recording_at_position(medium, position);
            if (recording == NULL)
                continue;

            sectors = _disc_get_sectors_item(Disc, position);
            if (sectors < 0)
                continue;

            if (_disc_add_tracks(
                    disc, ctxs, sectors, recording, release) != 0) {
                Disc = NULL;
                break;
            }
        }


        /* If the disc does not match, or does not contain any tracks
         * consider the next one.  Otherwise, assign discid.
         */
        if (Disc == NULL || disc->nmemb == 0)
            continue;

        ne_buffer_grow(ID, mb5_disc_get_id(Disc, NULL, 0) + 1);
        mb5_disc_get_id(Disc, ID->data, ID->length);
        ne_buffer_altered(ID);

        disc->id = strdup(ID->data);
        if (disc->id == NULL) {
            ne_buffer_destroy(ID);
            fp3_free_disc(disc);
            return (-1);
        }


        /* If there is an entry for the disc in the AccurateRip
         * database, determine the offset for all tracks with matching
         * sector lengths.  Not all offset calculations need to
         * succeed; such will be flagged as bad rips later on.  The
         * offsets for the disc are calculated as the union of offsets
         * for each of its constituent tracks.  If the union is not
         * empty, proper AccurateRip verification will be performed
         * later on.  If there are gaps, i.e. the previous and/or next
         * track is missing, subsequent AccurateRip verification may
         * fail for non-zero offsets.
         *
         * There may not be a response [e.g. Summer Party], and this
         * is OK.
         *
         * XXX What about folding accurate_get() and
         * accurate_eac_get() into the same function?  Right now,
         * there may be funny stuff happening in accuraterip.c where
         * identical metadata is potentially duplicated.
         */
#if 0
        response = accuraterip_get(ar_ctx, Disc);
#else
        response = accuraterip_localhost_get(ar_ctx, Disc);
#endif
        if (response != NULL) {
            if (_disc_add_response(disc, response, ctxs) != 0) {
                ne_buffer_destroy(ID);
                fp3_free_disc(disc);
                return (-1);
            }

            if (disc->offset_list != NULL && disc->offset_list->nmemb > 0) {
                // XXX remember the AR discid (response->path)
            }
        }

        response = accuraterip_eac_get(ar_ctx, Disc);
        if (response != NULL) {
            if (_disc_add_response(disc, response, ctxs) != 0) {
                ne_buffer_destroy(ID);
                fp3_free_disc(disc);
                return (-1);
            }

            if (disc->offset_list != NULL && disc->offset_list->nmemb > 0) {
                // XXX What's this good for here?
            }
        }

        if (fp3_medium_add_disc(medium, disc) == NULL) {
            ne_buffer_destroy(ID);
            fp3_free_disc(disc);
            return (-1);
        }
    }
    ne_buffer_destroy(ID);
    fp3_free_disc(disc);

    return (0);
}


/* Add matching discs to the release.  It is not checked that there is
 * at most one MB medium at each position.  Should these loops perhaps
 * be reversed?  Check with above function before flipping.
 *
 * Return value: the number of discs added?  The smallest number of
 * discs added for any medium?
 */
static int
_release_add_discs(struct fp3_release *release,
                   struct fingersum_context **ctxs,
                   size_t nmemb,
                   struct accuraterip_context *ar_ctx,
                   Mb5Release Release)
{
    Mb5Medium Medium;
    Mb5MediumList MediumList;
    struct fp3_medium *medium;
    size_t j;
    int i, position;


    MediumList = mb5_release_get_mediumlist(Release);
    if (MediumList == NULL)
        return (-1);

    for (i = 0; i < mb5_medium_list_size(MediumList); i++) {
        Medium = mb5_medium_list_item(MediumList, i);
        if (Medium == NULL)
            return (-1);

        position = mb5_medium_get_position(Medium);
        for (j = 0; j < release->nmemb_media; j++) {
            medium = release->media[j];
            if (medium == NULL)
                return (-1);

            if (medium->position == position) {

                if (_medium_add_discs(
                        medium, ctxs, nmemb, ar_ctx, Medium, release) != 0) {
                    return (-1);
                }
                break;
            }
        }
    }

    return (0);
}


static int
_complete_medium(struct fp3_medium *medium,
                 Mb5Medium Medium,
                 struct fp3_fingerprint *indices)
{
    Mb5Recording Recording;
    Mb5Track Track;
    Mb5TrackList TrackList;
    struct fp3_recording *recording;
    size_t j;
    int i, position;


    TrackList = mb5_medium_get_tracklist(Medium);
    if (TrackList != NULL) {
        for (i = 0; i < mb5_track_list_size(TrackList); i++) {
            Track = mb5_track_list_item(TrackList, i);
            if (Track == NULL)
                continue;

            Recording = mb5_track_get_recording(Track);
            if (Recording == NULL)
                continue;

            position = mb5_track_get_position(Track);
            for (j = 0; j < medium->nmemb_tracks; j++) {
                recording = medium->tracks[j];
                if (recording->position == position) {
                    if (recording->nmemb == 0) {
                        /* Add fingerprints to any recording that does
                         * not have any.
                         */
/*
                        for (k = 0; k < indices->nmemb; k++) {
                            if (fp3_recording_add_fingerprint(
                                    recording,
                                    indices->fingerprints[k]) == NULL) {
                                return (-1);
                            }
                        }
*/
                        if (fp3_recording_add_fingerprint(
                                recording, indices) == NULL) {
                            return (-1);
                        }
                    }
                    break;
                }
            }

            if (j == medium->nmemb_tracks) {
                /* Add a new recording with the indices of all
                 * unmatched streams (i.e. streams not matched
                 * elsewhere for the release).
                 *
                 * XXX Need to call _complete_track with a new
                 * fp3_recording?  The recording should not
                 * necessarily be empty, but contain all streams not
                 * matched elsewhere for the release.
                 */
                recording = fp3_new_recording();
                if (recording == NULL)
                    return (-1);

                if (fp3_recording_add_fingerprint(recording, indices) == NULL) {
                    fp3_free_recording(recording);
                    return (-1);
                }

                recording->position = position;
                if (fp3_medium_add_recording(medium, recording) == NULL) {
                    fp3_free_recording(recording);
                    return (-1);
                }
                fp3_free_recording(recording);
            }
        }
    }

    return (0);
}


/* The _complete_release() function does release completion.
 * Recordings present in release but not in MediumList are erased.
 * Then the putative list of recordings for any stream without any
 * assigned recordings is set to the list of unassigned recordings
 * from MediumList.
 *
 * XXX This function should have been something like add_disc().  The
 * complete_release_real() function does what is advertised here!
 *
 * @note This will break if AcoustID and MusicBrainz are out of sync.
 *       If AcoustID returns a set of recordings that are subsequently
 *       not all matched by MusicBrainz XXX sentence incomplete.  XXX
 *       TEST WITH DYLAN, MUSIC FOR THE MASSES, APPETITE FOR
 *       DESTRUCTION, BLOOD SUGAR SEX MAGIK, BEACH BOYS, BUGGLES on old
 *       database.  Cannot reproduce 2015-02-24.
 *
 * @param release    The release structure from AcoustID
 * @param MediumList The corresponding MediumList from MusicBrainz
 * @return           Zero if successful, non-zero otherwise.  If an
 *                   error occurs the global variable @c errno is set
 *                   to indicate the error.
 *
 * @param nmemb Largest index, the indices must be contiguous between
 * [0, nmemb] XXX Could maybe pass a nmemb-long list of expected
 * indices.
 *
 * XXX Maybe this function should complete the list of indices for the
 * disc instead?  In that case, it could probably be merged with the
 * _disc_add() function.
 */
static int
_complete_release(struct fp3_release *release,
                  Mb5Release Release,
                  size_t nmemb)

{
    Mb5Medium Medium;
    Mb5MediumList MediumList;

    struct fp3_fingerprint *indices;
//    struct fp3_recording *indices;
    struct fp3_medium *medium;
    struct fp3_recording *track;
    struct fp3_fingerprint *fingerprint;
    struct fp3_stream *stream;

    size_t i, j, k, l, m;
    int n, position;


    /* Find unused indices.  Populate a fingerprint with all indices,
     * then remove the ones found in the release.
     *
     * Maybe this is a function of its own?  It
     * could return all this in a recording, which is basically a list
     * of fingerprints.  If there are no unused indices, there is
     * nothing to do.
     *
     * Old comment: Accumulate all recordings in MediumList that are
     * not present in release (i.e. all recordings unique to
     * MediumList).
     */
    indices = fp3_new_fingerprint();
    if (indices == NULL)
        return (-1);

    stream = fp3_new_stream();
    if (stream == NULL) {
        fp3_free_fingerprint(indices);
        return (-1);
    }

    for (i = 0; i < nmemb; i++) {
        stream->index = i;
        if (fp3_fingerprint_add_stream(indices, stream) == NULL) {
            fp3_stream_free(stream);
            fp3_free_fingerprint(indices);
            return (-1);
        }
    }
    fp3_stream_free(stream);

    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

        for (j = 0; j < medium->nmemb_tracks; j++) {
            track = medium->tracks[j];

            for (k = 0; k < track->nmemb; k++) {
                fingerprint = track->fingerprints[k];

                for (l = 0; l < fingerprint->nmemb; l++) {
                    stream = fingerprint->streams[l];

                    for (m = 0; m < indices->nmemb; m++) {
                        if (stream->index == indices->streams[m]->index) {
                            fp3_fingerprint_erase_stream(indices, m);
                            break;
                        }
                    }
                }
            }
        }
    }

//    printf("*** DUMP OF UNMATCHED STREAMS ***\n");
//    fp3_recording_dump(indices, 2, 0);
//    printf("*** DUMP OF UNMATCHED STREAMS ***\n");

    if (indices->nmemb == 0) {
        fp3_free_fingerprint(indices);
        return (0);
    }


    /* Accumulate all recordings in MediumList that are not present in
     * release (i.e. all recordings unique to MediumList).
     */
    MediumList = mb5_release_get_mediumlist(Release);
    if (MediumList != NULL) {
        for (n = 0; n < mb5_medium_list_size(MediumList); n++) {
            Medium = mb5_medium_list_item(MediumList, n);
            if (Medium == NULL)
                continue;

            position = mb5_medium_get_position(Medium);
            for (i = 0; i < release->nmemb_media; i++) {
                if (release->media[i]->position == position) {
                    if (_complete_medium(
                            release->media[i], Medium, indices) != 0) {
                        fp3_free_fingerprint(indices);
                        return (-1);
                    }
                    break;
                }
            }

            if (i == release->nmemb_media) {
                /* XXX Need to call _complete_medium() with a new
                 * (empty) fp3_medium!
                 */
                medium = fp3_release_add_medium(release, NULL);
                if (medium == NULL) {
                    fp3_free_fingerprint(indices);
                    return (-1);
                }

                medium->position = position;
                if (_complete_medium(
                        medium, Medium, indices) != 0) {
                    fp3_free_fingerprint(indices);
                    return (-1);
                }
            }
        }
    }

    fp3_free_fingerprint(indices);
    return (0);
}


/* The offsets needed for each stream is the union of the offsets
 * for each disk on which the stream is featured.
 *
 * XXX Maybe this function should take an nmemb argument to guard
 * against overruns?
 */
static int
_add_streams_offset_2(struct fingersum_context **ctxs,
                      struct fp3_release *release)
{
    const struct fp3_disc* disc;
    const struct fp3_medium* medium;
    const struct fp3_offset_list* offset_list;
    const struct fp3_track* track;
    size_t index, i, j, k, l, m;
    ssize_t offset;


    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

        for (j = 0; j < medium->nmemb_discs; j++) {
            disc = medium->discs[j];

            offset_list = disc->offset_list;
            if (offset_list == NULL) // May not be present if no disk matches
                continue;

            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];

                for (l = 0; l < track->nmemb; l++) {
                    index = track->indices[l];

                    for (m = 0; m < offset_list->nmemb; m++) {
                        offset = offset_list->offsets[m];
                        if (fingersum_add_offset(ctxs[index], offset) != 0)
                            return (-1); // XXX
                    }
                }
            }
        }
    }

    return (0);
}


#if 0
static int
_add_streams_offset(struct fingersum_context **ctxs,
                    struct fp3_result *result)
{
    const struct fp3_releasegroup* releasegroup;
    size_t i, j;


    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];

        for (j = 0; j < releasegroup->nmemb; j++) {
            if (_add_streams_offset_2(ctxs, releasegroup->releases[j]) != 0)
                return (-1); // XXX
        }
    }

    return (0);
}
#endif


/* Returns non-zero if any of the discs for the release has a matching
 * checksum for stream @p index.
 */
static int
_release_has_matching_index(struct fp3_release *release, size_t index)
{
    struct fp3_disc *disc;
    struct fp3_track *track;
    struct fp3_medium *medium;
    size_t i, j, k, l;


    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];
        if (medium == NULL)
            continue;

        for (j = 0; j < medium->nmemb_discs; j++) {
            disc = medium->discs[j];
            if (disc == NULL)
                continue;

            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];
                if (track == NULL)
                    continue;

                for (l = 0; l < track->nmemb; l++) {
                    if (track->indices[l] == index)
                        return (1);
                }
            }
        }
    }

//    fp3_medium_dump(medium, 2, 0);
    printf("    FAILED to find stream index %zd\n", index);

    return (0);
}


/* Returns non-zero if all streams indices appear at least once in any
 * of the discs.  XXX No point in going here if the release has no
 * discs, because _release_has_matching_index() will fail... should
 * probably warn about that beforehand?
 */
static int
_release_has_matching_discs(struct fp3_release *release)
{
    struct fp3_fingerprint *fingerprint;
    struct fp3_medium *medium;
    struct fp3_recording *recording;
    struct fp3_stream *stream;
    size_t i, j, k, l;


    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

        for (j = 0; j < medium->nmemb_tracks; j++) {
            recording = medium->tracks[j];

            for (k = 0; k < recording->nmemb; k++) {
                fingerprint = recording->fingerprints[k];

                for (l = 0; l < fingerprint->nmemb; l++) {
                    stream = fingerprint->streams[l];

                    if (_release_has_matching_index(
                            release, stream->index) == 0) {
                        return (0);
                    }
                }
            }
        }
    }

    return (1);
}


#if 0 // XXX Currently not used
/* Allocate and initialise space for the media.
 *
 * XXX Candidate for the structures module?
 */
static int
_grow_release_media(struct fp3_release *release, Mb5MediumList MediumList)
{
    size_t i;

    release->nmemb_media = mb5_medium_list_size(MediumList);
    release->media = (struct fp3_medium **)calloc(
        release->nmemb_media, sizeof(struct fp3_medium *));
    if (release->media == NULL)
        return (-1);

    for (i = 0; i < release->nmemb_media; i++) {
        release->media[i] = fp3_new_medium();
        if (release->media[i] == NULL)
            return (-1);
    }

    return (0);
}
#endif


#if 0 // XXX Currently not used
/* Side effects: there will be no streams without any recordings [if
 * there are, that may be an error, because that would indicate that
 * there are extraneous streams.
 *
 * XXX FIX UP THIS FUNCTION: Just return the bound (not the score).
 * And merge all the weird implementations above!
 */
static int
_toc_lower_bound(struct fingersum_context **ctxs,
                struct fp3_release *release,
                Mb5MediumList MediumList,
                struct toc_score *toc_score)
{
    struct toc_score best_score, disc_score;
    Mb5Disc Disc;
    Mb5DiscList DiscList;
    Mb5Medium Medium;
    Mb5OffsetList OffsetList;
    Mb5Recording Recording;
    Mb5Track Track;
    Mb5TrackList TrackList;
    Mb5Offset Offset; //, Offset_next;

    struct fp3_recording *recording_my;
    struct fp3_recording_list *recordings_my;
    ne_buffer *ID;
    size_t i, l;
    long int d, s;
    unsigned int sectors; // XXX is this really the correct type?
    int j, k, m, n, Position;


    /* Allocate and initialise space for the media.
     *
     * XXX Really need to do that here and now?
     *
     * MusicBrainz identifiers are 36 characters, plus one character
     * for NULL-termination.
     *
     * XXX duplication w.r.t. above
     */
    if (_grow_release_media(release, MediumList) != 0)
        return (-1);
    ID = ne_buffer_ncreate(36 + 1);


    /* THIS IS THE STUFF FOR THE ACTUAL LOWER BOUND
     *
     * XXX Really, should remember all permutations (all the discids)
     * that yield the best result.  Hopefully it does, now.
     *
     * Only handle CD (not DVD, etc)
     */
    toc_score->distance = 0;
    toc_score->score = 0;

    for (j = 0; j < mb5_medium_list_size(MediumList); j++) {
        Medium = mb5_medium_list_item(MediumList, j);
        if (Medium == NULL)
            continue; // XXX Correct?

        ne_buffer_grow(ID, mb5_medium_get_format(Medium, NULL, 0) + 1);
        mb5_medium_get_format(Medium, ID->data, ID->length);
        ne_buffer_altered(ID);
        if (strcmp(ID->data, "CD") != 0)
            continue; // XXX Correct

        DiscList = mb5_medium_get_disclist(Medium);
        if (DiscList == NULL)
            continue; // XXX Correct?

        TrackList = mb5_medium_get_tracklist(Medium);
        if (TrackList == NULL)
            continue; // XXX Correct?

        best_score.distance = LONG_MAX;
        best_score.score = 0;


        /* Test all the discs for this particular medium, keep only
         * the best matching disc.
         */
        for (k = 0; k < mb5_disc_list_size(DiscList); k++) {
            //printf("Checking disc %d of %d\n", k, mb5_disc_list_size(DiscList));

            Disc = mb5_disc_list_item(DiscList, k);
            if (Disc == NULL)
                continue; // XXX Correct?

            OffsetList = mb5_disc_get_offsetlist(Disc);
            if (OffsetList == NULL)
                continue; // XXX Correct?

            disc_score.distance = 0;
            disc_score.score = 0;

            for (m = 0; m < mb5_offset_list_size(OffsetList); m++) {
                Offset = mb5_offset_list_item(OffsetList, m);
                if (Offset == NULL)
                    continue; // XXX Correct?

#if 0
                if (m + 1 < mb5_offset_list_size(OffsetList)) {
                    Offset_next = mb5_offset_list_item(OffsetList, m + 1);
                    if (Offset_next == NULL)
                        continue; // XXX Correct?
                    s = mb5_offset_get_offset(Offset_next) -
                        mb5_offset_get_offset(Offset);
                } else {
                    s = mb5_disc_get_sectors(Disc)-
                        mb5_offset_get_offset(Offset);
                }
#else
                s = _disc_get_sectors_item(
                    Disc, mb5_offset_get_position(Offset));
                if (s < 0) {
                    printf("ERROR ON ELM STREET\n");
                    exit (-1);
                }
#endif


                /* Find the corresponding track, extract the
                 * recording.
                 */
                Position = mb5_offset_get_position(Offset);
                Recording = NULL;
                for (n = 0; n < mb5_track_list_size(TrackList); n++) {
                    Track = mb5_track_list_item(TrackList, n);
                    if (Track != NULL && mb5_track_get_position(
                            Track) == Position) {
                        Recording = mb5_track_get_recording(Track);
                        break;
                    }
                }

                if (Recording == NULL) {
                    ne_buffer_destroy(ID);
                    return (-1); // XXX Correct?
                }

                ne_buffer_grow(ID, mb5_recording_get_id(Recording, NULL, 0) + 1);
                mb5_recording_get_id(Recording, ID->data, ID->length);
                ne_buffer_altered(ID);


                /* Find the recording in the release (i.e. among the
                 * streams).
                 *
                 * XXX fingersum_get_sectors() could fail, as could
                 * the lrint() stuff from MusicBrainz!
                 *
                 * Find the best matching stream: set distance to s
                 * and score to zero if none found.
                 */
                long int d_min = s;
                float s_max = 0;
                for (i = 0; i < release->nmemb;i++) {
                    recordings_my = release->streams[i];
                    sectors = fingersum_get_sectors(ctxs[i]);
                    if (sectors < 0)
                        ; // XXX


                    /* XXX Why is this necessary again?  Is it because
                     * a stream may not have been matched?  In that
                     * case, this should not happen here (because of
                     * the above)!
                     *
                     * Unassigned streams should have been taken care
                     * of at this point!
                     */
                    if (recordings_my == NULL) {
                        printf("    ^^^^ THIS SHOULD NOT HAPPEN ^^^^\n");

                        printf("HAVE %zd, %zd, %zd\n",
                               release->nmemb, release->capacity, i);

                        exit (0);
                        continue;
                    }

                    for (l = 0; l < recordings_my->nmemb; l++) {
                        recording_my = recordings_my->recordings[l];

                        if (strcmp(ID->data, recording_my->id) != 0)
                            continue;

                        d = labs(s - sectors);
                        if (d < d_min || (d == d_min && recording_my->score > s_max)) {
                            d_min = d;
                            s_max = recording_my->score;
                        }
                    }
                }
                disc_score.distance += d_min;
                disc_score.score += s_max;
            }
//            printf("Got disc scores %ld %f\n",
//                   disc_score.distance, disc_score.score);

//            mb5_disc_get_id(mb5_disc_list_item(DiscList, k), ID, sizeof(ID));
            ne_buffer_grow(ID, mb5_disc_get_id(Recording, NULL, 0) + 1);
            mb5_disc_get_id(Recording, ID->data, ID->length);
            ne_buffer_altered(ID);

            if (disc_score.distance < best_score.distance ||
                (disc_score.distance == best_score.distance &&
                 disc_score.score > best_score.score)) {
                best_score.distance = disc_score.distance;
                best_score.score = disc_score.score;


                /* Delete all previous discids from medium, and add
                 * discid to medium.  If distance is equal, add it as
                 * an alternative.
                 */
                fp3_clear_medium(release->media[j]);
                fp3_add_discid(release->media[j], ID->data);

            } else if (disc_score.distance == best_score.distance) {
                fp3_add_discid(release->media[j], ID->data);
            }

            ne_buffer_clear(ID);
        }


        /* XXX How to handle missing media?  It does happen for
         * release 7c4903d6-dd15-45d2-a045-834b1c99a6f0 of
         * Overbombing.
         *
         * Also happens for disc 1 (the DVD) on release
         * 048bdb6b-8926-4b54-bad6-2363a6170269 of Coldplay Live.
         *
         * For now, just fail!  Nah, that's no good for the DVD+CD
         * release.  How about just ignore?
         */
        if (mb5_disc_list_size(DiscList) <= 0) {
            ne_buffer_destroy(ID);
            return (-1);
        }

        toc_score->distance += best_score.distance;
        toc_score->score += best_score.score;
    }

    ne_buffer_destroy(ID);

    return (0);
}
#endif


#if 0 // XXX Currently not used
/* A lot of duplication w.r.t. _toc_lower_bound() above
 */
static int
_toc_accuraterip(struct fingersum_context **ctxs,
                 struct fp3_release *release,
                 Mb5MediumList MediumList,
                 struct toc_score *toc_score,
                 struct accuraterip_context *ar_ctx)
{
    Mb5Disc Disc;
    Mb5DiscList DiscList;
    Mb5Medium Medium;
//    Mb5OffsetList OffsetList;
//    Mb5TrackList TrackList;

    ne_buffer *ID;

    size_t k, l, m; //, n;
    int i, j;

    const struct _cache *response;
    const struct _entry *entry;
    const struct _chunk *chunk;
    struct fp3_medium *medium;
    struct fp3_disc *disc;

//    struct fp3_recording_list *recordings;
//    struct fp3_recording *recording;
    ssize_t offset;


    /* Allocate and initialise space for the media.
     *
     * XXX Really need to do that here and now?
     */
//    if (_grow_release_media(release, MediumList) != 0)
//        return (-1);
    ID = ne_buffer_ncreate(36 + 1); // XXX Should have been length of discid

    toc_score->distance = 0; // XXX Mind this!
    toc_score->score = 0; // XXX Mind this!

    for (i = 0; i < mb5_medium_list_size(MediumList); i++) {
        Medium = mb5_medium_list_item(MediumList, i);
        if (Medium == NULL)
            continue; // XXX Correct?

        DiscList = mb5_medium_get_disclist(Medium);
        if (DiscList == NULL)
            continue; // XXX Correct?

//        TrackList = mb5_medium_get_tracklist(Medium);
//        if (TrackList == NULL)
//            continue; // XXX Correct?

        medium = fp3_new_medium();
        if (medium == NULL) {
            printf("fp3_medium_new() failed\n");
            return (-1);
        }

        medium->position = mb5_medium_get_position(Medium);
        medium = fp3_release_add_medium(release, medium);
        if (medium == NULL) {
            printf("fp3_release_add_medium() failed\n");
            return (-1);
        }

        for (j = 0; j < mb5_disc_list_size(DiscList); j++) {
            Disc = mb5_disc_list_item(DiscList, j);
            if (Disc == NULL)
                continue; // XXX Correct?

#if 0
            response = accuraterip_get(ar_ctx, Disc);
#else
            response = accuraterip_localhost_get(ar_ctx, Disc);
#endif
            if (response == NULL)
                continue; // XXX Correct?

            ne_buffer_grow(ID, mb5_disc_get_id(Disc, NULL, 0) + 1);
            mb5_disc_get_id(Disc, ID->data, ID->length);
            ne_buffer_altered(ID);

            printf("check here\n");

            disc = fp3_add_disc_by_id(medium, ID->data);
            if (disc == NULL) {
                printf("fp3_add_disc_by_id() failed\n");
                return (-1);
            }

            printf("Got the pointer %p back, %zd entries\n",
                   response, response->nmemb);

            for (k = 0; k < response->nmemb; k++) {
                entry = response->entries + k;
                printf("  Got %zd tracks\n", entry->track_count);
                for (l = 0; l < entry->track_count; l++) {
                    chunk = entry->chunks + l;
                    printf("    Got CRC 0x%08x\n", chunk->CRC);

                    for (m = 0; m < release->nmemb; m++) {
//                        recordings = release->streams[m];

                        if (fingersum_find_offset(ctxs[m], chunk->unk, &offset) == 0)
                            printf("      FOUND OFFSET %zd\n", offset);


                        /* XXX CONTINUE HERE: add the recording to the
                         * disc at the given position (and should
                         * allow the disc to hold more than one
                         * recording per position.  And make sure the
                         * sector lengths match.  And make sure this
                         * function cleans up properly.  Discs should
                         * have a list of offsets, and perhaps a
                         * confidence for each offset as in XLD.
                         *
                         * Bigger issue: If we move recordings from
                         * streams to the disc, how to backtrack in
                         * case of error (or if assignment is not
                         * complete)?  And how to handle the second
                         * and third, etc, discs?
                         *
                         * Solution: Copy the streams from one release
                         * structure and return a newly created
                         * release structure here!
                         */
//                        for (n = 0; n < recordings->nmemb; n++) {
//                            recording = recordings->recordings[n];
//                        }
                    }
                }
            }
        }
    }
    fp3_release_dump(release, 2, 0);

    exit(EXIT_SUCCESS);
    return (0);
}
#endif


// *************** DISTANCE STUFF BELOW ***********

/* The levenshtein() function calculates the case-insensitive
 * Levenshtein distance between the two NULL-terminated strings
 * pointed to by @p s and @p t.  This memory-efficient Levenshtein
 * implementation is adapted from "Fast, memory efficient Levenshtein
 * algorithm"
 * [http://www.codeproject.com/Articles/13525/Fast-memory-efficient-Levenshtein-algorithm]
 * by Sten Hjelmqvist.
 *
 * See also
 * http://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C
 * and http://en.wikipedia.org/wiki/Levenshtein_distance
 *
 * XXX Note somewhere that this doesn't scale so well, but that we use
 * it for short strings.
 *
 * XXX This will probably break with Unicode!  The Unicode stuff must
 * be fixed, but it should probably not be case-insensitive!
 *
 * @param s XXX
 * @param t XXX
 * @return  The Levenshtein distance if successful, @c SIZE_MAX
 *          otherwise.  If an error occurs the global variable @c
 *          errno is set to indicate the error.
 */
size_t
//levenshtein(const char *s, const char *t)
levenshtein(const wchar_t *s, const wchar_t *t)
{
    size_t *v;
    size_t cost, i, j, m, n, v0j, vj1;


    /* Degenerate cases.  If cost in the loop is case-insensitive,
     * this must be case-insensitive.
     */
//    if (strcasecmp(s, t) == 0)
//    if (wcscasecmp(s, t) == 0)
    if (wcscmp(s, t) == 0)
        return (0);

    m = wcslen(s);
    n = wcslen(t);
    if (m == 0)
        return (n);
    if (n == 0)
        return (m);


    /* Allocate and initialise v, the previous row of distances.  This
     * row is A[0][i], edit distance for an empty s.  The distance is
     * just the number of characters to delete from t.
     */
    v = calloc(m + 1, sizeof(size_t));
    if (v == NULL)
        return (SIZE_MAX);

    for (j = 0; j < m + 1; j++)
        v[j] = j;

    for (i = 0; i < n; i++) {
        /* Calculate current row distance from the previous row v.
         * First element of current row distances is A[i + 1][0].
         * Edit distance is delete(i + 1) characters from s to match
         * empty t.
         */
        v[0] = i + 1;
        v0j = i;

        for (j = 0; j < m; j++) {
            vj1 = v[j + 1];
//            cost = tolower(s[j]) == tolower(t[i]) ? 0 : 1;
//            cost = towlower(s[j]) == towlower(t[i]) ? 0 : 1;
            cost = s[j] == t[i] ? 0 : 1;


            /* Use formula to fill in the rest of the row,
             *
             *   v[j + 1] = min(v[j] + 1, v[j + 1] + 1, v0j + cost).
             *
             * In the code, v0j is corresponds to v[j] from the
             * previous iteration, vj1 will become v[j] in the next
             * iteration.
             */
            v[j + 1] = v[j + 1] + 1;
            if (v[j] + 1 < v[j + 1])
                v[j + 1] = v[j] + 1;
            if (v0j + cost < v[j + 1])
                v[j+ 1] = v0j + cost;

            v0j = vj1;
        }
    }

    cost = v[m];
    free(v);

    return (cost);
}

// *************** DISTANCE STUFF ABOVE ***********


/*** DIFFING FUNCTIONS ***/

/* XXX
 */
int
_append_artist(
    Mb5Work work, const char *filter, Mb5Artist **artists, size_t *nmemb, const char *composer_ref, int optional)
{
    char str[1024];
    char str2[1024];

    Mb5AttributeList al;
    Mb5RelationList rl;
    Mb5RelationListList rll;
    Mb5Attribute attribute;
    Mb5Relation relation;
    Mb5Artist artist;

    int i, j, optional_save; //, translated;
    void *p;
    size_t k;


//    printf("  *** in _append_artist() with ->%s<- and ->%s<-\n",
//           filter, composer_ref);

    rll = mb5_work_get_relationlistlist(work);
    if (rll == NULL)
        return (0);

    optional_save = optional;
    for (i = 0; i < mb5_relationlist_list_size(rll); i++) {
        rl = mb5_relationlist_list_item(rll, i);
        if (rl == NULL)
            continue;

        for (j = 0; j < mb5_relation_list_size(rl); j++) {
            relation = mb5_relation_list_item(rl, j);
            if (relation == NULL)
                continue;

            mb5_relation_get_type(relation, str, sizeof(str));
            if (strcmp(str, filter) != 0)
                continue;


            /* Check if the "translated" attribute is present.
             * Sometimes the translator is credited [e.g. "Over the
             * mountains" on "Meanwhile..."], sometimes not
             * [e.g. "This is me" on "Meanwhile..."].  XXX The same
             * applies to arranger on e.g. Monty Python.
             *
             * XXX Kludge: include the translator if present in
             * composer_ref.
             *
             * Multiple ways to denote legal name: see Beck and AnNa
             * R. for examples.  But can we fish out the legal name
             * for Ahmed Khelifati Mohamed from artist
             * 634ef2ec-ffe2-4846-ba68-156f852bf5cb for "Desert Rose"?
             *
             * XXX There is a actually a difference: "lyricist [with
             * attribute translated]" and "translator".  This
             * distinction appears to be gone now [2015-08-23].
             *
             * "additional" credits are always optional.  Makes sense?
             *
             * XXX If we set optional here, then all subsequent
             * artists will be treated as optional, too: restore the
             * original setting!
             */
            al = mb5_relation_get_attributelist(relation);
            optional = optional_save;
            for (k = 0; k < mb5_attribute_list_size(al); k++) {
                attribute = mb5_attribute_list_item(al, k);
                mb5_attribute_get_text(attribute, str2, sizeof(str2));
//                printf("    #### GOT AN ATTRIBUTE ->%s<-\n", str2);
                if (strcmp(str2, "additional") == 0) {
                    optional = 1;
                    break;
                }
            }

            artist = mb5_relation_get_artist(relation);
            if (artist == NULL)
                continue;

            if (strcmp(filter, "arranger") == 0) {
                mb5_artist_get_name(artist, str2, sizeof(str2));
//                printf(
//                    "Considering artist ->%s<-, "
//                    "current count %zd at %p ptr %p\n",
//                    str2, *nmemb, *artists, artist);
                /* XXX composer_ref can be NULL if there is no
                 * composer in the stream--duh!  See also below.
                 */
                if (composer_ref == NULL || strstr(composer_ref, str2) == NULL) {
//                    printf("strstr() says NULL\n");
                    continue;
                }
            }

            if (strcmp(filter, "translator") == 0) {
                mb5_artist_get_name(artist, str2, sizeof(str2));
//                printf(
//                    "Considering artist ->%s<-, "
//                    "current count %zd at %p ptr %p\n",
//                    str2, *nmemb, *artists, artist);


                /* XXX How can composer_ref be NULL here?
                 */
                if (composer_ref == NULL || strstr(composer_ref, str2) == NULL) {
//                    printf("strstr() says NULL\n");
                    continue;
                }
            }

            if (optional != 0) {
                mb5_artist_get_name(artist, str2, sizeof(str2));
//                printf(
//                    "Considering artist ->%s<-, "
//                    "current count %zd at %p ptr %p\n",
//                    str2, *nmemb, *artists, artist);


                /* XXX How can composer_ref be NULL here?
                 */
                if (composer_ref == NULL || strstr(composer_ref, str2) == NULL) {
//                    printf("strstr() says NULL\n");
                    continue;
                }
            }

            mb5_artist_get_id(artist, str, sizeof(str));
//            printf("See artist with id ->%s<-\n", str);
            for (k = 0; k < *nmemb; k++) {
                mb5_artist_get_id((*artists)[k], str2, sizeof(str2));
                if (strcmp(str, str2) == 0)
                    break;
            }

            if (k < *nmemb)
                continue;

            p = realloc(*artists, (*nmemb + 1) * sizeof(Mb5Artist));
            if (p == NULL)
                ; // XXX
            *artists = (Mb5Artist *)p;
            (*artists)[*nmemb] = artist;
            *nmemb += 1;

//            printf("  ADDED to %p as %p\n", *artists, (*artists)[*nmemb - 1]);
        }
    }

    return (0);
}


/* XXX This appears to be redundant now, and could be merged back into
 * get_mb_values() proper.
 */
struct metadata *
get_mb_values_reduced(struct fp3_release *release, size_t medium_position, size_t recording_position)
{
    //    const struct fp3_medium *medium;
    const struct fp3_recording *recording;
//    const struct fp3_recording_list *recordings;
//    const struct fp3_release *release;
//    const struct fp3_releasegroup *releasegroup;
//    size_t i, j, k, l;
//    char mb_title[1024];
//    const char *stream_title;

    Mb5Recording mb_recording;
    Mb5Track mb_track;
    Mb5ArtistCredit ac;
    struct metadata *metadata;
    ne_buffer *txt;

    int len;

    txt = ne_buffer_create(); // XXX Must be freed!


    /* XXX Fishy: unassigned?  Can we even have multiple recordings at
     * this stage?  If so, need another index parameter!
     */
#if 0
    if (index >= release->nmemb) {
        printf("      --- NO RECORDINGS ---\n");
        return (NULL); // continue;
    }

    recordings = release->streams[index];
    if (recordings == NULL) {
        printf("      --- NO RECORDINGS ---\n");
        return (NULL); // continue;
    }

    if (recordings->nmemb != 1) {
        printf("      --- ZERO OR MULTIPLE RECORDINGS ---\n");
        return (NULL);
    }
#else
    recording = _release_recording_at_position(
        release, medium_position, recording_position);
    if (recording == NULL) {
        printf("      --- NO RECORDING ON MEDIUM %zd, POSITION %zd ---\n",
               medium_position, recording_position);
        return (NULL);
    }
#endif


    /* This will happen if recordings were not matched up by
     * fingerprint -- for summer party, it should be possible to match
     * them up by disc instead.
     *
     * It may be caused by the recording not being associated with a
     * fingerprint at MB.  For really short audioclips,
     * acoustid-fingerprinter won't submit any fingerprints (11 s gets
     * no fingerprint, but 13 s does).
     *
     * THINK!  See below about recording->id being NULL!
     *
     * XXX Would really want to print the list of fingerprints [and
     * their scores] here, but it appears that libmusicbrainz provides
     * no means of accessing that information.
     */
    if (recording->id == NULL) {
        printf("Would have taken the exit for mp %zd and tp %zd\n",
               medium_position, recording_position);
//        return (NULL);
    }


    /* Allocate the metadata structure.  XXX Should initialise it as
     * well and do all that in a separate function.
     */
    metadata = metadata_new();
    if (metadata == NULL)
        return (NULL);


    /* Find the MB5 recording.
     *
     * Also disc and track, iTunes-style.
     */
//    mb_recording = _release_get_recording(
//        release->mb_release, recordings->recordings[0]->id);

    int ii, jj;
    Mb5MediumList ml;
    Mb5Medium m;
    Mb5TrackList tl;
    Mb5Recording rec;
    Mb5Track t;
    char rid[256];


    mb_recording = NULL;
    ml = mb5_release_get_mediumlist(release->mb_release);
    for (ii = 0; ii < mb5_medium_list_size(ml); ii++) {
        m = mb5_medium_list_item(ml, ii);
        tl = mb5_medium_get_tracklist(m);

        for (jj = 0; jj < mb5_track_list_size(tl); jj++) {
            t = mb5_track_list_item(tl, jj);
            rec = mb5_track_get_recording(t);
            mb5_recording_get_id(rec, rid, sizeof(rid));


            /* XXX THINK!  See above about recording->id being NULL!
             * Match by id if available, otherwise use medium/track
             * position.
             */
            int match = 0;
            if (recording->id != NULL) {
                match = strcmp(rid, recording->id) == 0;
            } else {
                match =
                    (mb5_medium_get_position(m) == medium_position &&
                     mb5_track_get_position(t) == recording_position);
            }

            if (match) {
                /* XXX What if more than recording matches?
                 */
                mb_recording = rec;
                mb_track = t;

                len = snprintf(NULL, 0, "%d/%d",
                               mb5_track_get_position(t),
                               mb5_track_list_size(tl));
                metadata->track = (char *)calloc(len + 1, sizeof(char));
                if (metadata->track == NULL)
                    ; // XXX
                sprintf(metadata->track, "%d/%d",
                         mb5_track_get_position(t),
                         mb5_track_list_size(tl));


                /* Disc
                 */
                len = snprintf(NULL, 0, "%d/%d",
                               mb5_medium_get_position(m),
                               mb5_medium_list_size(ml));
                metadata->disc = (char *)calloc(len + 1, sizeof(char));
                if (metadata->disc == NULL)
                    ; // XXX
                sprintf(metadata->disc, "%d/%d",
                         mb5_medium_get_position(m),
                         mb5_medium_list_size(ml));
            }
        }
    }

    if (mb_recording == NULL) {
        /* This happened 2015-01-07: AcoustID said track 2 of release
         * b094240a-1caa-4f90-961b-d56d12ce4f7e should be recording
         * f7267a31-b2b3-4dd5-b0fc-8e00711eb4a1, but MusicBrainz
         * database from 2014-XX-XX (probably November 2014) contains
         * recording 097290ca-e3eb-49f9-b611-086a2554bcd5 at that
         * position.
         *
         * Updating the database made the problem go away!
         */
        printf("*** HIT THE BROWN PROBLEM ***\n");
        return (NULL);
    }


    /* XXX Check this with the other code from somewhere else.  Is the
     * type of len correct?
     *
     * XXX Use the track title, fall back on the title of the
     * recording.  Wait until the Sid Vicious edits come through, then
     * revisit this idea.  Also check with track 6 of love in itself
     */
    len = mb5_track_get_title(mb_track, NULL, 0);
    if (len > 0) {
//        printf("        ### GOT THE TRACK TITLE\n");
        metadata->title = (char *)calloc(len + 1, sizeof(char));
        if (metadata->title == NULL)
            return (NULL);
        mb5_track_get_title(mb_track, metadata->title, len + 1);
    } else {
//        printf("        ### GOT THE RECORDING TITLE\n");
        len = mb5_recording_get_title(mb_recording, NULL, 0);
        metadata->title = (char *)calloc(len + 1, sizeof(char));
        if (metadata->title == NULL)
            return (NULL);
        mb5_recording_get_title(mb_recording, metadata->title, len + 1);
    }


    /* Album
     */
    len = mb5_release_get_title(release->mb_release, NULL, 0);
    metadata->album = (char *)calloc(len + 1, sizeof(char));
    if (metadata->album == NULL)
        return (NULL);
    mb5_release_get_title(release->mb_release, metadata->album, len + 1);


    /* Artist -- XXX This actually requires some thought.
     *
     * XXX Use namecredit or artist?  Possibility of using namecredit
     * for the recordings and artist for the album artist?
     *
     * Code is butt-ugly.  And it should prefer the track artist (as
     * in title, above).
     *
     * XXX "Fat of the land" will have album artist "The Prodigy", but
     * artist "Prodigy".  This, however, requires generating the
     * metadata from MB5 on a per-release basis.
     */
    Mb5NameCreditList ncl;
    Mb5NameCredit nc;
    Mb5Artist a;
    int i, len2;

    metadata->artist = (char *)calloc(1024, sizeof(char));
    if (metadata->artist == NULL)
        return (NULL);
    metadata->artist[0] = '\0';

    metadata->sort_artist = (char *)calloc(1024, sizeof(char));
    if (metadata->sort_artist == NULL)
        return (NULL);
    metadata->sort_artist[0] = '\0';

    len = 0;
    len2 = 0;

    ac = mb5_track_get_artistcredit(mb_track);
    if (ac == NULL)
        ac = mb5_recording_get_artistcredit(mb_recording);

//    ac = mb5_track_get_artistcredit(mb_track);
    ncl = mb5_artistcredit_get_namecreditlist(ac);

    for (i = 0; i < mb5_namecredit_list_size(ncl); i++) {
        nc = mb5_namecredit_list_item(ncl, i);
        a = mb5_namecredit_get_artist(nc);


        /* Is this logic correct?  Use namecredit if it exists, fall
         * back on name of artist.  XXX May need additional space
         * here?
         *
         * Deal with artist->SortName() while we're here.
         *
         * XXX Maybe not here: default sort order for artist should
         * alphabetically by last name.
         */
        if (mb5_namecredit_get_name(nc, NULL, 0) > 0) {
            len += mb5_namecredit_get_name(
                nc, metadata->artist + len, 1024 - len);
        } else {
            len += mb5_artist_get_name(
                a, metadata->artist + len, 1024 - len);

        }


        /* Note that sort_artist is always taken from artist proper,
         * as opposed to namecredit.  Does that make sense?  XXX Maybe
         * not, but it appears there is no alternative, because
         * namecredit does not come with a sort option.
         */
        len += mb5_namecredit_get_joinphrase(
            nc, metadata->artist + len, 1024 - len);

        len2 += mb5_artist_get_sortname(
            a, metadata->sort_artist + len2, 1024 - len2);


        // XXX This is probably a bit more complicated: what if
        // nothing was added above?
        ne_buffer_grow(txt, mb5_namecredit_get_joinphrase(nc, NULL, 0) + 1);
        mb5_namecredit_get_joinphrase(nc, txt->data, txt->length);
        ne_buffer_altered(txt);


        /* Replace joinphrase of comma with semicolon for sort artist.
         * XXX Does it make sense to *always* use semicolon for
         * joinphrase in sort order?  Trying that now...
         */
//        if (strcmp(txt->data, ", ") == 0) {
        if (i + 1 < mb5_namecredit_list_size(ncl)) {
            strcat(metadata->sort_artist + len2, "; ");
            len2 += 2;
        } else {
            len2 += mb5_namecredit_get_joinphrase(
                nc, metadata->sort_artist + len2, 1024 - len2);
        }
    }


    /* Album artist, very similar to above!
     *
     * The album artist is mainly for sorting, not so much display.
     * All "Lemonheads" albums have the same album artist, whether
     * they credit themselves as "Lemonheads" or "The Lemonheads".
     * The same applies to "Prodigy" vs "The Prodigy".  See
     * https://musicbrainz.org/doc/Style/Artist_Credits.
     *
     * Bottom line: do not use namecredit for album artist.  But what
     * about for composer credits?
     */

    /* The "Various Artists" special-purpose artist, used as the
     * artist of compilations.  See
     * https://musicbrainz.org/doc/Style/Unknown_and_untitled/Special_purpose_artist.
     */
    const char *artist_various = "89ad4ac3-39f7-470e-963a-56509c546377";


    metadata->album_artist = (char *)calloc(1024, sizeof(char));
    if (metadata->album_artist == NULL)
        return (NULL);
    metadata->album_artist[0] = '\0';

    metadata->sort_album_artist = (char *)calloc(1024, sizeof(char));
    if (metadata->sort_album_artist == NULL)
        return (NULL);
    metadata->sort_album_artist[0] = '\0';

    metadata->compilation = NULL;

    len = 0;
    len2 = 0;

    ac = mb5_release_get_artistcredit(release->mb_release);
    ncl = mb5_artistcredit_get_namecreditlist(ac);

    for (i = 0; i < mb5_namecredit_list_size(ncl); i++) {
        nc = mb5_namecredit_list_item(ncl, i);
        a = mb5_namecredit_get_artist(nc);

//        printf("  NISSE %02d/%02d: got ->%s<-\n",
//               i, mb5_namecredit_list_size(ncl), rid);


        /* Leave album_artist empty for compilations, i.e. where the
         * sole album_artist is "Various Artists".  XXX How to deal
         * with other special-purpose artists, such as [traditional]
         * (See "Kapitalismen" on "Guldkorn")?  They should probably
         * never be album artists.
         *
         * For documentation: this is good: otherwise Bangles
         * "Greatest Hits" and The Four Aces feat. Al Alberts
         * "Greatest Hits" both end up in the "Compilations/Greatest
         * Hits" directory.
         *
         * Always ignore namecredit for album artist in order to group
         * artist properly.  The same artist should be sorted together
         * on the iTunes filesystem, irrespective of how they choose
         * to credit themselves on the release: see "The Prodigy" vs
         * "Prodigy" on "Fat of the land".
         *
         * XXX This has issues with Senor Coconut: check and migrate
         * issue to TODO if persists.  The reason is that there are
         * several artists representing Senor Coconut.
         */
        mb5_artist_get_id(a, rid, sizeof(rid));
        if (strcmp(rid, artist_various) == 0 &&
            mb5_namecredit_list_size(ncl) == 1) {
            metadata->compilation = strdup("1");
        } else {
//            if (mb5_namecredit_get_name(nc, NULL, 0) > 0) {
//                len += mb5_namecredit_get_name(
//                    nc, metadata->album_artist + len, 1024 - len);
//            } else {
                len += mb5_artist_get_name(
                    a, metadata->album_artist + len, 1024 - len);
//            }

//            len += mb5_namecredit_get_joinphrase(
//                nc, metadata->album_artist + len, 1024 - len);

            len2 += mb5_artist_get_sortname(
                a, metadata->sort_album_artist + len2, 1024 - len2);

            char jp[64];

            mb5_namecredit_get_joinphrase(nc, jp, 64);


            /* Always use semicolon for joinphrase in sort album
             * artist.  See above.
             *
             * Now also do this for "plain" album artist: even though
             * this name is displayed in "album view", it will put
             * "Henry Mancini and His Orchestra" in the same directory
             * as "Henry Mancini & His Orchestra".  XXX Maybe a better
             * way is to canonicalize it like "a, b, c & d" (which is
             * kinda what MusicBrainz does by default)?  It should now
             * be canonicalized.
             *
             * Comma and semicolon have sepecial meanings for
             * classical releases.  Also, an artist may appear more
             * than once.  See for instance "John Cage, Herbert Henck;
             * Herbert Henck".  But always use semicolon to separate
             * sort album artist.
             */
           if (strcmp(jp, ", ") == 0 || strcmp(jp, "; ") == 0) {
                strcat(metadata->album_artist + len, jp);
                len += strlen(jp);

                strcat(metadata->sort_album_artist + len2, "; ");
                len2 += strlen("; ");

            } else if (i + 2 == mb5_namecredit_list_size(ncl)) {
                strcat(metadata->album_artist + len, " & ");
                len += 3;

                strcat(metadata->sort_album_artist + len2, "; ");
                len2 += 2;
            } else if (i + 1 < mb5_namecredit_list_size(ncl)) {
                strcat(metadata->album_artist + len, ", ");
                len += 2;

                strcat(metadata->sort_album_artist + len2, "; ");
                len2 += 2;
            } else {
                len += mb5_namecredit_get_joinphrase(
                    nc, metadata->album_artist + len, 1024 - len);

                len2 += mb5_namecredit_get_joinphrase(
                    nc, metadata->sort_album_artist + len2, 1024 - len2);
            }
        }
    }

//    printf("NISSE FINAL      album_artist: ->%s<-\n",
//           metadata->album_artist);
//    printf("NISSE FINAL sort_album_artist: ->%s<-\n",
//           metadata->sort_album_artist);

//    printf("NISSE FINAL      artist: ->%s<-\n",
//           metadata->artist);
//    printf("NISSE FINAL sort_artist: ->%s<-\n",
//           metadata->sort_artist);


    /* Should probably erase the sort-artist if it's identical to the
     * artist (same applied to album artist).  XXX See also composer
     * below!
     *
     * XXX Odd corner case: the only difference is the semicolon,
     * e.g. "[traditional], [anonymous], [unknown]" has sort order
     * "[traditional]; [anonymous]; [unknown]" on Chanticleer's "An
     * American Folk Medley".
     */
//    printf("HATTNE CHECK ->%s<- vs ->%s<-\n",
//           metadata->artist, metadata->sort_artist);

    if (strcmp(metadata->artist, metadata->sort_artist) == 0)
        metadata->sort_artist[0] = '\0';
    if (strcmp(metadata->album_artist, metadata->sort_album_artist) == 0)
        metadata->sort_album_artist[0] = '\0';

    return (metadata);
}


struct metadata *
get_mb_values(struct fp3_release *release, size_t medium_position, size_t recording_position, const char *composer_ref)
{
    /* Tricker?! Composer
     *
     * MusicBrainz uses comma (,) as the separator, but what about the
     * delimiter? See
     * http://citationstyles.org/downloads/specification.html [THIS APPEARS TO BE RELEVANT FOR THE DOCBOOK WORK]!
     * XXX Ask the library!  Here's what they say: http://www.ncbi.nlm.nih.gov/books/NBK7256
     */
#if 1
    Mb5RelationListList rll; //, wrll;
    Mb5RelationList rl; //, wrl;
    Mb5Relation r; //, wr;
    Mb5Work w;
    int j; //, k, l;

//    Mb5Artist a;
    int i;


    struct metadata *metadata;
    metadata = get_mb_values_reduced(
        release, medium_position, recording_position);
    if (metadata == NULL) {
        /* XXX See The comment in get_mb_values reduced()
         */
        return (NULL);
    }


    /* XXX Duplication w.r.t. get_mb_values_reduced()!
     */
//    const struct fp3_recording_list *recordings;
    struct fp3_recording *recording;
    Mb5Recording mb_recording;

    int ii, jj;
    Mb5MediumList ml;
    Mb5Medium m;
    Mb5TrackList tl;
    Mb5Recording rec;
    Mb5Track t;
    Mb5ReleaseGroup rg;
    Mb5SecondaryTypeList stl;
    Mb5SecondaryType st;
    char rid[256];


    /* Get the compilation XXX this does not strictly adhere to the
     * (new?) Apple interpretation.
     *
     * Here's my take on the Apple interpretation (hopefully
     * implemented here): "Album is a compilation of songs by various
     * artists" [from the iTunes 12.3.3.17 tooltip].  The remix album
     * by Jazzanova is a case in point.
     */
    rg = mb5_release_get_releasegroup(release->mb_release);
    stl = mb5_releasegroup_get_secondarytypelist(rg);
    for (ii = 0; ii < mb5_secondarytype_list_size(stl); ii++) {
        st = mb5_secondarytype_list_item(stl, ii);
        mb5_secondarytype_get_secondarytype(st, rid, sizeof(rid));

        if (strcmp(rid, "Compilation") == 0) { // XXX Case insensitive?
            ; // metadata->compilation = strdup("1"); XXX NEW TEST CODE ABOVE!
        }
    }

#if 0
    recordings = release->streams[index];
    if (recordings == NULL)
        ;
#else
    recording = _release_recording_at_position(
        release, medium_position, recording_position);
    if (recording == NULL) {
        printf("      --- NO RECORDING ON MEDIUM %zd, POSITION %zd ---\n",
               medium_position, recording_position);
        return (NULL);
    }
#endif

    mb_recording = NULL;
    ml = mb5_release_get_mediumlist(release->mb_release);
    for (ii = 0; ii < mb5_medium_list_size(ml); ii++) {
        m = mb5_medium_list_item(ml, ii);
        tl = mb5_medium_get_tracklist(m);

        for (jj = 0; jj < mb5_track_list_size(tl); jj++) {
            t = mb5_track_list_item(tl, jj);
            rec = mb5_track_get_recording(t);
            mb5_release_get_id(rec, rid, sizeof(rid));


            /* XXX THINK!  See above about recording->id being NULL!
             * Match by id if available, otherwise use medium/track
             * position.
             */
            int match = 0;
            if (recording->id != NULL) {
                match = strcmp(rid, recording->id) == 0;
            } else {
                match =
                    (mb5_medium_get_position(m) == medium_position &&
                     mb5_track_get_position(t) == recording_position);
            }

            if (match) {
                /* XXX What if more than recording matches?
                 */
                mb_recording = rec;
            }
        }
    }

    metadata->composer = (char *)calloc(1024, sizeof(char));
    if (metadata->composer == NULL)
        ;
    metadata->composer[0] = '\0';
//    int n_composers = 0;

    metadata->sort_composer = (char *)calloc(1024, sizeof(char));
    if (metadata->sort_composer == NULL)
        ;
    metadata->sort_composer[0] = '\0';
//    int n_sort_composers = 0;

    rll = mb5_recording_get_relationlistlist(mb_recording);

//    printf("HAVE %d relationships\n", mb5_relationlist_list_size(rll));
    Mb5Artist *artists = NULL;
    size_t nmemb_artists = 0;


    /* XXX Should probably use artist of TRACK instead of the
     * RECORDING!  The same applies to title!
     */
//    printf("get_mb_values() marker #0\n");

    for (i = 0; i < mb5_relationlist_list_size(rll); i++) {
        rl = mb5_relationlist_list_item(rll, i);
        if (rl == NULL)
            continue;

        for (j = 0; j < mb5_relation_list_size(rl); j++) {
            r = mb5_relation_list_item(rl, j);
            if (r == NULL)
                continue;

            w = mb5_relation_get_work(r);


            /* TEST 2016-09-06 */
            int is_instrumental = 0;
            {
                Mb5AttributeList al;
                Mb5Attribute a;
                char txt[256];
                al = mb5_relation_get_attributelist(r);
                if (al != NULL) {
                    int k;
                    for (k = 0; k < mb5_attribute_list_size(al); k++) {
                        a = mb5_attribute_list_item(al, k);
                        if (a != NULL) {
                            mb5_attribute_get_text(a, txt, 256);
                            if (strcmp(txt, "instrumental") == 0)
                                is_instrumental = 1;
                        }
                    }
                }
            }


            /* Librettist is always optional; sometimes they are
             * credited, sometimes not.
             */
            if (w != NULL) {
                _append_artist(
                    w, "composer", &artists, &nmemb_artists, composer_ref, 0);
                _append_artist(
                    w, "writer", &artists, &nmemb_artists, composer_ref, 0);
                _append_artist(
                    w, "librettist", &artists, &nmemb_artists, composer_ref, 1);
                _append_artist(
                    w, "arranger", &artists, &nmemb_artists, composer_ref, 0);


                /* XXX Wishlist: don't append lyricist if it's an
                 * instrumental recording: see "Holiday for Strings"
                 * on "Musical Mayhem" as an example.
                 *
                 * Or better yet: make it optional
                 */
                if (is_instrumental == 0) {
                    _append_artist(
                        w, "lyricist", &artists, &nmemb_artists, composer_ref, 0);
                    _append_artist(
                        w, "translator", &artists, &nmemb_artists, composer_ref, 0);
                } else {
                    _append_artist(
                        w, "lyricist", &artists, &nmemb_artists, composer_ref, 1);
                    _append_artist(
                        w, "translator", &artists, &nmemb_artists, composer_ref, 1);
                }


                // XXX Bring in arranger as well?  See
                // http://musicbrainz.org/recording/12dd1eb3-e893-464f-8ab0-f58625b21641

#if 0
                char str[1024];
                //int len =
                mb5_relation_get_type(r, str, sizeof(str));

                wrll = mb5_work_get_relationlistlist(w);

//                printf("  Relation %d:%d: ->%s<- [%d] %p\n",
//                       i, j, str, len, w);
//                printf("  Got work relationlistlist size %d\n",
//                       mb5_relationlist_list_size(wrll));

                for (k = 0; k < mb5_relationlist_list_size(wrll); k++) {
                    wrl = mb5_relationlist_list_item(wrll, k);


                    /* First, append composer.
                     */
                    for (l = 0; l < mb5_relation_list_size(wrl); l++) {
                        wr = mb5_relation_list_item(wrl, l);
                        a = mb5_relation_get_artist(wr);

                        if (a != NULL) {
                            char str2[1024];
                            //int len2 =
                            mb5_relation_get_type(
                                wr, str2, sizeof(str2));

                            if (strcmp(str2, "composer") == 0) {
                                char str3[1024];
                                //int len3 =
                                mb5_artist_get_name(
                                    a, str3, sizeof(str3));
                                if (strstr(metadata->composer, str3) == NULL) {
                                    sprintf(
                                        metadata->composer + strlen(metadata->composer),
                                        "%s%s",
                                        strlen(metadata->composer) > 0 ? ", " : "",
                                        str3);
                                }

                                mb5_artist_get_sortname(
                                    a, str3, sizeof(str3));
                                if (strstr(metadata->sort_composer, str3) == NULL) {
                                    sprintf(
                                        metadata->sort_composer + strlen(metadata->sort_composer),
                                        "%s%s",
                                        strlen(metadata->sort_composer) > 0 ? "; " : "",
                                        str3);
                                }
//                                printf("Found %s ->%s<-\n", str2, str3);
                            }
                        }
                    }


                    /* Second, append writer.  XXX Should probably
                     * have lyricist in between here [because I assume
                     * composer precedes lyricist].  See the mappings
                     * for more
                     * [http://picard.musicbrainz.org/docs/mappings]
                     */
                    for (l = 0; l < mb5_relation_list_size(wrl); l++) {
                        wr = mb5_relation_list_item(wrl, l);
                        a = mb5_relation_get_artist(wr);

                        if (a != NULL) {
                            char str2[1024];
                            //int len2 =
                            mb5_relation_get_type(
                                wr, str2, sizeof(str2));

                            if (strcmp(str2, "writer") == 0) {
                                char str3[1024];
                                //int len3 =
                                mb5_artist_get_name(
                                    a, str3, sizeof(str3));
                                if (strstr(metadata->composer, str3) == NULL) {
                                    sprintf(
                                        metadata->composer + strlen(metadata->composer),
                                        "%s%s",
                                        strlen(metadata->composer) > 0 ? ", " : "",
                                        str3);
                                }

                                mb5_artist_get_sortname(
                                    a, str3, sizeof(str3));
                                if (strstr(metadata->sort_composer, str3) == NULL) {
                                    sprintf(
                                        metadata->sort_composer + strlen(metadata->sort_composer),
                                        "%s%s",
                                        strlen(metadata->sort_composer) > 0 ? "; " : "",
                                        str3);
                                }
//                                printf("Found %s ->%s<-\n", str2, str3);
                            }
                        }
                    }
                }
#endif
            }
        }
    }
#endif

//    printf("get_mb_values() marker #1\n");


    size_t k;
    char str2[1024];


/*
    printf("HAVE %zd artists at %p\n", nmemb_artists, artists);
    for (k = 0; k < nmemb_artists; k++) {
        mb5_artist_get_name(artists[k], str2, sizeof(str2));
        printf("  %p %s\n", artists[k], str2);
    }
*/

#if 1
    char trial_composer[1024];

//    printf("REFERENCE IS ->%s<-\n", composer_ref);
//    size_t nmemb_artists_old = nmemb_artists;

    while (nmemb_artists > 0) {

        size_t d_min = SIZE_MAX;
        size_t k_min = 0;


        /* XXX Should go through all the aliases for each artist and
         * pick the closest one.  See NSYNC on Mr Music 1997 for an
         * example.  Will this work with "Beck" vs "Beck Hansen", and
         * "Prodigy" vs "The Prodigy" as well?
         */
//        printf("looking for best match\n");
        for (k = 0; k < nmemb_artists; k++) {
//            printf("    getting next artist [%p]\n", artists[k]);
            mb5_artist_get_name(artists[k], str2, sizeof(str2));


#if 0
            /* ATTEMPT TO GET LEGALNAME
             *
             * This probably won't work: we don't seem to get an alias
             * list for artists that related to the the work.  Would
             * have to look up the artist separately (or the work?) to
             * get at that information.
             */
            {
                char str3[1024];
                Mb5AliasList al = mb5_artist_get_aliaslist(artists[k]);
                printf("FOR ->%s<- GOT ALIASLIST ->%p<-\n", str2, al);
                for (i = 0; i < mb5_alias_list_size(al); i++) {
                    Mb5Alias alias = mb5_alias_list_item(al, i);
                    mb5_alias_get_type(alias, str3, sizeof(str3));
                    printf("Got alias of type ->%s<-\n", str3);
                }
            }
#endif
//            printf("    got ->%s<-\n", str2);
            strcpy(trial_composer, metadata->composer);

//            printf("    have ->%s<- of length %zd, appending ->%s<-\n",
//                   trial_composer, strlen(trial_composer), str2);

            sprintf(
                trial_composer + strlen(trial_composer),
                "%s%s",
                strlen(trial_composer) > 0 ? ", " : "",
                str2);


            /* XXXX composer_ref could be NULL!  If so
             * strlen(composer_ref) will fail as well, so do
             * strlen(tst) instead?
             */
            char *tst = strdup(composer_ref == NULL ? "" : composer_ref);

            if (strlen(tst) > strlen(trial_composer))
                tst[strlen(trial_composer)] = '\0';

//            size_t d = levenshtein(composer_ref, trial_composer);

            wchar_t s1[1024];
            wchar_t s2[1024];

            mbstowcs(s1, tst, 1024);
            mbstowcs(s2, trial_composer, 1024);
            size_t d = levenshtein(s1, s2);
//            size_t d = levenshtein(tst, trial_composer);

            free(tst);

/*
            if (strlen(composer_ref) > strlen(trial_composer)) {
                size_t e = strlen(composer_ref) - strlen(trial_composer);

                printf("Got e %zd (from %zd - %zd) d %zd\n", e,
                       strlen(composer_ref), strlen(trial_composer), d);

                if (d >= e)
                    d -= e;
            }
*/

//            printf("    COMPARING ->%s<- and ->%s<- distance %zd\n",
//                   composer_ref, trial_composer, d);

            if (d < d_min) {
                d_min = d;
                k_min = k;
            }
        }

//        for (k = 0; k < nmemb_artists; k++) {
        mb5_artist_get_name(artists[k_min], str2, sizeof(str2));

//        printf("Appending best match %zd of %zd ->%s<- distance %zd\n",
//               k_min, nmemb_artists, str2, d_min);

        sprintf(
            metadata->composer + strlen(metadata->composer),
            "%s%s",
            strlen(metadata->composer) > 0 ? ", " : "",
            str2);

        mb5_artist_get_sortname(artists[k_min], str2, sizeof(str2));
        sprintf(
            metadata->sort_composer + strlen(metadata->sort_composer),
            "%s%s",
            strlen(metadata->sort_composer) > 0 ? "; " : "",
            str2);

//        printf("Pruning list\n");

        memmove(artists + k_min,
                artists + k_min + 1,
                (nmemb_artists > k_min + 1 ? nmemb_artists - k_min - 1 : 0) * sizeof(Mb5Artist));
//        printf("OK, %zd artists remaining, after shifting %zd\n",
//               nmemb_artists - 1,
//               nmemb_artists > k_min + 1 ? nmemb_artists - k_min - 1 : 0);
        nmemb_artists -= 1;
//        }
    }
#endif
//    printf("get_mb_values() marker #2\n");


    /* Should probably erase the sort-composer if it's identical to
     * the composer.  XXX See artist and album artist above.
     */
    if (strcmp(metadata->composer, metadata->sort_composer) == 0)
        metadata->sort_composer[0] = '\0';

//    printf("Set composer      to ->%s<-\n", metadata->composer);
//    printf("Set sort_composer to ->%s<-\n", metadata->sort_composer);

//    if (composer_ref != NULL && nmemb_artists_old > 0)
//        exit(0);

    return (metadata);
}


/* XXX Maybe there is a cleverer way to deal with this, e.g. offsets
 * to the members of a structure.
 */
const char *
extract_title(struct metadata *metadata)
{
    if (metadata->title == NULL)
        return ("");
    return (metadata->title);
}


const char *
extract_album(struct metadata *metadata)
{
    if (metadata->album == NULL)
        return ("");
    return (metadata->album);
}


const char *
extract_artist(struct metadata *metadata)
{
    if (metadata->artist == NULL)
        return ("");
    return (metadata->artist);
}


const char *
extract_sort_artist(struct metadata *metadata)
{
    if (metadata->sort_artist == NULL)
        return ("");
    return (metadata->sort_artist);
}


const char *
extract_album_artist(struct metadata *metadata)
{
    if (metadata->album_artist == NULL)
        return ("");
    return (metadata->album_artist);
}


const char *
extract_sort_album_artist(struct metadata *metadata)
{
    if (metadata->sort_album_artist == NULL)
        return ("");
    return (metadata->sort_album_artist);
}


const char *
extract_composer(struct metadata *metadata)
{
    if (metadata->composer == NULL)
        return ("");
    return (metadata->composer);
}


/* XXX Maybe, instead of def, should have just returned "0" her if NULL?
 */
const char *
extract_compilation(struct metadata *metadata)
{
    if (metadata->compilation == NULL)
        return ("0");
    return (metadata->compilation);
}


const char *
extract_sort_composer(struct metadata *metadata)
{
//    printf("HATTNE CHECK %p ->%s<-\n",
//           metadata->sort_composer, metadata->sort_composer);

    if (metadata->sort_composer == NULL)
        return ("");
    return (metadata->sort_composer);
}


const char *
extract_disc(struct metadata *metadata)
{
    if (metadata->disc == NULL)
        return ("");
    return (metadata->disc);
}


const char *
extract_track(struct metadata *metadata)
{
    if (metadata->track == NULL)
        return ("");
    return (metadata->track);
}


void
diff_item(
    struct metadata *ref, struct metadata **alts, size_t nmemb,
    const char *(*extractor)(struct metadata *))
{
    size_t i;
    int match, identical; // Probably not necessary!


    /* Check if the title from the stream matches the title from
     * all releases.
     */
    match = 1;
    for (i = 0; i < nmemb; i++) {
        if (strcmp(extractor(ref), extractor(alts[i])) != 0) {
            match = 0;
            break;
        }
    }


    /* Check if all the titles are the same
     */
    if (match == 0) {
        identical = 1;
        for (i = 1; i < nmemb; i++) {
            if (strcmp(extractor(alts[0]), extractor(alts[i])) != 0) {
                identical = 0;
                break;
            }
        }

        if (identical) {
            printf("MISMATCH MB5 EQ: ->%s<-\n"
                   "         stream: ->%s<-\n",
                   extractor(alts[0]), extractor(ref));
        } else {
            for (i = 0; i < nmemb; i++) {
                /* XXX This should also print the id of the release
                 * this is coming from!  And the kind of item being
                 * diffed (title, artist, etc).
                 */
                printf("MISMATCH MB5:    ->%s<-\n"
                       "         stream: ->%s<-\n",
                       extractor(alts[i]), extractor(ref));
            }
        }
    }
}


/* This is the old inner code from diff_stream()
 */
static int
diff_stream_2(struct fp3_release *release,
              size_t position_medium,
              size_t position_track,
              struct fingersum_context *ctx)
{
    struct metadata *mb_metadata;
    struct metadata *stream_metadata;


    /* Get metadata for each track.  This will be compared against the
     * corresponding metadata from MB.  This is gives the reference
     * composer, which will all the MB5 composers to be sorted to best
     * match.  This will be used
     *
     * There should not really be more than one stream at this
     * stage...
     */
    stream_metadata = fingersum_get_metadata(ctx);
    if (stream_metadata == NULL)
        return (-1);


    /* Get all the metadata from MB.
     *
     * XXX Should probably use position instead of index here!  That
     * should avoid the problem of matching using MB ID:s, which may
     * not always be consistent in AcoustID and MB.
     *
     * XXX See comment in get_mb_values_reduced().  Should this be a
     * continue or a return (-1);
     */
    mb_metadata = get_mb_values(
        release, position_medium, position_track, stream_metadata->composer);
    if (mb_metadata == NULL)
        return (-1);


    /* Should note somewhere that it if the sort_album_artist is
     * identical to album_artist, it may be difficult to change with
     * iTunes; in fact it may not even be visible.  This may happen
     * with tracks ripped with XLD and converted to m4a using avconv
     * without touching the metadata.
     *
     * That's what the clear_sort_album_artist.scpt script in
     * ~/Library/iTunes/Scripts is for.  Will have similar problems
     * with sort_title, etc but the program currently does not detect
     * that.
     */
//    printf("  title\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_title);
//    printf("  album\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_album);
//    printf("  artist\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_artist);
//    printf("  sort_artist\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_sort_artist);
//    printf("  album_artist\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_album_artist);
//    printf("  sort_album_artist\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_sort_album_artist);
//    printf("  composer\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_composer);
//    printf("  sort_composer\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_sort_composer);
//    printf("  compilation\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_compilation);
//    printf("  disc\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_disc);
//    printf("  track\n");
    diff_item(stream_metadata, &mb_metadata, 1, &extract_track);

    return (0);
}


/* Returns 1 if the disc matches perfectly, 0 otherwise.
 */
static int
_check_disc_checksum(struct fp3_disc *disc)
{
    struct fp3_track *track;
    size_t m, n;


    for (m = 0; m < disc->nmemb; m++) {
        track = disc->tracks[m];
        if (track == NULL)
            return (0);

        for (n = 0; n < track->nmemb_checksums; n++) {
            if (track->checksums[n]->checksum_v1 > 0 ||
                track->checksums[n]->checksum_v2 > 0) {
                break;
            }
        }

        if (n >= track->nmemb_checksums)
            return (0);
    }

    return (1);
}


#if 0
/* Returns non-zero if the release has/maps stream identified by index
 * @p index.
 */
static int
_release_has_track(struct fp3_release *release, size_t index)
{
    struct fp3_disc *disc;
    struct fp3_medium *medium;
    struct fp3_track *track;
    size_t i, j, k, l;


    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];
        for (j = 0; j < medium->nmemb_discs; j++) {
            disc = medium->discs[j];
            for (k = 0; k < disc->nmemb; k++) {
                track = disc->tracks[k];
                for (l = 0; l < track->nmemb; l++) {
                    if (track->indices[l] == index)
                        return (1);
                }
            }
        }
    }

    return (0);
}


/* Returns non-zero if the release has all tracks and no extra tracks
 */
static int
_check_release_no_extra_tracks(struct fp3_release *release, size_t index_max)
{
    struct fp3_disc *disc;
    struct fp3_medium *medium;
    size_t i, j, n_tracks, n_tracks_on_largest_disc;


    /* Check whether the release has all tracks.
     */
    for (i = 0; i < index_max; i++) {
        if (_release_has_track(release, i) == 0)
            return (0);
    }


    /* Count the number of tracks
     */
    n_tracks = 0;

//    printf("RELEASE %s\n", release->id);
    for (i = 0; i < release->nmemb_media; i++) {
        medium = release->media[i];

        if (medium->nmemb_discs == 0)
            return (0);


        /* XXX This is useless: the medium will only have those tracks
         * that are assigned.  Instead, return zero if the medium does
         * not have any discs, or the discs do not have any tracks.
         */
        n_tracks_on_largest_disc = 0;
        for (j = 0; j < medium->nmemb_discs; j++) {
            disc = medium->discs[j];
            if (disc->nmemb == 0)
                return (0);
            if (disc->nmemb > n_tracks_on_largest_disc)
                n_tracks_on_largest_disc = disc->nmemb;
        }

//        printf("  MEDIUM %zd: %zd tracks\n", i, n_tracks_on_largest_disc);

        n_tracks += n_tracks_on_largest_disc;
    }

    if (n_tracks > index_max + 1)
        return (0);
    if (n_tracks < index_max + 1)
        exit(0); // XXX NOTREACHED: already checked that all tracks are there!

    return (1);
}
#endif


/* XXX Should print all the releases, even the ones that
 * matches if there is no consensus.
 *
 * XXX Check this against XLD output again [XLD says OK as well].
 */
// XXX ctx should be const, too!
// XXX Sure I do not have to worry about UTF here?
// XXX Optional (configurable): if one operand is empty, don't show the diff (like diff's -N option?)
static int
diff_stream(struct fp3_result *result, struct fingersum_context **ctxs)
{
    /* Extract the field from the stream.
     */

    /* Diff it against all the corresponding recordings.
     */

//    const struct fp3_medium *medium;
//    const struct fp3_recording *recording;
//    const struct fp3_recording_list *recordings;
    struct fp3_release *release;
    struct fp3_releasegroup *releasegroup;
    size_t i, j, k, l, m, n;

//    char mb_title[1024];
//    const char *stream_title;
//    size_t nmemb, n_releases;
//    Mb5Recording mb_recording;
//    struct metadata *mb_metadata;
//    struct metadata *stream_metadata;


    /* Extract the number of streams.  XXX Should really be done
     * better than taking the first release from the first
     * releasegroup.
     */
//    releasegroup = result->releasegroups[0];
//    release = releasegroup->releases[0];
//    nmemb = release->nmemb;


    /* Extract the number of releases.  XXX This may actually be
     * correct?!  But we could have grown this as we go along below!
     */
//    n_releases = 0;
//    for (i = 0; i < result->nmemb; i++)
//        n_releases += result->releasegroups[i]->nmemb;
//    mb_metadata = (struct metadata **)calloc(
//        n_releases, sizeof(struct metadata *));

//    printf("diff_stream(): marker #0 [%zd %zd]\n",
//           result->nmemb, release->nmemb);


    /* Only considering the first release.  XXX Should probably
     * traverse the releasegroups instead.
     *
     * There could still be ambiguities, but here we should probably
     * look up the correct track from the disc, and warn if
     * ambiguities still remain.
     *
     * Other checks/pruning steps to implement:
     *
     *  Remove discs that do not match so well
     *
     *  If any release has a matching disck, remove releases that do
     *  not have any matching discs.
     *
     *  Prune empty releasegroups
     */
    struct fp3_fingerprint *fingerprint;
    struct fp3_medium *medium;
    struct fp3_disc *disc;
    struct fp3_recording *recording;
    struct fp3_track *track;
    struct fp3_stream *stream;
//    struct fp3_fingerprint *fingerprint;
    size_t index;


#if 1
    /* Remove discs that do not match so well: check if there
     * are any discs in the release that match perfectly.  If
     * so, remove all those that do not.
     *
     * XXX Fix: if there is a perfectly matching disc for the
     * medium, remove all those that do not match.  If there
     * is a disc with at least one match, remove all those
     * that have no matches.
     *
     * This may not be quite done yet: should really loop over medium
     * 1 for each release, then medium 2 for each release, ...  Or
     * better: first loop over all releases, keep track of good media
     * for each release, then prune in second loop over all releases.
     */
    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        if (releasegroup == NULL)
            continue;

        int releasegroup_has_matching_release = 0;

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];
            if (release == NULL)
                continue;
            printf("Round 1: checking discs on release %s\n", release->id);

            int release_has_mismatched_disc = 0;

            for (k = 0; k < release->nmemb_media; k++) {
                medium = release->media[k];
                if (medium == NULL || medium->nmemb_discs == 0)
                    continue;


                /* Check if any disc matches perfectly, i.e. if it has
                 * at least one matching checksum for each track.
                 */
                for (l = 0; l < medium->nmemb_discs; l++) {
                    disc = medium->discs[l];
                    if (disc == NULL)
                        continue;
                    printf("  Round 1: checking disc %s\n", disc->id);

                    if (_check_disc_checksum(disc) != 0) {
                        printf("    Disc %s matches perfectly\n", disc->id);
                        break;
                    }
                }


                /* There is at least one disc with a perfect match:
                 * delete discs that do not match perfectly.
                 *
                 * XXX Need to remove the release if it has no
                 * matching discs?
                 */
                if (l >= medium->nmemb_discs) {
                    release_has_mismatched_disc = 1;

                } else {
                    for (l = 0; l < medium->nmemb_discs; l++) {
                        disc = medium->discs[l];
                        if (disc == NULL)
                            continue;

                        if (_check_disc_checksum(disc) == 0) {
                            printf("    Removing disc %s: imperfect match\n",
                                   disc->id);
                            fp3_erase_disc(medium, l);
                            l--;
                            continue;
                        }
                    }
                }
            }


            /* There is at least one release where all the discs match.
             *
             * XXX Not true: this triggers even if there are NO discs!
             */
            if (release_has_mismatched_disc == 0) {
                printf("  Release %s has matching discs\n", release->id);
                releasegroup_has_matching_release = 1;
            }
        }


        /* If there is at least one release that has matching discs
         * for all the media, delete all releases that do not.
         */
        if (releasegroup_has_matching_release != 0) {
            for (j = 0; j < releasegroup->nmemb; j++) {
                release = releasegroup->releases[j];
                if (release == NULL)
                    continue;
                printf("Round 2: checking discs on release %s\n", release->id);

                for (k = 0; k < release->nmemb_media; k++) {
                    medium = release->media[k];
                    if (medium == NULL || medium->nmemb_discs == 0)
                        continue;

                    for (l = 0; l < medium->nmemb_discs; l++) {
                        disc = medium->discs[l];
                        if (disc == NULL)
                            continue;

                        if (_check_disc_checksum(disc) == 0)
                            break;
                    }

                    if (l < medium->nmemb_discs)
                        break;
                }

                if (k < release->nmemb_media) {
                    printf("  Removing release %s "
                           "because it has mismatching discs\n", release->id);
                    fp3_erase_release(releasegroup, j);
                    j--;
                    continue;
                }
            }
        }
    }
#endif


    /* MAYBE SOMEWHERE ELSE THAN HERE: Remove releases that have extra
     * (unused) media.  This happens for instance when a discs has its
     * own release, but also features in a big collection box
     * (e.g. "Count meets Duke").
     */
    size_t max_index = 0;
    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];
            for (k = 0; k < release->nmemb_media; k++) {
                medium = release->media[k];
                for (l = 0; l < medium->nmemb_discs; l++) {
                    disc = medium->discs[l];
                    for (m = 0; m < disc->nmemb; m++) {
                        track = disc->tracks[m];

                        for (n = 0; n < track->nmemb; n++) {
                            if (track->indices[n] > max_index)
                                max_index = track->indices[n];
                        }
                    }
                }
            }
        }
    }
    //printf("Found max index %zd\n", max_index);


    /* Remove releases with extra tracks, prune empty releasegroups.
     *
     * This does not work so well for releases with extra disks,
     * e.g. Coldplay Live 2003, which has a bonus DVD.  XXX Should
     * disregard extra disks if they're not CD:s?
     */
#if 0
    int have_release_without_extra_tracks = 0;
    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];

            if (_check_release_no_extra_tracks(release, max_index) != 0) {
                have_release_without_extra_tracks = 1;
                //printf("release %s: NONZERO\n", release->id);
            } else {
                ; //printf("release %s: ZERO\n", release->id);
            }
        }
    }

    if (have_release_without_extra_tracks != 0) {
        for (i = 0; i < result->nmemb; i++) {
            releasegroup = result->releasegroups[i];
            for (j = 0; j < releasegroup->nmemb; j++) {
                release = releasegroup->releases[j];

                if (_check_release_no_extra_tracks(release, max_index) == 0) {
                    fp3_erase_release(releasegroup, j);
                    j--;
                    continue;
                }
            }

            if (releasegroup->nmemb == 0) {
                fp3_erase_releasegroup(result, i);
                i--;
            }
        }
    }
#endif


    /* Now back to where we were...
     */
    for (i = 0; i < result->nmemb; i++) {
        releasegroup = result->releasegroups[i];
        if (releasegroup == NULL)
            continue;
        printf("*** RELEASEGROUP %s\n", releasegroup->id);

        for (j = 0; j < releasegroup->nmemb; j++) {
            release = releasegroup->releases[j];
            if (release == NULL)
                continue;
            printf("  *** RELEASE %s\n", release->id);


            /* XXX Output barcode here, because I actually use it
             * quite frequently!
             */
            for (k = 0; k < release->nmemb_media; k++) {
                medium = release->media[k];
                if (medium == NULL)
                    continue;
                if (medium->nmemb_discs == 0) {
                    printf("    *** MEDIUM %zd: NO MATCHING DISCS IN MB ***\n",
                           medium->position);

                    for (l = 0; l < medium->nmemb_tracks; l++) {
                        /* Since there is no disc, there cannot be a
                         * match against AccurateRip or EAC.
                         */
                        recording = medium->tracks[l];

                        printf("      *** TRACK %zd\n", recording->position);
                        if (recording->nmemb > 1)
                            printf("      *** MORE THAN ONE FINGERPRINT! ***\n");

                        for (m = 0; m < recording->nmemb; m++) {
                            fingerprint = recording->fingerprints[m];

                            for (n = 0; n < fingerprint->nmemb; n++) {
                                stream = fingerprint->streams[n];
                                index = stream->index;


                                /* XXX It seems there are often
                                 * several streams in a fingerprint,
                                 * but with the same index.  That
                                 * should probably not happen: each
                                 * stream should occur at most once in
                                 * a fingerprint?
                                 */
                                if (n > 0) {
                                    if (index != fingerprint->streams[0]->index) {
                                        printf("*** MORE THANE ONE STREAM! ***\n");
                                    } else {
                                        continue;
                                    }
                                }

                                if (diff_stream_2(release,
                                                  medium->position,
                                                  recording->position,
                                                  ctxs[index]) != 0) {
                                    printf("HORRIBLE FAILURE #1\n");
                                }
                            }
                        }
                    }
                } else {
                    printf("    *** MEDIUM %zd\n", medium->position);
                }

                for (l = 0; l < medium->nmemb_discs; l++) {
                    /* There is at least one matching disc.
                     */
                    disc = medium->discs[l];
                    if (disc == NULL)
                        continue;
                    if (0 /* medium->accuraterip_id == NULL */)
                        printf("*** NO MATCHING DISC IN ACCURATERIP ***\n");
                    printf("      *** DISC %s\n", disc->id);

                    for (m = 0; m < disc->nmemb; m++) {
                        track = disc->tracks[m];
                        if (track == NULL)
                            continue;
                        printf("        *** TRACK %zd\n", track->position);


                        /* XXX Should also print if there was a
                         * matching fingerprint, and the delta to the
                         * number of sectors.
                         *
                         * XXX Should distinguish between
                         *
                         *   (1) No matching disc in MusicBrainz
                         *   (2) No hit at accuraterip
                         *
                         * XXX Idea for final presentation: if an
                         * offset does not actually match anything on
                         * the disc (but some other offset does),
                         * remove it from all the tracks.  This
                         * happens on "Heavy weather".
                         *
                         * XXX Fix (but before here): if there is a
                         * perfectly matching disc for the medium,
                         * remove all those that do not match.  If
                         * there is a disc with at least one match,
                         * remove all those that have no matches.
                         *
                         * XXX Print fingerprint in super-verbose
                         * mode.
                         */
                        if (track->confidence_total > 0 || track->confidence_eac_total > 0) {
                            size_t n;
                            size_t total;

                            total = 0;
                            for (n = 0; n < track->nmemb_checksums; n++) {
                                total += track->checksums[n]->checksum_v1;
                                total += track->checksums[n]->checksum_v2;
                                total += track->checksums[n]->count_eac;
                            }

                            if (total == 0) {
                                printf("          *** BAD RIP *** XXX %zd\n", track->nmemb_checksums);
                            } else {
                                for (n = 0; n < track->nmemb_checksums; n++) {
                                    /* Reduce the output: XXX this
                                     * should not really need to
                                     * happen that often!
                                     */
                                    if (track->checksums[n]->checksum_v1 == 0 &&
                                        track->checksums[n]->checksum_v2 == 0 &&
                                        track->checksums[n]->count_eac == 0) {
                                        continue;
                                    }

                                    printf("          *** %d+%d/%d [max %d, offset %" PRId64 "] ***\n",
                                           track->checksums[n]->checksum_v1,
                                           track->checksums[n]->checksum_v2,
                                           track->confidence_total,
                                           track->confidence_max,
                                           track->checksums[n]->offset);
                                    printf("          *** %zd/%d [max %d, offset %" PRId64 "] ***\n",
                                           track->checksums[n]->count_eac,
                                           track->confidence_eac_total,
                                           track->confidence_eac_max,
                                           track->checksums[n]->offset);
                                }
                            }
                        }

                        if (track->nmemb > 1)
                            printf("*** MORE THAN ONE STREAM! ***\n");

                        for (n = 0; n < track->nmemb; n++) {
                            index = track->indices[n];

                            if (diff_stream_2(release,
                                              medium->position,
                                              track->position,
                                              ctxs[index]) != 0) {
                                printf("HORRIBLE FAILURE #2\n");
                            }
                        }
                    }
                }
            }
        }
    }

    return (0);
}


int
main(int argc, char *argv[])
{

    size_t i;
    FILE **streams;
    struct fingersum_context **ctxs;
    struct pool_context *pc, *pc2;

#if 0
    printf("check %zd\n", levenshtein(L"GAMBOL", L"GUMBO"));
    printf("check %zd\n", levenshtein(L"GUMBO", L"GAMBOL"));
    printf("check %zd\n", levenshtein(L"nisse", L"nisse"));
    printf("check %zd\n", levenshtein(L"nisse", L"nisseo"));
    printf("check %zd\n", levenshtein(L"nisse", L"nisso"));
    printf("check %zd\n", levenshtein(L"nisse", L"nisseapa"));
    printf("check %zd\n", levenshtein(L"nisseprutt", L"nisseapa"));
    printf("check %zd\n", levenshtein(L"nisse", L"niSsEo"));
    printf("check %zd\n", levenshtein(L"nisse", L"niSsE"));
    return (0);
#endif


  /* It appears we need to set the locale (see setlocale(3)) to ensure
   * it's UTF-8 for string conversion stuff mbstowc(3).
   *
   * XXX This is probably not the right way to do this.  Read
   * http://www.cl.cam.ac.uk/~mgk25/unicode.html, the OpenBSD man
   * pages, and search Mendeley for Unicode.
   */
  setlocale(LC_CTYPE, "en_US.UTF-8");


    /* Initialise the neon library and its dependencies, must be
     * called once before the first ne_session_create().  Should be
     * matched by ne_sock_exit().  Even though neon is not directly
     * used here, it is initialised here so as to avoid unnecessary
     * initialisation in the individual modules.  As long as
     * ne_sock_exit() is called on exit, this should work because of
     * the reference-counting.
     */
    if (ne_sock_init() != 0)
        return (-1);

    streams = (FILE **)calloc(
        argc - 1, sizeof(FILE *));
    if (streams == NULL)
        printf("*** FAILURE #1\n"); // XXX This should be done externally!

    ctxs = (struct fingersum_context **)calloc(
        argc - 1, sizeof(struct fingersum_context *));
    if (ctxs == NULL)
        printf("*** FAILURE #2\n"); // XXX

    pc = pool_new_pc(4); // XXX Hardcoded!
    if (pc == NULL) {
        free(ctxs);
        printf("*** FAILURE #3\n"); // XXX
        return (-1);
    }


    /* XXX This must all be released somewhere!
     *
     * Track numbers are one-based.  Also acoustid indexes from 1 (see
     * message on message board).  None of that actually matters here.
     */
    for (i = 0; i < argc - 1; i++) {
        streams[i] = fopen(argv[i + 1], "r");
        if (streams[i] == NULL) {
            pool_free_pc(pc);
            free(ctxs);
            return (-1);
        }

        ctxs[i] = fingersum_new(streams[i]);
        if (ctxs[i] == NULL) {
            pool_free_pc(pc);
            free(ctxs);
            return (-1);
        }

        if (add_request(pc, ctxs[i], (void *)i, POOL_ACTION_CHROMAPRINT) != 0) {
            pool_free_pc(pc);
            free(ctxs);
            printf("Oh no, we're all gonna die\n"); // XXX
            return (-1);
        }
    }


    /* Note that stuff may arrive out of order!
     *
     * XXX A progress bar may be better here!
     *
     * Keep the thread pool busy!  Immediately submit AccurateRip
     * calculations, but collect them in a different pool.  Otherwise
     * Chromaprint and CRC:s become mixed up in the same pool, and it
     * can happen that the first CRC is finished before the last
     * Chromaprint is done.
     *
     * XXX No point in computing AccurateRip unless its lossless.
     *
     * XXX Permutation needed because we may compute fingerprints out
     * of order, and acoustid returns them in the order they were
     * given
     */
    struct acoustid_context *ac;
    struct fp3_result *result3;
    struct fingersum_context *ctx;
    char *fingerprint;
//    void *arg; // XXX Make it the pointer--integer type!
    intptr_t arg;
    int status;

//    size_t *permutation;
//    permutation = (size_t *)calloc(
//        argc - 1, sizeof(size_t));
//    if (permutation == NULL) {
//        pool_free_pc(pc);
//        free(ctxs);
//        return (-1);
//    }

    ac = acoustid_new();
    if (ac == NULL) {
//        free(permutation);
        pool_free_pc(pc);
        free(ctxs);
        return (-1);
    }

    pc2 = pool_new_pc(4); // XXX Hardcoded!
    if (pc2 == NULL) {
        acoustid_free(ac);
//        free(permutation);
        pool_free_pc(pc);
        free(ctxs);
        return (-1);
    }

    for (i = 0; i < argc - 1; i++) {
        printf("Fingerprinting... ");
        fflush(stdout);
        if (get_result(pc, &ctx, (void **)(&arg), &status) != 0 ||
            (status & POOL_ACTION_CHROMAPRINT) == 0 ||
            fingersum_get_fingerprint(ctx, NULL, &fingerprint) != 0) {
            pool_free_pc(pc2);
            acoustid_free(ac);
//            free(permutation);
            pool_free_pc(pc);
            free(ctxs);
            return (-1);
        }
        printf("'%s' OK\n", basename(argv[arg + 1]));

        if (acoustid_add_fingerprint(
                ac, fingerprint, fingersum_get_duration(ctx), (size_t)arg) != 0) {
            pool_free_pc(pc2);
            acoustid_free(ac);
//            free(permutation);
            pool_free_pc(pc);
            free(ctxs);
            return (-1);
        }

//        permutation[i] = (size_t)arg; //i; // XXX not correct!
//        permutation[(size_t)arg] = i;
        free(fingerprint);


        /* This is done later, now.
         */
#if 0
        if (add_request(pc2, ctx, (void *)arg, POOL_ACTION_ACCURATERIP) != 0) {
            pool_free_pc(pc2);
            acoustid_free(ac);
//            free(permutation);
            pool_free_pc(pc);
            free(ctxs);
            return (-1);
        }
#endif
    }

    pool_free_pc(pc);
    result3 = acoustid_request(ac);
    acoustid_free(ac);
    if (result3 == NULL) {
        printf("AcoustID request failed\n");
        pool_free_pc(pc2);
//        free(permutation);
        free(ctxs);
        return (EXIT_FAILURE); // XXX CLEANUP!
    }


    /* XXX Fold sort and permute into one?
     */
/*
    fp3_sort_result(result3);
    if (fp3_permute_result(
            result3, permutation, argc - 1) != 0) {
        pool_free_pc(pc2);
        free(permutation);
        free(ctxs);
        return (EXIT_FAILURE);
    }
    free(permutation);
*/


    /* Only keep releases which contain all matches.  If there
     * are no releasegroups left, there is nothing to do.
     *
     * XXX This eliminates the possibility to handle mixtapes.  If
     * there are any extraneous tracks, the program will exit here.
     */
    ssize_t num_matched;

    num_matched = _filter_incomplete(result3);
    if (num_matched < 0) {
        pool_free_pc(pc2);
        free(ctxs);
        return (EXIT_FAILURE);
    }

    if (result3->nmemb == 0) {
        printf("No matching releasegroups, exiting...\n");
        pool_free_pc(pc2);
        free(ctxs);
        return (EXIT_SUCCESS);
    }


    /* OLD COMMENT: Think about this output a bit; could be quite
     * useful.  For instance, how many "configurations" do we have?
     */
    struct fp3_release *release3;
    struct fp3_releasegroup *releasegroup3;
    size_t num_releases;
    size_t j; // k, l, m, n;
    int verbose = 3; //3;


    switch (verbose) {
    case 0:
        break;

    case 1:
        /* In least verbose mode, output just how many releases in how
         * many releasegroups, how many streams not matched.
         */
        num_releases = 0;
        for (i = 0; i < result3->nmemb; i++) {
            releasegroup3 = result3->releasegroups[i];
            if (releasegroup3 != NULL)
                num_releases += releasegroup3->nmemb;
        }

        printf("Have %zd releases in %zd releasegroups, "
               "%zd streams missing fingerprints\n",
               num_releases,
               result3->nmemb,
               argc - 1 - num_matched);
        break;

    case 2:
        /* XXX Output what streams lack matches in AcoustID, maybe in
         * addition to the above.  The ability to obtain a list of
         * unmatched streams will probably be required later on
         * anyway.
         */
        break;

    case 3:
    default:
        /* XXX Should probably also include what the streams lacking
         * AcoustID matches.  Maybe intermediate level: only print
         * releasegroups and releases?
         *
         * If the expected release (or release group) is not listed in
         * the output below, it may be the case that it was recently
         * added to MusicBrainz and has not been synchronised to
         * AcoustID yet.  "Patience, young Padawan." [sic]
         */
        printf("Response from AcoustID:\n");
        if (fp3_result_dump(result3, 2, 0) < 0) {
            pool_free_pc(pc2);
            free(ctxs);
            return (EXIT_FAILURE);
        }
        break;
    }


    /* Next: compute match to TOC from MusicBrainz (and use the fuzzy
     * match as a distance metric?).  But what about multi-disc
     * releases?
     *
     * XXX Is cube_distance() what I want?
     *
     * http://codereview.musicbrainz.org/r/349/ and
     * http://www.postgresql.org/docs/8.3/static/cube.html
     * http://doxygen.postgresql.org/cube_8c.html#a26c35cb0f81942a35d55c9fe99ad9acf
     * http://doxygen.postgresql.org/cube_8c.html#a9cf512679a22589106b3f054f51d9be9
     *
     * Or http://thomas.apestaart.org/morituri/trac/
     *
     * XXX Should make sure this works with proxies.  Maybe it just
     * does on account of underlying neon?
     *
     * At this stage, the purpose is only to do sector-length
     * comparison.  If the offset-list could be retrieved from
     * AcoustID, this MusicBrainz-lookup could be skipped.
     *
     * XXX Note somewhere else than here: discid-matching does not
     * work so well if a multi-disc release does not have DISCID's for
     * all the discs [then the program says no matching disc in MB
     * even though the first disc is there].
     *
     * XXX What about background-fetching from the MusicBrainz
     * servers?  That should now be implemented.  Could probably
     * submit the queries in one loop, then wait for them in another.
     */
    Mb5Release Release;
//    struct toc_score toc_score;
    int prutt_num;
//    fp3_recording_list *recordings3;

    struct musicbrainz_ctx *mb_ctx;

    mb_ctx = musicbrainz_new();
    if (mb_ctx == NULL) {
        pool_free_pc(pc2);
        free(ctxs);
        printf("Failed to initialise mb_ctx\n");
        return (EXIT_FAILURE);
    }

    /* XXX This appears to not crash, but maybe we won't need all of
     * those here, now.
     */
    char *prutt_names[] = {(char *)"inc",
                           (char *)"release-group"};
    char *prutt_values[] = {(char *)"artist-credits "
                            "discids "
                            "media "
                            "recordings", NULL};
    prutt_num = 2;

    struct accuraterip_context *ar_ctx;
    int release_has_matching_discs = 0;

#if 0
    ar_ctx = accuraterip_new("localhost", 8080);
#else
    ar_ctx = accuraterip_new(NULL, 0);
#endif
    if (ar_ctx == NULL)
        ; // XXX

    for (i = 0; i < result3->nmemb; i++) {
        releasegroup3 = result3->releasegroups[i];
        releasegroup3->distance = LONG_MAX;
        prutt_values[1] = releasegroup3->id;

        printf("Submitting query for release-group %s [%zd candidates]\n",
               releasegroup3->id, releasegroup3->nmemb);
        if (musicbrainz_query(mb_ctx,
                              "release",
                              "",
                              "",
                              prutt_num,
                              prutt_names,
                              prutt_values) != 0) {
            ; // XXX
        }

        for (j = 0; j < releasegroup3->nmemb; j++) {
            /* Find the MusicBrainz MediumList corresponding to the
             * release, and complete the AcoustID response
             * accordingly.
             *
             * XXX If there is no release or no MediumList in
             * MusicBrainz should probably erase the release here
             *
             * This is basically the Brown problem (out of sync).
             */
            release3 = releasegroup3->releases[j];
            printf("MB query for release ->%s<- [%zd/%zd]\n",
                   release3->id, j + 1, releasegroup3->nmemb);

            Release = musicbrainz_get_release(mb_ctx,
                                              "release",
                                              "",
                                              "",
                                              prutt_num,
                                              prutt_names,
                                              prutt_values,
                                              release3->id);
            if (Release == NULL)
                continue;

            if (_complete_release(
                    release3, Release, argc - 1) != 0) {
                printf("XXX _complete_release() failed!\n");
            }

//            printf("*** POST _complete_release() DUMP\n");
//            fp3_release_dump(release3, 2, 0);
//            printf("*** POST _complete_release() DUMP\n");

            if (_release_add_discs(
                    release3, ctxs,
                    argc - 1, ar_ctx, Release) != 0) {
                printf("XXX _release_add_discs() failed!\n");
            }

//            printf("*** POST _release_add_discs() DUMP\n");
//            fp3_release_dump(release3, 2, 0);
//            printf("*** POST _release_add_discs() DUMP\n");


            /* Make sure the correct offsets are calculated for each
             * stream.
             *
             * XXX This may be premature: we may not actually need to
             * calculate all these offsets?
             */
            if (_add_streams_offset_2(ctxs, release3) != 0)
                exit (-1); // XXX

            if (_release_has_matching_discs(release3) != 0) {
                printf("  setting release_has_matching_discs\n");
                release_has_matching_discs = 1;
            }

//            if (_toc_accuraterip(
//                    ctxs, release3, MediumList, &toc_score, ar_ctx) != 0) {
//                printf("_toc_accuraterip() failed!");
//                exit(EXIT_FAILURE);
//            }


            // XXX Need this for later; see remark about clone below
            release3->mb_release = Release;


#if 0
            /* 2016-04-27: Should probably fall back on some version
             * of this if _toc_accuraterip() fails.  IDEA: position
             * the tracks using accuraterip.  If that fails use the
             * sector lengths.  If that fails, use the information
             * from acoustid.
             *
             * XXX Should probably fall back on checking lower bound
             * using the track length instead!
             *
             * XXX At this stage: calculate lower_bound, populate list
             * of discs with bound and AccurateRip path, calculate
             * Levenshtein distance.  Next (non-network) loop: eliminate.
             *
             * XXX Probably want to do this completely differently:
             * assign position (track and medium) from recording, then
             * match against TOC, fall back on matching against track
             * length (and invert sign).  Populate a list of media and
             * calculate distance for each disc.  Key discs by discid,
             * and use the special NULL discid if the TOC-match had to
             * be done against track lengths.
             */
            if (_toc_lower_bound(ctxs, release3, MediumList, &toc_score) != 0) {
                printf("SILLY HATTNE failed _toc_lower_bound()\n");
                toc_score.distance = 1000; // XXX LONG_MAX;
                toc_score.score = 0;
            }

            printf("  Lower bound is %ld %f\n",
                   toc_score.distance, toc_score.score);

            release3->distance = toc_score.distance;
//            release3->score = toc_score.score;


            /* XXX What about the clone business?  Would be good not
             * to have to worry about this!
             */
            release3->mb_release = Release;

            if (toc_score.distance < releasegroup3->distance ||
                (toc_score.distance == releasegroup3->distance /* &&
                                                                  toc_score.score > releasegroup3->score */)) {
                releasegroup3->distance = toc_score.distance;
//                releasegroup3->score = toc_score.score;
            }
#endif
        }
    }


    /* If there is a release for which all streams are matched in
     * AccurateRip, prune all releases which do not.
     *
     * XXX Could similarly implement the Levenshtein stuff here.
     *
     * XXX Should probably distinguish the case when the release is
     * removed because there are no matching discs.  Do this step in a
     * separate loop and print what disc ID are assigned.  Under
     * certain circumstances (e.g. if all/most streams are assigned)
     * it would be possible to determine what (submitted) discs
     * actually DO match.
     *
     * XXX This won't work for e.g. Queens of the Stone Age, because
     * it has a track before the first (which was not ripped).  Nah!
     * Broken edit https://musicbrainz.org/edit/63832060 at MB?
     */
    if (release_has_matching_discs != 0) {
        for (i = 0; i < result3->nmemb; i++) {
            releasegroup3 = result3->releasegroups[i];

            for (j = 0; j < releasegroup3->nmemb; j++) {
                release3 = releasegroup3->releases[j];

                if (_release_has_matching_discs(release3) == 0) {
                    printf("Removing release %s "
                           "because it does not cover all streams\n",
                           release3->id);
                    fp3_erase_release(releasegroup3, j);
                    j--;
                    continue;
                }
            }

            /* Remove the releasegroup if all its releases are erased.
             */
            if (releasegroup3->nmemb == 0) {
                fp3_erase_releasegroup(result3, i);
                i--;
            }
        }
    }

    printf("%%%% BLABLABLA DUMP %%%%\n");
//    sleep(3);
//    if (fp3_result_dump(result3, 2, 0) < 0) {
        //pool_free_pc(pc2);
        //free(ctxs);
        //return (EXIT_FAILURE);
//    }
//    exit (0);


    /* Make sure the correct offsets are calculated for each stream.
     * Submit each stream for calculation.
     */
//    if (_add_streams_offset(ctxs, result3) != 0)
//        exit (-1); // XXX

    for (i = 0; i < argc - 1; i++) {
        printf("ADDING request %zd %p\n", i, ctxs[i]);

        if (add_request(pc2, ctxs[i], (void *)i, POOL_ACTION_ACCURATERIP) != 0) {
            pool_free_pc(pc2);
            free(ctxs);
            printf("Oh no, we're all gonna die... again\n"); // XXX
            return (-1);
        }
    }


    /* Sort the releases by scores
     *
     * Could probably sort the releases above to save some time here.
     */
//    fp3_sort_result(result3);
//                exit(0); // XXX


    /* Wait for the AccurateRip checksums, otherwise the stuff below
     * that depends on it will break.  But maybe this could be done
     * later?
     *
     * XXX This step appears to be surprisingly slow on e.g. "Songs in
     * A minor": is multiprocessing working right, or is it just that
     * we are recalculating the AcoustID fingerprints?
     */
    for (i = 0; i < argc - 1; i++) {
        struct fingersum_context *ctx;
        void *arg;
        int status;

        printf("Checksumming... ");
        fflush(stdout);
        if (get_result(pc2, &ctx, &arg, &status) != 0)
            printf("ERROR #1\n"); // XXX
        printf("[%zd: %lu sectors] OK %zd\n",
               (size_t)arg, fingersum_get_sectors(ctx), i);

        if ((status & POOL_ACTION_ACCURATERIP) == 0)
            printf("ERROR #2 %d %d\n", status, POOL_ACTION_ACCURATERIP);
    }
    printf("Freeing the pool...");
    fflush(stdout);
    pool_free_pc(pc2);
    printf(" OK\n");


    /* CHECK AGAINST ACCURATERIP
     *
     * If the minimum distance is zero, eliminate all releases with
     * positive distances (and prune releasegroups if appropriate).
     *
     * Only try to check stuff against AccurateRip if the distance is
     * zero.  Do not bother verifying a release against AccurateRip
     * unless the distance is zero.  XXX That may not actually be
     * preferable--test against Cure!  And note that Snatch doesn't
     * match anything!
     *
     * But what if we've got all the tracks on one disk (but not the
     * other discs of the release)?
     *
     * Design choice: do all the Musicbrainz lookups while the worker
     * threads are computing the CRC:s, then do all the AccurateRip
     * lookups.  Alternatively, could interleave them by releasegroup.
     *
     * Otherwise, eliminate all but the best matching releasegroup.
     *
     * Prune the releases: if a zero-distance releasegroup exist,
     * prune all release groups with distance >0.  Within each
     * remaining releasegroup prune all releases with distance >0 if a
     * zero-distance release exists.  Otherwise keep the best
     * releasegroup / release.
     *
     * XXX If any ambiguities remain (i.e. more than one recording for
     * a stream), and if no match in AccurateRip was found, use
     * metadata to resolve it here.
     *
     * XXX This seems to be a tad broken for Coldplay's X&Y (which
     * takes 63 seconds to complete).
     *
     * XXX Should output release title and release barcode in this
     * loop?
     */
//    exit(0);

    long int min_distance = LONG_MAX;
//    float max_score = 0;
    int confidence_min_max = 0;

//    struct accuraterip_context *ar_ctx;
    struct _match_release *mr_best;

//    ar_ctx = accuraterip_new();
//    if (ar_ctx == NULL)
//        ; // XXX

    mr_best = NULL;

    for (i = 0; i < result3->nmemb; i++) {
        releasegroup3 = result3->releasegroups[i];
        printf("AccurateRip releasegroup [%zd/%zd] ->%s<-\n",
               i + 1, result3->nmemb, releasegroup3->id);

        for (j = 0; j < releasegroup3->nmemb; j++) {
            release3 = releasegroup3->releases[j];
            printf("  AccurateRip release [%zd/%zd] ->%s<-\n",
                   j + 1, releasegroup3->nmemb, release3->id);


            /* Ignore the score for this comparison, because it may
             * not be all that informative
             *
             * Remove the release, because there is a better
             * (perfect) alternative!
             */
//            printf("SILLY HATTNE %zd vs %zd\n",
//                   release3->distance, min_distance);
            if (release3->distance > min_distance) {
                printf("Ignoring because better result already there\n");
                fp3_erase_release(releasegroup3, j);
                j--;
                continue;
            }

            Mb5MediumList ml = mb5_release_get_mediumlist(
                release3->mb_release);

            struct toc_score toc_score;

#if 0 // XXX introduced out 2015-11-25 for new test
            //toc_match(ctxs, release3, ml, &toc_score);
            toc_match2(ctxs, release3, ml, &toc_score);
            printf("    Got distance %ld score %f\n",
                   toc_score.distance, toc_score.score);

            if (toc_score.distance < min_distance ||
                (toc_score.distance == min_distance &&
                 toc_score.score > max_score)) {
                min_distance = toc_score.distance;
                max_score = toc_score.score;
            }
#endif

            if (1 || toc_score.distance == 0) { // XXX bypassed 2015-11-25
                printf("    Will check AccurateRip against %s\n",
                       release3->id);

                // CHECK
//                printf("*** PRE accuraterip_url() DUMP\n");
//                fp3_release_dump(release3, 2, 0);
//                printf("*** PRE accuraterip_url() DUMP\n");

                struct _match_release *mr;
                mr = accuraterip_url(ar_ctx, ctxs, release3, ml);
                printf("    accuraterip_url() returned %p\n", mr);

                if (mr_best == NULL) {
                    mr_best = mr;

                } else if (mr == NULL || _match_release_compar(mr_best, mr) < 0) {
                    printf("2: Ignoring because better result already there\n");
                    fp3_erase_release(releasegroup3, j);
                    j--;
                    if (mr != NULL)
                        _match_release_free(mr);
                    continue;
                } else {
                    _match_release_free(mr_best);
                    mr_best = mr;
                }

                printf("   ... SCORING ...\n");

                if (mr_best != NULL)
                    printf("    score best    %d\n", _score_release(mr_best));
                if (mr != NULL)
                    printf("    score current %d\n", _score_release(mr));

                if (mr_best != NULL && mr != NULL) {
                    printf("    comparison %d\n",
                           _match_release_compar(mr_best, mr));
                }
//                sleep(5);

                if (release3->confidence_min > confidence_min_max)
                    confidence_min_max = release3->confidence_min;

/*
                for (k = 0; k < release3->nmemb; k++) {
                    printf(
                        "Stream %02zd confidence %d confidence_max %d [v%1d]\n",
                        k,
                        release3->streams[k]->recordings[0]->confidence,
                        release3->streams[k]->recordings[0]->confidence_max,
                        release3->streams[k]->recordings[0]->version);
                }
*/
            }
        }


        /* Remove the releasegroup if all its releases are erased.
         */
        if (releasegroup3->nmemb == 0) {
            fp3_erase_releasegroup(result3, i);
            i--;
        }
    }

    accuraterip_free(ar_ctx);


    /* Pruning step, added 2015-11-10: remove all releases with
     * suboptimal matches in AccurateRip.  Otherwise, the levenshtein
     * step below might remove a perfectly matching release because
     * another (mismatching) release fits the metadata better.
     *
     * XXX This is duplication w.r.t. the step after the levenshtein
     * test.
     */
    for (i = 0; i < result3->nmemb; i++) {
        releasegroup3 = result3->releasegroups[i];

        for (j = 0; j < releasegroup3->nmemb; j++) {
            release3 = releasegroup3->releases[j];


            /* Ignore the score for this comparison, because it may
             * not be all that informative
             *
             * Remove the release, because there is a better
             * (perfect) alternative!
             *
             * XXX Evil duplication w.r.t. above.
             */
            if (confidence_min_max > 0 && release3->confidence_min < confidence_min_max) {
                printf("Will remove because %d < %d\n",
                       release3->confidence_min, confidence_min_max);
                fp3_erase_release(releasegroup3, j);
                j--;
                continue;
            }
        }


        /* Remove the releasegroup if all its releases are erased.
         *
         * XXX Evil duplication w.r.t. above.
         */
        if (releasegroup3->nmemb == 0) {
            fp3_erase_releasegroup(result3, i);
            i--;
        }
    }


    /* ORDER BY levenshtein distance.  Had 13 seconds for Overbombing
     * before.
     *
     * XXX Keep track of distance, eliminate the >0 ones if there is
     * at least one zero-distance release.
     */
#if 1
    size_t metadata_min_distance = SIZE_MAX;
    struct metadata *metadata_mb, *metadata_stream;
    size_t release_distance = 0;
    size_t k, l, m, n, index;
    struct fp3_medium *medium3;
    struct fp3_disc *disc3;
    struct fp3_track *track3;


    for (i = 0; i < result3->nmemb; i++) {
        releasegroup3 = result3->releasegroups[i];

        for (j = 0; j < releasegroup3->nmemb; j++) {
            release3 = releasegroup3->releases[j];
            release_distance = 0;

            fp3_sort_release(release3);

            for (k = 0; k < release3->nmemb_media; k++) {
                medium3 = release3->media[k];

                fp3_sort_medium(medium3); // XXX Placement!  Required
                                          // for Kauffman's Puccini
                                          // album.

                for (l = 0; l < medium3->nmemb_discs; l++) {
                    disc3 = medium3->discs[l];

//                    fp3_sort_disc(disc3); // XXX Placement!  Have not
                                          // seen this be necessary
                                          // quite yet.

                    for (m = 0; m < disc3->nmemb; m++) {
                        track3 = disc3->tracks[m];

                        for (n = 0; n < track3->nmemb; n++) {
                            index = track3->indices[n];

                            metadata_stream =
                                fingersum_get_metadata(ctxs[index]);
                            if (metadata_stream == NULL)
                                printf("HORRIBLE FAILURE\n"); // XXX


                            /* XXX k should probably be position here!
                             *
                             * XXX This will break: need a function to
                             * get limited metadata (title, artist,
                             * album).
                             *
                             * XXX And should probably do the
                             * wide-character conversion when the
                             * metadata is read from file.
                             */
                            metadata_mb = get_mb_values(
                                release3,
                                medium3->position,
                                track3->position,
                                NULL);
                            if (metadata_mb == NULL) {
                                release_distance += 10000; // XXX
                                continue;
                            }

                            wchar_t s1[1024];
                            wchar_t s2[1024];

                            if (metadata_stream->title != NULL)
                                mbstowcs(s1, metadata_stream->title, 1024);
                            else
                                s1[0] = '\0';
                            mbstowcs(s2, metadata_mb->title, 1024);
                            release_distance += levenshtein(s1, s2);

                            if (metadata_stream->artist != NULL)
                                mbstowcs(s1, metadata_stream->artist, 1024);
                            else
                                s1[0] = '\0';
                            mbstowcs(s2, metadata_mb->artist, 1024);
                            release_distance += levenshtein(s1, s2);

                            if (metadata_stream->album != NULL)
                                mbstowcs(s1, metadata_stream->album, 1024);
                            else
                                s1[0] = '\0';
                            mbstowcs(s2, metadata_mb->album, 1024);
                            release_distance += levenshtein(s1, s2);
                        }
                    }
                }

                printf("For release ->%s<- have distance %zd\n",
                       release3->id, release_distance);
                release3->metadata_distance = release_distance;
            }


/*
            printf("HATTNE STUPID CHECK:\n");
            for (size_t ii = 0; ii < release3->nmemb; ii++) {
                struct fp3_recording_list *recordings = release3->streams[ii];
                for (size_t jj = 0; jj < recordings->nmemb; jj++) {
                    struct fp3_recording *recording = recordings->recordings[jj];

                    printf("  assigned %d %d %d %d\n",
                           recording->version,
                           recording->confidence,
                           recording->confidence_max,
                           recording->confidence_total);
                }
            }
*/

            if (release_distance < metadata_min_distance)
                metadata_min_distance = release_distance;
        }
    }

    for (i = 0; i < result3->nmemb; i++) {
        releasegroup3 = result3->releasegroups[i];

        for (j = 0; j < releasegroup3->nmemb; j++) {
            release3 = releasegroup3->releases[j];


            /* Ignore the score for this comparison, because it may
             * not be all that informative
             *
             * Remove the release, because there is a better
             * (perfect) alternative!
             *
             * XXX Evil duplication w.r.t. above.
             *
             * XXX Cannot do this here, but must do it later!  It may
             * be the case the metadata matches perflectly, but the
             * release is nevertheless wrong (e.g. does not contain
             * the right discs).  This happens for e.g. Siamese.
             */
            if (metadata_min_distance == 0 && release3->metadata_distance > metadata_min_distance && 0) {
                printf("Will remove because %zd > %zd\n",
                       release3->metadata_distance, metadata_min_distance);
                fp3_erase_release(releasegroup3, j);
                j--;
                continue;
            }
        }


        /* Remove the releasegroup if all its releases are erased.
         *
         * XXX Evil duplication w.r.t. above.
         */
        if (releasegroup3->nmemb == 0) {
            fp3_erase_releasegroup(result3, i);
            i--;
        }
    }
#endif
//    exit(0); // XXX


    /* If any release had a match in accuraterip, remove all those
     * that didn't.  XXX This really calls for the erase() functions!
     */
    char *prutt_names2[] = {(char *)"inc"};
//    char *prutt_values2[] = {(char *)"artist-credits discids media recordings work-rels"}; // XXX May need to do a second lookup to get this to work!  Make a comment to at some point in the future verify whether that's really necessary.  Looks like recording-level-rels is the only one that made a difference?  And work-level-rels makes it crash?

    /* See line 925 in https://git.gnome.org/browse/sound-juicer/tree/libjuicer/sj-metadata-musicbrainz5.c: this is what they're using: "aliases artists artist-credits labels recordings \
release-groups url-rels discids recording-level-rels work-level-rels work-rels \
artist-rels"
     */

    // This works
//    char *prutt_values2[] ={(char *)"aliases artists artist-credits discids media recordings recording-rels work-rels recording-level-rels work-level-rels artist-rels"};

    // This appears to be the minimum set that works
//    char *prutt_values2[] ={(char *)"artists artist-credits discids recordings work-rels recording-level-rels work-level-rels artist-rels"};

    // But need release-groups to get the type of a release (or
    // rather, release-group).
    char *prutt_values2[] ={(char *)"artists artist-credits discids recordings work-rels recording-level-rels release-groups work-level-rels artist-rels"};

#if 0 // XXX This is from sound-juicer
    char *prutt_values[] = {(char *)"aliases "
        "artists "
        "artist-credits "
        "labels "
        "recordings "
        "release-groups "
        "url-rels "
        "discids "
        "recording-level-rels "
        "work-level-rels "
        "work-rels "
        "artist-rels", NULL};
#endif

    int prutt_num2 = 1;


    /* XXX Really, we already have all the information we need except
     * for the work (composer).  Is it possible to look that up more
     * efficiently?
     *
     * XXX Could we perhaps look up all the works in one go instead?
     */
    printf("FINAL MB LOOKUP\n");

    for (i = 0; i < result3->nmemb; i++) {
        releasegroup3 = result3->releasegroups[i];

        for (j = 0; j < releasegroup3->nmemb; j++) {
            release3 = releasegroup3->releases[j];


            /* XXX THIS IS UNSTABLE: sometimes Buggles never proceeds
             * to "leaving check".  Update: it appears the error (if
             * any) occurs elsewhere.
             *
             * Nah, it is here: release3->confidence_min must be
             * assigned, otherwise this will break... naturally!
             */
            printf("  entering check %zd %zd | %d %d\n",
                   releasegroup3->nmemb, j,
                   release3->confidence_min, confidence_min_max);
            if (release3->confidence_min < confidence_min_max) {
                fp3_erase_release(releasegroup3, j);
                j--;
                continue;
            }
            printf("  leaving check\n");

#if 1
            /*** XXX LOOK UP THE RELEASE AGAIN HERE ***/
            if (musicbrainz_query(mb_ctx,
                                  "release",
                                  release3->id,
                                  "",
                                  prutt_num2,
                                  prutt_names2,
                                  prutt_values2) != 0) {
                ; // XXX
            }


            // XXX What about the clone business?
            release3->mb_release = musicbrainz_get_release(mb_ctx,
                                                           "release",
                                                           release3->id,
                                                           "",
                                                           prutt_num2,
                                                           prutt_names2,
                                                           prutt_values2,
                                                           release3->id);
            if (release3->mb_release == NULL)
                continue;

            char id[256];
            mb5_release_get_id(release3->mb_release, id, sizeof(id));
            printf("lookup up release ->%s<-\n", id);

//            Mb5ReleaseList rl = mb5_metadata_get_releaselist(metadata);
#endif
        }

        printf("  left inner loop, %zd\n", releasegroup3->nmemb);

        if (releasegroup3->nmemb == 0) {
            printf("    clear MARKER #0\n");
            fp3_erase_releasegroup(result3, i);
            i--;
            printf("    clear MARKER #1\n");
        }
    }

    //HATTNE_DUMP
//    fp3_result_dump(result3, 2, 0);


    /* XXX Now do the diff against the metadata in the streams.
     *
     * Looks like there are occasional crashes in diff_stream().
     *
     * XXX It would appear we sometimes get here with no releases left!
     */
    printf("Calling diff_stream()\n");
    diff_stream(result3, ctxs);
    printf("Returned from diff_stream()\n");
//    exit(0);


    /* Cleanup
     */
    printf("Cleaning up...\n");
//    pool_free_pc(pc2);
//    acoustid_free(ac);
    ne_sock_exit();

    for (i = 0; i < argc - 1; i++) {
        fingersum_free(ctxs[i]);
        fclose(streams[i]);
    }
    free(ctxs);
    free(streams);

    printf("Thank you, call again!\n");
    return (EXIT_SUCCESS);
}
