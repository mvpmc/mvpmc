#!/usr/bin/python
#
# From: http://www.scons.org/cgi-sys/cgiwrap/scons/moin.cgi/WgetSourceCode
#

import os.path
import sys
import popen2
import SCons.Action
import SCons.Builder
import SCons.Util


def generate(env):
  """
  Add a Builder factory function and construction variables for
  WGET'ing to an Environment.
  """

  def WGetDownload(target, source, env):
    ret = []
    for i in range(0, len(target)):
      url1 = str(target[i])
      if url1.find('http:') != -1:
        div = url1.find('http:')
        outn = url1[ :div - 1]
        url = url1[div:]
        url = url.replace('http:/','http://')
      elif url1.find('ftp:') != -1:
        div = url1.find('ftp:')
        outn = url1[ :div - 1]
        url = url1[div:]
        url = url.replace('ftp:/','ftp://')
      basename = str(url)[str( url).rfind(os.sep) + 1:]
      out = '/' + outn
      if not os.path.exists( out):
        cmd = popen2.Popen4('wget -O %s %s' % ('/' + out,url) )
        rc = cmd.wait()
        if rc != 0 :
          print "wget %s returned: %s" % (url, rc)
          print "%s" % cmd.fromchild.read()
          sys.exit()
    return None

  def WGetFactory(env=env):
    """ """
    act = SCons.Action.Action(WGetDownload)
    return SCons.Builder.Builder(action = act, env = env)

  env.WGet = WGetFactory
  env['WGET'] = 'wget'
  env['WGETFLAGS'] = SCons.Util.CLVar('')
  env['WGETCOM'] = '$WGET $WGETFLAGS $TARGET'

def exists(env):
  return env.Detect('wget')
