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

#include <stdio.h>

#include <limits.h>
#include <string.h>

#include <neon/ne_string.h>

#include "configuration.h"
#include "fingersum.h"


/* MusicBrainz disc identifiers are 28 characters long, see
 * https://musicbrainz.org/doc/Disc_ID_Calculation#Calculating_the_Disc_ID.
 */
#define LEN_DISCID 28


/* The _cfg2_next_discid() function returns a disc ID from @p
 * DiscList.  The returned ID is lexographically compared to @p
 * discid, and the function returns the smallest discid larger than @p
 * discid.  .  could be obsoleted in favour of this function.  DiscID
 * are lexographically compared
 *
 * @param DiscList XXX  Should have been Medium?
 * @param discid   Returned Disc ID has to be larger than this
 * @return         @c NULL if exhausted, has to be freed
 */
static char *
_cfg2_next_discid(Mb5DiscList DiscList, const char *discid)
{
    Mb5Disc Disc;
    ne_buffer *id_best, *id_curr, *id_temp;
    int i;

    id_best = NULL;
    id_curr = ne_buffer_ncreate(LEN_DISCID + 1);
    for (i = 0; i < mb5_disc_list_size(DiscList); i++) {
        Disc = mb5_disc_list_item(DiscList, i);
        if (Disc == NULL)
            continue;

        ne_buffer_grow(id_curr, mb5_disc_get_id(Disc, NULL, 0) + 1);
        mb5_disc_get_id(Disc, id_curr->data, id_curr->length);
        ne_buffer_altered(id_curr);

        if (discid != NULL && strcmp(id_curr->data, discid) <= 0)
            continue;
        
        if (id_best == NULL) {
            id_best = id_curr;
            id_curr = ne_buffer_ncreate(LEN_DISCID + 1);
        } else if (strcmp(id_curr->data, id_best->data) < 0) {
            id_temp = id_best;
            id_best = id_curr;
            id_curr = id_temp;
        }
    }

    ne_buffer_destroy(id_curr);
    if (id_best != NULL)
        return (ne_buffer_finish(id_best));
    return (NULL);
}


/* @param MediumList List of media to traverse
 * @param pos        One-based position of the sought medium in the release
 * @return           The medium at position @p position, or @c NULL,
 *                   if @p MediumList does not contain a medium at
 *                   position @p position.
 */
Mb5Medium
_cfg2_medium_at_position(Mb5MediumList MediumList, int pos)
{
    Mb5Medium Medium;
    int i;

    for (i = 0; i < mb5_medium_list_size(MediumList); i++) {
        Medium = mb5_medium_list_item(MediumList, i);
        if (Medium != NULL && mb5_medium_get_position(Medium) == pos)
            return (Medium);
    }

    return (NULL);
}


#if 0 // XXX Why is this not used?
static int
_cfg2_stream_compar(const void *a, const void *b)
{
    int ra, rb;

    ra = abs(((const struct _cfg2_stream *)a)->residual);
    rb = abs(((const struct _cfg2_stream *)b)->residual);

    if (ra < rb)
        return (-1);
    if (ra > rb)
        return (+1);
    return (0);
}
#endif


Mb5Track
_cfg2_track_at_position(Mb5TrackList TrackList, int pos)
{
    Mb5Track Track;
    int i;

    for (i = 0; i < mb5_track_list_size(TrackList); i++) {
        Track = mb5_track_list_item(TrackList, i);
        if (Track != NULL && mb5_track_get_position(Track) == pos)
            return (Track);
    }

    return (NULL);
}


/* The _cfg2_track_duration() function returns the duration, in
 * sectors, of the track at position @p pos on disc @p Disc.
 *
 * @param Disc XXX
 * @param pos  One-based position of the track on the medium
 * @return     Duration, in sectors, or negative on failure
 */
static int
_cfg2_track_duration(Mb5Disc Disc, int pos)
{
    Mb5Offset Offset;
    Mb5OffsetList OffsetList;
    int end, i, j, start;

    OffsetList = mb5_disc_get_offsetlist(Disc);
    if (OffsetList == NULL)
        return (-1);

    end = start = -1;
    for (i = 0; i < mb5_offset_list_size(OffsetList); i++) {
        Offset = mb5_offset_list_item(OffsetList, i);
        if (Offset == NULL)
            return (-1);

        j = mb5_offset_get_position(Offset);
        if (j == pos) {
            start = mb5_offset_get_offset(Offset);
            if (end >= 0)
                break;

        } else if (j == pos + 1) {
            end = mb5_offset_get_offset(Offset);
            if (start >= 0)
                break;
        }
    }

    if (start < 0)
        return (-1);
    if (end < 0)
        return (mb5_disc_get_sectors(Disc) - start);
    return (end - start);
}


/* Returns the first matching disc.  But there should be only one.
 */
Mb5Disc
_cfg2_disc_with_id(Mb5Medium Medium, const char *discid)
{
    Mb5Disc Disc;
    Mb5DiscList DiscList;
    ne_buffer *id;
    int i;

    DiscList = mb5_medium_get_disclist(Medium);
    if (DiscList == NULL)
        return (NULL);
    
    id = ne_buffer_ncreate(LEN_DISCID + 1);
    for (i = 0; i < mb5_disc_list_size(DiscList); i++) {
        Disc = mb5_disc_list_item(DiscList, i);
        if (Disc == NULL)
            continue;

        ne_buffer_grow(id, mb5_disc_get_id(Disc, NULL, 0) + 1);
        mb5_disc_get_id(Disc, id->data, id->length);
        ne_buffer_altered(id);
        
        if (strcmp(id->data, discid) == 0) {
            ne_buffer_destroy(id);
            return (Disc);
        }
    }

    ne_buffer_destroy(id);
    return (NULL);
}


/* XXX Only used once?!  Could be rewritten as _new() functions, and
 * then the _free() functions could free their argument pointer.
 */
static void
_cfg2_track_init(struct _cfg2_track *track)
{
    track->nmemb = 0;
    track->selected = 0;
    track->streams = NULL;
}


static void
_cfg2_medium_init(struct _cfg2_medium *medium)
{
    medium->discid_str = NULL;
    medium->n_tracks = 0;
    medium->tracks = NULL;
}


static void
_cfg2_cfg_init(struct _cfg2_cfg *cfg)
{
    cfg->n_media = 0;
    cfg->media = NULL;
}


static void
_cfg2_track_free(struct _cfg2_track *track)
{
    if (track->streams != NULL)
        free(track->streams);
}


static void
_cfg2_medium_free(struct _cfg2_medium *medium)
{
    size_t i;

    if (medium->discid_str != NULL)
        free(medium->discid_str);

    if (medium->tracks != NULL) {
        for (i = 0; i < medium->n_tracks; i++)
            _cfg2_track_free(medium->tracks + i);
        free(medium->tracks);
    }
}


static void
_cfg2_cfg_free(struct _cfg2_cfg *cfg)
{
    size_t i;

    if (cfg->media != NULL) {
        for (i = 0; i < cfg->n_media; i++)
            _cfg2_medium_free(cfg->media + i);
        free(cfg->media);
    }

    free(cfg); // XXX This breaks the pattern!
}


/* Insert a stream in the track such that streams are ordered by
 * increasing absolute residual.
 *
 * @return Zero on success, non-zero otherwise
 */
static int
_cfg2_track_insert_stream(struct _cfg2_track *track, size_t index, int residual)
{
    void *p;
    size_t i;


    /* Grow the array to accomodate one more stream.  Determine the
     * index i of the stream before which to insert.
     *
     * XXX This could have used a bisection algorithm instead.
     */
    p = realloc(
        track->streams, (track->nmemb + 1) * sizeof(struct _cfg2_stream));
    if (p == NULL)
        return (-1);
    track->streams = p;

    for (i = 0; i < track->nmemb; i++) {
        if (abs(residual) < abs(track->streams[i].residual))
            break;
    }


    /* Unless the stream will be appended, move subsequent streams to
     * create a gap.  Assign the values of the new stream.
     */
    if (i < track->nmemb) {
        memmove(track->streams + i + 1,
                track->streams + i,
                (track->nmemb - i) * sizeof(struct _cfg2_stream));
    }

    track->streams[i].index = index;
    track->streams[i].residual = residual;
    track->nmemb += 1;
    return (0);
}


/* Rather, what are the best streams (in order) for the track?
 *
 * May leave track in an inconsistent state on failure... nah, I don't
 * think so!
 *
 * @param Disc Can be @c NULL
 *
 * XXX This could possibly have been track_new()
 */
static int
_cfg2_track_assign(struct _cfg2_track *track,
                   Mb5Disc Disc,
                   Mb5Track Track,
                   struct fp3_release *release,
                   struct fingersum_context **ctxs)
{
    Mb5Recording Recording;
    ne_buffer *id;
    size_t i;
    long int dur_mb5, res;


    /* Get the sector duration of the track and the ID of its
     * corresponding recording.
     *
     * XXX There need not necessarily be a disc!
     */
    dur_mb5 = 0;
    if (Disc != NULL) {
        dur_mb5 = _cfg2_track_duration(Disc, mb5_track_get_position(Track));
        if (dur_mb5 < 0)
            dur_mb5 = 0; //return (-1);
    }

    Recording = mb5_track_get_recording(Track);
    if (Recording == NULL)
        return (-1);

    id = ne_buffer_ncreate(mb5_recording_get_id(Recording, NULL, 0) + 1);
    mb5_recording_get_id(Recording, id->data, id->length);
    ne_buffer_altered(id);


    /* For each stream matching the recording, calculate the duration
     * residual.
     *
     * Find all streams which match the recording corresponding to the
     * track and append them to the track's streams array.  Record the
     * index of the matching stream and the residual to the duration
     * of the corresponding track on the disc.
     */
    track->nmemb = 0;
    track->streams = NULL;
    for (i = 0; i < release->nmemb; i++) {
        if (fp3_recording_list_find_recording(
                release->streams[i], id->data) == NULL) {
            continue;
        }

        res = fingersum_get_sectors(ctxs[i]) - dur_mb5;
        if (res < 0)
            ; // XXX
        if (_cfg2_track_insert_stream(track, i, res) != 0) {
            ne_buffer_destroy(id);
            return (-1);
        }
    }
    ne_buffer_destroy(id);


    /* Select the stream with the smallest residual, or leave it
     * "unselected" if residuals cannot be calculated.
     */
    if (Disc == NULL)
        track->selected = track->nmemb;
    else
        track->selected = 0;

    return (0);
}


/* Create a DISC configuration by selecting the first disc for each
 * medium.
 *
 * The configuration is not complete, as track allocations or not set.
 * The configuration is not necessarily valid, but that is of course
 * moot.
 */
struct _cfg2_cfg *
_cfg2_first_disc_configuration(Mb5MediumList MediumList)
{
    Mb5DiscList DiscList;
    Mb5Medium Medium;
    Mb5Track Track;
    Mb5TrackList TrackList;
    struct _cfg2_cfg *cfg;
    struct _cfg2_medium *medium;
    void *p;
    int i, j, k, pos;


    cfg = malloc(sizeof(struct _cfg2_cfg));
    if (cfg == NULL)
        return (NULL);
    _cfg2_cfg_init(cfg);

    for (i = 0; i < mb5_medium_list_size(MediumList); i++) {
        Medium = _cfg2_medium_at_position(MediumList, i + 1);
        if (Medium == NULL)
            continue;

        DiscList = mb5_medium_get_disclist(Medium);
        if (DiscList == NULL)
            continue;

        TrackList = mb5_medium_get_tracklist(Medium);
        if (TrackList == NULL)
            continue;

        pos = mb5_medium_get_position(Medium);
        if (pos > cfg->n_media) {
            p = realloc(cfg->media, pos * sizeof(struct _cfg2_medium));
            if (p == NULL) {
                _cfg2_cfg_free(cfg);
                return (NULL);
            }

            cfg->media = p;
            for (j = cfg->n_media; j < pos; j++)
                _cfg2_medium_init(cfg->media + j);
            cfg->n_media = pos;
        }
        

        /* Initially, select the first disc for each medium, and
         * initialize the track list.
         */
        medium = cfg->media + pos - 1;
        medium->discid_str = _cfg2_next_discid(DiscList, NULL);
        medium->n_tracks = 0;
        medium->tracks = NULL;

//        printf("SET DISCID on medium %zd to ->%s<-\n", i, medium->discid_str);
        
        for (j = 0; j < mb5_track_list_size(TrackList); j++) {
            Track = _cfg2_track_at_position(TrackList, j + 1);
            if (Track == NULL)
                continue;

            pos = mb5_track_get_position(Track);
            if (pos > medium->n_tracks) {
                p = realloc(medium->tracks, pos * sizeof(struct _cfg2_track));
                if (p == NULL) {
                    _cfg2_cfg_free(cfg);
                    return (NULL);
                }

                medium->tracks = p;
                for (k = 0; k < pos; k++)
                    _cfg2_track_init(medium->tracks + k);
                medium->n_tracks = pos;
            }
        }
    }

    return (cfg);
}


int
_cfg2_next_disc_configuration(struct _cfg2_cfg *cfg, Mb5MediumList MediumList)
{
    Mb5Medium Medium;
    Mb5DiscList DiscList;
    size_t i, j;
    char *discid;

    for (i = 0; i < cfg->n_media; i++) {
        /* Don't even bother iterating through the discs for this
         * medium if there are no tracks assigned to it.
         *
         * Otherwise Weather Report's "Heavy Weather" takes forever...
         */
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            if (cfg->media[i].tracks[j].nmemb > 0)
                break;
        }

        if (j >= cfg->media[i].n_tracks)
                continue;

        Medium = _cfg2_medium_at_position(MediumList, i + 1);
        if (Medium == NULL)
            continue;

        DiscList = mb5_medium_get_disclist(Medium);
        if (DiscList == NULL)
            continue;

        discid = _cfg2_next_discid(DiscList, cfg->media[i].discid_str);
        if (discid != NULL) {
//            printf("INCREASED DISCID on medium %zd from ->%s<- to ->%s<-\n",
//                   i, cfg->media[i].discid_str, discid);
//            sleep(10);

            if (cfg->media[i].discid_str != NULL)
                free(cfg->media[i].discid_str);
            cfg->media[i].discid_str = discid;


            /* Reset the discid on all previous media to the first
             * discid.
             */
            while (i-- > 0) {
                Medium = _cfg2_medium_at_position(MediumList, i + 1);
                if (Medium == NULL)
                    continue;

                DiscList = mb5_medium_get_disclist(Medium);
                if (DiscList == NULL)
                    continue;

                discid = _cfg2_next_discid(DiscList, NULL);
                if (discid != NULL) {
                    if (cfg->media[i].discid_str != NULL)
                        free(cfg->media[i].discid_str);
                    cfg->media[i].discid_str = discid;
                }
            }
            
            return (0);
        }
    }

    return (-1); // EXHAUSTED
}


/* A configuration is valid if and only if each stream occurs at most
 * once.
 */
int
_cfg2_configuration_is_valid(const struct _cfg2_cfg *cfg)
{
    const struct _cfg2_track *track;
    size_t i, index, j, k, l;

    for (i = 0; i < cfg->n_media; i++) {
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            /* Skip the track if it does not have an assigned stream.
             */
            track = cfg->media[i].tracks + j;
            if (track->selected >= track->nmemb)
                continue;
            index = track->streams[track->selected].index;


            /* Check whether the index of the stream assigned to the
             * current track is also assigned to another track on the
             * same medium.
             */
            for (l = j + 1; l < cfg->media[i].n_tracks; l++) {
                track = cfg->media[i].tracks + l;
                if (track->selected >= track->nmemb)
                    continue;

                if (track->streams[track->selected].index == index)
                    return (0);
            }


            /* Check whether stream is assigned to tracks on
             * subsequent media.
             */
            for (k = i + 1; k < cfg->n_media; k++) {
                for (l = 0; l < cfg->media[k].n_tracks; l++) {
                    track = cfg->media[k].tracks + l;
                    if (track->selected >= track->nmemb)
                        continue;

                    if (track->streams[track->selected].index == index)
                        return (0);
                }
            }
        }
    }

    return (1);
}


void
_cfg2_dump_track_configuration(const struct _cfg2_cfg *cfg)
{
    struct _cfg2_track *track;
    size_t i, j;

    for (i = 0; i < cfg->n_media; i++) {
        printf("  Medium %02zd/%02zd:\n", i + 1, cfg->n_media);
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            track = cfg->media[i].tracks + j;

            printf("    Track %02zd/%02zd:",
                   j + 1, cfg->media[i].n_tracks);

            if (track->nmemb > 0 && track->selected < track->nmemb) {
#if 0 // Output "standard" format
                printf(" selected %02zd/%02zd stream %02zd residual %d",
                       track->selected + 1,
                       track->nmemb,
                       track->streams[track->selected].index,
                       track->streams[track->selected].residual);
#else // Output all candidate stream indices and their residuals
                size_t k;
                for (k = 0; k < track->nmemb; k++) {
                    printf(" %zd [%d]",
                           track->streams[k].index + 1,
                           track->streams[k].residual);
                }
#endif            
            } else {
                printf(" UNASSIGNED");
            }

            printf("\n");
        }
    }
}


void
_cfg2_dump_disc_configuration(const struct _cfg2_cfg *cfg)
{
    size_t i;

    for (i = 0; i < cfg->n_media; i++) {
        printf("  Medium %02zd/%02zd: %s\n",
               i + 1, cfg->n_media, cfg->media[i].discid_str);
    }
}


/*** XXX THIS IS WHERE WE'RE AT ***/
/* @return Zero if exhausted, non-zero if a step was taken
 */
static int
_cfg2_step_forward_new(struct _cfg2_cfg *cfg)
{
    struct _cfg2_track *track;
    size_t i, j;


    for (i = 0; i < cfg->n_media; i++) {
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            track = cfg->media[i].tracks + j;


            /* If the track can be increased without increasing the
             * residual, take the step and reset all previous steps.
             * This will count upwards.
             */
            if (track->selected + 1 < track->nmemb &&
                abs(track->streams[track->selected + 1].residual) <=
                abs(track->streams[track->selected + 0].residual)) {

                track->selected += 1;
                for (i += 1; i-- > 0; ) {
                    for (j += 1; j-- > 0; )
                        cfg->media[i].tracks[j].selected = 0;
                }

                return (1);
            }
        }
    }

    return (0);
}


#if 0
/* Apply the smallest step forward.
 *
 * Find the stream with the smallest increment
 *
 * @return The absolute residual increment of the step, or negative if
 * no more steps possible
 */
static int
_cfg2_step_forward(struct _cfg2_cfg *cfg)
{
    struct _cfg2_track *track, *track_min;
    size_t i, j;
    int r, r_best;

    size_t i_step, j_step; // XXX For diagnostics only

    
    r_best = INT_MAX;
    track_min = NULL;
    
    for (i = 0; i < cfg->n_media; i++) {
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            track = cfg->media[i].tracks + j;
        
            if (track->selected + 1 < track->nmemb) {
                r = abs(track->streams[track->selected + 1].residual) -
                    abs(track->streams[track->selected].residual);
                if (r < r_best) {
                    r_best = r;
                    track_min = track;

                    i_step = i;
                    j_step = j;
                }
            }
        }
    }

    if (track_min == NULL)
        return (-1); // EXHAUSTED

    printf("  ** FORWARD  STEP @ %02zd/%02zd: %02zd->%02zd/%02zd dr=%d\n",
           i_step + 1, j_step + 1,
           track_min->selected, track_min->selected + 1, track_min->nmemb,
           r_best);

    track_min->selected += 1;
    return (r_best);
}
#endif


#if 0
/* Take as many backward steps as possible (in order of decreasing
 * magnitude) so that the total distance still exceeds d.
 *
 * This cannot fail
 *
 * The total residual decrease of all backward steps will not exceed
 * r_max
 *
 * This cannot be "exhausted"--returns zero on NOP
 */
static int
_cfg2_step_backward(struct _cfg2_cfg *cfg, int r_max)
{
    struct _cfg2_medium *m;
    struct _cfg2_track *t, *t_max;
    size_t i, j;
    int r, r_best, r_tot;

    size_t i_step, j_step; // XXX For diagnostics only


    r_tot = 0;
    while (r_tot < r_max) {
        r_best = 0;
        t_max = NULL;

        for (i = 0; i < cfg->n_media; i++) {
            m = cfg->media + i;

            for (j = 0; j < m->n_tracks; j++) {
                t = m->tracks + j;

                if (t->selected > 0) {
                    r = abs(t->streams[t->selected].residual) -
                        abs(t->streams[t->selected - 1].residual);
                    if (r > r_best && r_tot + r < r_max) {
                        r_best = r;
                        t_max = t;

                        i_step = i;
                        j_step = j;
                    }
                }
            }
        }

        if (t_max == NULL)
            break; //return (-1);

        printf("  ** BACKWARD STEP @ %02zd/%02zd: %02zd->%02zd/%02zd dr=%d\n",
               i_step + 1, j_step + 1,
               t_max->selected, t_max->selected - 1, t_max->nmemb,
               r_best);

        t_max->selected -= 1;
        r_tot += r_best;
    }

    return (r_tot);
}
#endif


/* This generates the best configuration when the disc indices are set
 * to zero.
 *
 * For the given selection of discs for each medium, choose the best
 * valid selection of streams for each track.
 *
 * This should really be _best_track_configuration() or some such,
 * because it does not touch the disc configuration.
 *
 * This returns a valid configuration.  But the configuration is not
 * guaranteed to be unique.
 */
int
_cfg2_first_configuration(struct _cfg2_cfg *cfg,
                          Mb5MediumList MediumList,
                          struct fp3_release *release,
                          struct fingersum_context **ctxs)
{
    Mb5Disc Disc;
    Mb5Medium Medium;
    Mb5Track Track;
    Mb5TrackList TrackList;
//    struct _cfg2_cfg *cfg;
//    size_t index, k, l;
    size_t i, j;
    int r; //, r_new;


    /* XXX There may not be a disc, but there should always be a
     * medium and a track list.
     */
//    printf("FIRST CONFIGURATION MARKER #0\n");
    for (i = 0; i < cfg->n_media; i++) {
        Medium = _cfg2_medium_at_position(MediumList, i + 1);
        if (Medium == NULL)
            continue;

        Disc = _cfg2_disc_with_id(Medium, cfg->media[i].discid_str);
        if (Disc == NULL)
            ; //continue;

        TrackList = mb5_medium_get_tracklist(Medium);
        if (TrackList == NULL)
            continue;

        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            Track = _cfg2_track_at_position(TrackList, j + 1);
            if (Track == NULL)
                continue;

            if (_cfg2_track_assign(cfg->media[i].tracks + j,
                                   Disc,
                                   Track,
                                   release,
                                   ctxs) != 0) {
                return (-1);
            }
        }
    }


    /* Find the best valid configuration. Chose the next best
     * configuration
     *
     * XXX THIS IS COMPLETELY UNTESTED!  Needs a fair amount of
     * thought, still!  Test on Beach Boys, Weather Report, etc.
     *
     * Notes from manuscript:
     *
     *  (1) What's the smallest we can add?  Try to add all, in order
     *  of increasing magnitude, stop after first. XXX PROBLEM: WE MAY
     *  HAVE TO BACKTRACK.  How does counting really work?
     *
     *  (2) What's the largest we can remove?  Try to remove all, in
     *  order of decreasing magnitude.
     *
     * XXX CONTINUE HERE
     *
     * This is all changed, now.  Check the best (trivial)
     * configuration.  If this is a zero-residual configuration, check
     * all subsequent configuration that do not increase the
     * (absolute) residual until a valid configuration is found.  This
     * is therefore not guaranteed to be unique.
     */
#if 0 // DUMPER DUPLICATE -- REMOVE AT WILL
    printf("*** First configuration TRIVIAL CONFIGURATION ***\n");
    _cfg2_dump_track_configuration(cfg);
#endif

    r = _cfg2_residual_configuration(cfg);
    if (r != 0)
        return (-1); // XXX Only care about zero-residual configurations

    while (_cfg2_configuration_is_valid(cfg) == 0) {
        int r_tmp;

        printf("## r=%d\n", r); //# STRAYING INTO THE UNKNOWN ###\n");
//        sleep(1);

#if 0
        r_tmp = _cfg2_step_forward(cfg);
        if (r_tmp < 0) {
            printf("EXHAUSTED!\n");
            break;
        }
        r += r_tmp;

        r_tmp = _cfg2_step_backward(cfg, r_tmp);
        if (r_tmp < 0)
            ; // XXX Moving on...
        else {
            printf("      subtracting %d\n", r_tmp);
            r -= r_tmp;
        }
#else
        r_tmp = _cfg2_step_forward_new(cfg);
        printf("_cfg2_step_forward_new() returned %d\n", r_tmp);
        if (r_tmp == 0)
            return (-1); // XXX EXHAUSTED--should return NULL?
#endif

        printf("## r=%d [CHECK]\n", r); //# STRAYING INTO THE UNKNOWN ###\n");
        r = _cfg2_residual_configuration(cfg); // r_new;
    }

#if 0 // DUMPER
    printf("*** First configuration ***\n");
    _cfg2_dump_track_configuration(cfg);
#endif

    return (0);
}


/* This is really another scoring function.  A configuration is scored
 * based on its distance in duration to the release in MB.  Very
 * similar to _cfg2_first_configuration() above?
 *
 * XXX This is probably not so good...  Maybe should say that this
 * function only considers tracks where a stream has been selected?
 *
 * XXX Then we probably need another function to total the duration of
 * all the unassigned tracks.
 *
 * @return Total residual of the configuration
 */
long int
_cfg2_residual_configuration(const struct _cfg2_cfg *cfg)
{
    struct _cfg2_track *track;
    size_t i, j;
    long int residual;

    residual = 0;
    for (i = 0; i < cfg->n_media; i++) {
        for (j = 0; j < cfg->media[i].n_tracks; j++) {
            track = cfg->media[i].tracks + j;
            if (track->nmemb > 0 && track->selected < track->nmemb)
                residual += abs(track->streams[track->selected].residual);
        }
    }

    return (residual);
}
