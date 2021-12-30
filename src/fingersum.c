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

#include <sys/stat.h>

#include <iconv.h> // XXX WIP
#include <pthread.h>

#define USE_CRC32 1

#ifdef USE_CRC32 // XXX WIP
#  include <zlib.h>
#endif

#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "fingersum.h"


/* Length of the audio data used for Chromaprint fingerprint
 * calculation, in seconds
 *
 * XXX Sort out where the magic 120 comes from!  The default length
 * for Chromaprint's fpcalc example program is 120 seconds.
 */
#define CHROMAPRINT_LEN 120


/* Must be at least 5 * 588.  Bit of a misnomer; max offset is
 * actually 1024?
 */
#define MAX_OFFSET (5 * 588 + 1024)

#define MAX_OFFSET_2 1024


/* XXX Think about these lengths, 5 * 558 - 1 or just 5 * 558?  (5 *
 * 588 - 1) samples skipped in the leader, (5 * 588) samples in the
 * trailer.
 */
struct _fingersum_checksum_v1
{
    /* Number of processed samples
     */
    size_t nmemb;

    uint64_t offset_array_1[MAX_OFFSET - 1]; // Leading array 1: data
    uint64_t offset_array_2[MAX_OFFSET - 1]; // Leading array 2: i * data

    uint64_t offset_test_1; // central counter 1: sum(data)
    uint64_t offset_test_2; // central counter 2: sum(i * data)

    uint64_t offset_array_3[MAX_OFFSET]; // Trailing array 1: data
    uint64_t offset_array_4[MAX_OFFSET]; // Trailing array 2: i * data
};


struct _fingersum_checksum_v2
{
    /* Number of processed samples
     */
    size_t nmemb;

    uint64_t offset_array_1[MAX_OFFSET - 1]; // Leading array 1: data
    uint64_t offset_array_2[MAX_OFFSET - 1]; // Leading array 2: i * data

    uint64_t offset_test_1; // central counter 1: sum(data)
    uint64_t offset_test_2; // central counter 2: sum(i * data)

    uint64_t offset_array_3[MAX_OFFSET]; // Trailing array 1: data
    uint64_t offset_array_4[MAX_OFFSET]; // Trailing array 2: i * data
};


/* XXX What we need:
 *
 * Ability to skip first 5 * 588 +/- offset samples
 *
 * Ability to skip last  5 * 588 +/- offset samples
 *
 * Ability to add last offset samples from previous track, starting at 1
 *
 * Ability to skip last offset samples from current track
 */
struct _fingersum_offset
{
    /* XXX Or is this better as an int64_t or something?  Must be
     * signed, because offsets can be positive and negative.  Maybe
     * even int32_t owing to the limitation of the offset?
     */
    int32_t offset;

    /* See below: lead-in, bulk, lead-out.  For both v1 and v2:
     *
     * 0: checksum of first (5 * 588 - offset) samples, indexed from
     * one.  These are not counted if the track is first on the disc.
     *
     * 1: The middle samples, everything between 0 and 2, indexed by
     * natural index.
     *
     * 2: Checksum of last (5 * 588 - offset) samples, indexed from
     * number of sample.  These are not counted if the track is last
     * on the disc.
     *
     * 3: Last offset samples, indexed from 1.  These may need to be
     * added to the next track if the offset is negative (XXX or is it
     * positive?!)  NO -- THIS IS GONE, NOW!
     */
    uint32_t checksum_v1[3];
    uint32_t checksum_v2[3];

#ifdef USE_CRC32 // XXX WIP
    uLong crc32[3];
    uLong crc32_skip_zero;
#endif
};


/* Opaque fingersum context
 */
struct fingersum_context
{
    /* AccurateRip checksums assuming the track is the first
     * (lead-in), middle (bulk), or last (lead-out)
     *
     * Checksum of the first five sectors, all the middle frames, XXX
     * Comment not done.  Must know which track is first and which is
     * last for the checksum to work!
     *
     * The first and last sectors of the *disc* are ignored.
     */
//    uint32_t checksum_v1[3];
//    uint32_t checksum_v2[3];

    /* Resampling context
     *
     * This member will be @c NULL if no resampling context is
     * required.  If present, it must be released in fingersum_free().
     */
    struct SwrContext *swr_ctx;

    /* Decoder
     *
     * XXX Is this really freed?  What about avcodec_free_context()?
     */
    AVCodecContext *avcc;

    /* Libav input context
     *
     * The input context must be released in fingersum_free().  XXX
     * What about avcodec_close()?
     */
    AVFormatContext *ic;

    /* Libav buffer for decoded audio data
     *
     * The audio buffer must be released in fingersum_free().
     */
    AVFrame *frame;

    /* Best-matching Libav stream structure
     *
     * The stream is part of the input context and is determined in
     * fingersum_new().  It is included here for convenience.  It will
     * be released with ic in fingersum_free().  XXX Verify with
     * Valgrind that this actually is the case.
     */
    AVStream *stream;

    /* The Chromaprint context
     *
     * This member will be @c NULL if an external Chromaprint context
     * is used.  If non-null, it must be released in fingersum_free().
     */
    ChromaprintContext *cc;

    /* The calculated Chromaprint fingerprint, compressed and encoded
     * in base-64 with the URL-safe scheme, or @c NULL if it not yet
     * available
     *
     * This null-terminated string is directly suitable for the
     * AcoustID service
     */
    void *fingerprint;

    /* Number of samples required to complete the Chromaprint
     * fingerprint
     */
    uint64_t remaining;

    /* Number of processed samples
     *
     * Total number of samples seen so far for the AccurateRip's CRC
     * calculation XXX Maybe better named pos like in fpos()?
     *
     * Unlike the AccurateRip code, this is counting all channels like
     * "remaining" above.
     */
    uint64_t samples_tot;

    /* XXX PLAYGROUND! */
//    struct _fingersum_checksum_v1 v1;
//    struct _fingersum_checksum_v2 v2;

    /* The offset sum, sum(i * v)
     *
     * XXX Really, no need to keep two arrays; can use the front and
     * the back as in Jon's math.
     */
    uint32_t sof[2 * MAX_OFFSET_2 + 588 + 1];

    /* The straight sum, sum(v)
     */
    uint32_t sum[2 * MAX_OFFSET_2 + 588 + 1];

    uLong crc32_off[2 * 5 * 588 + 1];

//    uint64_t prutt_1;
//    uint64_t prutt_2;
//    uint64_t prutt_3;
//    uint64_t prutt_4;

    struct _fingersum_offset *offsets;
    size_t nmemb;

    /* The raw, first max_offset samples.  These may need to be added
     * to the previous track at an offset which depends on the length
     * of the previous track.
     *
     * XXX Since 2018-12-10: (2 * 5 * 588 + 1) first samples and (2 *
     * 5 * 588 + 1) last samples, respectively.  So max_offset = (2 *
     * 5 * 588 + 1), fixed (but it now means the range of offsets
     * supported).
     */
    uint32_t *samples;
    uint32_t *samples_end;
    uint64_t max_offset;
};


/* Global mutex to ensure that non-thread-safe Chromaprint and Libav
 * code runs in serial.
 */
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;


/* The _init() function performs one-time initialisation of Libav.
 * The function is thread-safe and can be called multiple times.
 *
 * @return 0 if successful, -1 otherwise.  If an error occurs, the
 *         global variable @c errno is set to indicate the error.
 */
static int
_init()
{
    static int initialised = 0;

    if (pthread_mutex_lock(&_mutex) != 0)
        return (-1);

    if (initialised == 0) {
#if LIBAVFORMAT_VERSION_MAJOR < 58
        /* av_register_all() was made a NOP and deprecated in FFmpeg
         * 4.0, but needs to be called for older versions.
         */
        av_register_all();
#endif
        av_log_set_level(AV_LOG_ERROR);
        initialised = 1;
    }

    if (pthread_mutex_unlock(&_mutex) != 0)
        return (-1);

    return (0);
}


/* fread()-equivalent for AVIOContext.
 */
static int
_read(void *opaque, unsigned char *buf, int buf_size)
{
    FILE *stream;
    int ret;

    stream = opaque;
    ret = fread(buf, 1, buf_size, stream);
    if (ret != buf_size && ferror(stream))
        return (-1);
    return (ret);
}


/* fseek()-equivalent for AVIOContext.
 */
static int64_t
_seek(void *opaque, int64_t offset, int whence)
{
    struct stat sb;
    FILE *stream;

    stream = opaque;
    if (whence == AVSEEK_SIZE) {
        if (fstat(fileno(stream), &sb) != 0)
            return (-1);
        return (sb.st_size);
    }

    if (fseek(stream, offset, whence) != 0)
        return (-1);
    return (ftello(stream));
}


struct fingersum_context *
fingersum_new(FILE *stream)
{
    AVCodec *decoder;
    AVDictionary *options;
    AVDictionaryEntry *option;
    struct fingersum_context *ctx;
    void *buffer;
    int64_t channel_layout;
    int stream_index;


    /* Initialise the module, allocate the fingersum context and
     * initialise it such that fingersum_free() can be called at any
     * time from now.
     */
    if (_init() != 0)
        return (NULL);

    ctx = malloc(sizeof(struct fingersum_context));
    if (ctx == NULL)
        return (NULL);

    ctx->swr_ctx = NULL;
    ctx->avcc = NULL;
    ctx->ic = NULL;
    ctx->cc = NULL;
    ctx->frame = NULL;
    ctx->fingerprint = NULL;


    /* Open the data stream and return with EPROTONOSUPPORT in case of
     * failure.  This function will fail if Libav has not been
     * initialised properly.
     *
     * XXX Buffer should be blocksize if fixed by protocol.  Otherwise
     * a cache page (e.g. 4 kB).  A future project may be to measure
     * performance as a function of blocksize on different platforms.
     * Free with av_freep(), or will it be released by the other
     * functions--check with Valgrind?  What's the Libav default?
     *
     * XXX Is ctx->ic->pb also automagically released?  Check with
     * Valgrind.
     */
    ctx->ic = avformat_alloc_context();
    if (ctx->ic == NULL) {
        fingersum_free(ctx);
        errno = ENOMEM;
        return (NULL);
    }
    buffer = av_malloc(4 * 1024);
    if (buffer == NULL) {
        fingersum_free(ctx);
        errno = ENOMEM;
        return (NULL);
    }
    ctx->ic->pb = avio_alloc_context(
        buffer, 4 * 1024, 0, stream, _read, NULL, _seek);
    if (ctx->ic->pb == NULL) {
        av_freep(buffer);
        fingersum_free(ctx);
        errno = ENOMEM;
        return (NULL);
    }

    options = NULL;
    if (av_dict_set(&options, "export_all", "1", 0) < 0) {
        fingersum_free(ctx);
        errno = EPROTONOSUPPORT;
        return (NULL);
    }
    if (avformat_open_input(&ctx->ic, NULL, NULL, &options) != 0) {
        fingersum_free(ctx);
        errno = EPROTONOSUPPORT;
        return (NULL);
    }
    option = NULL;
    while (av_dict_get(options, "", option, AV_DICT_IGNORE_SUFFIX) != NULL) {
        /* XXX Oddly option appears to be NULL with flac-files from
         * XLD on vindaloo, and av_dict_get() continues to return
         * non-NULL!
         */
        if (option == NULL)
            break;
        fprintf(stderr,
                "Option %s not recognised by the demuxer.\n", option->key);
    }


    /* Locate an audio stream and ensure that it contains either
     * sensible mono or stereo data.  Return with ENOMSG in case of
     * failure.
     *
     * An alternative may be to calculate the duration as
     *
     *   ctx->ic->duration / AV_TIME_BASE
     *
     * but that would lead to problems later on.
     */
    if (avformat_find_stream_info(ctx->ic, NULL) < 0) {
        fingersum_free(ctx);
        errno = ENOMSG;
        return (NULL);
    }

    stream_index = av_find_best_stream(
        ctx->ic, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (stream_index < 0) {
        fingersum_free(ctx);
        errno = ENOMSG;
        return (NULL);
    }
    if (decoder == NULL) {
        fingersum_free(ctx);
        errno = ENOTSUP;
        return (NULL);
    }
    ctx->stream = ctx->ic->streams[stream_index];

    if (ctx->stream->duration == AV_NOPTS_VALUE ||
        ctx->stream->duration <= 0 ||
        ctx->stream->time_base.den <= 0 ||
        ctx->stream->time_base.num <= 0) {
        fingersum_free(ctx);
        errno = ENOMSG;
        return (NULL);
    }


    /* Open the decoder and request interleaved, signed 16-bit
     * samples.  Return with EPROTO in case of failure.  Note that
     * avcodec_open2() is not thread-safe.
     */
    ctx->avcc = avcodec_alloc_context3(decoder);
    if (ctx->avcc == NULL) {
        errno = ENOMEM;
        return (NULL);
    }
    if (avcodec_parameters_to_context(ctx->avcc, ctx->stream->codecpar) != 0) {
        errno = EPROTO;
        return (NULL);
    }
    ctx->avcc->request_sample_fmt = AV_SAMPLE_FMT_S16;
    if (pthread_mutex_lock(&_mutex) != 0) {
        fingersum_free(ctx);
        return (NULL);
    }
    if (avcodec_open2(ctx->avcc, decoder, NULL) < 0) {
        fingersum_free(ctx);
        pthread_mutex_unlock(&_mutex);
        errno = EPROTO;
        return (NULL);
    }
    if (pthread_mutex_unlock(&_mutex) != 0) {
        fingersum_free(ctx);
        return (NULL);
    }

    printf("Have %d channels at %d\n",
           ctx->avcc->channels, ctx->avcc->sample_rate);
    if (ctx->avcc->channels < 1 ||
        ctx->avcc->channels > 2 ||
        ctx->avcc->sample_rate <= 0) {
        fingersum_free(ctx);
        errno = EPROTO;
        return (NULL);
    }


    /* Allocate and initialise an audio converter, if data cannot be
     * natively provided as interleaved, signed 16-bit samples.  This
     * will fail with EPROTONOSUPPORT if the converter could not be
     * initialised.
     */
    if (ctx->avcc->sample_fmt != AV_SAMPLE_FMT_S16) {
        channel_layout = ctx->avcc->channel_layout;
        if (channel_layout == 0)
            channel_layout = av_get_default_channel_layout(ctx->avcc->channels);

        ctx->swr_ctx = swr_alloc();
        if (ctx->swr_ctx == NULL) {
            fingersum_free(ctx);
            errno = ENOMEM;
            return (NULL);
        }

        av_opt_set_int(
            ctx->swr_ctx, "in_channel_layout", channel_layout, 0);
        av_opt_set_sample_fmt(
            ctx->swr_ctx, "in_sample_fmt", ctx->avcc->sample_fmt, 0);
        av_opt_set_int(
            ctx->swr_ctx, "in_sample_rate", ctx->avcc->sample_rate, 0);

        av_opt_set_int(
            ctx->swr_ctx, "out_channel_layout", channel_layout, 0);
        av_opt_set_sample_fmt(
            ctx->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
        av_opt_set_int(
            ctx->swr_ctx, "out_sample_rate", ctx->avcc->sample_rate, 0);

        if (swr_init(ctx->swr_ctx) < 0) {
            fingersum_free(ctx);
            errno = EPROTONOSUPPORT;
            return (NULL);
        }
    }


    /* Allocate a reusable frame for the decoded audio data.
     */
    ctx->frame = av_frame_alloc(); // avcodec_alloc_frame();
    if (ctx->frame == NULL) {
        fingersum_free(ctx);
        errno = ENOMEM;
        return (NULL);
    }


    /* Initialise the rest of the structure for bookkeeping.
     */
//    ctx->checksum_v1[0] = 0;
//    ctx->checksum_v1[1] = 0;
//    ctx->checksum_v1[2] = 0;
//    ctx->checksum_v2[0] = 0;
//    ctx->checksum_v2[1] = 0;
//    ctx->checksum_v2[2] = 0;
    ctx->remaining =
        CHROMAPRINT_LEN * ctx->avcc->channels * ctx->avcc->sample_rate;
    ctx->samples_tot = 0;


    /* XXX PLAYGROUND! */
    {
        size_t i;

//        ctx->v1.offset_test_1 = 0;
//        ctx->v1.offset_test_2 = 0;

//        ctx->v2.offset_test_1 = 0;
//        ctx->v2.offset_test_2 = 0;

//        for (i = 0; i < MAX_OFFSET - 1; i++) {
//            ctx->v1.offset_array_1[i] = 0;
//            ctx->v1.offset_array_2[i] = 0;

//            ctx->v1.offset_array_3[i] = 0;
//            ctx->v1.offset_array_4[i] = 0;

//            ctx->v2.offset_array_1[i] = 0;
//            ctx->v2.offset_array_2[i] = 0;

//            ctx->v2.offset_array_3[i] = 0;
//            ctx->v2.offset_array_4[i] = 0;
//        }

//        ctx->v1.offset_array_3[MAX_OFFSET - 1] = 0;
//        ctx->v1.offset_array_4[MAX_OFFSET - 1] = 0;

//        ctx->v2.offset_array_3[MAX_OFFSET - 1] = 0;
//        ctx->v2.offset_array_4[MAX_OFFSET - 1] = 0;

//        ctx->prutt_1 = 0;
//        ctx->prutt_2 = 0;
//        ctx->prutt_3 = 0;
//        ctx->prutt_4 = 0;

        for (i = 0; i < 2 * MAX_OFFSET_2 + 588 + 1; i++) {
            ctx->sof[i] = 0;
            ctx->sum[i] = 0;
        }

        for (i = 0; i < 2 * 5 * 588 + 1; i++)
            ctx->crc32_off[i] = crc32(0, Z_NULL, 0);

        ctx->offsets = NULL;
        ctx->nmemb = 0;
        ctx->samples = NULL;
        ctx->samples_end = NULL;
        ctx->max_offset = 0;
    }

//    struct fp3_ar *foo;
//    printf("sizeof() = %zd, sizeof() = %zd\n",
//           sizeof(struct fingersum_checksum),
//           sizeof(((struct fp3_ar *)NULL)->checksums[0]));

    return (ctx);
}


/* Ignore any pthread-related errors, because there is nothing that
 * can be done about them here.  Note that chromaprint_free() is not
 * thread-safe if Chromaprint was compiled with FFTW.
 */
void
fingersum_free(struct fingersum_context *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->fingerprint != NULL)
        chromaprint_dealloc(ctx->fingerprint);

    if (ctx->cc != NULL && pthread_mutex_lock(&_mutex) == 0) {
        chromaprint_free(ctx->cc);
        pthread_mutex_unlock(&_mutex);
    }

    if (ctx->frame != NULL)
        av_frame_free(&ctx->frame); // avcodec_free_frame(&ctx->frame);

    if (ctx->swr_ctx != NULL)
        swr_free(&ctx->swr_ctx);

    if (ctx->ic != NULL)
        avformat_close_input(&ctx->ic);


    /* XXX PLAYGROUND! */
    {
        if (ctx->offsets != NULL)
            free(ctx->offsets);

        if (ctx->samples != NULL)
            free(ctx->samples);

        if (ctx->samples_end != NULL)
            free(ctx->samples_end);
    }

    free(ctx);
}


/* XXX PLAYGROUND! */

/* This should probably rewind (reset the file position); whenever the
 * list of offsets changes, the checksums must be restarted.  Only add
 * the offset if it is not already in the list.
 */
int
fingersum_add_offset(struct fingersum_context *ctx, int32_t offset)
{
    void *p;
    size_t i;


//    printf("pre-checking %zd offsets for %d...\n", ctx->nmemb, offset);

    for (i = 0; i < ctx->nmemb; i++) {
        if (ctx->offsets[i].offset == offset)
            return (0);
    }

//    printf("reallocating pointer %p\n", ctx->offsets);

    p = realloc(
        ctx->offsets, (ctx->nmemb + 1) * sizeof(struct _fingersum_offset));
    if (p == NULL)
        return (-1);

    ctx->offsets = p;
    ctx->offsets[ctx->nmemb].offset = offset;

    ctx->offsets[ctx->nmemb].checksum_v1[0] = 0;
    ctx->offsets[ctx->nmemb].checksum_v1[1] = 0;
    ctx->offsets[ctx->nmemb].checksum_v1[2] = 0;

    ctx->offsets[ctx->nmemb].checksum_v2[0] = 0;
    ctx->offsets[ctx->nmemb].checksum_v2[1] = 0;
    ctx->offsets[ctx->nmemb].checksum_v2[2] = 0;

#ifdef USE_CRC32 // XXX WIP
    ctx->offsets[ctx->nmemb].crc32[0] = crc32(0, Z_NULL, 0);
    ctx->offsets[ctx->nmemb].crc32[1] = crc32(0, Z_NULL, 0);
    ctx->offsets[ctx->nmemb].crc32[2] = crc32(0, Z_NULL, 0);

    ctx->offsets[ctx->nmemb].crc32_skip_zero = crc32(0, Z_NULL, 0);
#endif

    ctx->nmemb += 1;

//    printf("post-add dump:\n");
//    fingersum_dump(ctx);


    /* XXX Check the documentation of this one!
     */
    if (av_seek_frame(ctx->ic, ctx->stream->index, 0, 0) < 0)
        return (-1);

    ctx->samples_tot = 0;

    return (0);
}


/* The _decode_frame() function extracts the next frame from the
 * fingersum context pointed to by @p ctx and returns a pointer to the
 * decoded interleaved, signed 16-bit samples in @p *data.  If no
 * resampling is required @p *data will be freed if non-NULL on entry
 * and set to point to a buffer internal to Libav on return.  If
 * resampling is required, _decode_frame() must be called with @p
 * *data pointing to a buffer of size @p *size allocated by the
 * caller.  If @p *data is @c NULL or not big enough to accommodate
 * the decoded frame, it will be reallocated inside _decode_frame().
 *
 * @param data Double pointer to interleaved, signed 16-bit samples.
 *             If the buffer was allocated within _decode_frame() @p
 *             *data will be non-Null on successful return and must be
 *             freed with av_freep().  XXX IS THIS INPUT OR OUTPUT?
 * @param size Size of @p *data buffer or 0 if @p *data points to a
 *             buffer internal to Libav.  If the buffer was allocated
 *             within _decode_frame(), @p *size will be >0 on
 *             successful return, and @p *data may subsequently be
 *             used as an argument to the function av_freep().
 * @return     The total number of decoded samples, or 0 when the end
 *             of the stream has been reached.  The function returns
 *             -1 in case of failure and sets @c errno to @c ENOMEM
 *             and @c EPROTO in the case of memory exhaustion and
 *             audio decoding errors, respectively.  XXX CHANNEL BUSINESS HERE!  It returns the number of "mono samples", but it would probably be more convenient if it returned the number of samples without taking the number of channels into account.
 */
static int
_decode_frame(struct fingersum_context *ctx, uint8_t **data, int *size)
{
    AVPacket packet;
    int linesize;


    /* Get the next packet from the appropriate stream and decode it.
     */
gimme_another:
//    printf("HATTNE entering _decode_frame() #0\n");
    for ( ; ; ) {
        if (av_read_frame(ctx->ic, &packet) < 0)
            return (0);
        if (packet.stream_index == ctx->stream->index) {
            if (avcodec_send_packet(ctx->avcc, &packet) != 0) {
                av_packet_unref(&packet);
                errno = EPROTO;
                return (-1);
            }
            av_packet_unref(&packet);
            break;
        }
        av_packet_unref(&packet);
    }

//    printf("HATTNE entering _decode_frame() #1\n");
    int ret = avcodec_receive_frame(ctx->avcc, ctx->frame);
    if (ret == AVERROR(EAGAIN))
        goto gimme_another;
    if (ret != 0) {
        printf("avcodec_recieve_frame() said %d vs %d: '%s'\n", ret, AVERROR(EAGAIN), av_err2str(ret));
        errno = EPROTO;
        return (-1);
    }


    /* If resampling is not required, release any externally allocated
     * buffer and return a pointer to the frame's data.
     */
    if (ctx->swr_ctx == NULL) {
        /* XXX Note to self: flac files, both from morituri and XLD on
         * vindaloo, go here.
         */
        if (*data != NULL && *size > 0) {
            av_freep(data);
            *size = 0;
        }
        *data = ctx->frame->data[0];
        return (ctx->frame->nb_samples * ctx->avcc->channels);
    }


    /* Grow the output buffer if necessary before converting it to
     * interleaved, signed 16-bit samples.
     */
    if (*data == NULL || ctx->frame->nb_samples > *size) {
        if (*data != NULL)
            av_freep(data);
        if (av_samples_alloc(
                data, &linesize, ctx->avcc->channels,
                ctx->frame->nb_samples, AV_SAMPLE_FMT_S16, 1) < 0) {
            *data = NULL;
            *size = 0;
            errno = ENOMEM;
            return (-1);
        }
        *size = ctx->frame->nb_samples;
    }

    if (swr_convert(
            ctx->swr_ctx,
            data, ctx->frame->nb_samples,
            (const uint8_t **)ctx->frame->data, ctx->frame->nb_samples) < 0) {
        errno = EPROTO;
        return (-1);
    }

    return (ctx->frame->nb_samples * ctx->avcc->channels);
}


/* The _feed_checksum() function sends audio data to the AccurateRip
 * checksum calculator.  This function cannot fail.
 *
 * XXX Is it really safe to assume that stream->duration is the total
 * number of samples?  Perhaps try it on the m3u SLAYRadio stream?  If
 * not revert to tagger.cpp.bak-04.  Alternatively, scan the last
 * stream twice: once to figure out its length, a second time to
 * actually decode it.
 *
 * @param ctx  Pointer to an opaque fingersum context
 * @param data Pointer to raw audio data, a @p len-long array of
 *             16-bit signed integers in native byte order
 * @param len  Length of data, in 16-bit samples
 */
static void
_feed_checksum(struct fingersum_context *ctx, const void *data, int len)
{
    int i;
    uint64_t j, p;
    uint32_t s;


    /* Traverse the signed 16-bit data in 32-bit strides, i.e. one
     * 2-channel stereo sample.  Accumulate separate sums for the
     * first 5 sectors, the last five sectors, and everything in
     * between.  Note that the last sample of the fifth sector is
     * included in the last sum.
     *
     * XXX Will this still work on a big-endian machine?  May need to
     * extract the left and right channels separately.  And which one
     * is which? LSB <=> left channel, MSB <=> right channel, but that
     * depends on the channel layout.  Will this break if the layout
     * is tweaked?
     *
     * XXX Note that j is a one-based index here!
     *
     * ctx->samples_tot is the number of mono samples seen so far (in
     * stereo, two for each time point), not counting the current
     * block.  i loops over current block, j count total samples seen
     * so far.
     *
     * For Pettson, XLD says:
     *
     *        #  | Absolute | Relative | Confidence
     *    ------------------------------------------
     *        1  |     77   |     29   |      1
     *
     *    CRC32 hash               : 4B772E2F
     *    CRC32 hash (skip zero)   : 12B9C54E
     *    AccurateRip v1 signature : A42DB403 (E06BBC36 w/correction)
     *    AccurateRip v2 signature : C3A7EEE3
     *
     * For Track 1 & 2, Boney M., XLD says:
     *
     *        #  | Absolute | Relative | Confidence
     *    ------------------------------------------
     *        1  |     -6   |    -12   |     16
     *
     *    CRC32 hash               : E7CE235F
     *    CRC32 hash (skip zero)   : 843D6E8A
     *    AccurateRip v1 signature : 1091E1AF (B03CEC52 w/correction)
     *    AccurateRip v2 signature : 00FAF5C3 (A361183B w/correction)
     *
     *    CRC32 hash               : 13B77FC4
     *    CRC32 hash (skip zero)   : D1294496
     *    AccurateRip v1 signature : EEA5E484 (EC514B1B w/correction)
     *    AccurateRip v2 signature : E1D77E0B (E2A23E0C w/correction)
     *
     * Affected rips [vindaloo]:
     *   ./fredriksson/Den Ständiga Resan.log
     *   ./hedningarna/Trä.log
     *   ./mancini/The Best Of Mancini.log
     *   ./pettson/Pannkakstårtan.log
     *   ./piaf/disc2/Mon Légionnaire.log
     *   ./radio_city/Radio City Hits 2.log
     *   ./soft_cell/Tainted Love.log
     *   ./spike_jones/Musical Mayhem.log
     *   ./svenska/disc2/Svenska klassiska favoriter.log
     *
     * Affected rips [dale]:
     *  ./album/Coldplay - Coldplay Live 2003
     *  ./album/Grandmaster Flash & The Furious Five - The Message
     *  ./compilation/Boney M. - Gold_ Greatest Hits
     *  ./compilation/The Monkees - The Monkees Greatest Hits
     *
     * Only seem to be getting one offset: for "Hedningarna", would
     * expect [-17, +664].
     *
     * offset_array_1  data [MAX_OFFSET]
     * offset_array_2  i * data [MAX_OFFSET]
     *
     * offset_test_1  sum(data)
     * offset_test_2  sum(i * data)
     *
     * XXX Corner cases: what if track too short for there to be no
     * mid section?  What if leader and trailer overlap?
     *
     * XXX What's the range for j in this loop?  I guess
     * j=1..ctx->stream->duration?  That affects l in the inner loop
     * (l=-ctx->offset[m].offset..ctx->stream->duration-ctx->offset[m].offset).
     * 2018-12-10: confirm that l starts at 1.
     */
    for (i = 0, j = ctx->samples_tot / 2 + 1; i < len / 2; i++, j++) {
        size_t m;
        int64_t l;

        for (m = 0; m < ctx->nmemb; m++) {
            l = j - ctx->offsets[m].offset;
//            p = j * ((uint32_t *)data)[i];
            p = l * ((uint32_t *)data)[i];
            s = ((p >> 32) + p); // & 0xffffffff;

//            printf("ADDING TO OFFSET %ld\n", ctx->offsets[m].offset);

/*
            printf("while _feed_checksum %zd %lld %d 0x%08X 0x%08x\n", m, l, ctx->offsets[m].offset,
                   p, // ctx->offsets[m].checksum_v1[2],
                   s); // ctx->offsets[m].checksum_v2[2]);

            printf("adding  _feed_checksum %zd %lld"
                   "0x%08X 0x%08x "
                   "0x%08X 0x%08x "
                   "0x%08X 0x%08x\n", m, l,
                   ctx->offsets[m].checksum_v1[0],
                   ctx->offsets[m].checksum_v2[0],
                   ctx->offsets[m].checksum_v1[1],
                   ctx->offsets[m].checksum_v2[1],
                   ctx->offsets[m].checksum_v1[2],
                   ctx->offsets[m].checksum_v2[2]);
*/

#ifdef USE_CRC32 // XXX WIP
                /*
  Computer says: skip first 2940 samples?

  track 1/16 [offset 16]
    n_block:      5
      block 1/5
        crc32:    2A6864B3
        count:    9
        date2:    00000343
      block 2/5
        crc32:    D85D1476
        count:    1
        date2:    00000023
      block 3/5
        crc32:    8E5D4AA2
        count:    5
        date2:    00000403
      block 4/5
        crc32:    F6740D65 <--- THIS IS WHAT WE WANT for offset 0
        count:    15
        date2:    000003E3
      block 5/5
        crc32:    95B6C69B
        count:    3
        date2:    000002E5

  Computer says: skip last 2940 samples?

  track 16/16 [offset 988]
    n_block:      2
      block 1/2
        crc32:    66136869
        count:    8
        date2:    00000361
      block 2/2
        crc32:    C5A8BB58 <--- THIS IS WHAT WE WANT for offset 0
        count:    24
        date2:    00000421
                */

            /* XXX Looks like this is offset-independent: move it out
             * of the offset loop!  And crc32 does the looping
             * internally: move it out of the outer loop as well!
             * That really affects a whole bunch of things!
             *
             * l starts at 1: this handles the 5 * 588 first samples
             * and the 5 * 588 last samples separately.
             */
            if (j <= 2 * 5 * 588 + 1) { // was l, not j; no factor 2, no offset 1
                ctx->offsets[m].crc32[0] =
                    crc32(
                        ctx->offsets[m].crc32[0],
                        data + 2 * i * sizeof(int16_t),
                        2 * sizeof(int16_t));

            } else if (j + 2 * 5 * 588 + 1 > ctx->stream->duration) { // was l, not j; no factor 2, no offset 1
                ctx->offsets[m].crc32[2] =
                    crc32(
                        ctx->offsets[m].crc32[2],
                        data + 2 * i * sizeof(int16_t),
                        2 * sizeof(int16_t));

            } else {
                ctx->offsets[m].crc32[1] =
                    crc32(
                        ctx->offsets[m].crc32[1],
                        data + 2 * i * sizeof(int16_t),
                        2 * sizeof(int16_t));
            }
#endif

            if (l < 5 * 588) {
                ctx->offsets[m].checksum_v1[0] += p; // & 0xffffffff;
                ctx->offsets[m].checksum_v2[0] += s;

            } else if (l + 5 * 588 > ctx->stream->duration) {
                ctx->offsets[m].checksum_v1[2] += p; // & 0xffffffff;
                ctx->offsets[m].checksum_v2[2] += s;

                // XXX This should probably not be necessary
//                if (j + ctx->offsets[m].offset > ctx->stream->duration) {
//                    ctx->offsets[m].checksum_v1[3] += p; // & 0xffffffff;
//                    ctx->offsets[m].checksum_v2[3] += s;
//                }

            } else {
                ctx->offsets[m].checksum_v1[1] += p; // & 0xffffffff;
                ctx->offsets[m].checksum_v2[1] += s;
            }
        }


        /* XXX PLAYGROUND START */

        /* The leader: do the cumulative thing here, so nothing needs
         * to be done to finalise, but subtraction needed in checksum
         * comparator/offset finder instead.
         *
         * XXX Note that now, offset_array_{1,2} is kept base zero.
         */
        uint64_t t = ((uint32_t *)data)[i];
        uint64_t u = t;
//        u = ((u >> 32) + u);

#if 0
        if (j == 1) {
            ctx->v1.offset_array_1[0] = t;
            ctx->v1.offset_array_2[0] = j * t;

            ctx->v2.offset_array_1[0] = u;
            ctx->v2.offset_array_2[0] = (j * u) >> 32;

        } else if (j < MAX_OFFSET) {
            ctx->v1.offset_array_1[j - 1] =
                ctx->v1.offset_array_1[j - 2] + t;
            ctx->v1.offset_array_2[j - 1] =
                ctx->v1.offset_array_2[j - 2] + j * t;

            ctx->v2.offset_array_1[j - 1] =
                ctx->v2.offset_array_1[j - 2] + u;
            ctx->v2.offset_array_2[j - 1] =
                ctx->v2.offset_array_2[j - 2] + ((j * u) >> 32);

        } else {
            /* The middle only sums the middle, because of the way the
             * circular array for the trailer works.
             */
            ctx->v1.offset_test_1 +=
                ctx->v1.offset_array_3[(j - 1) % MAX_OFFSET];
            ctx->v1.offset_test_2 +=
                ctx->v1.offset_array_4[(j - 1) % MAX_OFFSET];

            ctx->v2.offset_test_1 +=
                ctx->v2.offset_array_3[(j - 1) % MAX_OFFSET];
            ctx->v2.offset_test_2 +=
                ctx->v2.offset_array_4[(j - 1) % MAX_OFFSET];


            /* The trailer, kept in circular array.  We may not want
             * to rely on duration being reported correctly (e.g. the
             * SLAYRadio stream).  But we probably have to, otherwise
             * there is no way to get at the duration of the stream
             * without first traversing it.
             */
            ctx->v1.offset_array_3[(j - 1) % MAX_OFFSET] = t;
            ctx->v1.offset_array_4[(j - 1) % MAX_OFFSET] = j * t;

            ctx->v2.offset_array_3[(j - 1) % MAX_OFFSET] = u;
            ctx->v2.offset_array_4[(j - 1) % MAX_OFFSET] = (j * u) >> 32;
        }


//        int k = 0;

        /* This is what we want.
         */
//        ctx->prutt_1 += (((j + k) * u) >> 32) + (j + k) * u;


        /* What's this?
         */
//        ctx->prutt_2 += (j + k) * u;
//        ctx->prutt_3 += ((j + k) * u) >> 32;


        /* Works for k == 0
         */
//        ctx->prutt_1 += (((j + k) * u) >> 32) + (j + k) * u;
//        ctx->prutt_2 += j * u;
//        ctx->prutt_3 += u;
//        ctx->prutt_4 += (j * u + k * u) & 0xffffffff;


        /* Test Monday, OK for k == 0
         */
//        ctx->prutt_1 += ((j + k) * u) >> 32;
//        ctx->prutt_2 += ((j + k) * u);
//        ctx->prutt_3 += u & 0xffffffff;
//        ctx->prutt_4 += ((j + k) * u) & 0xffffffff;


        /* Playground Tuesday -- OK
         *
         * Hit the snag when replacing (j + k) in prutt_3 with + k *
         * prutt_4 in final expression!
         */
//        ctx->prutt_1 += ((j + k) * u) >> 32;
//        ctx->prutt_2 += ((j + k) * u);
//        ctx->prutt_3 += ((j + k) * u) & 0xffffffff;

//        ctx->prutt_1 += (((j + k) * u) >> 32) + (j + k) * u;
//        ctx->prutt_2 += (j * u);
//        ctx->prutt_3 += ((j + k) * u) % 0x100000000; // & 0xffffffff;
//        ctx->prutt_4 += u;

/*
        if (j == 8122044 - 5 * 588) {
            printf("  IN-FLIGHT 0x%016llx 0x%016llx\n",
                   ctx->prutt, ctx->v2.offset_test_2);
        }
*/
#endif


        /* Offset-finding stuff.
         */
        size_t k = j - (450 * 588 + 0 - MAX_OFFSET_2);
        if (k == 1) {
            ctx->sof[0] = (k + 0) * u;
            ctx->sum[0] = u;

        } else if (k > 1 && k <= 2 * MAX_OFFSET_2 + 588 + 1) {
//            printf("Writing at %zd\n", k);

            ctx->sof[k - 1] = ctx->sof[k - 2] + (k + 0) * u;
            ctx->sum[k - 1] = ctx->sum[k - 2] + u;
        }
        /* XXX PLAYGROUND STOP */
    }


    /* Offset-finding for EAC
     *
     *   ctx->samples_tot Number of mono-samples seen so far
     *   len              Number of mono samples in this call
     *
     * There certainly is a cleverer way to do this.
     *
     * XXX We seem to get here a lot more often than we should.
     */
#if 1
//    int done = 0;
    {
//        uLong my_crc;
//        goto skip_crc32;
        if (ctx->crc32_off[2 * 588 + 1 - 1] != crc32(0, Z_NULL, 0)) {
//            printf("  BEEN HERE, DONE THAT %p %lld %ld\n",
//                   ctx, ctx->stream->duration, 450 * 588);
            goto skip_crc32;
        }


        /* XXX Arbitrary limit; would have expected (450 + 1 + 5) *
         * 588 to make more sense, but that does not work for the
         * pause track on Duke.
         */
        if (ctx->stream->duration < (500 + 1) * 588) {
            printf("Skipped track duration %" PRId64 "\n",
                   ctx->stream->duration);
            goto skip_crc32;
        }

//        printf("PRESSING AHEAD\n");
//        goto skip_crc32;

        for (int i = -5 * 588; i <= +5 * 588; i++) {
            /* Map the range we got onto the range needed for the
             * current index.
             */
//            int i_crc = ctx->samples_tot / 2 - (450 * 588 + i);
//            int f_crc = (ctx->samaples_tot + n) / 2 - (450 * 588 + i);

            /* Map the range for the current CRC onto the range we
             * got.
             */
            int i_dat = (450 * 588 + i) - ctx->samples_tot / 2;
            int f_dat = (451 * 588 + i) - ctx->samples_tot / 2;

//            printf("Got i=%d f=%d\n", i_dat, f_dat);

            if (i_dat >= 0 && i_dat < len / 2) {
                /* This is the first data block for the CRC; only the
                 * tail of the data block will be included in the CRC.
                 */
                int my_len = 588 < len / 2 - i_dat ? 588 : len / 2 - i_dat;

//                printf("%03d FIRST:  samples=%llu len=%d i=%d f=%d l=%d\n",
//                       i, ctx->samples_tot, len, i_dat, f_dat, my_len);
//                if (my_len >= 588)
//                    done = 1;

//                printf("START i=%d i_dat=%d l=%d len=%d\n", i, i_dat, my_len, len);

                ctx->crc32_off[i + 5 * 588] = crc32(
                    crc32(0, Z_NULL, 0),
                    data + i_dat * 2 * sizeof(int16_t),
                    my_len * 2 * sizeof(int16_t)); // XXX for Duke: Instead of my_len: 587 is OK, 588 is not

//                printf("STOP\n");

            } else if (i_dat < 0 && f_dat >= len / 2) {
                /* The entire data block is to be included in the CRC.
                 *   i_dat >= 0     => f_dat > 0
                 *   f_dat <  n / 2 => i_dat < n / 2
                 *
                 * Unlikely to ever see this case, because len > 588.
                 */
                int my_len = len / 2;

//                printf("%03d MIDDLE: samples=%llu len=%d i=%d f=%d\n",
//                       i, ctx->samples_tot, len, i_dat, f_dat);
//                done = 0;

                ctx->crc32_off[i + 5 * 588] = crc32(
                    ctx->crc32_off[i + 5 * 588],
                    data + i_dat * 2 * sizeof(int16_t),
                    my_len * 2 * sizeof(int16_t));

            } else if (f_dat > 0 && f_dat < len / 2) {
                /* This is the last data block for the CRC; only the
                 * head of the data block will be included in the CRC.
                 */
                int my_len = f_dat;

//                printf("%03d END:    samples=%llu len=%d i=%d f=%d l=%d\n",
//                       i, ctx->samples_tot, len, i_dat, f_dat, my_len);
//                done = 1;

                ctx->crc32_off[i + 5 * 588] = crc32(
                    ctx->crc32_off[i + 5 * 588],
                    data,
                    my_len * 2 * sizeof(int16_t));
            }

#if 0
            switch(ctx->crc32_off[i + 5 * 588]) {
#if 1
                // August
            case 0x004CE982: // +6  [count 81]
            case 0x733A7847:
            case 0xD75B3115: // -62 [count 1]
            case 0x0A99519A:
            case 0xA8A50BDB:
            case 0x2AC8B12F:
            case 0xDE5AE38B:
            case 0x7B87209D: //  0 [count 4]
#else
                // Duke
            case 0x15FC2681:
            case 0xC1FCB369: // -104 [count 1]
            case 0xDB2008BE:
            case 0x2A0D927E: //  0   [count 15]
            case 0x0B6E43D4: // +338 [count 3]
#endif
                printf("#1 Offset %d: 0x%08lx\n", i, ctx->crc32_off[i + 5 * 588]);
                //exit(0);
                break;
            default:
                break;
            }
#endif
        }

    skip_crc32:
        ;
    }

#endif

//    if (done != 0)
//        exit(0);

#if 0
/*
  From Duke: partial track 1
  track 2/17
      block 1/5
        crc32:    15FC2681
        count:    9
      block 2/5
        crc32:    C1FCB369
        count:    1
      block 3/5
        crc32:    DB2008BE
        count:    5
      block 4/5
        crc32:    2A0D927E
        count:    15
      block 5/5
        crc32:    0B6E43D4
        count:    3

  From August: partial tracks
  track 2/13
      block 1/8
        crc32:    004CE982
        count:    81
      block 2/8
        crc32:    733A7847
        count:    1
      block 3/8
        crc32:    D75B3115
        count:    1
      block 4/8
        crc32:    0A99519A
        count:    2
      block 5/8
        crc32:    A8A50BDB
        count:    2
      block 6/8
        crc32:    2AC8B12F
        count:    1
      block 7/8
        crc32:    DE5AE38B
        count:    1
      block 8/8
        crc32:    7B87209D
        count:    4
  track 3/13
      block 1/8
        crc32:    5CB60456
        count:    79
      block 2/8
        crc32:    5B6CE89E
        count:    1
      block 3/8
        crc32:    1D0F9317
        count:    1
      block 4/8
        crc32:    8A174407
        count:    2
      block 5/8
        crc32:    B1A8AEC4
        count:    2
      block 6/8
        crc32:    DC703EE3
        count:    1
      block 7/8
        crc32:    85DBB477
        count:    1
      block 8/8
        crc32:    2D22E14A
        count:    4
  track 4/13
      block 1/8
        crc32:    5FFD0914
        count:    82
      block 2/8
        crc32:    24B9D095
        count:    1
      block 3/8
        crc32:    5EEF8457
        count:    1
      block 4/8
        crc32:    5E4BEB15
        count:    2
      block 5/8
        crc32:    E4EEB7DF
        count:    2
      block 6/8
        crc32:    559FF31C
        count:    1
      block 7/8
        crc32:    26002EDB
        count:    1
      block 8/8
        crc32:    1C27E2C5
        count:    4
  track 5/13
      block 1/8
        crc32:    16539E70
        count:    79
      block 2/8
        crc32:    54FEBF45
        count:    1
      block 3/8
        crc32:    C3F1CEB0
        count:    1
      block 4/8
        crc32:    A1098E22
        count:    2
      block 5/8
        crc32:    A6D43530
        count:    2
      block 6/8
        crc32:    F5D58B7F
        count:    1
      block 7/8
        crc32:    980E8865
        count:    1
      block 8/8
        crc32:    EAD19186
        count:    4
  track 6/13
      block 1/8
        crc32:    28E6E29E
        count:    79
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    FD60D498
        count:    1
      block 4/8
        crc32:    353C6F67
        count:    2
      block 5/8
        crc32:    0E6DCB07
        count:    2
      block 6/8
        crc32:    FC895D61
        count:    1
      block 7/8
        crc32:    6888BE2F
        count:    1
      block 8/8
        crc32:    9933375E
        count:    4
  track 7/13
      block 1/8
        crc32:    8A797016
        count:    79
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    FAC1ABD5
        count:    1
      block 4/8
        crc32:    4240D94C
        count:    2
      block 5/8
        crc32:    035207F2
        count:    2
      block 6/8
        crc32:    2C358604
        count:    1
      block 7/8
        crc32:    2CAB5F45
        count:    1
      block 8/8
        crc32:    983C318D
        count:    4
  track 8/13
      block 1/8
        crc32:    A6D120D0
        count:    78
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    93B3EB79
        count:    1
      block 4/8
        crc32:    7ED17847
        count:    2
      block 5/8
        crc32:    7E4215AB
        count:    2
      block 6/8
        crc32:    9AC3DBE6
        count:    1
      block 7/8
        crc32:    7D3DC058
        count:    1
      block 8/8
        crc32:    3C9D6EA4
        count:    4
  track 9/13
      block 1/8
        crc32:    E230DA87
        count:    78
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    88DA6CE8
        count:    1
      block 4/8
        crc32:    CC442BD0
        count:    2
      block 5/8
        crc32:    39288759
        count:    2
      block 6/8
        crc32:    93850A3C
        count:    1
      block 7/8
        crc32:    BE47C560
        count:    1
      block 8/8
        crc32:    D112875A
        count:    4
  track 10/13
      block 1/8
        crc32:    4D19A26C
        count:    79
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    A6CC21D4
        count:    1
      block 4/8
        crc32:    840A2A6C
        count:    2
      block 5/8
        crc32:    D49F5280
        count:    2
      block 6/8
        crc32:    0CD80944
        count:    1
      block 7/8
        crc32:    572DF7C5
        count:    1
      block 8/8
        crc32:    83728A3C
        count:    4
  track 11/13
      block 1/8
        crc32:    710654BF
        count:    78
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    634A53CF
        count:    1
      block 4/8
        crc32:    825AB847
        count:    2
      block 5/8
        crc32:    0E73ABFB
        count:    2
      block 6/8
        crc32:    6AD21D27
        count:    1
      block 7/8
        crc32:    EBCC62AC
        count:    1
      block 8/8
        crc32:    712B290D
        count:    4
  track 12/13
      block 1/8
        crc32:    7301D65C
        count:    79
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    616F069C
        count:    1
      block 4/8
        crc32:    CD0494C7
        count:    2
      block 5/8
        crc32:    8276F796
        count:    2
      block 6/8
        crc32:    391F0782
        count:    1
      block 7/8
        crc32:    52FB0579
        count:    1
      block 8/8
        crc32:    E95C31CA
        count:    5
  track 13/13
      block 1/8
        crc32:    49EA35A1
        count:    78
      block 2/8
        crc32:    BE97CE3F
        count:    1
      block 3/8
        crc32:    D4107799
        count:    1
      block 4/8
        crc32:    FB7B6A54
        count:    2
      block 5/8
        crc32:    497A5DB0
        count:    2
      block 6/8
        crc32:    3A824AEE
        count:    1
      block 7/8
        crc32:    FCF2584B
        count:    1
      block 8/8
        crc32:    46F711BA
        count:    4
*/
#endif

//    size_t m;

//    for (m = 0; m < ctx->nmemb; m++) {
//        printf("leaving _feed_checksum 0x%08X 0x%08x\n",
//               ctx->offsets[m].checksum_v1[1],
//               ctx->offsets[m].checksum_v2[1]);
//    }
}


/* The _feed_chromaprint() function sends audio data to the
 * Chromaprint fingerprint calculator.  If the fingerprint calculator
 * has accumulate enough data, _feed_chromaprint() finalises the
 * fingerprint calculation after which it does nothing.
 *
 * XXX Could data become const?  Look into the source and maybe
 * comment on
 * https://groups.google.com/forum/?pli=1#!topic/acoustid/sJjSA0zEWSI
 * [but check first what the changes in the latest version were, the
 * update was installed 2014-12-16]
 *
 * @param ctx  Pointer to an opaque fingersum context
 * @param cc   The Chromaprint context
 * @param data Pointer to raw audio data, a @p len-long array of
 *             16-bit signed integers in native byte order
 * @param len  Length of data, in 16-bit samples
 * @return     0 if successful, -1 otherwise.  If an error occurs, the
 *             global variable @c errno is set to @c EPROTO.
 */
static int
_feed_chromaprint(
    struct fingersum_context *ctx, ChromaprintContext *cc, void *data, int len)
{
    void *fingerprint;
    int encoded_size, ret, size;


    if (len > ctx->remaining)
        len = ctx->remaining;
    if (len > 0 && chromaprint_feed(cc, data, len) != 1) {
        errno = EPROTO;
        return (-1);
    }
    ctx->remaining -= len;


    /* If all requested samples have been fed or the end of the stream
     * has been reached, get the raw fingerprint and encode it in
     * base-64 with the URL-safe scheme.
     */
    if (ctx->remaining <= 0 || len <= 0) {
        if (chromaprint_finish(cc) != 1) {
            errno = EPROTO;
            return (-1);
        }

        if (chromaprint_get_raw_fingerprint(cc, (uint32_t **)&fingerprint, &size) != 1) { // XXX Weird cast?
            errno = EPROTO;
            return (-1);
        }

        ret = chromaprint_encode_fingerprint(
            fingerprint, size, CHROMAPRINT_ALGORITHM_DEFAULT,
            (char **)&ctx->fingerprint, &encoded_size, 1); // XXX Weird cast?
        chromaprint_dealloc(fingerprint);
        if (ret != 1) {
            errno = EPROTO;
            return (-1);
        }

        // XXX Sanity check
        if (((char *)ctx->fingerprint)[encoded_size] != '\0')
            printf("*** fingerprint not null-terminated ***\n");
    }

    return (0);
}


/*** NEW PUBLIC FUNCTION TO DIFF STREAMS ***
 *
 * XXX Maybe we should be using callbacks instead?
 *
 * XXX Needs more documentation!
 *
 * @return -1 on failure, or if the streams do not have the same
 *         number of bits per sample.  Otherwise the number of octets
 *         that differ between @p ctx1 and @p ctx2
 */
ssize_t
fingersum_diff(struct fingersum_context *ctx1, struct fingersum_context *ctx2)
{
    uint8_t *data1, *data2, *next1, *next2;
    ssize_t ret;
    int avail1, avail2, n, ops, size1, size2;
    int tot1, tot2; // NEW

    int m, mono1 = 1, mono2 = 1; // NEW, for mono check


    /* Calculate the number of octets per sample.  There is no point
     * in trying to diff if the number of bits per sample are not
     * identical, or not a multiple of eight.
     */
    ops = ctx1->avcc->bits_per_raw_sample / 8;
    if (ctx1->avcc->bits_per_raw_sample != ops * 8 ||
        ctx2->avcc->bits_per_raw_sample != ops * 8) {
//        errno = XXX;
        return (-1);
    }


    /* XXX Vadfalls?! bits_per_raw_sample is sometimes 0!
     */
    if (ops <= 0)
        ops = 1;

    avail1 = avail2 = 0;
    data1 = data2 = NULL;
    ret = 0;
    size1 = size2 = 0;
    tot1 = tot2 = 0; // NEW
    for ( ; ; ) {
        /* Refill the buffers if necessary.  Break out of the loop on
         * error.  Stop iterating when both streams are exhausted.
         */
        if (avail1 == 0) {
            n = _decode_frame(ctx1, &data1, &size1);
            if (n < 0) {
                ret = -1;
                break;
            }
            avail1 += n;
            next1 = data1;

            tot1 += n; // NEW

            for (m = 0; m < n && mono1 != 0; m += 4) { // NEW: mono check
                if (*(int16_t *)(data1 + m + 0) !=
                    *(int16_t *)(data1 + m + 2)) {
                    mono1 = 0;
                    break;
                }
            }
        }

        if (avail2 == 0) {
            n = _decode_frame(ctx2, &data2, &size2);
            if (n < 0) {
                ret = -1;
                break;
            }
            avail2 += n;
            next2 = data2;

            tot2 += n; // NEW

            for (m = 0; m < n && mono2 != 0; m += 4) { // NEW: mono check
                if (*(int16_t *)(data2 + m + 0) !=
                    *(int16_t *)(data2 + m + 2)) {
                    mono2 = 0;
                    break;
                }
            }
        }

        if (avail1 == 0 && avail2 == 0)
            break;


        /* If one stream is exhausted, the remainder of the other
         * stream contributes to the number of differing octets.
         */
        if (avail1 == 0) {
            ret += avail2;
            avail2 = 0;
            continue;
        } else if (avail2 == 0) {
            ret += avail1;
            avail1 = 0;
            continue;
        }


        /* Diff the buffers octet by octet.
         */
        for ( ; avail1 > 0 && avail2 > 0; avail1--, avail2--) {
            for (n = 0; n < ops; n++) {
                if (*next1++ != *next2++)
                    ret += 1;
            }
        }
    }

    printf("Compared %d and %d samples: %zd\n", tot1, tot2, ret); // NEW
    printf("File 1: %s, File 2: %s\n",
           mono1 != 0 ? "mono" : "stereo",
           mono2 != 0 ? "mono" : "stereo");


    /* Clean up and exit.
     */
    if (data1 != NULL && size1 > 0)
        av_freep(&data1);
    if (data2 != NULL && size2 > 0)
        av_freep(&data2);
    return (ret);
}


/* Allocate and initialise a Chromaprint context unless the fingersum
 * context already has one.  _process() will feed at least @ len
 * samples to the checksumming and fingerprinting algorithms before
 * returning.  The fingerprint calculation is not finalised here,
 * because it may not be required.
 *
 * @param ctx Pointer to an opaque fingersum context
 * @param cc  The Chromaprint context, or @c NULL to use a context
 *            local to @p ctx
 * @param len Samples to process, or negative to process the remainder
 *            of the stream
 * @return    The Chromaprint context if successful, @c NULL
 *            otherwise.  If an error occurs during the Chromaprint
 *            calculation, the global variable @c errno is set to @c
 *            EPROTO.
 */
static int
_process(struct fingersum_context *ctx, ChromaprintContext *cc, int64_t len)
{
    uint8_t *data;
    int n, ret, size;


    /* Create a new Chromaprint context if needed and store it in the
     * fingersum context for later reference.  It is an error if this
     * needs to be done after the first samples have been processed.
     * Note that chromaprint_new() is not thread-safe if Chromaprint
     * was compiled with FFTW.
     *
     * XXX Why do we need the Chromaprint context to be accessible
     * from the outside again?
     */
    if (cc == NULL) {
        if (ctx->cc == NULL) {
            if (ctx->samples_tot > 0) {
                errno = EPROTO;
                return (-1);
            }

            if (pthread_mutex_lock(&_mutex) != 0)
                return (-1);
            ctx->cc = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
            if (ctx->cc == NULL) {
                pthread_mutex_unlock(&_mutex);
                errno = EPROTO;
                return (-1);
            }
            if (pthread_mutex_unlock(&_mutex) != 0)
                return (-1);
        }

        cc = ctx->cc;
    }


    /* If this is the first data feed prepare the Chromaprint context
     * for a new audio stream.
     */
    if (ctx->samples_tot == 0) {
        if (chromaprint_start(
                cc, ctx->avcc->sample_rate, ctx->avcc->channels) != 1) {
            errno = EPROTO;
            return (-1);
        }
    }


    /* XXX Determine the maximum offset; maybe better as its own
     * function?
     */
    size_t i;
    ctx->max_offset = 0;
    for (i = 0; i < ctx->nmemb; i++) {
        if (abs(ctx->offsets[i].offset) > ctx->max_offset)
            ctx->max_offset = abs(ctx->offsets[i].offset);
//        printf("HATTNE see offset %ld [%zd/%zd]\n",
//               ctx->offsets[i].offset, i + 1, ctx->nmemb);
    }
    ctx->max_offset = 2 * 5 * 588 + 1; // XXX Ouch!

    if (ctx->samples == NULL) {
        ctx->samples = calloc(2 * 5 * 588 + 1, 2 * sizeof(int16_t));
        if (ctx->samples == NULL)
            return (-1);
    }

    if (ctx->samples_end == NULL) {
        ctx->samples_end = calloc(2 * 5 * 588 + 1, 2 * sizeof(int16_t));
        if (ctx->samples_end == NULL)
            return (-1);
    }


    /* Decode as many frames as requested and feed the checksumming
     * and fingerprinting algorithms.
     */
    data = NULL;
    n = 0;
    ret = 0;
    size = 0;
    do {
        n = _decode_frame(ctx, &data, &size);
        if (n < 0) {
            ret = -1;
            break;
        }

        _feed_checksum(ctx, data, n);
        if (ctx->fingerprint == NULL) {
            if (_feed_chromaprint(ctx, cc, data, n) != 0) {
                ret = -1;
                break;
            }
        }

#if 0 && defined(USE_CRC32) // XXX WIP
        // XXX Apparently, the zero-skip CRC32 is reported by EAC.
        if (n > 0) {
            ctx->offsets[m].crc32 = crc32(ctx->offsets[m].crc32, data, 2 * n);

            size_t k;
            for (k = 0; k < n; k++) {
                if (((uint16_t *)data)[k] != 0)
                    ctx->offsets[m].crc32_skip_zero = crc32(ctx->offsets[m].crc32_skip_zero, data + 2 * k, 2);
            }
        }
#endif


        /* Remember the first 2 * 5 * 588 + 1 samples.
         */
        if (ctx->samples_tot < (2 * 5 * 588 + 1) * 2) {
            memcpy(ctx->samples + ctx->samples_tot / 2, // XXX Should ctx->samples and ctx->samples_end be void ptr?
                   data,
                   (ctx->samples_tot + n < (2 * 5 * 588 + 1) * 2 ?
                    n : (2 * 5 * 588 + 1) * 2 - ctx->samples_tot) * sizeof(int16_t));
        }


        /* Remember the last 2 * 5 * 588 + 1 samples as well.  Circular
         * buffer, no let's not do that, because we need to rely on
         * ctx->stream->duration anyhow!
         *
         * This should really just be the above, but backwards!
         *
         * Alternatively: store all 2 * 5 * 588 + 1 16-bit stereo samples at
         * beginning and end (about (2 * 5 * 588 + 1) / 44100 = 0.13 s).
         *
         *   2 * (2 * 5 * 588 + 1) * 2 * 2 = 46 k per track, probably acceptable
         *
         * And note that the ARv1 checksum can be precalculated
         * and stored as two numbers and that the leading offset
         * can be precalculated and stored as partial checksums.
         *
         * XXX This part may be buggered!  See Boney M.
         *
         * XXX Complex, see "Offset-finding for EAC" in this file.
         */
        if (n > 0 && ctx->samples_tot / 2 + n / 2 > ctx->stream->duration - (2 * 5 * 588 + 1)) {

//            printf("\n");
//            printf("Checking %lld + %d = %lld, %lld - %lld = %lld\n",
//                   ctx->samples_tot / 2, n / 2, ctx->samples_tot / 2 + n / 2,
//                   ctx->stream->duration, 2 * 5 * 588 + 1, ctx->stream->duration - (2 * 5 * 588 + 1));

            uint64_t dst, len, src;
            uint64_t stm = ctx->samples_tot / 2 + (2 * 5 * 588 + 1);

            src = ctx->samples_tot / 2 > ctx->stream->duration - (2 * 5 * 588 + 1)
                ? 0
                : ctx->stream->duration - (2 * 5 * 588 + 1) - ctx->samples_tot / 2;
            dst = ctx->samples_tot / 2 > ctx->stream->duration - (2 * 5 * 588 + 1)
                ? ctx->samples_tot / 2 - (ctx->stream->duration - (2 * 5 * 588 + 1))
                : 0;
            len = n / 2 < stm + n / 2 - ctx->stream->duration
                ? n / 2
                : stm + n / 2 - ctx->stream->duration;

//            printf("Copying %lld samples from %lld to %lld\n",
//                   len, src, dst);

//            printf("  stm = %lld, duration = %lld, n = %d, n / 2 = %d, max_offset = %lld\n",
//                   stm, ctx->stream->duration, n, n / 2, (2 * 5 * 588 + 1));
//            printf("\n");

            memcpy(ctx->samples_end + dst,
                   (uint32_t *)data + src, // Because data is (uint8_t *)
                   len * sizeof(uint32_t));

//            for (i = 0; i < 2 * 5 * 588 + 1; i++)
//                printf("  SET DATA %zd: 0x%08x\n", i, ctx->samples_end[i]);
        }

        ctx->samples_tot += n;
    } while (n > 0 && (len < 0 || (len -= n) > 0));


    /* Free the mallocs!
     */
    if (data != NULL && size > 0)
        av_freep(&data);
    return (ret);
}


/* XXX PLAYGROUND FUNCTION
 */
#if 0
static struct fingersum_result *
_fingersum_result_new()
{
    struct fingersum_result *result;

    result = malloc(sizeof(struct fingersum_result));
    if (result == NULL)
        return (NULL);
    result->nmemb = 0;
    result->offsets = NULL;
    return (result);
}
#endif


#if 0
void
fingersum_result_free(struct fingersum_result *result)
{
    if (result->offsets != NULL)
        free(result->offsets);
    free(result);
}
#endif


#if 0
static int
_fingersum_result_add(struct fingersum_result *result, ssize_t offset)
{
    void *p;

    p = realloc(result->offsets, (result->nmemb + 1) * sizeof(ssize_t));
    if (p == NULL)
        return (-1);

    result->offsets = p;
    result->offsets[result->nmemb] = offset;
    result->nmemb += 1;
    return (0);
}
#endif


#if 0
/* XXX Sort out the limits of the offset.  May need to change the code
 * to handle zeroes as we step out of the arrays.
 *
 * Currently, this function returns a list of all matching offsets.
 * That may or may not make sense.  If this is changed to return a
 * single offset instead, the iteration should probably proceed away
 * from zero (e.g. 0, +1, -1, +2, -2, ...).
 */
static struct fingersum_result *
_foo1(struct _fingersum_checksum_v1 *leader,
      struct _fingersum_checksum_v1 *center,
      struct _fingersum_checksum_v1 *trailer,
      uint32_t checksum)
{
    struct fingersum_result *result;
    ssize_t k, l, m;
    uint32_t t, u, v;


//    return (NULL);

//    printf("Looking for 0x%08X... %p %p %p\n",
//           checksum, leader, center, trailer);

/*
    if (checksum == 0xe06bbc36 || // Pettson
//        checksum == 0xb03cec52 || // Boney M., track  1
//        checksum == 0xec514b1b || // Boney M., track  2
//        checksum == 0x1334a1fa || // Boney M., track  3
//        checksum == 0x5f3c8261 || // Boney M., track  4
//        checksum == 0x2da170d7 || // Boney M., track  5
//        checksum == 0xe5fa586a || // Boney M., track  6
//        checksum == 0xe3498167 || // Boney M., track  7
//        checksum == 0x6556334b || // Boney M., track  8
//        checksum == 0x2d2ee463 || // Boney M., track  9
//        checksum == 0xcbc321fc || // Boney M., track 10
//        checksum == 0x70dacad2 || // Boney M., track 11
//        checksum == 0x658e31c2 || // Boney M., track 12
//        checksum == 0xc3abdf88 || // Boney M., track 13
//        checksum == 0x76ff9d24 || // Boney M., track 14
//        checksum == 0xd2f91578 || // Boney M., track 15
//        checksum == 0xcd90ff38 || // Boney M., track 16
//        checksum == 0xde546cf9 || // Boney M., track 17
//        checksum == 0x4b0a70aa || // Boney M., track 18
//        checksum == 0x030069c0 || // Hedningarna, track  1
//        checksum == 0x20ab630f || // Hedningarna, track  2
//        checksum == 0x2d992e04 || // Hedningarna, track  3
//        checksum == 0xb517452d || // Hedningarna, track  4
//        checksum == 0xdf92ccdb || // Hedningarna, track  5
//        checksum == 0xd541dfd5 || // Hedningarna, track  6
//        checksum == 0xd82e982f || // Hedningarna, track  7
//        checksum == 0x0613a7a9 || // Hedningarna, track  8
//        checksum == 0xec766907 || // Hedningarna, track  9
//        checksum == 0x73db7ecd || // Hedningarna, track 10
        checksum == 0x47a4e7ab) { // Hedningarna, track 11
*/

    result = NULL;
    for (k = -5 * 588 + 2; k <= MAX_OFFSET - 5 * 588; k++) {
        /* The bit in the middle.  This should not change, and should
         * probably go first.
         */
        t = center->offset_test_2 - k * center->offset_test_1;

        u = v = 0;
        if (leader == NULL) {
            /* For the first track: skip (5 * 588 - 1) first sectors.
             * Needs to be added to current track.  The difference may
             * need to be added to the previous track.
             *
             *   offset_array_1[k] = sum(i = k .. MAX_OFFSET)
             *   offset_array_2[k] = sof(i = k .. MAX_OFFSET)
             *
             * May need to be subtracted from current track.  The
             * difference may need to added to the next track.
             *
             *   offset_array_3[k] = sum(i = duration .. duration - k)
             *   offset_array_4[k] = sof(i = duration .. duration - k)
             */
            l = 5 * 588 - 1 + k;
            t += (center->offset_array_2[MAX_OFFSET - 2] -
                  center->offset_array_2[l - 1]) -
                k * (center->offset_array_1[MAX_OFFSET - 2] -
                     center->offset_array_1[l - 1]);
        } else {
            if (k < 0) {
                /* For negative offset, all tracks except the first:
                 * add the last |k| samples from the previous track.
                 *
                 * XXX This handles the case where k <= 0, k > 0 not
                 * implemented yet.  And k == 0 should be a NOP.
                 */
                t += center->offset_array_2[MAX_OFFSET - 2] -
                    k * center->offset_array_1[MAX_OFFSET - 2];

                l = (leader->nmemb - 1 + k) % MAX_OFFSET;
                m = (leader->nmemb - 1) % MAX_OFFSET;

                u = (leader->offset_array_4[m] -
                     leader->offset_array_4[l]) -
                    (leader->nmemb + k) *
                    (leader->offset_array_3[m] -
                     leader->offset_array_3[l]);
            } else {
                /* Skip the first k samples from the current track
                 */
                t += (center->offset_array_2[MAX_OFFSET - 2] -
                      center->offset_array_2[k - 1]) -
                    k * (center->offset_array_1[MAX_OFFSET - 2] -
                         center->offset_array_1[k - 1]);
            }
        }

        if (trailer == NULL) {
            /* Skip the last 5 * 588 samples XXX THINK ABOUT THE -1 in
             * lskip!
             *
             * NOW: add the trailer except for the last 5 * 588
             * samples.
             */
            l = (center->nmemb - 1 - 5 * 588 + k) % MAX_OFFSET;
            v = center->offset_array_4[l] - k * center->offset_array_3[l];
        } else {
            if (k < 0) {
                /* Negative offset, all tracks except the last: remove
                 * (not skip) the last 12 samples, which were
                 * previously added with an offset.
                 *
                 * NOW: add the trailer except for the last 12
                 * samples.
                 */
                l = (center->nmemb - 1 + k) % MAX_OFFSET;
                v = center->offset_array_4[l] -
                    k * center->offset_array_3[l];
            } else {
                /* For positive offset, add |k| first samples from the
                 * next track.  Add all of the current trailer
                 */
                l = (center->nmemb - 1) % MAX_OFFSET;

                v = center->offset_array_4[l] -
                    k *
                    center->offset_array_3[l];

                v += trailer->offset_array_2[k - 1] +
                    (center->nmemb - k) *
                    (trailer->offset_array_1[k - 1]);
            }
        }

        t = t + u + v;
        if (t == checksum) {
            if (result == NULL) {
                result = _fingersum_result_new();
                if (result == NULL)
                    return (NULL);
            }

            if (_fingersum_result_add(result, k) != 0) {
                fingersum_result_free(result);
                return (NULL);
            }

//            printf("  k = % 3d c = %08X (u = %08X, v = %08X)%s\n",
//                   k,
//                   t, u, v,
//                   t == checksum ? " ***" : "");
        }
    }

    return (result);
}
#endif


#if 0
static struct fingersum_result *
_foo2(struct _fingersum_checksum_v1 *leader1,
      struct _fingersum_checksum_v2 *leader2,
      struct _fingersum_checksum_v1 *center1,
      struct _fingersum_checksum_v2 *center2,
      struct _fingersum_checksum_v1 *trailer1,
      struct _fingersum_checksum_v2 *trailer2,
      uint32_t checksum)
{
    struct fingersum_result *result;
    ssize_t k, l, m;
    uint64_t t, u, v;
    uint64_t t2, u2, v2;


//    return (NULL);

//    printf("Looking for 0x%08X... %p %p %p\n",
//           checksum, leader, center, trailer);

/*
    if (checksum == 0xe06bbc36 || // Pettson
//        checksum == 0xb03cec52 || // Boney M., track  1
//        checksum == 0xec514b1b || // Boney M., track  2
//        checksum == 0x1334a1fa || // Boney M., track  3
//        checksum == 0x5f3c8261 || // Boney M., track  4
//        checksum == 0x2da170d7 || // Boney M., track  5
//        checksum == 0xe5fa586a || // Boney M., track  6
//        checksum == 0xe3498167 || // Boney M., track  7
//        checksum == 0x6556334b || // Boney M., track  8
//        checksum == 0x2d2ee463 || // Boney M., track  9
//        checksum == 0xcbc321fc || // Boney M., track 10
//        checksum == 0x70dacad2 || // Boney M., track 11
//        checksum == 0x658e31c2 || // Boney M., track 12
//        checksum == 0xc3abdf88 || // Boney M., track 13
//        checksum == 0x76ff9d24 || // Boney M., track 14
//        checksum == 0xd2f91578 || // Boney M., track 15
//        checksum == 0xcd90ff38 || // Boney M., track 16
//        checksum == 0xde546cf9 || // Boney M., track 17
//        checksum == 0x4b0a70aa || // Boney M., track 18
//        checksum == 0x030069c0 || // Hedningarna, track  1
//        checksum == 0x20ab630f || // Hedningarna, track  2
//        checksum == 0x2d992e04 || // Hedningarna, track  3
//        checksum == 0xb517452d || // Hedningarna, track  4
//        checksum == 0xdf92ccdb || // Hedningarna, track  5
//        checksum == 0xd541dfd5 || // Hedningarna, track  6
//        checksum == 0xd82e982f || // Hedningarna, track  7
//        checksum == 0x0613a7a9 || // Hedningarna, track  8
//        checksum == 0xec766907 || // Hedningarna, track  9
//        checksum == 0x73db7ecd || // Hedningarna, track 10
        checksum == 0x47a4e7ab) { // Hedningarna, track 11
*/

    result = NULL;
    for (k = -5 * 588 + 2; k <= MAX_OFFSET - 5 * 588; k++) {
        /* The bit in the middle.  This should not change, and should
         * probably go first.
         */
        t = center1->offset_test_2 - k * center1->offset_test_1;
        t2 = center2->offset_test_2 - k * center2->offset_test_1;

        u = v = 0;
        u2 = v2 = 0;
        if (leader1 == NULL) {
            /* For the first track: skip (5 * 588 - 1) first sectors.
             * Needs to be added to current track.  The difference may
             * need to be added to the previous track.
             *
             *   offset_array_1[k] = sum(i = k .. MAX_OFFSET)
             *   offset_array_2[k] = sof(i = k .. MAX_OFFSET)
             *
             * May need to be subtracted from current track.  The
             * difference may need to added to the next track.
             *
             *   offset_array_3[k] = sum(i = duration .. duration - k)
             *   offset_array_4[k] = sof(i = duration .. duration - k)
             */
            l = 5 * 588 - 1 + k;
            t += (center1->offset_array_2[MAX_OFFSET - 2] -
                  center1->offset_array_2[l - 1]) -
                k * (center1->offset_array_1[MAX_OFFSET - 2] -
                     center1->offset_array_1[l - 1]);

            t2 += (center2->offset_array_2[MAX_OFFSET - 2] -
                   center2->offset_array_2[l - 1]) -
                k * (center2->offset_array_1[MAX_OFFSET - 2] -
                     center2->offset_array_1[l - 1]);

        } else {
            if (k > 0) {
                /* Skip the first k samples from the current track
                 */
                t += (center1->offset_array_2[MAX_OFFSET - 2] -
                      center1->offset_array_2[k - 1]) -
                    k * (center1->offset_array_1[MAX_OFFSET - 2] -
                         center1->offset_array_1[k - 1]);

                t2 += (center2->offset_array_2[MAX_OFFSET - 2] -
                       center2->offset_array_2[k - 1]) -
                    k * (center2->offset_array_1[MAX_OFFSET - 2] -
                         center2->offset_array_1[k - 1]);

            } else {
                /* For negative offset, all tracks except the first:
                 * add the last |k| samples from the previous track.
                 *
                 * XXX This handles the case where k <= 0, k > 0 not
                 * implemented yet.  And k == 0 should be a NOP?
                 */
                t += center1->offset_array_2[MAX_OFFSET - 2] -
                    k * center1->offset_array_1[MAX_OFFSET - 2];

                t2 += center2->offset_array_2[MAX_OFFSET - 2] -
                    k * center2->offset_array_1[MAX_OFFSET - 2];

                if (k < 0) {
                    l = (leader1->nmemb - 1 + k) % MAX_OFFSET;
                    m = (leader1->nmemb - 1) % MAX_OFFSET;

                    u = (leader1->offset_array_4[m] -
                         leader1->offset_array_4[l]) -
                        (leader1->nmemb + k) *
                        (leader1->offset_array_3[m] -
                         leader1->offset_array_3[l]);

                    u2 = (leader2->offset_array_4[m] -
                          leader2->offset_array_4[l]) -
                        (leader2->nmemb + k) *
                        (leader2->offset_array_3[m] -
                         leader2->offset_array_3[l]);
                }
            }
        }

        if (trailer1 == NULL) {
            /* Skip the last 5 * 588 samples XXX THINK ABOUT THE -1 in
             * lskip!
             *
             * NOW: add the trailer except for the last 5 * 588
             * samples.
             */
            l = (center1->nmemb - 1 - 5 * 588 + k) % MAX_OFFSET;
            v = center1->offset_array_4[l] - k * center1->offset_array_3[l];
            v2 = center2->offset_array_4[l] - k * center2->offset_array_3[l];
        } else {
            if (k < 0) {
                /* Negative offset, all tracks except the last: remove
                 * (not skip) the last 12 samples, which were
                 * previously added with an offset.
                 *
                 * NOW: add the trailer except for the last 12
                 * samples.
                 */
                l = (center1->nmemb - 1 + k) % MAX_OFFSET;
                v = center1->offset_array_4[l] -
                    k * center1->offset_array_3[l];
                v2 = center2->offset_array_4[l] -
                    k * center2->offset_array_3[l];
            } else {
                /* For non-negative offset add all of the current
                 * trailer.  For positive offset, add k first samples
                 * from the next track.
                 */
                l = (center1->nmemb - 1) % MAX_OFFSET;

                v = center1->offset_array_4[l] -
                    k * center1->offset_array_3[l];

                v2 = center2->offset_array_4[l] -
                    k * center2->offset_array_3[l];

                if (k > 0) {
                    v += trailer1->offset_array_2[k - 1] +
                        (center1->nmemb - k) *
                        (trailer1->offset_array_1[k - 1]);

                    v2 += trailer2->offset_array_2[k - 1] +
                        (center2->nmemb - k) *
                        (trailer2->offset_array_1[k - 1]);
                }
            }
        }

/*
        t = center1->offset_array_2[MAX_OFFSET - 2] +
            center1->offset_test_2 +
            center1->offset_array_4[(center1->nmemb - 1) % MAX_OFFSET];
*/

        t = t + u + v; // + ((t2 + u2 + v2) >> 32);
//        t = ((t >> 32) + t) & 0xffffffff; // ADDITION

        t2 = t2 + u2 + v2;

//        if (k == 0) {
//            printf("Prutt-check 0x%016llX, 0x%016llX, 0x%016llX %zd\n",
//                   t, u, v, center1->nmemb);
//        }

//        t &= 0xffffffff; // ADDITION
        t = (t + t2) & 0xffffffff; // ADDITION

        if (t == checksum) {
            if (result == NULL) {
                result = _fingersum_result_new();
                if (result == NULL)
                    return (NULL);
            }

            if (_fingersum_result_add(result, k) != 0) {
                fingersum_result_free(result);
                return (NULL);
            }

//            printf("  k = % 3d c = %08X (u = %08X, v = %08X)%s\n",
//                   k,
//                   t, u, v,
//                   t == checksum ? " ***" : "");
        }
    }

    return (result);
}
#endif


#if 0
struct fingersum_result *
fingersum_check_checksum1(struct fingersum_context *leader,
                          struct fingersum_context *center,
                          struct fingersum_context *trailer,
                          uint32_t checksum)
{
    return (_foo1(leader != NULL ? &leader->v1 : NULL,
                 &center->v1,
                 trailer != NULL ? &trailer->v1 : NULL, checksum));
}
#endif


#if 0
struct fingersum_result *
fingersum_check_checksum2(struct fingersum_context *leader,
                          struct fingersum_context *center,
                          struct fingersum_context *trailer,
                          uint32_t checksum)
{
//    int k = 0;

//    uint64_t t =
//        ((center->prutt_3 >> 32) - 12 * ((center->prutt_2) >> 32)) +
//        ((center->prutt_3 & 0xffff) - 12 * (center->prutt_2 & 0xffff));


    /* Playground Tuesday -- OK
     */
//    uint64_t t =
//        (center->prutt_2 - (center->prutt_3 & 0xffffffff00000000)) >> 32;

//    uint64_t t = (center->prutt_2 + k * center->prutt_4);
//    t = (t >> 32) + t;

//    printf("Prutt-check 0x%016llX 0x%016llX%s\n",
//           center->prutt_1,
//           center->prutt_2 + center->prutt_3,
//           center->prutt_1 == center->prutt_2 + center->prutt_3 ? " ***" : "");

//           t,
//           (center->prutt_1 & 0xffffffff) == t ? " ***" : "");


           /* Works for k == 0
            */
//           ((center->prutt_2 +
//             k * center->prutt_3 -
//             (center->prutt_4 & 0xffffffff00000000)) >> 32) +
//           center->prutt_2 +
//           k * center->prutt_3,

           /* Test Monday, OK for k == 0
            */
//           (center->prutt_2 >> 32) - (
//               (center->prutt_4 - k * center->prutt_3) >> 32),


//    printf("HATTNE has prutt4 0x%016llX\n", center->prutt_4);
//    printf("HATTNE has prutt5 0x%016llX\n", center->prutt_4 + k * center->prutt_3);

    return (_foo2(leader != NULL ? &leader->v1 : NULL,
                  leader != NULL ? &leader->v2 : NULL,
                  &center->v1,
                  &center->v2,
                  trailer != NULL ? &trailer->v1 : NULL,
                  trailer != NULL ? &trailer->v2 : NULL, checksum));
}
#endif


//int
struct fp3_offset_list *
fingersum_find_offset(const struct fingersum_context *ctx, uint32_t crc) //, ssize_t *offset)
{
    struct fp3_offset_list *offset_list;
    size_t k;
    uint32_t sof, sum;


    // XXX Correct?  For very short tracks (e.g. track 1 on snatch)
    // crc will be zero.  Otherwise, we will generate lots of bogus
    // offsets!
    //
    // But potentially, zero could be a valid crc and there is no way
    // of knowing in the caller whether it's a real zero or an
    // artefact.  Maybe keep track of how many samples went into the
    // offset calculation here and skip if it's zero samples?
    //
    // Probably better: calculate the precise limits of the offsets.
    // If no offsets can be used the range (interval) should have zero
    // length, and this function should return nothing.
    //
    // Distinguish between failure (return NULL) and no results
    // (return empty list).
    offset_list = NULL;
    if (crc == 0)
        return (offset_list);

    for (k = 0; k < 2 * MAX_OFFSET_2 + 1; k++) {
        sof = ctx->sof[k + 588 - 1] - (k > 0 ? ctx->sof[k - 1] : 0);
        sum = ctx->sum[k + 588 - 1] - (k > 0 ? ctx->sum[k - 1] : 0);

        if (((sof - k * sum) & 0xffffffff) == crc) {
            if (offset_list == NULL) {
                offset_list = fp3_new_offset_list();
                if (offset_list == NULL)
                    return (NULL);
            }

            if (fp3_offset_list_add_offset(
                    offset_list, k - MAX_OFFSET_2) != 0) {
                fp3_free_offset_list(offset_list);
                return (NULL);
            }

//            *offset = k - MAX_OFFSET_2;
//            return (0);
        }
    }

//    return (-1);
    return (offset_list);
}


/* XXX Might want to keep two offset lists, one for AccurateRip and
 * one for EAC, because they often (how often?) have unique but
 * overlapping sets of offsets
 */
struct fp3_offset_list *
fingersum_find_offset_eac(const struct fingersum_context *ctx, uint32_t crc32)
{
    struct fp3_offset_list *offset_list;
    size_t k;

    offset_list = NULL;
    for (k = 0; k < 2 * 5 * 588 + 1; k++) {
        if (ctx->crc32_off[k] == crc32) {
            if (offset_list == NULL) {
                offset_list = fp3_new_offset_list();
                if (offset_list == NULL)
                    return (NULL);
            }

            if (fp3_offset_list_add_offset(offset_list, k - 5 * 588) != 0) {
                fp3_free_offset_list(offset_list);
                return (NULL);
            }
        }
    }

    return (offset_list);
}


#if 0
int
fingersum_get_checksum(struct fingersum_context *ctx,
                       ChromaprintContext *cc,
                       uint32_t checksum[3])
{
    /* Finalise the checksum calculation if necessary.  XXX Again
     * assuming that stream->duration is the total number of samples!
     *
     * XXX Should not be 2 but cc->channels?  Or maybe better: make
     * samples_tot account for channels!  Or maybe better: check for
     * EOF instead of expected number of samples.
     */
    if (ctx->samples_tot < 2 * ctx->stream->duration) {
        if (_process(ctx, cc, -1) != 0)
            return (-1);


        /* XXX PLAYGROUND CODE:
         *
         * Transform the cumulative leader and trailer arrays into a
         * "skip arrays".  This code must only be run once when all
         * the samples have been processed.
         *
         * trailing array starts at (ctx->samples_tot / 2 + 0), and
         * this is the oldest element in the array; the last element
         * written is at (ctx->samples_tot / 2 - 1), because it is now
         * written with index base 0.
         *
         * The direction of the first cumulative array should be
         * sorted out while building it.  The direction of the second
         * is currently arbitrary; maybe doing the math documentation
         * will determine it?
         */
        size_t i;
        ssize_t j;

        for (i = 1; i < MAX_OFFSET; i++) {
            j = ctx->samples_tot / 2 + i;

            ctx->v1.offset_array_3[j % MAX_OFFSET] +=
                ctx->v1.offset_array_3[(j - 1) % MAX_OFFSET];

            ctx->v1.offset_array_4[j % MAX_OFFSET] +=
                ctx->v1.offset_array_4[(j - 1) % MAX_OFFSET];

            ctx->v2.offset_array_3[j % MAX_OFFSET] +=
                ctx->v2.offset_array_3[(j - 1) % MAX_OFFSET];

            ctx->v2.offset_array_4[j % MAX_OFFSET] +=
                ctx->v2.offset_array_4[(j - 1) % MAX_OFFSET];
        }


        ctx->v1.nmemb = ctx->samples_tot / 2;
        ctx->v2.nmemb = ctx->samples_tot / 2;
    }


    /* Optionally, copy over the three checksums: for the first
     * checksum (corresponding to the first track) omit the first five
     * frames, for the last checksum (corresponding to the last track)
     * omit the last five frames.  The middle checksum uses data from
     * all the frames.  XXX This documentation snippet should really
     * occur only once!
     *
     * XXX This must cover the situation where there is only one
     * track.  No point in considering offsets larger than 5 * 588,
     * because that would lose data!
     */
    if (checksum != NULL) {
        checksum[0] = ctx->checksum_v1[1] + ctx->checksum_v1[2];
        checksum[1] = checksum[0] + ctx->checksum_v1[0];
        checksum[2] = ctx->checksum_v1[0] + ctx->checksum_v1[1];
    }


    /* XXX PLAYGROUND */
    size_t i;
    struct _fingersum_offset *off = ctx->offsets + 0;

    printf("[fingersum_get_checksum() %08X %08X %08X: ",
           off->checksum_v1[1] + off->checksum_v1[2],
           off->checksum_v1[1] + off->checksum_v1[2] + off->checksum_v1[0],
           off->checksum_v1[0] + off->checksum_v1[1]);

    if (ctx->offsets != NULL && ctx->nmemb > 0) {
        printf("%d", ctx->offsets[0].offset);

        for (i = 1; i < ctx->nmemb; i++)
            printf(", %d", ctx->offsets[i].offset);
    }

    printf("]");

    return (0);
}
#endif


#if 0
/* XXX See above!
 */
int
fingersum_get_checksum2(struct fingersum_context *ctx,
                        ChromaprintContext *cc,
                        uint32_t checksum[3])
{
    if (ctx->samples_tot < 2 * ctx->stream->duration) {
        if (_process(ctx, cc, -1) != 0)
            return (-1);
    }

    if (checksum != NULL) {
        checksum[0] = ctx->checksum_v2[1] + ctx->checksum_v2[2];
        checksum[1] = checksum[0] + ctx->checksum_v2[0];
        checksum[2] = ctx->checksum_v2[0] + ctx->checksum_v2[1];
    }

    printf("RETURNING FROM fingersum_get_checksum2()\n");

    return (0);
}
#endif


void
fingersum_dump(struct fingersum_context *ctx)
{
    size_t i;
    printf("HAVE %zd OFFSETS\n", ctx->nmemb);
    for (i = 0; i < ctx->nmemb; i++) {
        printf("%zd/%zd: %d\n", i + 1, ctx->nmemb, ctx->offsets[i].offset);
    }
}


/* API modelled after zlib's crc32(); len must be divisible by 4
 *
 * Update a running checksum with the octets buf[0..len-1] and return the
 * updated checksum.  XXX offset: int64_t -> off_t?
 *
 * @param offset Offset of first sample, one-based (first sample has
 *               offset = 1)
 * @param buf    Buffer, should return zero (the required initial
 *               value for the checksum) if @c NULL?
 */
static uint32_t
_ar1_cksum(uint32_t cksum, int64_t offset, const void *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len / sizeof(uint32_t); i++)
        cksum += (offset + i) * ((uint32_t *)buf)[i];

    return (cksum);
}


static uint32_t
_ar2_cksum(uint32_t cksum, int64_t offset, const void *buf, size_t len)
{
    size_t i;
    uint64_t p; // Must be 64-bit to capture overflow

    for (i = 0; i < len / sizeof(uint32_t); i++) {
        p = (offset + i) * ((uint32_t *)buf)[i];
        cksum += (p >> 32) + p;
    }

    return (cksum);
}


#if 0
// XXX Giant wart; should go!
static uint32_t
_ar1_cksum_MINUS(uint32_t cksum, int64_t offset, const void *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len / sizeof(uint32_t); i++)
        cksum -= (offset + i) * ((uint32_t *)buf)[i];

    return (cksum);
}


// XXX Giant wart; should go!
static uint32_t
_ar2_cksum_MINUS(uint32_t cksum, int64_t offset, const void *buf, size_t len)
{
    size_t i;
    uint64_t p; // Must be 64-bit to capture overflow

    for (i = 0; i < len / sizeof(uint32_t); i++) {
        p = (offset + i) * ((uint32_t *)buf)[i];
        cksum -= (p >> 32) + p;
    }

    return (cksum);
}
#endif


/* XXX Parameters might have been clearer as previous, current, next
 * in order to avoid confusion with the leader and trailer of the
 * track.
 *
 * XXX ACTUALLY: If we do not have any offsets, calculate the
 * offset-finding checksum, otherwise calculate the offset
 * checksums.
 *
 * XXX Alternative design: register a previous and next track in a
 * separate function, and invalidate calculated checksums if they are
 * different.  Also invalidate calculated checksums if offsets change.
 * Lazy evaluation: only actually update on call to this function.
 */
struct fp3_ar *
fingersum_get_result_3(struct fingersum_context *leader,
                       struct fingersum_context *center,
                       struct fingersum_context *trailer)
{
    struct fp3_ar *result;
    struct _fingersum_offset *offset_leader, *offset_trailer, *offset_center;
    void *p;
    size_t i, j;

//    printf("fingersum_get_result_3() #0\n");

    // Ensure all data has been processed for all streams.  XXX Are we
    // sure the streams each have a Chromaprint context?
    if (leader != NULL && leader->samples_tot < 2 * leader->stream->duration) {
        if (_process(leader, leader->cc, -1) != 0)
            return (NULL);
    }

//    printf("fingersum_get_result_3() #1\n");

    if (center != NULL && center->samples_tot < 2 * center->stream->duration) {
        if (_process(center, center->cc, -1) != 0)
            return (NULL);
    }

//    printf("fingersum_get_result_3() #2\n");

    if (trailer != NULL && trailer->samples_tot < 2 * trailer->stream->duration) {
        if (_process(trailer, trailer->cc, -1) != 0)
            return (NULL);
    }

//    printf("fingersum_get_result_3() #3\n");

    result = malloc(sizeof(struct fp3_ar));
    if (result == NULL)
        return (NULL);

//    printf("fingersum_get_result_3() #4\n");

    result->checksums = NULL;
    result->nmemb = 0;


    /* Drop out if center is NULL?
     */
    if (center == NULL) {
        printf("DROPPING OUT\n");
        return (result);
    }

    for (i = 0; i < center->nmemb; i++) {
        offset_center = center->offsets + i;

        /* Find the corresponding offset in the leader and the
         * trailer.
         */
//        printf("fingersum_get_result_3() #5.0 checking offset %d\n", offset_center->offset);


        /* If the leader or the trailer does not have this offset,
         * move on to the next offset.  This is not an error!
         */
        offset_leader = NULL;
        if (leader != NULL) {
            for (j = 0; j < leader->nmemb; j++) {
                if (leader->offsets[j].offset == center->offsets[i].offset) {
                    offset_leader = leader->offsets + j;
                    break;
                }
            }

            if (offset_leader == NULL)
                continue;
        }

//        printf("fingersum_get_result_3() #5.1\n");

        offset_trailer = NULL;
        if (trailer != NULL) {
            for (j = 0; j < trailer->nmemb; j++) {
                if (trailer->offsets[j].offset == center->offsets[i].offset) {
                    offset_trailer = trailer->offsets + j;
                    break;
                }
            }

            if (offset_trailer == NULL)
                continue;
        }

//        printf("fingersum_get_result_3() #5.2\n");


        /* Allocate space for a new set of checksums for the given
         * offset.
         */
        p = realloc(result->checksums,
                    (result->nmemb + 1) *
                    sizeof(struct fp3_checksums));
        if (p == NULL) {
            fp3_ar_free(result);
            return (NULL);
        }
        result->checksums = p;
        result->checksums[result->nmemb].offset = offset_center->offset;

//        printf("fingersum_get_result_3() #5.3\n");

//        size_t i;
//        uint64_t p; // Must be 64-bit to capture the overflow
//        uint32_t s;
//        int64_t l;
        void *buf;
        size_t len;

        if (offset_center->offset < 0) {
            /* Negative offset.  If there is a previous track, include
             * the lead-in plus the offset last samples from the
             * previous track.
             *
             * Unless this is the last track, include the lead-out
             * minus the offset last samples.
             */
            if (leader != NULL) {
                buf = (void *)(leader->samples_end + (2 * 5 * 588 + 1) + offset_center->offset);
                len = -offset_center->offset * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].checksum_v1 = _ar1_cksum(
                    0, 1, buf, len);
                result->checksums[result->nmemb].checksum_v2 = _ar2_cksum(
                    0, 1, buf, len);
                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0), buf, len);

                buf = (void *)(center->samples);
                len = (2 * 5 * 588 + 1) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].checksum_v1 +=
                    offset_center->checksum_v1[0];
                result->checksums[result->nmemb].checksum_v2 +=
                    offset_center->checksum_v2[0];
                result->checksums[result->nmemb].crc32_eac = crc32(
                    result->checksums[result->nmemb].crc32_eac, buf, len);

            } else {
                /* XXX Remove samples from current sum?  Implement AR
                 * sums here!  But that can probably not be done
                 * sensibly until AR uses the 2 * 588 + 1 buffers.
                 */
                buf = (void *)(center->samples + 1 * 5 * 588 + 0 + offset_center->offset);
                len = (1 * 5 * 588 + 1 - offset_center->offset) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].checksum_v1 =
                    0;
                result->checksums[result->nmemb].checksum_v2 =
                    0;
                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0), buf, len);
            }

            result->checksums[result->nmemb].checksum_v1 +=
                offset_center->checksum_v1[1];
            result->checksums[result->nmemb].checksum_v2 +=
                offset_center->checksum_v2[1];
            result->checksums[result->nmemb].crc32_eac = crc32_combine(
                result->checksums[result->nmemb].crc32_eac,
                offset_center->crc32[1],
                (center->stream->duration - (2 * 5 * 588 + 1) - (2 * 5 * 588 + 1)) * 2 * sizeof(int16_t));

            if (trailer != NULL) {
                /* XXX This part appears to be buggered (for AR)!  See
                 * Boney M.
                 *
                 * Note silly business because the AR code assumes 5 *
                 * 588 head/tail buffers, not 2 * 5 * 588 + 1.
                 */
                buf = (void *)(center->samples_end + 1 * 5 * 588 + 1 + offset_center->offset);
                len = (1 * 5 * 588) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].checksum_v1 = _ar1_cksum(
                    result->checksums[result->nmemb].checksum_v1,
                    center->stream->duration - (1 * 5 * 588 + 0) + 1, buf, len);
                result->checksums[result->nmemb].checksum_v2 = _ar2_cksum(
                    result->checksums[result->nmemb].checksum_v2,
                    center->stream->duration - (1 * 5 * 588 + 0) + 1, buf, len);

                buf = (void *)center->samples_end;
                len = (2 * 5 * 588 + 1 + offset_center->offset) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].crc32_eac = crc32(
                    result->checksums[result->nmemb].crc32_eac, buf, len);

            } else {
                /* XXX Remove samples from end of current sum?
                 * Implement AR sums here!
                 */
                buf = (void *)center->samples_end;
                len = (1 * 5 * 588 + 1 + offset_center->offset) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].crc32_eac = crc32(
                    result->checksums[result->nmemb].crc32_eac, buf, len);
            }

        } else if (offset_center->offset > 0) {
            /* Positive offset.  If there is a previous track, include
             * the lead-in minus the offset first samples.  Unless
             * this is the last track, include the leadout plus the
             * offset first samples of the next track.
             */
            if (leader != NULL) {

#if 0
                result->checksums[result->nmemb].checksum_v1 =
                    offset_center->checksum_v1[0];
                result->checksums[result->nmemb].checksum_v2 =
                    offset_center->checksum_v2[0];

                for (i = 0; i < offset_center->offset; i++) {
                    l = i + 1 - offset_center->offset;
                    p = l * ((uint32_t *)(center->samples))[i];
                    s = ((p >> 32) + p); // & 0xffffffff;

                    result->checksums[result->nmemb].checksum_v1 -= p; // & 0xffffffff;
                    result->checksums[result->nmemb].checksum_v2 -= s;
                }
#endif

                buf = (void *)(center->samples + offset_center->offset);
                len = (1 * 5 * 588 - 1) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].checksum_v1 = _ar1_cksum(
                    0, 1, buf, len);
                result->checksums[result->nmemb].checksum_v2 = _ar2_cksum(
                    0, 1, buf, len);

                buf = (void *)(center->samples + offset_center->offset);
                len = ((2 * 5 * 588 + 1) - offset_center->offset) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0), buf, len);

            } else {
                /* XXX Include 5 * 588 + 1 - offset samples from the
                 * leader.  Implement this for AccurateRip, once
                 * buffers are fixed.
                 *
                 * XXX There appears to be some breakage here: see
                 * Twin Peaks.  Maybe these sort of issues are better
                 * addressed once the code is a tad tidier.
                 */
                buf = (void *)(center->samples + 1 * 5 * 588 + 1 + offset_center->offset);
                len = ((1 * 5 * 588 + 0) - offset_center->offset) * 2 * sizeof(int16_t);

                result->checksums[result->nmemb].checksum_v1 =
                    0;
                result->checksums[result->nmemb].checksum_v2 =
                    0;
                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0), buf, len);
            }

            result->checksums[result->nmemb].checksum_v1 +=
                offset_center->checksum_v1[1];
            result->checksums[result->nmemb].checksum_v2 +=
                offset_center->checksum_v2[1];
            result->checksums[result->nmemb].crc32_eac = crc32_combine(
                result->checksums[result->nmemb].crc32_eac,
                 offset_center->crc32[1],
                (center->stream->duration - (2 * 5 * 588 + 1) - (2 * 5 * 588 + 1)) * 2 * sizeof(int16_t));

            if (trailer != NULL) {
                result->checksums[result->nmemb].checksum_v1 +=
                    offset_center->checksum_v1[2];
                result->checksums[result->nmemb].checksum_v2 +=
                    offset_center->checksum_v2[2];
                result->checksums[result->nmemb].crc32_eac = crc32_combine(
                    result->checksums[result->nmemb].crc32_eac,
                    offset_center->crc32[2],
                    (2 * 5 * 588 + 1) * 2 * sizeof(int16_t));

                buf = (void *)(trailer->samples);
                len = offset_center->offset;

                result->checksums[result->nmemb].checksum_v1 = _ar1_cksum(
                    result->checksums[result->nmemb].checksum_v1,
                    center->samples_tot / 2 + 1 - offset_center->offset,
                    buf, len * 2 * sizeof(int16_t));
                result->checksums[result->nmemb].checksum_v2 = _ar2_cksum(
                    result->checksums[result->nmemb].checksum_v2,
                    center->samples_tot / 2 + 1 - offset_center->offset,
                    buf, len * 2 * sizeof(int16_t));
                result->checksums[result->nmemb].crc32_eac = crc32(
                    result->checksums[result->nmemb].crc32_eac,
                    buf, len * 2 * sizeof(int16_t));

            } else {
                /* XXX Include offset samples from the trailer.
                 * Implement this for AccurateRip... later.
                 */
                buf = (void *)(center->samples_end);
                len = (1 * 5 * 588 + 1 + offset_center->offset);

                result->checksums[result->nmemb].crc32_eac = crc32(
                    result->checksums[result->nmemb].crc32_eac,
                    buf, len * 2 * sizeof(int16_t));
            }

        } else {
            /* Zero-offset.  If there is a previous track, include the
             * lead-in.  Unless this is the last track, include the
             * lead-out.
             *
             * XXX Not sure we need actually pre-calculate the
             * checksums in the leader and the trailer.  Offsets are
             * common, and we could just do it on demand... at least
             * for CRC32?
             *
             * XXX Problems remain with first and last tracks: check
             * Coldplay's B-sides and rarities for an example.
             */
            result->checksums[result->nmemb].checksum_v1 =
                offset_center->checksum_v1[1];
            result->checksums[result->nmemb].checksum_v2 =
                offset_center->checksum_v2[1];

            if (leader != NULL) {
                result->checksums[result->nmemb].checksum_v1 +=
                    offset_center->checksum_v1[0];
                result->checksums[result->nmemb].checksum_v2 +=
                    offset_center->checksum_v2[0];

                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0),
                    (void *)(center->samples),
                    (2 * 5 * 588 + 1) * 2 * sizeof(int16_t));
            } else {
                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0),
                    (void *)(center->samples + 1 * 5 * 588 + 1),
                    (1 * 5 * 588 + 0) * 2 * sizeof(int16_t));
            }

            result->checksums[result->nmemb].crc32_eac = crc32_combine(
                result->checksums[result->nmemb].crc32_eac,
                offset_center->crc32[1],
                (center->stream->duration - (2 * 5 * 588 + 1) - (2 * 5 * 588 + 1)) * 2 * sizeof(int16_t));

            if (trailer != NULL) {
                result->checksums[result->nmemb].checksum_v1 +=
                    offset_center->checksum_v1[2];
                result->checksums[result->nmemb].checksum_v2 +=
                    offset_center->checksum_v2[2];

                result->checksums[result->nmemb].crc32_eac = crc32(
                    result->checksums[result->nmemb].crc32_eac,
                    (void *)(center->samples_end),
                    (2 * 5 * 588 + 1) * 2 * sizeof(int16_t));
            } else {
                result->checksums[result->nmemb].crc32_eac = crc32(
                    crc32(0, Z_NULL, 0),
                    (void *)(center->samples_end),
                    (1 * 5 * 588 + 1) * 2 * sizeof(int16_t));
            }
        }

        result->nmemb += 1;
    }

    return (result);
}


int
fingersum_get_fingerprint(struct fingersum_context *ctx,
                          ChromaprintContext *cc,
                          char **fingerprint)
{
    /* Finalise the fingerprint calculation if necessary.
     */
    if (ctx->fingerprint == NULL) {
        if (_process(ctx, cc, ctx->remaining) != 0)
            return (-1);
    }


    /* Optionally, duplicate the fingerprint to the supplied pointer.
     */
    if (fingerprint != NULL) {
        *fingerprint = strdup(ctx->fingerprint);
        if (*fingerprint == NULL) {
            errno = ENOMEM;
            return (-1);
        }
    }

    return (0);
}


/* It should be safe to demote the int64_t to an unsigned integer.  An
 * unsigned integer is at least 16 bits wide and 2**16 seconds, or
 * 18.2 hours, is more than would fit onto any audio CD.
 */
unsigned int
fingersum_get_duration(const struct fingersum_context *ctx)
{
    uint64_t duration;

    duration = ctx->stream->time_base.num * ctx->stream->duration /
        ctx->stream->time_base.den;

    return ((unsigned int)duration);
}


/* XXX Don't forget about all the metadata that we cannot extract,
 * such as the iTunes tags.  Reread the Libav documentation on this,
 * in particular, what is AV_DICT_IGNORE_SUFFIX?
 *

Dictionary has 17 entries
major_brand=M4A
minor_version=0
compatible_brands=M4A mp42isom
creation_time=2014-01-30 20:42:57
title=Liquid insects
artist=Amorphous androgynous
composer=Brian Dougans, Garry Cobain
album=Tales of ephidrina
genre=Electronica
track=1/8
disc=1/1
date=1993
compilation=0
gapless_playback=0
tmpo=
encoder=iTunes 11.1.3
album_artist=Amorphous androgynous

 *
 */

/* FROM XLD:

ALBUM
ARTIST
COMPOSER
DATE
DISCTOTAL
ENCODER
GENRE
ITUNES_CDDB_1
TITLE
TOTALDISCS
TOTALTRACKS
TRACKTOTAL
disc
track
*/


/* FROM morituri:

ALBUM
ARTIST
DATE
MUSICBRAINZ_ALBUMARTISTID
MUSICBRAINZ_ALBUMID
MUSICBRAINZ_ARTISTID
MUSICBRAINZ_DISCID
MUSICBRAINZ_TRACKID
TITLE
track
*/

//#define TEST_ICONV 1
struct metadata *
fingersum_get_metadata(const struct fingersum_context *ctx)
{
    AVDictionaryEntry *entry;
    struct metadata *metadata;
    char *value;
    void *p;

#ifdef TEST_ICONV
    iconv_t cd;
    wchar_t *str;
    size_t inbytesleft, outbytesleft, ret; // XXX what are the POSIX names?


    /* XXX Should be part of context?  Is metadata always UTF-8?  How
     * to avoid mojibake http://en.wikipedia.org/wiki/Mojibake?  See
     * also http://en.wikipedia.org/wiki/ID3.  Check
     * https://libav.org/documentation/doxygen/master/group__metadata__api.html.
     */
    cd = iconv_open("wchar_t", "UTF-8");
    if (cd == (iconv_t)-1) {
        printf("iconv_open(3): %s\n", strerror(errno));
        exit(-1);
    }
#endif

    metadata = metadata_new();
    if (metadata == NULL)
        return NULL;

    entry = NULL;
    while ((entry = av_dict_get(
                ctx->ic->metadata, "", entry, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        value = strdup(entry->value);
        if (value == NULL) {
            metadata_free(metadata);
            return (NULL);
        }

#ifdef TEST_ICONV
        str = calloc(1024, 1);
        if (str == NULL)
            ;
        inbytesleft = strlen(value);
        outbytesleft = 1024;
        printf("calling iconv() on ->%s<- (%zd)\n", value, inbytesleft);

        char *s_in = value;
        char *s_out = (char *)str;

        ret = iconv(cd, &s_in, &inbytesleft, &s_out, &outbytesleft);
        if (ret < 0) {
            strerror(errno);
            exit(-1);
        }
        printf("%zd: converted ->%s<- (%zd) to ->%ls<- (%zd)\n", ret,
               value, strlen(value),
               str, wcslen(str));
        free(str);
#endif

        if (strcmp(entry->key, "album") == 0) {
            metadata->album = value;

        } else if (strcmp(entry->key, "album_artist") == 0) {
            metadata->album_artist = value;

        } else if (strcmp(entry->key, "artist") == 0) {
            metadata->artist = value;

        } else if (strcmp(entry->key, "compilation") == 0) {
            metadata->compilation = value;

        } else if (strcmp(entry->key, "composer") == 0) {
            metadata->composer = value;

        } else if (strcmp(entry->key, "date") == 0) {
            metadata->date = value;

        } else if (strcmp(entry->key, "disc") == 0) {
            metadata->disc = value;

        } else if (strcmp(entry->key, "sort_album_artist") == 0) {
            metadata->sort_album_artist = value;

        } else if (strcmp(entry->key, "sort_artist") == 0) {
            metadata->sort_artist = value;

        } else if (strcmp(entry->key, "sort_composer") == 0) {
            metadata->sort_composer = value;

        } else if (strcmp(entry->key, "title") == 0) {
            metadata->title = value;

        } else if (strcmp(entry->key, "track") == 0) {
            metadata->track = value;

        } else {
            free(value);
            value = strdup(entry->key);
            if (value == NULL) {
                metadata_free(metadata);
                return (NULL);
            }

            p = realloc(
                metadata->unknown, (metadata->nmemb + 1) * sizeof(char *));
            if (p == NULL) {
                metadata_free(metadata);
                free(value);
                return (NULL);
            }

            metadata->unknown = p;
            metadata->unknown[metadata->nmemb++] = value;
        }
    }

#ifdef TEST_ICONV
    if (iconv_close(cd) != 0) {
        strerror(errno);
        exit(-1);
    }
#endif

    return (metadata);
}


/* One audio CD sector holds 1/75:th of a second of audio data, or
 * 44100 / 75 = 588 samples.  A long int is at least 32 bits wide and
 * 2**31 sectors corresponds to 331 days of audio data.  Returning an
 * signed long int is safe.
 */
long int
fingersum_get_sectors(const struct fingersum_context *ctx)
{
    uint64_t sectors;

    if (ctx->stream->duration % 588 != 0) { // XXX Sanity check
        /* This may not succeed for lossy rips.
         *
         * XXX Should probably result in an error then; there's a
         * bunch of stuff that cannot be done for lossy rips.
         *
         * This happens for initial rip of Renegades.
         */
        fprintf(stderr, "NOT INTEGER MULTIPLE OF FRAME SIZE %" PRId64 "d\n",
                ctx->stream->duration);
        return (-1);
    }

    sectors = (ctx->stream->duration + 588 - 1) / 588;

    return ((unsigned long int)sectors);
}
