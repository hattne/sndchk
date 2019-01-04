# Libav from Gentoo portage [11.3] is not recent enough: it won't extract
# sort_composer.

CPPFLAGS += -I$(HOME)/projects/libav/image/include -I$(HOME)/projects/libmusicbrainz/install/include -I$(HOME)/Gentoo/usr/include -I$(HOME)/Gentoo/usr/include/libxml2
#CPPFLAGS += -I$(HOME)/projects/libmusicbrainz/install/include -I$(HOME)/Gentoo/usr/include -I$(HOME)/Gentoo/usr/include/libxml2
LDFLAGS += -L$(HOME)/projects/libav/image/lib -L$(HOME)/projects/libmusicbrainz/install/lib -L$(HOME)/Gentoo/usr/lib
#LDFLAGS += -L$(HOME)/projects/libmusicbrainz/install/lib -L$(HOME)/Gentoo/usr/lib

#CC =$(HOME)/Gentoo/usr/bin/gcc
#CXX =$(HOME)/Gentoo/usr/bin/g++

#CC =$(HOME)/Gentoo/usr/bin/clang
#CXX =$(HOME)/Gentoo/usr/bin/clang++

CC=$(HOME)/Gentoo/usr/lib/llvm/7/bin/clang
CXX=$(HOME)/Gentoo/usr/lib/llvm/7/bin/clang++

CFLAGS = -Wall
CXXFLAGS = -Wall


# XXX had -lstdc++ between -lm and -lneon.  Looks like -lneon must be
# before -lm
sndchk: src/accuraterip.o src/acoustid.o src/configuration.o src/fingersum.o src/gzip.o src/metadata.o src/musicbrainz.o src/pool.o src/ratelimit.o src/structures.o src/sndchk.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lneon -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lmusicbrainz5 -lm -lz -liconv

	install_name_tool -change @rpath/libchromaprint.1.dylib $(HOME)/Gentoo/usr/lib/libchromaprint.1.dylib sndchk

accurip: accurip.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lneon -lavcodec -lavdevice -lavformat -lavresample -lavutil -lm  -lz

fingersum: src/fingersum.o src/metadata.o src/pool.o src/structures.o test/fingersum.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lm  -lz

fingerquery: src/acoustid.o src/fingersum.o src/gzip.o src/metadata.o src/pool.o src/ratelimit.o src/structures.o test/fingerquery.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lneon -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lm -lz

	install_name_tool -change @rpath/libchromaprint.1.dylib $(HOME)/Gentoo/usr/lib/libchromaprint.1.dylib fingerquery

ratelimit: src/ratelimit.o test/ratelimit.o
	$(CC) $(LDFLAGS) -o $(@) $(^)

diff: src/fingersum.o src/metadata.o src/structures.o test/diff.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lneon -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lm -lz

	install_name_tool -change @rpath/libchromaprint.1.dylib $(HOME)/Gentoo/usr/lib/libchromaprint.1.dylib diff

tags: test/tags.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lavformat -lavutil

mb: mb.o
	$(CXX) $(LDFLAGS) -o $(@) $(^) -lmusicbrainz5

all: sndchk accurip fingersum fingerquery ratelimit diff tags mb

clean:
	rm -f *.o src/*.o test/*.o
