#
# $Id: Makefile 19 2006-03-14 05:24:19Z gettler $
#
# MediaMVP Media Center (mvpmc)
# http://mvpmc.sourceforge.net/
#

all: mvp host

mvp:
	scons -Q -j 2 TARGET=mvp

host:
	scons -Q -j 2 TARGET=host

kernel:
	scons -Q TARGET=kernel

docs:
	doxygen Doxyfile

cscope:
	find src libs include -name \*.c -or -name \*.h > cscope.files
	cscope -b -q -k

clean:
	scons -c TARGET=mvp
	scons -c TARGET=host
	rm -rf `find libs -name obj -type d`
	rm -rf `find src -name obj -type d`

distclean: clean
	rm -rf dongle/install
	rm -rf dongle/filesystem/install
	rm -rf tools/toolchains/glibc/crosstool-0.42
	rm -rf tools/toolchains/uclibc/crosstool-0.28-rc5
	rm -rf tools/genext2fs/genext2fs-1.4rc1
	rm -rf `find dongle -name mvp -type d`
	rm -rf `find dongle -name host -type d`
	rm -rf `find . -name .sconsign -type f`
	rm -rf home
	rm -rf doc/html
	rm -rf cscope*
