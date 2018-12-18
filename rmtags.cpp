/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 *
 * $Id:$
 */

#include <err.h>

#include <iomanip>
#include <iostream>

#include <taglib/tag_c.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>


int
main(int argc, char *argv[])
{
    TagLib::FileRef f;
    TagLib::Tag *tag;
    int i;


    for(i = 1; i < argc; i++) {
        f = TagLib::FileRef(argv[i]);
        if (f.isNull())
            continue;

        tag = f.tag();
        if (tag == NULL)
            continue;

        tag->setTitle(TagLib::String::null);
        tag->setArtist(TagLib::String::null);
        tag->setAlbum(TagLib::String::null);
        tag->setComment(TagLib::String::null);
        tag->setGenre(TagLib::String::null);
        tag->setYear(0);
        tag->setTrack(0);

        f.file()->removeUnsupportedProperties(
            f.file()->properties().unsupportedData());
        f.file()->setProperties(TagLib::PropertyMap());

	if (!f.save())
            err(EXIT_FAILURE, "Failed to save %s", argv[i]);
    }

    return 0;
}
