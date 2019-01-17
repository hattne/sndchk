/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 *
 * $Id:$
 */

#include <stdint.h>

#include <inttypes.h>

#include <libavformat/avformat.h>
#include <libavresample/avresample.h>

/* XXX Which ones are actually needed?
 */
#include <neon/ne_session.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
#include <neon/ne_xml.h>

#include <zlib.h>


struct chunk
{
    uint32_t CRC;
    uint32_t unk;
    
    int confidence;
};


struct entry
{
    struct chunk *chunks;

    size_t track_count;
    uint32_t disc_cddb;
    uint32_t disc_id1;
    uint32_t disc_id2;
};


struct userdata
{
    struct entry *entries;
    size_t nmemb;


    /* cannot be const, because then realloc() in _ud_append() breaks!
     */
    char *buf;

    size_t len;
    size_t capacity;
};


static size_t
_sum_digits(uint64_t x)
{
    size_t s;
    for (s = 0; x > 0; x /= 10)
        s += x % 10;
    return (s);
}


#if 0
static int
_ud_append(struct userdata *ud, const char *buf, size_t len)
{
    void *p;

    if (ud->len + len > ud->capacity) {
        p = realloc(ud->buf, (ud->len + len) * sizeof(char));
        if (p == NULL)
            return (-1);
        ud->buf = p;
        ud->capacity = ud->len + len;
    }

    memcpy(ud->buf + ud->len, buf, len);
    ud->len += len;
    return (0);
}


/* Note that the buffers may overlap, hence use memmove(3).
 */
static int
_ud_save(struct userdata *ud, const char *buf, size_t len)
{
    void *p;

    if (len > ud->capacity) {
        p = realloc(ud->buf, len * sizeof(char));
        if (p == NULL)
            return (-1);
        ud->buf = p;
        ud->capacity = len;
    }
    
    memmove(ud->buf, buf, len);
    ud->len = len;
    return (0);
}


/* Returns zero on success, or non-zero on error.  XXX Need to handle
 * the case where we're actually blocked!
 *
 * buf should possibly be (uint8_t *) to guarentee that we're working
 * with octets.
 *
 * XXX Does this actually work as advertised, i.e. when called several
 * times with different blocks?
 */
static int
my_block_reader(void *userdata, const char *buf, size_t len)
{
    struct userdata *ud;
    void *p;
    size_t i;
    size_t off;


    /* Exit if this is the last block. XXX Ensure that all the
     * buffered stuff is processed?
     */
    printf("Got response of length %zd\n", len);
    ud = (struct userdata *)userdata;
    if (len <= 0) {
        if (ud->len > 0)
            printf("INCONSISTENCY: buffer is not empty!\n");
        return (0);
    }


    /* Append to existing buffer, if necessary
     */
    if (ud->len > 0) {
        if (_ud_append(ud, buf, len) != 0)
            ; // XXX

        buf = ud->buf;
        len = ud->len;
    }

    off = 0;
    do {
        struct entry *entry;
        //uint32_t chunk_disc_cddb, chunk_disc_id1, chunk_disc_id2;
        //uint8_t chunk_track_count;
        size_t chunk_track_count;


        /* If we don't have enough data for the "headers", save
         * (unless already saved) and return.  If we don't have enough
         * data for the "table" save (unless already saved) and
         * return.
         */
        if (len < 13) {
            if (ud->buf != buf && _ud_save(ud, buf, len) != 0)
                ; // XXX
            return (0);
        }

        chunk_track_count = *((uint8_t *)(buf + off + 0));
        //chunk_disc_id1 = *((uint32_t *)(buf + off + 1));
        //chunk_disc_id2 = *((uint32_t *)(buf + off + 5));
        //chunk_disc_cddb = *((uint32_t *)(buf + off + 9));

        if (len < 13 + chunk_track_count * 9) {
            if (ud->buf != buf && _ud_save(ud, buf, len) != 0)
                ; // XXX
            return (0);
        }

        /* Allocate all the necessary memory.
         */
        p = realloc(ud->entries, (ud->nmemb + 1) * sizeof(struct entry));
        if (p == NULL)
            ;
        ud->entries = p;
        entry = &ud->entries[ud->nmemb++];

        entry->chunks = calloc(chunk_track_count, sizeof(struct chunk));
        if (entry->chunks == NULL)
            ; // XXX
        entry->track_count = chunk_track_count;
        entry->disc_cddb = *((uint32_t *)(buf + off + 9));
        entry->disc_id1 = *((uint32_t *)(buf + off + 1));
        entry->disc_id2 = *((uint32_t *)(buf + off + 5));

//        printf("Chunks: %zd\n", entry->track_count);
//        printf("CDDB: 0x%08x\n", entry->disc_cddb);
//        printf("ID1: 0x%08x\n", entry->disc_id1);
//        printf("ID2: 0x%08x\n", entry->disc_id2);

        /* XXX Check that it matched up with what was expected.  The
         * choose the matching one below.  XXX What is unk?
         */
        for (i = 0; i < chunk_track_count; i++) {
            //uint32_t CRC, unk;
            //uint8_t confidence;

            //confidence = *((uint8_t  *)(buf + off + 13 + 9 * i + 0));
            //CRC =        *((uint32_t *)(buf + off + 13 + 9 * i + 1));
            //unk =        *((uint32_t *)(buf + off + 13 + 9 * i + 5));

            entry->chunks[i].confidence = *((uint8_t  *)(buf + off + 13 + 9 * i + 0));;
            entry->chunks[i].CRC = *((uint32_t *)(buf + off + 13 + 9 * i + 1));;
            entry->chunks[i].unk = *((uint32_t *)(buf + off + 13 + 9 * i + 5));;

//            printf("  Track %0.2zd: confidence %3d CRC %08x %08x\n",
//                   i + 1,
//                   entry->chunks[i].confidence,
//                   entry->chunks[i].CRC,
//                   entry->chunks[i].unk);
        }

        buf += 13 + chunk_track_count * 9;
        len -= 13 + chunk_track_count * 9;

    } while (len > 0);

    printf("Ending with %zd and %zd\n", off, len);


    /* All the buffered data has been processed now.
     */
    ud->len = 0;

/*
Track  1: rip accurate     (max confidence     21) [38e0f8af], DB [38e0f8af]
Track  2: rip accurate     (max confidence     20) [2f9f439f], DB [2f9f439f]
Track  3: rip accurate     (max confidence     21) [10b12126], DB [10b12126]
Track  4: rip accurate     (max confidence     21) [0b75f8ea], DB [0b75f8ea]
Track  5: rip accurate     (max confidence     21) [ef7f7234], DB [ef7f7234]
Track  6: rip accurate     (max confidence     21) [cca9c17b], DB [cca9c17b]
Track  7: rip accurate     (max confidence     20) [ff4f11f1], DB [ff4f11f1]
Track  8: rip accurate     (max confidence     21) [0906eb93], DB [0906eb93]
*/

    return (0);
}
#endif


/* XXX Should operate like cksum(1)?
 */
int
main(int argc, char *argv[])
{
    /* XXX This is wasting about 2 * 11.5 kiB of memory.  That's ugly.
     *
     * How does metaflac --show-total-samples do it?
     */
    uint64_t checksum_ca[5 * 588 + 1];

    size_t i;
    int ret, stream_index;
    AVCodec *decoder;
    AVCodecContext *cc;


    av_log_set_level(AV_LOG_ERROR);


#if 0
    /* URL http://www.accuraterip.com/accuraterip/a/6/e/dBAR-008-000b0e6a-004998ec-64089008.bin
     */
    struct userdata ud;
    ne_request *req;
    ne_session *sess;
    int rc;
    

    if (ne_sock_init() != 0)
        ; // XXX
     
    sess = ne_session_create("http", "www.accuraterip.com", 80);
    
    req = ne_request_create(
        sess, "GET",
        "/accuraterip/a/6/e/dBAR-008-000b0e6a-004998ec-64089008.bin");
    
    ud.buf = NULL;
    ud.capacity = 0;
    ud.len = 0;

    ne_add_response_body_reader(
        req, ne_accept_always, my_block_reader, &ud);

    switch (ne_request_dispatch(req)) {
    case NE_AUTH:
    case NE_PROXYAUTH:
        printf("NE_AUTH: server or proxy server authentication error\n");
    case NE_OK:
        /* On success or authentication error, the actual
         * response-status can be retrieved using ne_get_status().
         */
        rc = ne_get_status(req)->code;
        if (rc != 200)
            printf("GOT HTTP failure (%d)!\n", rc); // XXX
        break;

    case NE_CONNECT:
        printf("NE_CONNECT connection could not be established\n");
        break;
    case NE_TIMEOUT:
        printf("NE_TIMEOUT timeout occurred sending or reading from the server");
        break;
    case NE_ERROR:
        printf("NE_ERROR: other fatal dispatch error\n");
        break;
    }

    ne_request_destroy(req);
    ne_session_destroy(sess);
    ne_sock_exit();

    for (i = 0; i < ud.nmemb; i++) {
        struct entry *entry;
        size_t j;
        
        entry = &ud.entries[i];

        printf("Chunks: %zd\n", entry->track_count);
        printf("CDDB: 0x%08x\n", entry->disc_cddb);
        printf("ID1: 0x%08x\n", entry->disc_id1);
        printf("ID2: 0x%08x\n", entry->disc_id2);

        for (j = 0; j < entry->track_count; j++) {
            printf("  Track %0.2zd: confidence %3d CRC %08x %08x\n",
                   j + 1,
                   entry->chunks[j].confidence,
                   entry->chunks[j].CRC,
                   entry->chunks[j].unk);
        }
    }

    return (0);
#endif


    /* See http://www.accuraterip.com/3rdparty-access.htm for more
     *
     * Technical details:
     * http://forum.dbpoweramp.com/showthread.php?20641-AccurateRip-CRC-Calculation
     */
#if 0
    FILE *stream = fopen("t2.bin", "r");
    do {
        uint8_t chunk_track_count;
        fread(&chunk_track_count, 1, 1, stream);
        printf("Chunks: %d\n", chunk_track_count);

        uint32_t chunk_disc_id1;
        fread(&chunk_disc_id1, 4, 1, stream);
        printf("ID1: 0x%08x\n", chunk_disc_id1);

        uint32_t chunk_disc_id2;
        fread(&chunk_disc_id2, 4, 1, stream);
        printf("ID2: 0x%08x\n", chunk_disc_id2);

        uint32_t chunk_disc_cddb;
        fread(&chunk_disc_cddb, 4, 1, stream);
        printf("CDDB: 0x%08x\n", chunk_disc_cddb);
    

        /* XXX Check that it matched up with what was expected.  The
         * choose the matching one below.
         */
        for (i = 0; i < chunk_track_count; i++) {
            uint32_t CRC, unk;
            uint8_t confidence;

            fread(&confidence, 1, 1, stream);
            fread(&CRC, 4, 1, stream);
            fread(&unk, 4, 1, stream);
            printf("  Track %0.2zd: confidence %3d CRC %08x %08x\n",
                   i + 1, confidence, CRC, unk);

        }
        printf("Status of stream %d %d %ld\n",
               feof(stream), ferror(stream), ftell(stream));

        //break;
        
    } while (!ferror(stream) && !feof(stream));

    return (0);
#endif


    /* For the URL construction
     */
    uint64_t offset = 0, discId1 = 0, discId2 = 0, cddbDiscId = 0;
    size_t total_samples = 0, track_length = 0, track_samples = 0;


    /* For the "The age of plastic".  Can this be looked up someplace?
     *
     * http://musicbrainz.org/cdtoc/ytynvHrgv94Rf7aEfTLFPvQv5ks-
     *
     * Starts at 150 + 32 = 182
     *
     * The big question then, how does morituri handle the 150-sector
     * pregap?  Is it just subtracted?  Or rather, do all start
     * sectors have 150 subtracted in the AR database?
     */
    offset = 18816 / 588; //0x5069;
    cddbDiscId = 0;


    /* For "Total madness".
     */
    offset = 0;
    cddbDiscId = 0;


    /* For "A slight case of overbombing".
     *
     * http://musicbrainz.org/cdtoc/c7Jy1cSdyXbA1SQgoXJp8WtQaPk-
     *
     * Starts at 150 + 32 = 182
     */
    offset = 32;
    cddbDiscId = 0;

    long int skip = strtol(argv[1], NULL, 10);
    printf("Read skip %ld\n", skip);

    for (i = 2; i < argc; i++) {
        AVPacket packet;
        AVAudioResampleContext *rc;
        AVFormatContext *ic;
        AVStream *stream;
        AVFrame *frame;
        uint8_t **data, *output[1];
        int64_t channel_layout;
        int consumed, duration, nb_samples,  linesize;

        ic = NULL;
        rc = NULL;
        if (avformat_open_input(&ic, argv[i], NULL, NULL) != 0) {
            /* fprintf(
             *     stderr, "ERROR: couldn't open the file\n");
             * XXX See av_strerror
             */
            return (-1);
        }

        ret = avformat_find_stream_info(ic, NULL);
        if (ret < 0) {
            /* fprintf(
             *     stderr,
             *     "ERROR: couldn't find stream information in the file\n");
             * XXX See av_strerror
             */
            return (-1);
        }

        stream_index = av_find_best_stream(
            ic, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (stream_index < 0 || decoder == NULL) {
            /* fprintf(
             *     stderr,
             *     "ERROR: couldn't find any audio stream in the file\n");
             * XXX See av_strerror
             */
            return (-1);
        }

        stream = ic->streams[stream_index];
        if (stream->duration != AV_NOPTS_VALUE) {
            /*

              // Safe to assume duration is the number of samples?

            printf("HATTNE got %d %d %d\n",
                   stream->time_base.num,
                   stream->time_base.den,
                   stream->duration);
            */
            duration = stream->time_base.num * stream->duration /
                stream->time_base.den;
        } else if (ic->duration != AV_NOPTS_VALUE) {
            duration = ic->duration / AV_TIME_BASE;
        } else {
            /* fprintf(
             *     stderr, "ERROR: couldn't detect the audio duration\n");
             */
            return (-1);
        }

        /* avcodec_open2() is not thread-safe!
         */
        cc = stream->codec;
        cc->request_sample_fmt = AV_SAMPLE_FMT_S16;
        ret = avcodec_open2(cc, decoder, NULL);
        if (ret < 0) {
            /* fprintf(
             *    stderr, "ERROR: couldn't open the decoder\n");
             * XXX See av_strerror
             */
            return (-1);
        }
        if (cc->channels <= 0) {
            /* fprintf(
             *     stderr, "ERROR: no channels found in the audio stream\n");
             */
            return (-1); // XXX Possible leak!
        }

        if (cc->sample_fmt != AV_SAMPLE_FMT_S16) {
            channel_layout = cc->channel_layout;
            if (!channel_layout)
                channel_layout = av_get_default_channel_layout(cc->channels);

            rc = avresample_alloc_context();
            if (rc == NULL) {
                /* fprintf(
                 *     stderr, "ERROR: couldn't allocate audio converter\n");
                 */
                return (-1);
            }

            av_opt_set_int(rc, "in_channel_layout", channel_layout, 0);
            av_opt_set_int(rc, "in_sample_fmt", cc->sample_fmt, 0);
            av_opt_set_int(rc, "in_sample_rate", cc->sample_rate, 0);
            av_opt_set_int(rc, "out_channel_layout", channel_layout, 0);
            av_opt_set_int(rc, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            av_opt_set_int(rc, "out_sample_rate", cc->sample_rate, 0);

            ret = avresample_open(rc);
            if (ret < 0) {
                /* fprintf(
                 *     stderr, "ERROR: couldn't initialize the audio converter\n");
                 * XXX See av_strerror
                 */
                return (-1);
            }
        }

        frame = av_frame_alloc();
        if (frame == NULL)
            /* XXX Set errno?
             */
            return (-1);

        output[0] = NULL;
        nb_samples = 0;

        uint32_t checksum = 0;
        size_t j, k;
        uLong my_crc32 = crc32(0, Z_NULL, 0);

        k = 0;

        checksum_ca[5 * 588 + 1 - 1] = 0;
        track_samples = 0;
        
        for ( ; ; ) {
            if (av_read_frame(ic, &packet) < 0)
                break;
            if (packet.stream_index != stream->index) {
                av_packet_unref(&packet);
                continue;
            }

            if (avcodec_send_packet(cc, &packet) != 0) {
                av_packet_unref(&packet);
                errno = EPROTO;
                return (-1);
            }
            av_packet_unref(&packet);

            consumed = avcodec_receive_frame(cc, frame);
            if (consumed < 0)
                /* fprintf(
                 *     stderr, "WARNING: error decoding audio\n");
                 */
                continue;

            if (rc != NULL) {
                if (frame->nb_samples > nb_samples) {
                    av_freep(&output[0]);
                    ret = av_samples_alloc(
                        output, &linesize, cc->channels,
                        frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                    if (ret < 0) {
                        /* fprintf(
                         *     stderr,
                         *     "ERROR: couldn't allocate audio converter buffer\n");
                         * XXX See av_strerror
                         */
                        return (-1);
                    }
                    nb_samples = frame->nb_samples;
                }

                ret = avresample_convert(
                    rc, output, 0, frame->nb_samples,
                    (uint8_t **)frame->data, 0, frame->nb_samples);
                if (ret < 0) {
                    /* fprintf(
                     *     stderr, "ERROR: couldn't convert the audio\n");
                     * XXX See av_strerror
                     */
                    return (-1);
                }
                data = output;
            } else {
                data = frame->data;
            }


            /** THIS IS WHERE IT STARTS **/

            /* SEE AccurateRipChecksumTask.do_checksum_buffer() in
             * morituri/morituri/common/checksum.py
             *
             * http://forum.dbpoweramp.com/showthread.php?20641-AccurateRip-CRC-Calculation
             *
             * http://www.hydrogenaud.io/forums/index.php?showtopic=66233&hl=
             */

            // Expect 2f9f439f for track 2
            //        0906eb93 for track 8

            total_samples += frame->nb_samples;
            track_samples += frame->nb_samples;
            for (j = 0; j < frame->nb_samples; j++) {
                uint32_t value = ((uint32_t *)(data[0]))[j];
                k += 1;

//                if (i == 0 && k < 5 * 588)
//                    continue;
#if 0
                /* This is for skipping the leader...
                 */
                if (k < skip + 1)
                    continue;
#endif
#if 0
                /* This is for skipping the trailer...
                 */
                if (k > stream->duration - skip)
                    continue;
#endif
                if (i == argc - 1) {
                    checksum_ca[(k - 1) % (5 * 588 + 1)] =
                        checksum_ca[(k - 2 + 5 * 588 + 1) % (5 * 588 + 1)] + k * value;
                    //continue;
                }

                checksum += (uint64_t)k * value;
                my_crc32 = crc32(my_crc32, (void *)((uint32_t *)(data[0]) + j), 2 * 2);
            }
        }

        printf("track samples: %zd (diff to stream %llu)\n",
               track_samples, stream->duration - track_samples);

//        if (i == argc - 1)
//            checksum = checksum_ca[(k - 0) % (5 * 588 + 1)];

        //printf("Did ->%s<-\n", argv[i]);
        printf("%zd / %d: got checksum 0x%08x [skip=%ld]\n",
               i, argc - 1, checksum, skip);
        printf("%zd / %d: got CRC32    0x%08lx [skip=%ld]\n",
               i, argc - 1, my_crc32, skip);

        track_length = track_samples / 588;
        if (track_length * 588 != track_samples)
            printf("Neon lights\n");

        //offset = 0; // Frame index of start of track
        //offset = getTrackStart(i);
        discId1 += offset;
        discId2 += (offset != 0 ? offset : 1) * (i + 0);

        //cddbDiscId += _sum_digits(lrint(offset / 75.0) + 2);
        cddbDiscId += _sum_digits(offset / 75 + 2);

        printf("  offset %08llx cddb %llu (%ld %zd)\n", offset, cddbDiscId,
               lrint(offset / 75),
               _sum_digits(lrint(offset / 75.0)));
        
        offset += track_length;
    }

    discId1 += offset;
    discId2 += offset * ((argc - 1) + 1); // Number of tracks plus 1
     
    printf("total samples: %zd\n", total_samples);

    // Offset of last track - offset of first track
    cddbDiscId =
        ((cddbDiscId % 255) << 24) +
        ((offset / 75 - 0 / 75) << 8) +
        (argc - 1); // Number of tracks

/*
  trackNo 0 offset 00000000 cddb 2 (0 0)
  trackNo 1 offset 00000020 cddb 4 (0 0)
  trackNo 2 offset 00005a73 cddb 8 (308 11)
  trackNo 3 offset 0000a4ce cddb 23 (562 13)
  trackNo 4 offset 0000e288 cddb 42 (773 17)
  trackNo 5 offset 00013a3d cddb 54 (1072 10)
  trackNo 6 offset 00017ee4 cddb 66 (1306 10)
  trackNo 7 offset 0001ce76 cddb 80 (1578 21)
  trackNo 8 offset 000222a1 cddb 102 (1865 20)
  trackNo 9 offset 00028249 cddb 102 (2192 14)
Id1 000b0e6a Id2 0054a757 cddb 66089009
*/

    /*
    offset = 96663084 / 1; // Frame index of end of last track
    //offset = getTrackEnd(last.number) + 1;
    */

    discId1 &= 0xffffffff;
    discId2 &= 0xffffffff;


    /* For Buggles:
     *
     *  discId1: 000b0e6a
     *  discId2: 004998ec  or 0054a757 from ARFlac.pl
     *  cddbdiscid: 64089008
     */
    printf("Got DiscId1 0x%08llx DiscId2 0x%08llx cddb 0x%08llx\n", discId1, discId2, cddbDiscId);

    printf("http://www.accuraterip.com/accuraterip/%.1x/%.1x/%.1x/dBAR-%.3d-%.8x-%.8x-%.8x.bin\n",
           (int)(discId1 & 0xf),
           (int)((discId1 >> 4) & 0xf),
           (int)((discId1 >> 8) & 0xf),
           (argc - 1), // number of tracks
           (int)discId1,
           (int)discId2,
           (int)cddbDiscId);

    return (0);
}
