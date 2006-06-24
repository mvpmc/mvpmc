#!/bin/sh

MYPWD=`pwd`
echo $MYPWD

if [ ! -f $MYPWD/dongle/install/mvp/lib/libncurses.a ] ; then 
	cd dongle/libs || exit 1
	mkdir -p ncurses || exit 1
	cd ncurses || exit 1
	wget http://ftp.gnu.org/pub/gnu/ncurses/ncurses-5.5.tar.gz
	tar -xzf ncurses-5.5.tar.gz
	cd ncurses-5.5
	CC='/opt/crosstool/powerpc-405-linux-uclibc/gcc-3.3.3-uClibc-0.9.23/bin/powerpc-405-linux-uclibc-gcc' ./configure --prefix=$MYPWD/dongle/install/mvp --host=ppcle

	make
	make install
fi

cd $MYPWD

if [ -f $MYPWD/dongle/install/mvp/lib/mysql/libmysqlclient.a ] ; then 
	echo "mysql ok"
else 
	cd $MYPWD/dongle/libs
	mkdir -p mysql
	cd mysql
	wget http://mysql.osuosl.org/Downloads/MySQL-5.0/mysql-5.0.21.tar.gz 

	tar -xzf $MYPWD/dongle/libs/mysql/mysql-5.0.21.tar.gz  -C  $MYPWD/dongle/libs/mysql
	cp  $MYPWD/scons/configure.patch   $MYPWD/dongle/libs/mysql/mysql-5.0.21/
	cp  $MYPWD/scons/my_global.h.patch   $MYPWD/dongle/libs/mysql/mysql-5.0.21/include
	cd  $MYPWD/dongle/libs/mysql/mysql-5.0.21 || exit 1

	if [ -f configure.patch ] ; then 
		echo "Patch configure script"
		patch -p0 < configure.patch
	fi
	cd include || exit 1
	patch -p0 < my_global.h.patch

	cd $MYPWD/dongle/libs/mysql/mysql-5.0.21

	C_INCLUDE_PATH=$MYPWD/dongle/install/mvp/include CC='/opt/crosstool/powerpc-405-linux-uclibc/gcc-3.3.3-uClibc-0.9.23/bin/powerpc-405-linux-uclibc-gcc' CXX='/opt/crosstool/powerpc-405-linux-uclibc/gcc-3.3.3-uClibc-0.9.23/bin/powerpc-405-linux-uclibc-g++' CPP='/opt/crosstool/powerpc-405-linux-uclibc/gcc-3.3.3-uClibc-0.9.23/bin/powerpc-405-linux-uclibc-cpp'  CFLAGS=-I$MYPWD/dongle/install/mvp/include LDFLAGS=-L$MYPWD/dongle/install/mvp/lib ./configure --without-docs --without-man --without-debug --without-server --without-query-cache  --without-extra-tools -host=ppcle --without-perl --with-low-memory --enable-assembler  --with-comment --cache-file=build.cache --with-other-libc=/opt/crosstool/powerpc-405-linux-uclibc/gcc-3.3.3-uClibc-0.9.23/lib --prefix=$MYPWD/dongle/install/mvp 

	make

	make install
fi

