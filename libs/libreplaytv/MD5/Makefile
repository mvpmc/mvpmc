
WARN_ME_HARDER=-Wall -W -Wshadow -Wpointer-arith \
               -Wcast-align -Wstrict-prototypes \
               -Wmissing-prototypes -Wmissing-declarations 
OPTIMIZE=-O

DEBUG=-g
WARNINGS=$(WARN_ME_HARDER)

VER=0.1

CFLAGS=$(WARNINGS) $(OPTIMIZE) $(DEBUG) -I.
LDFLAGS=-L.
LDLIBS=-lmd5

LIB=libmd5.a

LIBOBJS= md5_dgst.o

all: $(LIB) $(PROGRAMS)
$(PROGRAMS): $(LIB)
$(LIB): $(LIBOBJS)
	ar r $@ $^
	ranlib $(LIB)

clean:
	-rm *.o $(PROGRAMS) $(LIB)
