/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 *
 * $Id:$
 */

#include <iomanip>
#include <iostream>

#include <taglib/tag_c.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

#include <taglib/tfilestream.h>


// Guess: taglib relies on file name for type determination (and
// that's why it can't take a stream as input).
//
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

#if 0
class HattneFileStream: public TagLib::FileStream
{

public:
    HattneFileStream(const char *path)
        : TagLib::FileStream(path)
    {
    }


    // See FileRef::create() in fileref.cpp called from XXX
    // 
    // How is this list populated?  Using addFileTypeResolver!
    File *
    create(fileName, bool readAudioProperties, AudioProperties::ReadStyle audioPropertiesStile) // static
    {
        
        List<const FileTypeResolver *>::ConstIterator it = FileRefPrivate::fileTypeResolvers.begin();

        for(; it != FileRefPrivate::fileTypeResolvers.end(); ++it) {
            File *file = (*it)->createFile(fileName, readAudioProperties, audioPropertiesStyle);
            if(file)
                return file;
        }
    }
};


class HattneFile : public
#endif


#if 1
#include <stdio.h>

extern "C" 
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}
    

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
#else
int
main(int argc, char *argv[])
{
    TagLib::FileRef f;
    TagLib::PropertyMap tags;
    TagLib::AudioProperties *properties;
    TagLib::StringList strings;
    TagLib::Tag *tag;
    TagLib::PropertyMap::ConstIterator j;
    TagLib::StringList::ConstIterator k;
    unsigned int longest;
    int i;


    for(i = 1; i < argc; i++) {
        printf("******************** \"%s\" ********************\n",
               argv[i]);

#if 1
        f = TagLib::FileRef(argv[i]);
#else
        HattneFileStream* nisse = new HattneFileStream(argv[i]);
        TagLib::File* sture = new TagLib::File(nisse);        
        f = TagLib::FileRef(sture);
#endif

        if (f.isNull())
            continue;

        tag = f.tag();
        if (tag == NULL)
            continue;

        printf("-- TAG (basic) --\n"
               "title   - \"%ls\"\n"
               "artist  - \"%ls\"\n"
               "album   - \"%ls\"\n"
               "comment - \"%ls\"\n"
               "genre   - \"%ls\"\n"
               "year    - \"%u\"\n"
               "track   - \"%u\"\n",
               tag->title().toCWString(),
               tag->artist().toCWString(),
               tag->album().toCWString(),
               tag->comment().toCWString(),
               tag->genre().toCWString(),
               tag->year(),
               tag->track());

        tags = f.file()->properties();
        longest = 0;
        for(j = tags.begin(); j != tags.end(); j++) {
            if (j->first.size() > longest)
                longest = j->first.size();
        }

        printf("-- TAG (properties) --\n");
        for(j = tags.begin(); j != tags.end(); j++) {
            for(k = j->second.begin(); k != j->second.end(); k++) {
                printf("%*ls - \"%ls\"\n",
                       -longest,
                       j->first.toCWString(),
                       k->toCWString());
            }
        }

        printf("-- TAG (unsupported) --\n");
        strings = tags.unsupportedData();
        for (k = strings.begin(); k != strings.end(); k++)
            printf("%ls\n", k->toCWString());

        properties = f.audioProperties();
        if(properties) {
            printf("-- AUDIO --\n"
                   "length      - %d:%02d\n"
                   "bitrate     - %d\n"
                   "sample rate - %d\n"
                   "channels    - %d\n",
                   properties->length() / 60,
                   properties->length() % 60,
                   properties->bitrate(),
                   properties->sampleRate(),
                   properties->channels());
        }
    }

    return 0;
}
#endif
