
 #  Copyright (C) 2004, John Honeycutt
 #
 #    This program is free software; you can redistribute it and/or modify
 #    it under the terms of the GNU General Public License as published by
 #    the Free Software Foundation; either version 2 of the License, or
 #    (at your option) any later version.
 #
 #    This program is distributed in the hope that it will be useful,
 #    but WITHOUT ANY WARRANTY; without even the implied warranty of
 #    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 #    GNU General Public License for more details.


WARN_ME_HARDER=-Wall -W -Wshadow -Wpointer-arith \
	       -Wcast-align -Wstrict-prototypes \
	       -Wmissing-prototypes -Wmissing-declarations 
OPTIMIZE=-O

DEBUG=-g
WARNINGS=$(WARN_ME_HARDER)

VER=0.1

CFLAGS=$(WARNINGS) $(OPTIMIZE) $(DEBUG)
LDFLAGS=-L.
LDLIBS=-lcli

LIB=libcli.a

LIBOBJS=			\
	readline_lite.o		\
	cli.o

all: $(LIB)
$(LIB): $(LIBOBJS)
	ar r $@ $^
	ranlib $(LIB)

clean:
	-rm *.o $(LIB)

