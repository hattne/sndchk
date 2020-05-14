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

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "../src/acoustid.h" // XXX path is bad
#include "../src/fingersum.h" // XXX path is bad
#include "../src/pool.h" // XXX path is bad


/* make fingersum && /usr/bin/time ./fingersum -c -j 4 ~/Music/iTunes/iTunes\ Media/Music/Buggles/The\ Age\ of\ Plastic/ *.m4a
 *
 * BROKEN CASE:
 *
 * make fingerquery && ./fingerquery ~/Music/iTunes/iTunes\ Media/Music/Compilations/Jazz\ Swing/2-13\ And\ the\ Angels\ Sing.m4a
 *
 * XXX Implement option to check against release (this seems to be a
 * common use case).
 *
 * XXX Should also output duration.  And probably the rest of the
 * metadata in the audio file as well.
 *
 * XXX Idea: would be good to have a tool that checks an audio file
 * against a bunch of fingerprints and reports the score.
 */
int
main(int argc, char *argv[])
{
    const char* optstring = ":j:";

    char *ep, *fingerprint;
    struct fingersum_context *ctx;
    int ch, i;

    size_t njobs;


    /* Default values for command line options.
     */
    njobs = 4;

    opterr = 0;
    while ((ch = getopt(argc, argv, optstring)) != -1) {
        switch (ch) {
        case 'j':
            errno = 0;
            njobs = strtol(optarg, &ep, 10);
            if (optarg[0] == '\0' || *ep != '\0' || errno != 0)
                errx(EXIT_FAILURE, "Illegal -j argument %s", optarg);
            break;

        case ':':
            /* Missing the required argument of an option.  Use the
             * last known option character (optopt) for error
             * reporting.
             */
            errx(EXIT_FAILURE, "Option -%c requires an argument", optopt);

        case '?':
            errx(EXIT_FAILURE, "Unrecognised option '%s'", argv[optind - 1]);

        default:
            exit(EXIT_FAILURE);
        }
    }

    if (argc <= optind)
        return (EXIT_FAILURE);
    argc -= optind;
    argv += optind;

    FILE **streams;
    struct acoustid_context *ac;
    struct fingersum_context **ctxs;
    struct pool_context *pc;

    streams = calloc(argc, sizeof(FILE *));
    if (streams == NULL)
        err(EXIT_FAILURE, NULL);

    ctxs = calloc(argc, sizeof(struct fingersum_context *));
    if (ctxs == NULL) {
        free(streams);
        err(EXIT_FAILURE, NULL);
    }

    pc = pool_new_pc(njobs);
    if (pc == NULL) {
        free(ctxs);
        free(streams);
        err(EXIT_FAILURE, NULL);
    }


    /* First iteration: submit jobs
     */
    for (i = 0; i < argc; i++) {
        void *arg;

        streams[i] = fopen(argv[i], "r");
        if (streams[i] == NULL) {
            warn("Failed to open '%s'", argv[i]); // XXX Cannot do this printout after the while, because i will be messed up!
            while (i-- > 1) {
                fingersum_free(ctxs[i]);
                fclose(streams[i]);
            }
        }

        ctxs[i] = fingersum_new(streams[i]);
        if (ctxs[i] == NULL) {
            warn("Failed to read '%s'", argv[i]);
            while (i-- > 0) {
                fingersum_free(ctxs[i]);
                fclose(streams[i]);
            }
        }

        arg = (void *)((intptr_t)i);
        if (add_request(pc, ctxs[i], arg, POOL_ACTION_CHROMAPRINT) != 0) {
            warn("Failed to queue '%s'", argv[i]);
            while (i-- > 0) {
                fingersum_free(ctxs[i]);
                fclose(streams[i]);
            }
        }

        printf("Submitted ->%s<-\n", argv[i]);
    }


    /* Second iteration: collect data, free the job context.
     */
    ac = acoustid_new();
    if (ac == NULL) {
        err(EXIT_FAILURE, "Failed to XXX");
    }

    for (i = 0; i < argc; i++) {
        void *arg;
        int status;

        printf("Getting the result\n");
        if (get_result(pc, &ctx, &arg, &status) != 0) {
            warn("Failed to get result for %s", argv[i]);
            continue;
        }

        printf("Got the result\n");
        if (status & POOL_ACTION_CHROMAPRINT &&
            fingersum_get_fingerprint(ctx, NULL, &fingerprint) == 0) {

            if (acoustid_add_fingerprint(
                    ac, fingerprint, fingersum_get_duration(ctx), (unsigned long)arg) != 0) {
                err(EXIT_FAILURE, "Failed to XXX");
            }

            free(fingerprint);
        } else {
            warn("Chromaprint calculation failed for %s", argv[i]);
        }
    }
    pool_free_pc(pc);


    /* Third iteration: clean up
     */
    for (i = 0; i < argc; i++) {
        fingersum_free(ctxs[i]);
        fclose(streams[i]);
    }

    free(ctxs);
    free(streams);


    /* Wait for the results to come in...
     *
     * XXX Want option to select only unique recordings
     *
     * XXX Should sort by score, then by how many tracks the recording
     * is featured on.
     */
    acoustid_query(ac);

    return (EXIT_SUCCESS);
}
