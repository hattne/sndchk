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

#include "../src/fingersum.h" // XXX path is bad
#include "../src/pool.h" // XXX path is bad


/* make fingersum && /usr/bin/time ./fingersum -c -j 4 ~/Music/iTunes/iTunes\ Media/Music/Buggles/The\ Age\ of\ Plastic/ *.m4a
 */
int
main(int argc, char *argv[])
{
    const char* optstring = ":cdfj:";

//    uint32_t checksum[3];
//    uint32_t checksum_v2[3];
    FILE *stream;
    char *ep, *fingerprint;
    struct fingersum_context *ctx;
    int ch, i, print_checksum, print_duration, print_fingerprint;

    struct fp3_ar *result;

    ssize_t njobs;


    /* Default values for command line options.
     */
    print_checksum = 0;
    print_duration = 0;
    print_fingerprint = 0;
    njobs = -1;

    opterr = 0;
    while ((ch = getopt(argc, argv, optstring)) != -1) {
        switch (ch) {
        case 'c':
            print_checksum = 1;
            break;

        case 'd':
            print_duration = 1;
            break;

        case 'f':
            print_fingerprint = 1;
            break;

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

    if (njobs < 0) {
        for (i = 0; i < argc; i++) {
            stream = fopen(argv[i], "r");
            if (stream == NULL)
                err(EXIT_FAILURE, "Failed to open %s", argv[i]);


            /* The default length for Chromaprint's fpcalc example
             * program is 120 seconds.
             */
            ctx = fingersum_new(stream);
            if (ctx == NULL) {
                fclose(stream);
                err(EXIT_FAILURE, "Failed to read %s", argv[i]);
            }


            /* Deliberately invert the order of AccurateRip and
             * Chromaprint to test that it works.
             */
            if (print_checksum) {
                result = fingersum_get_result_3(NULL, ctx, NULL);
                if (result == NULL) {
                    fingersum_free(ctx);
                    fclose(stream);
                    err(EXIT_FAILURE,
                        "Checksum calculation failed for %s", argv[i]);
                }

                // XXX Broken everywhere! Does not respect the offsets!
/*
                printf("Checksum[0]: 0x%08x 0x%08x\n",
                       checksum[0], checksum_v2[0]);
                printf("Checksum[1]: 0x%08x 0x%08x\n",
                       checksum[1], checksum_v2[1]);
                printf("Checksum[2]: 0x%08x 0x%08x\n",
                       checksum[2], checksum_v2[2]);
*/
            }

            if (print_duration)
                printf("   Duration: %d\n", fingersum_get_duration(ctx));

            if (print_fingerprint) {
                if (fingersum_get_fingerprint(ctx, NULL, &fingerprint) != 0) {
                    fingersum_free(ctx);
                    fclose(stream);
                    err(EXIT_FAILURE,
                        "Chromaprint calculation failed for %s", argv[i]);
                }
                printf("Chromaprint: %s\n", fingerprint);
                free(fingerprint);
            }


            /* Release the fingersum context and output requested
             * diagnostics.  XXX Make it fpcalc-style
             * (FINGERPRINT=...)?
             */
            fingersum_free(ctx);
            fclose(stream);
        }
    } else {
        FILE **streams;
        struct fingersum_context **ctxs;
        struct pool_context *pc;
        int flags;


        /* Multiprocessing!
         */
    
#if 0 // XXX Zap this!
        /* Stress-test pool_init(): repeatedly grow and shrink the
         * thread pool until it is the desired size.
         */
        for (i = 0; i <= njobs; i++) {
            if (pool_init(2 * i) != 0)
                err(EXIT_FAILURE, "Failed to initialise pool");
            if (pool_init(i) != 0)
                err(EXIT_FAILURE, "Failed to initialise pool");
        }
#endif

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

        flags = 0;
        if (print_checksum)
            flags |= POOL_ACTION_ACCURATERIP;
        if (print_fingerprint)
            flags |= POOL_ACTION_CHROMAPRINT;


        /* First iteration: submit jobs
         */
        for (i = 0; i < argc; i++) {
            streams[i] = fopen(argv[i], "r");
            if (streams[i] == NULL) {
                while (i-- > 1) {
                    fingersum_free(ctxs[i]);
                    fclose(streams[i]);
                }
                err(EXIT_FAILURE, "Failed to open %s", argv[i]);
            }

            ctxs[i] = fingersum_new(streams[i]);
            if (ctxs[i] == NULL) {
                while (i-- > 0) {
                    fingersum_free(ctxs[i]);
                    fclose(streams[i]);
                }
                err(EXIT_FAILURE, "Failed to read %s", argv[i]);
            }

            if (add_request(pc, ctxs[i], (void *)((intptr_t)i), flags) != 0) { // XXX double cast!
                pool_free_pc(pc);
                while (i-- > 0) {
                    fingersum_free(ctxs[i]);
                    fclose(streams[i]);
                }
                err(EXIT_FAILURE, "Failed to add to queue for %s", argv[i]);
            }
        }


        /* Second iteration: collect data, free the job context.
         */
        if (print_checksum || print_duration || print_fingerprint) {
            for (i = 0; i < argc; i++) {
//                struct fingersum_context *ctx;
                void *arg;
                int status;

                if (get_result(pc, &ctx, &arg, &status) != 0) {
                    warn("Failed to get result for %s", argv[i]);
                    continue;
                }

                if (print_checksum) {
                    if (status & POOL_ACTION_ACCURATERIP) {
                        result = fingersum_get_result_3(NULL, ctx, NULL);
                        if (result == NULL) {
                            // XXX Broken everywhere! Does not respect
                            // the offsets!

                            //printf("Checksum[0]: 0x%08x 0x%08x\n",
                            //       checksum[0], checksum_v2[0]);
                            //printf("Checksum[1]: 0x%08x 0x%08x\n",
                            //       checksum[1], checksum_v2[1]);
                            //printf("Checksum[2]: 0x%08x 0x%08x\n",
                            //       checksum[2], checksum_v2[2]);
                        }
                    } else {
                        warn("Checksum calculation failed for %s", argv[i]);
                    }
                }

                if (print_duration)
                    printf("   Duration: %d\n", fingersum_get_duration(ctx));

                if (print_fingerprint) {
//                    printf("Getting result for context %p\n", ctx);
                    if (status & POOL_ACTION_CHROMAPRINT &&
                        fingersum_get_fingerprint(
                            ctx, NULL, &fingerprint) == 0) {
                        printf("Chromaprint: %s\n", fingerprint);
                        free(fingerprint);
                    } else {
                        warn("Chromaprint calculation failed for %s", argv[i]); // XXX argv[i] probably not correct, need to use arg!
                    }
                }
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

#if 0 // XXX Zap this!
        /* Make sure that each call to pool_init() is matched by a
         * call to pool_free().
         */
        for (i = 0; i < 2 * (njobs + 1); i++)
            pool_free();
#endif
    }

    return (EXIT_SUCCESS);
}
