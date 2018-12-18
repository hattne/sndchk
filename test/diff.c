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

#include "../src/fingersum.h" // XXX path is bad


/* XXX Implement sensible options from diff(1).  Also do the
 * "recursive" thing from the old shell script.
 *
 * XXX But maybe this is not diff(1) but cmp(1)?  See
 * http://www.gnu.org/software/grub/manual/grub.html#cmp
 *
 * XXX Should have options to skip first and last 500 samples (by
 * default, or user-configurable number--optional argument)
 *
 * XXX Should really take offsets into account!
 *
 * XXX Something like this surely exists elsewhere already.
 *
 * XXX Use FFT to align/offset?
 */
int
main(int argc, char *argv[])
{
    FILE *stream1, *stream2;
    struct fingersum_context *ctx1, *ctx2;
    ssize_t ret;


    stream1 = fopen(argv[1], "r");
    if (stream1 == NULL)
        err(EXIT_FAILURE, "Failed to open %s", argv[1]);
    ctx1 = fingersum_new(stream1);
    
    stream2 = fopen(argv[2], "r");
    if (stream2 == NULL)
        err(EXIT_FAILURE, "Failed to open %s", argv[2]);
    ctx2 = fingersum_new(stream2);

    ret = fingersum_diff(ctx1, ctx2);
    fingersum_free(ctx2);
    fclose(stream2);
    fingersum_free(ctx1);
    fclose(stream2);

    if (ret < 0) {
        printf("Diff failed\n"); // XXX -- but also happens if files
                                 // have different durations
        return (EXIT_FAILURE);
    } else if (ret > 0) {
        printf("Audio files %s and %s differ in %zd octets\n",
               argv[1], argv[2], ret);
        return (EXIT_FAILURE);
    }
    
    return (EXIT_SUCCESS);
}
