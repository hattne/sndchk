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
	#$(CC) $(LDFLAGS) -o $(@) $(^) -lmp4v2 -lavformat
	$(CXX) $(LDFLAGS) -o $(@) $(^) -lneon -lmp4v2 -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lmusicbrainz5 -lm -lz -liconv

	install_name_tool -change @rpath/libchromaprint.1.dylib $(HOME)/Gentoo/usr/lib/libchromaprint.1.dylib sndchk

accurip: accurip.o
	$(CXX) $(LDFLAGS) -o $(@) $(^) -lneon -lmp4v2 -lavcodec -lavdevice -lavformat -lavresample -lavutil -lm  -lz

fingersum: src/fingersum.o src/metadata.o src/pool.o src/structures.o test/fingersum.o
	$(CXX) $(LDFLAGS) -o $(@) $(^) -lmp4v2 -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lm  -lz

fingerquery: src/acoustid.o src/fingersum.o src/gzip.o src/metadata.o src/pool.o src/ratelimit.o src/structures.o test/fingerquery.o
	$(CXX) $(LDFLAGS) -o $(@) $(^) -lneon -lmp4v2 -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lm -lz

	install_name_tool -change @rpath/libchromaprint.1.dylib $(HOME)/Gentoo/usr/lib/libchromaprint.1.dylib fingerquery

ratelimit: src/ratelimit.o test/ratelimit.o
	$(CXX) $(LDFLAGS) -o $(@) $(^)

diff: src/fingersum.o src/metadata.o src/structures.o test/diff.o
	$(CC) $(LDFLAGS) -o $(@) $(^) -lneon -lmp4v2 -lavcodec -lavdevice -lavformat -lavresample -lavutil -lchromaprint -lm -lz

	install_name_tool -change @rpath/libchromaprint.1.dylib $(HOME)/Gentoo/usr/lib/libchromaprint.1.dylib diff

tags: tags.o
	#$(CXX) $(LDFLAGS) -o $(@) $(^) -ltag -lstdc++
	$(CXX) $(LDFLAGS) -o $(@) $(^) -ltag -lavformat -lavutil
	#$(CXX) $(LDFLAGS) -o $(@) $(^) -ltag -lavcodec -lavformat -lavutil -lz -lbz2

rmtags: rmtags.o
	$(CXX) $(LDFLAGS) -o $(@) $(^) -ltag -lstdc++

mb: mb.o
	$(CXX) $(LDFLAGS) -o $(@) $(^) -lmusicbrainz5

# $(HOME)/Gentoo/usr/lib/libavformat.54.20.4.dylib

# -lavformat -lavcodec -lavutil -lavfilter

all: sndchk accurip fingersum fingerquery ratelimit diff tags rmtags mb

clean:
	rm -f *.o src/*.o test/*.o
