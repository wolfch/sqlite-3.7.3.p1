INCLUDES = -I.
INCLUDES += -I../../build
INCLUDES += -I../../src
INCLUDES += -I/Library/Frameworks/Python.framework/Versions/2.4/include/python2.4 

CC=/usr/bin/gcc-4.0

CFLAGS=-g -arch ppc -arch i386 -isysroot /Developer/SDKs/MacOSX10.4u.sdk \
       -fno-strict-aliasing -Wno-long-double -no-cpp-precomp -mno-fused-madd \
       -fno-common -dynamic -DNDEBUG -g -DEXPERIMENTAL=1 -DNDEBUG=1 \
       -DAPSW_FORK_CHECKER=1 -DPIC -DMACOSX $(INCLUDES)


LDFLAGS=-dynamiclib \
  -L/Library/Frameworks/Python.framework/Versions/2.4/lib/python2.4/config \
  -ldl -lpython2.4 -L../../build/.libs -lsqlite3

SRCS= pyproc.c

LIBNAME=libpyproc

OBJS = $(SRCS:%.c=%.o)

all: ${LIBNAME}.dylib
	cp ${LIBNAME}.dylib ../../build/.libs/
	cp ${LIBNAME}.dylib ../../build/

${LIBNAME}.dylib : $(OBJS)
	gcc $(LDFLAGS) -o ${LIBNAME}.dylib $(OBJS)

docpp:
	cpp $(INCLUDES) pyproc.c > pyproc.c.txt

clean:
	rm -f *.o
	rm -f *.dylib
