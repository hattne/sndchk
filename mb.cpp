/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

#include <musicbrainz5/Disc.h>
#include <musicbrainz5/Collection.h>
#include <musicbrainz5/CollectionList.h>
#include <musicbrainz5/HTTPFetch.h>
#include <musicbrainz5/Medium.h>
#include <musicbrainz5/Query.h>
#include <musicbrainz5/Recording.h>
#include <musicbrainz5/Release.h>
#include <musicbrainz5/ReleaseGroup.h>
#include <musicbrainz5/Track.h>
#include <musicbrainz5/TrackList.h>


void ListCollection(
    MusicBrainz5::CQuery& Query, const std::string& CollectionID)
{
    MusicBrainz5::CMetadata Metadata = Query.Query(
        "collection", CollectionID, "releases");
    std::cout << Metadata << std::endl;
}


int
main(int argc, char *argv[])
{
    //MusicBrainz5::CQuery Query("collectionexample-1.0","test.musicbrainz.org");
    MusicBrainz5::CQuery Query("collectionexample-1.0");
    try {
        MusicBrainz5::CQuery::tParamMap prutt;
        //prutt["recording"] = "3de6ea6f-1828-4129-bbf1-62d0cfa76895";
        //prutt["recording"] = "e40ee25e-9c04-4884-919b-7059f5135229";
        //prutt["recording"] = "98f50087-abaf-4764-947b-f70268eac88a";


        /* Browse
         */
        //prutt["recording"] = "e40ee25e-9c04-4884-919b-7059f5135229";
        prutt["recording"] = "98f50087-abaf-4764-947b-f70268eac88a";
        prutt["inc"] = "media recordings";


        /* Search
         */
//        prutt["query"] = "reid:5345137c-dc6e-4d56-9bf8-19d270c27155 OR reid:30d77e61-2062-4fcc-87c1-f35edd4efe64";
//        prutt["inc"] = "media recordings";

        MusicBrainz5::CMetadata metadata = Query.Query(
            "release", "", "", prutt);
        printf("MARKER 1\n");

        MusicBrainz5::CReleaseList* rl = metadata.ReleaseList();
        printf("MARKER 2: %d results\n", rl->NumItems());
        for (int i = 0; i < rl->NumItems(); i++) {
            MusicBrainz5::CRelease *r = rl->Item(i);
            MusicBrainz5::CMediumList *ml = r->MediumList();

            printf("Release %s \"%s\"\n",
                   r->ID().c_str(), r->Title().c_str());

            for (int j = 0; j < ml->NumItems(); j++) {
                MusicBrainz5::CMedium *m = ml->Item(j);
                MusicBrainz5::CTrackList *tl = m->TrackList();
                printf("  Got medium, tracklist %d\n", tl->NumItems());

                for (int k = 0; k < tl->NumItems(); k++)
                    printf("    Track %d: %d\n", k + 1, tl->Item(k)->Length());
            }
        }
    }

    catch (MusicBrainz5::CConnectionError& Error) {
        std::cout << "Connection Exception: '" << Error.what() << "'" << std::endl;
        std::cout << "LastResult: " << Query.LastResult() << std::endl;
        std::cout << "LastHTTPCode: " << Query.LastHTTPCode() << std::endl;
        std::cout << "LastErrorMessage: " << Query.LastErrorMessage() << std::endl;
    }
    
    catch (MusicBrainz5::CTimeoutError& Error) {
        std::cout << "Timeout Exception: '" << Error.what() << "'" << std::endl;
        std::cout << "LastResult: " << Query.LastResult() << std::endl;
        std::cout << "LastHTTPCode: " << Query.LastHTTPCode() << std::endl;
        std::cout << "LastErrorMessage: " << Query.LastErrorMessage() << std::endl;
    }
    
    catch (MusicBrainz5::CAuthenticationError& Error) {
        std::cout << "Authentication Exception: '" << Error.what() << "'" << std::endl;
        std::cout << "LastResult: " << Query.LastResult() << std::endl;
        std::cout << "LastHTTPCode: " << Query.LastHTTPCode() << std::endl;
        std::cout << "LastErrorMessage: " << Query.LastErrorMessage() << std::endl;
    }

    catch (MusicBrainz5::CFetchError& Error) {
        std::cout << "Fetch Exception: '" << Error.what() << "'" << std::endl;
        std::cout << "LastResult: " << Query.LastResult() << std::endl;
        std::cout << "LastHTTPCode: " << Query.LastHTTPCode() << std::endl;
        std::cout << "LastErrorMessage: " << Query.LastErrorMessage() << std::endl;
    }

    catch (MusicBrainz5::CRequestError& Error) {
        std::cout << "Request Exception: '" << Error.what() << "'" << std::endl;
        std::cout << "LastResult: " << Query.LastResult() << std::endl;
        std::cout << "LastHTTPCode: " << Query.LastHTTPCode() << std::endl;
        std::cout << "LastErrorMessage: " << Query.LastErrorMessage() << std::endl;
    }

    catch (MusicBrainz5::CResourceNotFoundError& Error) {
        std::cout << "ResourceNotFound Exception: '" << Error.what() << "'" << std::endl;
        std::cout << "LastResult: " << Query.LastResult() << std::endl;
        std::cout << "LastHTTPCode: " << Query.LastHTTPCode() << std::endl;
        std::cout << "LastErrorMessage: " << Query.LastErrorMessage() << std::endl;
    }

    return 0;
}
