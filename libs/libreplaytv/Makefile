# Copyright (C) 2004 John Honeycutt
# Copyright (C) 2002 John Todd Larason <jtl@molehill.org>

#    This Makefile is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.

#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.

BIGFILE=-DBIGFILE_BY_STDIO
LARGEFILE_SUPPORT=-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
PLATFORM_SUPPORT=-D__unix__ 
PLATFORM=$(shell /usr/libexec/config.guess)


# compiler-specific items:
# -Wwrite-strings
WARN_ME_HARDER=-Wall -W -Wshadow -Wpointer-arith \
	       -Wcast-align -Wstrict-prototypes \
	       -Wmissing-prototypes -Wmissing-declarations 
OPTIMIZE=-O
#  -DVERBOSE_OBFUSC -DEXPECT_CAN_EXIT
DEBUG=-g
WARNINGS=$(WARN_ME_HARDER)

# it's hoped you won't need to modify anything below here to compile
# on a Unix system

VER=0.1.0

CPPFLAGS=$(BIGFILE) $(LARGEFILE_SUPPORT) $(PLATFORM_SUPPORT)
CFLAGS=$(WARN_ME_HARDER) $(OPTIMIZE) $(DEBUG) -I../../install/include -I./MD5
LDFLAGS=-L. -L../../install/lib -static

LDLIBS=-lreplaytv -lexpat

RTVSHELL_PROG=rtv_shell
MPGTOOL_PROG=rtvmpgtool

LIB=libreplaytv.a
LIBCLI=shell/libcli.a
LIBMD5=MD5/libmd5.a
OBJMD5=MD5/md5_dgst.o

LIBOBJS=			\
	bigfile.o		\
	crypt.o			\
	crypt-test.o	\
	dump.o			\
	guideclient.o		\
	headend.o		\
	httpclient.o		\
	httpfsclient.o		\
	netclient.o 		\
	rtv.o			\
	sleep.o			\
	deviceinfoclient.o	\
	discovery.o	\
	rtvserver.o	\
	guideparser.o	\
	evtfileparser.o	\
	ndxclient.o	\
	rtvlib.o		\
	$(OBJMD5)

all: $(LIB) $(LIBCLI) $(LIBMD5) $(PROGRAMS) $(RTVSHELL_PROG) $(MPGTOOL_PROG)
$(PROGRAMS) : $(LIB)
$(LIB): $(LIBOBJS)
	ar r $@ $^
	ranlib $(LIB)

$(LIBCLI):
	(cd shell && echo "making cli lib..." && $(MAKE) CC='$(CC)' all ) || exit 1;

$(LIBMD5):
	(cd MD5 && echo "making md5 lib..." && $(MAKE) CC='$(CC)' all ) || exit 1;
$(OBJMD5):$(LIBMD5)
	@echo ""

$(RTVSHELL_PROG): $(LIB) $(LIBCLI) rtv_shell.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) rtv_shell.c $(LDLIBS) $(LIBCLI) -o $(RTVSHELL_PROG)

$(MPGTOOL_PROG): $(LIB) rtvmpegtool.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) rtvmpegtool.c $(LDLIBS) $(LIBCLI) -o $(MPGTOOL_PROG)

bindist: all
	-mkdir replaytvlib-$(VER)-$(PLATFORM)
	cp $(PROGRAMS) $(SCRIPTS) $(DOCS) replaytvlib-$(VER)-$(PLATFORM)
	tar cf - replaytvlib-$(VER)-$(PLATFORM) | gzip > replaytvlib-$(VER)-$(PLATFORM).tar.gz

clean:
	-rm *.o $(PROGRAMS) $(RTVSHELL_PROG) $(MPGTOOL_PROG) $(LIB) 
	(cd shell && echo "cleaning shell dir..." && $(MAKE) CC='$(CC)' clean )
	(cd MD5 && echo "cleaning MD5 dir..." && $(MAKE) CC='$(CC)' clean )

realclean: clean
	-rm -rf replaytvlib-$(VER)-$(PLATFORM) replaytvlib-$(VER)-$(PLATFORM).tar.gz
