#! /usr/bin/env python
"""
Simple HTTP URL redirector
Shreyas Cholia 10/01/2015
usage: redirect.py [-h] [--port PORT] [--ip IP] redirect_url
HTTP redirect server
positional arguments:
  redirect_url
optional arguments:
  -h, --help            show this help message and exit
  --port PORT, -p PORT  port to listen on
  --ip IP, -i IP        host interface to listen on

From https://gist.github.com/shreddd/b7991ab491384e3c3331
"""
import SimpleHTTPServer
import SocketServer
import sys
import argparse


def redirect_handler_factory():
    """Returns a request handler class

    XXX Migrate to Python 3 (except macOS 10.12 does not ship it).

    XXX Should really concatenate the answers when there are hits for
    with and without a data track.

    XXX Should also think about if and how rate limiting applies here.

    XXX Can reconstruct DiscID from FreeDB ID?  Only need the last
    part of the path (e.g. NNN-0013682b-00aee95b-9711560c), the rest
    can be reconstructed.  See https://musicbrainz.org/search, or
    maybe rather trace where the FreeDB stuff in libmusicbrainz comes
    from.

    """
    class RedirectHandler(SimpleHTTPServer.SimpleHTTPRequestHandler):

        # Path must start with a slash--use join() instead?
        discid2path = {
            # The best of The Doors, disc 2
            "ejrfwBlgmFMxnD3AiDOE4AqqMI8-":
            "/accuraterip/b/b/b/dBAR-018-002c7bbb-02480762-1f124813.bin",

            # Flash
            "fRovc1tARQUreSsbP4zvcg6HCO4-":
            "/accuraterip/5/1/0/dBAR-003-00035015-000bc47d-2806f604.bin",

            # In lust we trust
            "7vqa6F_exuZ4paBsKHn0GO.tCrw-":
            "/accuraterip/b/2/8/dBAR-011-0013682b-00aee95b-9711560c.bin",

            # Live 2003
            "ELycL.Syz4kvJT.qkCNjBvHRSJA-":
            "/accuraterip/9/7/e/dBAR-012-001f0e79-01222683-ba10f60d.bin",

            # Tour de France
            "IRLOxr5FVyI.c1he6OXS7hrNxZM-":
            "/accuraterip/a/a/3/dBAR-003-0002c3aa-0009f0cc-30065d04.bin"
        }

        def do_GET(self):
            #print "Got command " + self.command
            #print "Got path " + self.path

            #self.send_response(301)
            #self.send_header('Location', url)

            try:
                url = 'http://www.accuraterip.com' + self.discid2path[self.path]
            except KeyError:
                pass

            if 'url' in locals():
                # send_response() must be called before send_header()
                # XXX read the documentation
                self.send_response(301)
                self.send_header('Location', url)
            else:
                self.send_response(404)


            # response = 'http://www.accuraterip.com' + self.discid2path[self.path]
            # print "Will respond with " + response
            # self.send_response(404)

            self.end_headers()

    return RedirectHandler


def main():
    parser = argparse.ArgumentParser(description='HTTP redirect server')
    port = 1984

    # See OpenBSD's httpd for arguments, etc.
    parser.add_argument(
        '--ip', '-i', action="store", default="",
        help='address to listen on')
#    parser.add_argument('redirect_url', action="store")

    myargs = parser.parse_args()

    redirectHandler = redirect_handler_factory()

    handler = SocketServer.TCPServer((myargs.ip, port), redirectHandler)
    print("Listening on port %d" % port)
    handler.serve_forever()


if __name__ == "__main__":
    main()
