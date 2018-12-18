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

#ifndef METADATA_H
#define METADATA_H 1

#ifdef __cplusplus
#  define METADATA_BEGIN_C_DECLS extern "C" {
#  define METADATA_END_C_DECLS   }
#else
#  define METADATA_BEGIN_C_DECLS
#  define METADATA_END_C_DECLS
#endif

METADATA_BEGIN_C_DECLS

/**
 * @file metadata.h
 * @brief XXX
 *
 *
 * @note An Metadata context must be accessed only by one thread at
 *       the time.
 *
 * References
 *
 * https://acoustid.org/webservice
 *
 * XXX There is a place on MusicBrainz where metadata mappings are
 * discussed; find it!
 */


/**
 * @brief XXX
 *
 * NULL if it is not there, anything else (including the "" string)
 * means it was observed.
 *
 * Stuff to add that may make sense:
 *
 *  musicbrainz release id [used by fingerquery]
 *  musicbrainz recording id [used by fingerquery]
 */
struct metadata
{
    /* Or release title in MusicBrainz-speak
     *
     * XXX Looks like there is no sort_album in MusicBrainz.
     */
    char *album;
    char *album_artist;
    char *artist;

    /* XXX Note that iTunes appears to have clarified its definition
     * of a compilation: (different artists required).  iTunes
     * 12.0.1.26 now says "Album is a compilation of songs by various
     * artists".  Perhaps it would be sensible to require the
     * MusicBrainz compilation flag to be set AND the release not to
     * have a well-defined artist (e.g. not "Various artists", or some
     * such); "Something for Everybody" should probably not be a
     * compilation.
     *
     * https://musicbrainz.org/doc/Release_Group/Type
     *
     * XXX This is either "0" or "1", but probably only the "1" is
     * checked?
     */
    char *compilation;
    char *composer;

    /* XXX This is a bit tricky still, would like it to be the
     * recording year!  Should probably be the date of the recording,
     * but this seems poorly populated in Musicbrainz.
     * 
     * http://forum.kodi.tv/showthread.php?tid=171592
     *
     * Morituri seems to be doing the right thing (TM).
     */
    char *date;

    /* XXX On the form MM/NN.  Should probably have been size_t[2]!
     */
    char *disc;
    char *sort_album_artist;
    char *sort_artist;
    char *sort_composer;

    /* XXX Looks like there is no sort_title in MusicBrainz.
     */
    char *title;

    /* XXX On the form MM/NN.  Should probably have been size_t[2]!
     */
    char *track;

    // XXX genre?!

    /* XXX for unknown keys (tags).  Could perhaps have a capacity
     * here as well?
     */
    char **unknown;
    size_t nmemb;
};


/**
 * @brief Allocate and initialise a metadata structure
 *
 * @return Pointer to an initialised metadata structure if successful.
 *         Otherwise, @c NULL is returned, and the global variable @c
 *         errno is set to indicate the error.
 */
struct metadata *
metadata_new();


/**
 * @brief Free a metadata structure and all its contents
 *
 * All non-NULL members of @p metadata will be used as arguments to
 * the function free(3).
 *
 * @param metadata Pointer to a metadata structure
 */
void
metadata_free(struct metadata *metadata);

void
metadata_clear(struct metadata *metadata);

void
metadata_dump(struct metadata *metadata);

METADATA_END_C_DECLS

#endif /* !METADATA_H */
