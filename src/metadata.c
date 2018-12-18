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
#include <stdlib.h>

#include "metadata.h"


struct metadata *
metadata_new()
{
    struct metadata *metadata;

    metadata = malloc(sizeof(struct metadata));
    if (metadata == NULL)
        return (NULL);

    metadata->album = NULL;
    metadata->album_artist = NULL;
    metadata->artist = NULL;
    metadata->compilation = NULL;
    metadata->composer = NULL;
    metadata->date = NULL;
    metadata->disc = NULL;
    metadata->sort_album_artist = NULL;
    metadata->sort_artist = NULL;
    metadata->sort_composer = NULL;
    metadata->title = NULL;
    metadata->track = NULL;

    metadata->unknown = NULL;
    metadata->nmemb = 0;

    return (metadata);
}


void
metadata_clear(struct metadata *metadata)
{
    size_t i;

//    printf("  ... freeing #1 %p\n", metadata->album);
    if (metadata->album != NULL) {
        free(metadata->album);
        metadata->album = NULL;
    }
//    printf("  ... freeing #2 %p\n", metadata->album_artist);
    if (metadata->album_artist != NULL) {
        free(metadata->album_artist);
        metadata->album_artist = NULL;
    }
//    printf("  ... freeing #3 %p\n", metadata->artist);
    if (metadata->artist != NULL) {
        free(metadata->artist);
        metadata->artist = NULL;
    }
//    printf("  ... freeing #4 %p\n", metadata->compilation);
    if (metadata->compilation != NULL) {
        free(metadata->compilation);
        metadata->compilation = NULL;
    }
//    printf("  ... freeing #5 %p\n", metadata->composer);
    if (metadata->composer != NULL) {
        free(metadata->composer);
        metadata->composer = NULL;
    }
//    printf("  ... freeing #6 %p\n", metadata->date);
    if (metadata->date != NULL) {
        free(metadata->date);
        metadata->date = NULL;
    }
//    printf("  ... freeing #7 %p\n", metadata->disc);
    if (metadata->disc != NULL) {
        free(metadata->disc);
        metadata->disc = NULL;
    }
//    printf("  ... freeing #8 %p\n", metadata->album_artist);
    if (metadata->sort_album_artist != NULL) {
        free(metadata->sort_album_artist);
        metadata->sort_album_artist = NULL;
    }
//    printf("  ... freeing #9 %p\n", metadata->sort_artist);
    if (metadata->sort_artist != NULL) {
        free(metadata->sort_artist);
        metadata->sort_artist = NULL;
    }
//    printf("  ... freeing #11 %p\n", metadata->sort_composer);
    if (metadata->sort_composer != NULL) {
        free(metadata->sort_composer);
        metadata->sort_composer = NULL;
    }
//    printf("  ... freeing #11 %p\n", metadata->title);
    if (metadata->title != NULL) {
        free(metadata->title);
        metadata->title = NULL;
    }
//    printf("  ... freeing #12 %p\n", metadata->track);
    if (metadata->track != NULL) {
        free(metadata->track);
        metadata->track = NULL;
    }
//    printf("  ... freeing #13 %p\n", metadata->unknown);
    if (metadata->unknown != NULL) {
        for (i = 0; i < metadata->nmemb; i++) {
            free(metadata->unknown[i]);
            metadata->unknown[i] = NULL;
        }
        free(metadata->unknown);
    }
//    printf("  ... freeing #14\n");
}


void
metadata_free(struct metadata *metadata)
{
    metadata_clear(metadata);
    free(metadata);
}


/* Order metadata as in iTunes 12.1.0.50, except sort names appear
 * immediately after their "unsorted" ditto.
 */
void
metadata_dump(struct metadata *metadata)
{
    size_t i;

    /* "song name" in iTunes
     */
    if (metadata->title != NULL)
        printf("            Title: %s\n", metadata->title);

    /* "artist" and "sort as" in iTunes
     */
    if (metadata->artist != NULL) {
        printf("           Artist: %s\n", metadata->artist);
        if (metadata->sort_artist != NULL)
            printf("      Sort artist: %s [score]\n", metadata->sort_artist);
    }

    /* "composer" and "sort as" in iTunes
     */
    if (metadata->composer != NULL) {
        printf("         Composer: %s [result ID, a.k.a. fingerprint ID]\n", metadata->composer);
        if (metadata->sort_composer != NULL) 
            printf("    Sort composer: %s\n", metadata->sort_composer);
    }

    /* "year" in iTunes
     */
    if (metadata->date != NULL)
        printf("             Date: %s [release ID]\n", metadata->date);

    /* "album" in iTunes
     */
    if (metadata->album != NULL)
        printf("            Album: %s [medium title]\n", metadata->album);

    /* "album artist" and "sort as" in iTunes
     */
    if (metadata->album_artist != NULL) {        
        printf("     Album artist: %s\n", metadata->album_artist);
        if (metadata->sort_album_artist != NULL)
            printf("Sort album artist: %s\n", metadata->sort_album_artist);
    }

    /* "disc number" in iTunes
     */
    if (metadata->disc != NULL)
        printf("             Disc: %s\n", metadata->disc);

    /* "track" in iTunes
     */
    if (metadata->track != NULL)
        printf("            Track: %s\n", metadata->track);

    /* "compilation [checkbox] Album is compilation of songs by
     * various artists" in iTunes
     */
    if (metadata->compilation != NULL)
        printf("      Compilation: %s [recording ID]\n", metadata->compilation);

    /* This does not exist in iTunes.
     */
    for (i = 0; i < metadata->nmemb; i++)
        printf("          Unknown: %s\n", metadata->unknown[i]);
}
