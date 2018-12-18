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

#ifndef FINGERSUM_H
#define FINGERSUM_H 1

#ifdef __cplusplus
#  define FINGERSUM_BEGIN_C_DECLS extern "C" {
#  define FINGERSUM_END_C_DECLS   }
#else
#  define FINGERSUM_BEGIN_C_DECLS
#  define FINGERSUM_END_C_DECLS
#endif

FINGERSUM_BEGIN_C_DECLS

/**
 * @file fingersum.h
 * @brief Calculate the AccurateRip checksums and the Chromaprint
 *        fingerprint
 *
 * The fingersum module provide a thread-safe interface to read an
 * audio stream and calculate the AccurateRip checksums as well as the
 * Chromaprint acoustic fingerprint.  The results are calculated in a
 * single pass over the decoded audio samples and subsequently cached
 * in the opaque fingersum context, such that repeated invocations of
 * fingersum_get_checksum() or fingersum_get_fingerprint() do not
 * incur additional calculations or I/O.
 *
 * The fingersum_get_checksum() and fingerprint_get_fingerprint()
 * functions decode just enough data to perform their respective
 * tasks.  Since the Chromaprint fingerprint is calculated from the
 * leading part of the audio stream, it is usually finalised before
 * the checksum, which depends on every sample in the stream.
 *
 * XXX If the caller has allocated a Chromaprint context it may be
 * passed as @p cc; a value of @c NULL indicates that a context local
 * to the fingersum context will be used.  Why do we need this?
 *
 * XXX This paragraph is looking for a new home.  The checksum
 * algorithm ignores the first 2939 samples at the beginning of the
 * first track and the last 2940 samples at the end of the last track.
 * Therefore the CRC depends the order in which the tracks appear on
 * the disc.
 *
 * @note While the module is thread-safe, any particular context may
 *       only be operated on by a single thread at a time.
 *
 * @todo XXX Need to check for leaks, both when successful and all
 *       possible failure modes.  When resampling and when not!
 *
 * @todo XXX Run big test with fpcalc on taco.  It appears fpcalc
 *       cannot output the base-64 encoded URL-safe fingerprints;
 *       write the test script using the decode() function from neon.
 *       Report length of Chromaprint for the comment in acoustid.c.
 *
 * @todo XXX Implement a function to determine whether the underlying
 *       stream represents lossless audio CD data.  Must use lossless
 *       encoder, be 16-bit stereo at an integer multiple of 44.1 kHz
 *       and be an integer multiple of the frame size in length.
 *
 * @todo XXX Remove the "_get_" part from the getters.  Maybe not:
 *       they're present both in Chromaprint and libmusicbrainz.
 *
 * @todo XXX Rename the _free() functions to _delete(), as in C++?
 *
 * XXX Check this against
 * https://libav.org/doxygen/master/avcodec_8c_source.html#l00223 and
 * https://libav.org/doxygen/master/transcode__aac_8c_source.html#l00308
 *
 * References:
 * Chromaprint API:
 * https://bitbucket.org/acoustid/chromaprint/src/master/src/chromaprint.h
 *
 * AccurateRip at hydrogenaudio wiki:
 * http://wiki.hydrogenaud.io/index.php?title=AccurateRip
 *
 * AccurateRip CRC calculation by Spoon (including v2 and offset-finding):
 * http://forum.dbpoweramp.com/showthread.php?20641-AccurateRip-CRC-Calculation
 *
 * AccuraceRip CRC v2:
 * http://www.hydrogenaud.io/forums/index.php?showtopic=61468
 *
 * Efficiently calculate offset checksums:
 * http://jonls.dk/2009/10/calculating-accuraterip-checksums
 *
 * Secure ripping, overview
 * http://wiki.hydrogenaud.io/index.php?title=Secure_ripping
 * http://dbpoweramp.com/spoons-audio-guide-cd-ripping.htm
 */

#include <stdint.h>
#include <stdio.h>

#include <chromaprint.h>

#include "metadata.h"
#include "structures.h" // New addition, for multiple offsets [offset_list]


/**
 * @brief Allocate and initialise a fingersum context
 *
 * fingersum_new() creates a new context for checksumming and
 * fingerprinting the decoded audio data from the stream pointed to by
 * @p stream.  Upon successful completion a pointer to the fingersum
 * context is returned.  If an error occurs, fingersum_new() returns
 * @c NULL and sets the global variable @c errno to indicate the
 * error.  fingersum_new() will fail if:
 *
 * <dl>
 *
 *   <dt>@c ENOMSG</dt><dd>@p stream does not contain a suitable audio
 *   stream.  Audio must be either mono or stereo and have a finite
 *   duration.</dd>
 *
 *   <dt>@c EPROTO</dt><dd>A decoding error occurred</dd>
 *
 *   <dt>@c EPROTONOSUPPORT</dt><dd>The audio format is not supported,
 *   or cannot be resampled to interleaved, signed 16-bit samples</dd>
 *
 * </dl>
 *
 * Any other value of @c errno is due to failure in a function from
 * the standard library.
 *
 * @param stream Pointer to the stream of encoded audio data
 * @return       Pointer to an opaque fingersum context if successful, @c
 *               NULL otherwise.
 */
struct fingersum_context *
fingersum_new(FILE *stream);


/**
 * @brief Free a fingersum context and all its contents
 *
 * This function should not fail and does not set the external
 * variable @c errno.
 *
 * @param ctx Pointer to an opaque fingersum context
 */
void
fingersum_free(struct fingersum_context *ctx);


#if 0
/**
 * @brief Calculate the AccurateRip checksums
 *
 * The fingersum_get_checksum() function calculates the AccurateRip
 * checksum from all the samples in the audio data in the fingersum
 * context pointed to by @p ctx.  If successful and if @p checksum is
 * not @c NULL, the three checksums, corresponding the first,
 * intermediate, or last track, will be stored in the location it
 * points to.
 *
 * XXX Maybe three functions better for first, middle, and last?
 * Alternatively, a function called "match" with return value based on
 * whether it matched or not, and taking a pointer to a structure
 * which is populated with what version matched, wether sectors had to
 * be skipped in the beginning or the end, and what if any offset was
 * applied.
 *
 * XXX AccurateRip calls this thing a CRC
 *
 * @param ctx      Pointer to an opaque fingersum context
 * @param cc       The Chromaprint context, or @c NULL to use a
 *                 context local to @p ctx
 * @param checksum Pointer to a 3-long array of unsigned 32-bit
 *                 integers, corresponding to the first, intermediate,
 *                 and last track.
 * @return         0 if successful, -1 otherwise.  If an error occurs
 *                 during the audio decoding or fingerprint
 *                 calculation, the global variable @c errno is set to
 *                 @c EPROTO.  Other values of @c errno are due to
 *                 failures in standard library functions.
 */
int
fingersum_get_checksum(struct fingersum_context *ctx,
                       ChromaprintContext *cc,
                       uint32_t checksum[3]);
#endif


#if 0
/**
 * @brief Calculate the AccurateRip v2 checksums
 *
 * XXX
 */
int
fingersum_get_checksum2(struct fingersum_context *ctx,
                        ChromaprintContext *cc,
                        uint32_t checksum[3]);
#endif


/**
 * @brief Calculate the Chromaprint fingerprint
 *
 * The fingersum_get_fingerprint() function calculates the Chromaprint
 * fingerprint from the first samples of the audio data in the
 * fingersum context pointed to by @p ctx.  The length of the data
 * used for Chromaprint fingerprinting is set using the @p len
 * parameter to fingersum_new().  If successful and if @p fingerprint
 * is not @c NULL, a null-terminated string representation of the
 * compressed fingerprint will be stored at the location it points to.
 *
 * @param ctx         Pointer to an opaque fingersum context
 * @param cc          The Chromaprint context, or @c NULL to use a
 *                    context local to @p ctx
 * @param fingerprint Pointer to a null-terminated string of the
 *                    compressed Chromaprint fingerprint encoded in
 *                    base-64 with the URL-safe scheme, if successful.
 *                    The pointer may subsequently be used as an
 *                    argument to the function free(3).
 * @return            0 if successful, -1 otherwise.  If an error
 *                    occurs during the audio decoding or fingerprint
 *                    calculation, the global variable @c errno is set
 *                    to @c EPROTO.  Other values of @c errno are due
 *                    to failures in standard library functions.
 */
int
fingersum_get_fingerprint(struct fingersum_context *ctx,
                          ChromaprintContext *cc,
                          char **fingerprint);


/**
 * @brief Get the duration of the audio stream
 *
 * This, as well as fingersum_get_sectors() can be called (and should
 * return immediately) as soon as the context is initialiased (but see
 * the caveats regarding ctx->stream->duration in the .c file).
 *
 * XXX Maybe this returns an initial (approximate) estimate, while
 * fingersum_get_sectors() returns the proper value calculated after
 * traversing all the samples?
 *
 * @param ctx Pointer to an opaque fingersum context
 * @return    The duration of the audio stream in seconds, truncated
 *            to integer precision
 */
unsigned int
fingersum_get_duration(const struct fingersum_context *ctx);


/**
 * @brief Get the metadata of the audio stream
 *
 * @param ctx Pointer to an opaque fingersum context
 * @return    XXX
 */
struct metadata *
fingersum_get_metadata(const struct fingersum_context *ctx);


/**
 * @brief Get the duration of the audio stream in sectors
 *
 * @param ctx Pointer to an opaque fingersum context
 * @return    The number of sectors the audio stream would occupy on
 *            an audio CD, or -1 if the duration is not an integer
 *            multiple of the number of samples per sector
 */
long int
fingersum_get_sectors(const struct fingersum_context *ctx);


/**
 * @brief XXX This needs work!
 */
ssize_t
fingersum_diff(struct fingersum_context *ctx1, struct fingersum_context *ctx2);


/*** NEW STUFF ***/
#if 0
struct fingersum_result
{
    size_t nmemb;

    ssize_t *offsets;
};
#endif


#if 0
void
fingersum_result_free(struct fingersum_result *result);
#endif


#if 0
/**
 * @brief XXX This needs work!  Returns @c NULL for failure and
 * mismatch alike?  Need to use @c errno to distinguish between the
 * two.
 */
struct fingersum_result *
fingersum_check_checksum1(struct fingersum_context *leader,
                          struct fingersum_context *center,
                          struct fingersum_context *trailer,
                          uint32_t checksum);
#endif


#if 0
struct fingersum_result *
fingersum_check_checksum2(struct fingersum_context *leader,
                          struct fingersum_context *center,
                          struct fingersum_context *trailer,
                          uint32_t checksum);
#endif

struct fp3_offset_list *
fingersum_find_offset(const struct fingersum_context *ctx, uint32_t crc);

struct fp3_offset_list *
fingersum_find_offset_eac(const struct fingersum_context *ctx, uint32_t crc32);

int
fingersum_add_offset(struct fingersum_context *ctx, int32_t offset);

struct fp3_ar *
fingersum_get_result_3(struct fingersum_context *leader,
                       struct fingersum_context *center,
                       struct fingersum_context *trailer);

void
fingersum_dump(struct fingersum_context *ctx);

FINGERSUM_END_C_DECLS

#endif /* !FINGERSUM_H */
