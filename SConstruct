#!/usr/bin/python
#
# SCons build script for mvpmc
# http://www.mvpmc.org/
#

import os
import sys

pwd = os.getcwd()
os.environ['TOPLEVEL'] = pwd
sys.path.append('%s/scons' % pwd)

import mvpmc

env = Environment (ENV = os.environ)

target = ARGUMENTS.get('TARGET', Platform())
kernver = ARGUMENTS.get('KERNVER', Platform())

env.Replace(CCFLAGS = '-O3 -g -Wall -Werror')
env.Replace(TOPDIR = os.getcwd())

home = os.environ['HOME']

crosstool = '/opt/crosstool'
toolchains =  '/opt/toolchains/'

if os.path.exists(toolchains) == 0:
	toolchains =  home + '/toolchains/'

#
# parse the TARGET= option
#    mvp
#    host
#    kernel
#
if target == 'mvp':
	powerpc = 'powerpc-405-linux-uclibc'
	gcc = 'gcc-3.4.5-uClibc-0.9.28'
	prefix = powerpc + '-'
	crossroot = toolchains + '/' + powerpc + '/' + gcc + '/'
	cross = crossroot + '/bin/' + prefix
	cc = cross + 'gcc'
	cppflags = ''
elif target == 'host':
	cppflags = '-DMVPMC_HOST'
	crossroot='';
	env.Replace(CROSS = '')
elif target == 'kernel':
	print "kernel build"
	powerpc = 'powerpc-405-linux-uclibc'
	gcc = 'gcc-3.4.5-uClibc-0.9.28'
	crossroot = toolchains + '/' + powerpc + '/' + gcc + '/'
	prefix = powerpc + '-'
	cross = crossroot + '/bin/' + prefix
	cc = cross + 'gcc'
	cppflags = ''
else:
	print "Unknown target %s"%target
	sys.exit(1)

#
# Rebuilding the cross-compiler should be done in ~/toolchains
#
if (target != 'host') and (os.path.exists(cc) == 0):
	toolchains =  home + '/toolchains/'
	crossroot = toolchains + '/' + powerpc + '/' + gcc + '/'
	cross = crossroot + '/bin/' + prefix
	cc = cross + 'gcc'

#
# build binaries in the obj/TARGET directory
#
env.Replace(INCDIR = pwd + '/include')
env.Replace(INSTDIR = pwd + '/dongle/install/' + target)
env.Replace(INSTINCDIR = pwd + '/dongle/install/' + target + '/include')
env.Replace(INSTLIBDIR = pwd + '/dongle/install/' + target + '/lib')
env.Replace(INSTBINDIR = pwd + '/dongle/install/' + target + '/bin')
env.Replace(INSTSHAREDIR = pwd + '/dongle/install/' + target + '/usr/share/mvpmc')
env.Replace(BUILD_DIR = 'obj/' + target)
env.Replace(TARG = target)
env.Replace(DOWNLOADS = home + '/downloads')
env.Replace(TOPLEVEL = pwd)
env.Replace(CPPFLAGS = cppflags)
if target != 'host':
	env.Replace(CROSS = cross)
	env.Replace(CC = cross + 'gcc')
	env.Replace(CROSSPATH = crossroot + '/' + powerpc + '/bin')

Export('env','crossroot')

#
# ensure the download directory exits
#
downloads = env['DOWNLOADS']
if os.path.exists(downloads) == 0:
	os.mkdir(downloads)

if target == 'kernel':
	#
	# do the kernel build
	#
	cc = env['CC']
	kern = env.SConscript('dongle/kernel/linux-2.4.31/SConscript')
	gccbuild = 'tools/toolchains/uclibc/SConscript'
	if os.path.exists(cc) == 0:
		print "build kernel cross-compiler"
		gcc = env.SConscript(gccbuild)
		env.Depends(kern, gcc)
else:
	#
	# do the application build
	#
	inc = env.SConscript('include/SConscript')
	dir = env['BUILD_DIR']

	#
	# Only build the apps for the mvp.
	#
	if target == 'mvp':
		apps = env.SConscript('dongle/apps/SConscript')
		env.Depends(apps, inc)

	libs = env.SConscript('dongle/libs/SConscript')
	mvplibs = env.SConscript('libs/SConscript')
	mvpmc = env.SConscript('src/SConscript',
			       build_dir='src/'+dir, duplicate=0)
	themes = env.SConscript('themes/SConscript')
	images = env.SConscript('images/SConscript')

	env.Depends(mvpmc, inc)
	env.Depends(mvpmc, libs)
	env.Depends(mvplibs, libs)
	env.Depends(libs, inc)
	env.Depends(mvplibs, inc)

	#
	# Install the cross compilation tools, if needed.
	#
	if target == 'mvp':
		cc = env['CC']
		if os.path.exists(cc) == 0:
			print "build application cross-compiler"
			gcc = env.SConscript('tools/toolchains/uclibc/SConscript')
			env.Depends(libs, gcc)
			env.Depends(apps, gcc)
			env.Depends(mvplibs, gcc)
			env.Depends(mvpmc, gcc)

	#
	# Build the dongle.bin file
	#
	if target == 'mvp':
		dongle = env.SConscript('dongle/SConscript')
		env.Depends(dongle, mvpmc)
		env.Depends(dongle, mvplibs)
		env.Depends(dongle, apps)
		env.Depends(dongle, libs)
		env.Depends(dongle, themes)
		env.Depends(dongle, images)

	#
	# Build squashfs and mktree and mvprelay
	#
	if target == 'mvp':
		cc = env['CC']
		squashfs = env.SConscript('tools/squashfs/SConscript')
		mktree = env.SConscript('tools/mktree/SConscript')
		mvprelay = env.SConscript('tools/mvprelay/SConscript')
		env.Depends(dongle, squashfs)
		env.Depends(dongle, mktree)

	#
	# Misc tools
	#
	if target == 'mvp':
		misc = env.SConscript('tools/misc/SConscript')
		passwd = env.SConscript('tools/dongle_passwd/SConscript')

