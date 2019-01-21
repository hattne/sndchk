## -
## Copyright © 2019, Johan Hattne
##
## Copying and distribution of this file, with or without
## modification, are permitted in any medium without royalty provided
## the copyright notice and this notice are preserved.  This file is
## offered as-is, without any warranty.

bin_PROGRAMS = accurip diff fingerquery fingersum ratelimit sndchk sndtags

accurip_SOURCES = test/accurip.c
accurip_CFLAGS  = @NEON_CFLAGS@          \
                  @LIBAVCODEC_CFLAGS@    \
                  @LIBAVFORMAT_CFLAGS@   \
                  @LIBAVUTIL_CFLAGS@     \
                  @LIBSWRESAMPLE_CFLAGS@ \
                  @ZLIB_CFLAGS@
accurip_LDADD   = @NEON_LIBS@          \
                  @LIBAVCODEC_LIBS@    \
                  @LIBAVFORMAT_LIBS@   \
                  @LIBAVUTIL_LIBS@     \
                  @LIBSWRESAMPLE_LIBS@ \
                  @ZLIB_LIBS@

diff_SOURCES = src/fingersum.c  \
               src/metadata.c   \
               src/structures.c \
               test/diff.c
diff_CFLAGS  = @NEON_CFLAGS@           \
               @LIBAVCODEC_CFLAGS@     \
               @LIBAVFORMAT_CFLAGS@    \
               @LIBAVUTIL_CFLAGS@      \
               @LIBSWRESAMPLE_CFLAGS@  \
               @LIBCHROMAPRINT_CFLAGS@ \
               @ZLIB_CFLAGS@
diff_LDADD   = @NEON_LIBS@           \
               @LIBAVCODEC_LIBS@     \
               @LIBAVFORMAT_LIBS@    \
               @LIBAVUTIL_LIBS@      \
               @LIBSWRESAMPLE_LIBS@  \
               @LIBCHROMAPRINT_LIBS@ \
               @ZLIB_LIBS@           \
               @M_LIBS@

fingerquery_SOURCES = src/acoustid.c     \
                      src/fingersum.c    \
                      src/gzip.c         \
                      src/metadata.c     \
                      src/pool.c         \
                      src/ratelimit.c    \
                      src/structures.c   \
                      test/fingerquery.c
fingerquery_CFLAGS  = @NEON_CFLAGS@           \
                      @LIBAVCODEC_CFLAGS@     \
                      @LIBAVFORMAT_CFLAGS@    \
                      @LIBAVUTIL_CFLAGS@      \
                      @LIBSWRESAMPLE_CFLAGS@  \
                      @LIBCHROMAPRINT_CFLAGS@ \
                      @ZLIB_CFLAGS@
fingerquery_LDADD   = @NEON_LIBS@           \
                      @LIBAVCODEC_LIBS@     \
                      @LIBAVFORMAT_LIBS@    \
                      @LIBAVUTIL_LIBS@      \
                      @LIBSWRESAMPLE_LIBS@  \
                      @LIBCHROMAPRINT_LIBS@ \
                      @ZLIB_LIBS@           \
                      @M_LIBS@

fingersum_SOURCES = src/fingersum.c  \
                    src/metadata.c   \
                    src/pool.c       \
                    src/structures.c \
                    test/fingersum.c
fingersum_CFLAGS  = @LIBAVCODEC_CFLAGS@     \
                    @LIBAVFORMAT_CFLAGS@    \
                    @LIBAVUTIL_CFLAGS@      \
                    @LIBSWRESAMPLE_CFLAGS@  \
                    @LIBCHROMAPRINT_CFLAGS@ \
                    @ZLIB_CFLAGS@
fingersum_LDADD   = @LIBAVCODEC_LIBS@     \
                    @LIBAVFORMAT_LIBS@    \
                    @LIBAVUTIL_LIBS@      \
                    @LIBSWRESAMPLE_LIBS@  \
                    @LIBCHROMAPRINT_LIBS@ \
                    @ZLIB_LIBS@           \
                    @M_LIBS@

ratelimit_SOURCES = src/ratelimit.c  \
                    test/ratelimit.c

sndchk_SOURCES = src/accuraterip.c   \
                 src/acoustid.c      \
                 src/configuration.c \
                 src/fingersum.c     \
                 src/gzip.c          \
                 src/metadata.c      \
                 src/musicbrainz.c   \
                 src/pool.c          \
                 src/ratelimit.c     \
                 src/sndchk.c        \
                 src/structures.c
sndchk_CFLAGS  = @NEON_CFLAGS@            \
                 @LIBAVCODEC_CFLAGS@      \
                 @LIBAVFORMAT_CFLAGS@     \
                 @LIBAVUTIL_CFLAGS@       \
                 @LIBSWRESAMPLE_CFLAGS@   \
                 @LIBCHROMAPRINT_CFLAGS@  \
                 @LIBMUSICBRAINZ5_CFLAGS@ \
                 @ZLIB_CFLAGS@
sndchk_LDADD   = @NEON_LIBS@            \
                 @LIBAVCODEC_LIBS@      \
                 @LIBAVFORMAT_LIBS@     \
                 @LIBAVUTIL_LIBS@       \
                 @LIBSWRESAMPLE_LIBS@   \
                 @LIBCHROMAPRINT_LIBS@  \
                 @LIBMUSICBRAINZ5_LIBS@ \
                 @ZLIB_LIBS@            \
                 @M_LIBS@

# This was renamed from "tags" to "sndtags" so as not to clash with
# Automake's reserved word.
sndtags_SOURCES = test/tags.c
sndtags_CFLAGS  = @LIBAVFORMAT_CFLAGS@
sndtags_LDADD   = @LIBAVFORMAT_LIBS@
