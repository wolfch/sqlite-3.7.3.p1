INCLUDES = -I.
INCLUDES += -I/Users/cwolf/src/sqlite-3.7.3.p1/build
INCLUDES += -I/Users/cwolf/src/sqlite-3.7.3.p1/src
INCLUDES += -I/Library/Frameworks/Python.framework/Versions/2.4/include/python2.4 
CFLAGS=-g -arch ppc -arch i386 -isysroot /Developer/SDKs/MacOSX10.4u.sdk \
       -fno-strict-aliasing -Wno-long-double -no-cpp-precomp -mno-fused-madd \
       -fno-common -dynamic -DNDEBUG -g -DEXPERIMENTAL=1 -DNDEBUG=1 \
       -DAPSW_FORK_CHECKER=1 -DPIC -DMACOSX $(INCLUDES)

#       -I. -I/Users/cwolf/src/sqlite-3.7.3.p1/src \

LDFLAGS=-dynamiclib \
  -L/Library/Frameworks/Python.framework/Versions/2.4/lib/python2.4/config \
  -ldl -lpython2.4 -L/Users/cwolf/src/sqlite-3.7.3.p1/build/.libs -lsqlite3

SRCS= pyproc.c

LIBNAME=libpyproc

#SRCS= apsw.c apswbuffer.c backup.c blob.c connection.c cursor.c \
#      exceptions.c pyutil.c statementcache.c \
#      traceback.c util.c vfs.c vtable.c

OBJS = $(SRCS:%.c=%.o)

all: ${LIBNAME}.dylib
	cp ${LIBNAME}.dylib /Users/cwolf/src/sqlite-3.7.3.p1/build/.libs/
	cp ${LIBNAME}.dylib /Users/cwolf/src/sqlite-3.7.3.p1/build/

${LIBNAME}.dylib : $(OBJS)
	gcc $(LDFLAGS) -o ${LIBNAME}.dylib $(OBJS)

docpp:
	cpp $(INCLUDES) pyproc.c > pyproc.c.txt

clean:
	rm -f *.o
	rm -f *.dylib
