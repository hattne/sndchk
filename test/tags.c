/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 *
 * $Id:$
 */

// Alternative: get the metadata using libav?
// AVDictionary *metadata=container->metadata; [from http://stackoverflow.com/questions/9799560/decode-audio-using-libavcodec-and-play-using-libao]
//
// But what about BPM [or is this tmpo?], COMPILATION [cpil], aART
// [but this is album artist, so what about album artwork], and
// possibly pgap [gapless playback], as well as the Apple-specific
// ones:
//
//   ----:com.apple.iTunes:Encoding Params
//   ----:com.apple.iTunes:iTunNORM
//   ----:com.apple.iTunes:iTunes_CDDB_1
//   ----:com.apple.iTunes:iTunes_CDDB_TrackNumber
//
// Only remaining item is album artwork!
//
// make tags && ./tags test_album/01\ Liquid\ insects.m4a
// avmetareadwrite -o test_album/01\ Liquid\ insects.m4a 

#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>


/* See https://patches.libav.org/patch/56003/
 *
 * https://libav.org/doxygen/master/group__avoptions.html
 */
int main (int argc, char **argv)
{
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *tag = NULL;
    int ret;

    if (argc != 2) {
        printf("usage: %s <input_file>\n"
               "example program to demonstrate the use of the libavformat metadata API.\n"
               "\n", argv[0]);
        return 1;
    }

    AVDictionary *options = NULL;
    AVDictionaryEntry *e = NULL;
    av_dict_set(&options, "export_all", "1", 0);

    av_register_all();
    if ((ret = avformat_open_input(&fmt_ctx, argv[1], NULL, &options)))
        return ret;

    e = NULL;
    while (av_dict_get(options, "", e, AV_DICT_IGNORE_SUFFIX) != NULL) {
        if (e == NULL)
            break;
        fprintf(stderr, "Option %s not recognized by the demuxer.\n", e->key);
    }

//    av_dump_format(fmt_ctx, 0, NULL, 0);

/*
    const AVOption *opt;
    printf("av_opt_set() said %d\n",
           av_opt_set_int(fmt_ctx, "export_all", 1, 0));
    while ((opt = av_opt_next(fmt_ctx, opt)) != NULL) {
        printf("Got an option, name %s [%s] [%s]\n",
               opt->name, opt->help, opt->unit);
    }
*/    

    printf("Dictionary has %d entries\n", av_dict_count(fmt_ctx->metadata));

    while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        printf("%s=%s\n", tag->key, tag->value);

    avformat_free_context(fmt_ctx);
    return 0;
}
