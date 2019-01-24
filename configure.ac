## -
## Copyright © 2019, Johan Hattne
##
## Copying and distribution of this file, with or without
## modification, are permitted in any medium without royalty provided
## the copyright notice and this notice are preserved.  This file is
## offered as-is, without any warranty.

AC_PREREQ(2.59)
AC_INIT([sndchk],
        [0.0.0],
        [johan@hattne.se],
        [sndchk],
        [http://www.hattne.se/sndchk])
AC_CONFIG_AUX_DIR(build-aux)
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.9 foreign subdir-objects])


# Registered API key for sndchk 0.0.1.
AC_DEFINE([ACOUSTID_CLIENT], ["1L5a7F0OXu"], [AcoustID client API key])


# Checks for programs.  Using ${PTHREAD_CC} instead of ${CC} may be
# overkill, since not everything requires POSIX threads, but on most
# platforms, ${PTHREAD_CC} == ${CC} anyway.
AC_PROG_CC
AX_PTHREAD

CC="${PTHREAD_CC}"


# FFmpeg [https://www.ffmpeg.org] dependencies
#
# avcodec_send_packet() and avcodec_receive_frame() were introduced in
# 57.37.100.
PKG_CHECK_MODULES(LIBAVCODEC,
                  libavcodec >= 57.37.100,
                  have_libavcodec=yes,
                  have_libavcodec=no)
AS_IF([test "x${have_libavcodec}" = "xno"], [
    AC_MSG_ERROR([need libavcodec 57.37.100 or later])
])

# avformat_close_input() was introduced in 53.25.0.  The ability to
# extract sort_composer in mov_read_udta_string() was introduced on
# 2014-11-28 and merged into 2.6 on 2014-12-08; the closest release is
# 2.6.9 from 2016-05-03.
PKG_CHECK_MODULES(LIBAVFORMAT,
                  libavformat >= 56.25.101,
                  have_libavformat=yes,
                  have_libavformat=no)
AS_IF([test "x${have_libavformat}" = "xno"], [
    AC_MSG_ERROR([need libavformat 56.25.101 or later])
])

# av_opt_set_sample_fmt() was introduced in 52.9.100.
PKG_CHECK_MODULES(LIBAVUTIL,
                  libavutil >= 52.9.100,
                  have_libavutil=yes,
                  have_libavutil=no)
AS_IF(test "x${have_libavutil}" = "xno"], [
    AC_MSG_ERROR([need libavutil 52.9.100 or later])
])

# According to https://www.ffmpeg.org/olddownload.html, libswresample
# was introduced in 0.10, the latest release of which is 0.10.16 from
# 2015-03-12, with library version 0.6.100.
PKG_CHECK_MODULES(LIBSWRESAMPLE,
                  libswresample >= 0.6.100,
                  have_libswresample=yes,
                  have_libswresample=no)
AS_IF(test "x${have_libswresample}" = "xno"], [
    AC_MSG_ERROR([need libswresample 0.6.100 or later])
])


# Chromaprint [https://github.com/acoustid/chromaprint]: started using
# this library 2014-05-30.  1.1 was released 2013-11-23, 1.2 followed
# on 2014-08-26; earlier versions may work.
PKG_CHECK_MODULES(LIBCHROMAPRINT,
                  libchromaprint >= 1.1,
                  have_libchromaprint=yes,
                  have_libchromaprint=no)
AS_IF([test "x${have_libchromaprint}" = "xno"], [
    AC_MSG_ERROR([need libchromaprint 1.1 or later])
])


# The math functions may or may not be part of the C library.
libs_save=${LIBS}
AC_SEARCH_LIBS([acos], [m], [], [
  AC_MSG_ERROR([unable to find the acos() function])
])
M_LIBS=${LIBS}
LIBS=${libs_save}
AC_SUBST(M_LIBS)


# MusicBrainz Client Library
# [https://github.com/metabrainz/libmusicbrainz]: need a version after
# the current 5.1.0 release from 2014-11-13; in particular, sndchk
# requires offset-list parsing as provided by
# _mb5_disc_get_offsetlist, _mb5_offset_get_offset,
# _mb5_offset_get_position, _mb5_offset_list_item, and
# _mb5_offset_list_size, all introduced 2015-01-29.
PKG_CHECK_MODULES(LIBMUSICBRAINZ5,
                  libmusicbrainz5 >= 5.1.0,
                  have_libmusicbrainz5=yes,
                  have_libmusicbrainz5=no)
AS_IF([test "x${have_libmusicbrainz5}" = "xyes"], [
    cflags_save=${CFLAGS}
    CFLAGS="${CFLAGS} ${LIBMUSICBRAINZ5_CFLAGS}"
    libs_save=${LIBS}
    LIBS="${LIBS} ${LIBMUSICBRAINZ5_LIBS}"
    AC_CHECK_FUNC(mb5_disc_get_offsetlist,
                  have_libmusicbrainz5=yes,
                  have_libmusicbrainz5=no)
    LIBS=${libs_save}
    CFLAGS=${cflags_save}
])
AS_IF([test "x${have_libmusicbrainz5}" = "xno"], [
    AC_MSG_ERROR([need libmusicbrainz5 5.1.0 or later, with offset support])
])


# neon [http://www.webdav.org/neon]: need the "new" API names
# ("ne_*"), introduced in 0.15.0 (4 Jun 2001).
PKG_CHECK_MODULES(NEON, neon >= 0.15.0, have_neon=yes, have_neon=no)
AS_IF([test "x${have_neon}" = "xno"], [
    AC_MSG_ERROR([need neon 0.15.0 or later])
])


# zlib [https://zlib.net]: need the crc32_combine() function
# introduced in 1.2.2.1 (31 Oct 2004)
PKG_CHECK_MODULES(ZLIB, zlib >= 1.2.2.1, have_zlib=yes, have_zlib=no)
AS_IF([test "x${have_zlib}" = "xno"], [
    AC_MSG_ERROR([need zlib 1.2.2.1 or later])
])


# Checks for library functions.
AC_CHECK_FUNCS([clock_gettime])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
