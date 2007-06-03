'''Python helper functions for building mvpmc
'''

import os

def OSSBuild(targets, urls, env, e, cmd=[], patches=[]):
    topdir = env['TOPDIR']
    cross = env['CROSS']
    downloads = env['DOWNLOADS']
    build_target = env['TARG']
    instdir = env['INSTDIR']
    e.Tool('oss', toolpath=['%s/scons' % topdir])
    build = e.oss(targets, urls,
                  patches=patches,
                  command=cmd,
                  cross=cross,
                  dldir=downloads,
                  builddir=os.getcwd(),
                  prefix=instdir,
                  buildtarget=build_target)
    return build
